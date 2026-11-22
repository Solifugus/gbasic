/* The compiler (docs/llvm_backend_design.md §4, increment 2): a function's
 * body as machine code, through LLVM's ORC LLJIT. The only file that
 * includes LLVM's headers, and its flags are on this object alone.
 *
 * STEP 1: EVERY STATEMENT THROUGH THE RUNTIME. The IR is invoke_function's
 * statement loop unrolled, one block a statement:
 *
 *     s_i:  gbrt_stmt(loop, i); n = gbrt_after(loop, i)
 *           n == i + 1 ? s_(i+1) : dispatch
 *     dispatch:  switch n { k: s_k ... } default: done
 *
 * so what happens after a statement (a raise absorbed, a goto, a gosub and
 * its return, leaving) is decided by ONE function the interpreter's own loop
 * calls too, and cannot come out differently here. It saves nothing yet; it
 * is the plumbing, proved by the whole gate run with GBASIC_COMPILE=all. */
#include "gb_jit.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* What a slot holds once its function is to be interpreted: an address of
 * this file's, so a copied node cannot read it as an entry. */
static char jit_interpreted;

/* Read once. Unset, empty or `0`: nothing is compiled. */
int gb_jit_mode(void) {
    static int mode = -1;
    if (mode < 0) {
        const char *v = getenv("GBASIC_COMPILE");
        if (!v || !*v || strcmp(v, "0") == 0) {
            mode = 0;
        } else if (strcmp(v, "report") == 0) {
            mode = 2;
        } else {
            mode = 1;
        }
    }
    return mode;
}

#if HAVE_LLVM

#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/Error.h>
#include <llvm-c/LLJIT.h>
#include <llvm-c/Orc.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

/* What `report` prints at exit: each body compiled, in the order compiled. */
typedef struct {
    char *name;
    size_t statements;
    int failed;
    /* how many times the compiled body ran, counted BY the body: its first
     * instructions add one here, so a run the interpreter took instead is
     * not counted, and the report shows machine code ran */
    unsigned long *runs;
} JitRow;
static JitRow *jit_rows;
static size_t jit_row_count;
static int jit_report_armed;


static void jit_report(void) {
    size_t built = 0, statements = 0;
    for (size_t i = 0; i < jit_row_count; i++) {
        JitRow *r = &jit_rows[i];
        if (r->failed) {
            fprintf(stderr, "compile: %s: not compiled, interpreted\n", r->name);
        } else {
            built++;
            statements += r->statements;
            fprintf(stderr, "compile: %s: %zu statement%s, 0 lowered, ran %lu time%s\n",
                    r->name, r->statements, r->statements == 1 ? "" : "s",
                    *r->runs, *r->runs == 1 ? "" : "s");
        }
    }
    fprintf(stderr, "compile: %zu function%s compiled, %zu statement%s, 0 lowered\n",
            built, built == 1 ? "" : "s", statements, statements == 1 ? "" : "s");
}

static void jit_note(const char *name, size_t statements, unsigned long *runs) {
    if (gb_jit_mode() != 2) {
        return;
    }
    if (!jit_report_armed) {
        jit_report_armed = 1;
        atexit(jit_report);
    }
    JitRow *next = realloc(jit_rows, sizeof *next * (jit_row_count + 1));
    if (!next) {
        abort();
    }
    jit_rows = next;
    jit_rows[jit_row_count].name = strdup(name ? name : "(a function)");
    jit_rows[jit_row_count].statements = statements;
    jit_rows[jit_row_count].failed = runs == NULL;
    jit_rows[jit_row_count].runs = runs;
    jit_row_count++;
}

static LLVMOrcLLJITRef jit;
static LLVMOrcThreadSafeContextRef jit_ctx;
static int jit_broken;
static unsigned long jit_serial;

/* The reason, on stderr under `report` only: a failure to compile is never
 * the program's failure, since the loop runs instead. */
static void jit_error(const char *what, LLVMErrorRef err) {
    char *msg = LLVMGetErrorMessage(err);
    if (gb_jit_mode() == 2) {
        fprintf(stderr, "compile: %s: %s\n", what, msg);
    }
    LLVMDisposeErrorMessage(msg);
}

/* AT EXIT, THE JIT IS DISPOSED, which unregisters each body's unwinding
 * tables with libgcc: left to the process's end, valgrind reported each one
 * (48 bytes a function, malloc'd by __register_frame) as definitely lost, and
 * the compiled lane runs every suite's valgrind tier. */
static void jit_stop(void) {
    if (jit) {
        LLVMErrorRef err = LLVMOrcDisposeLLJIT(jit);
        if (err) {
            LLVMConsumeError(err);
        }
        jit = NULL;
    }
    if (jit_ctx) {
        LLVMOrcDisposeThreadSafeContext(jit_ctx);
        jit_ctx = NULL;
    }
}

static int jit_start(void) {
    if (jit) {
        return 1;
    }
    if (jit_broken) {
        return 0;
    }
    jit_broken = 1;
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();

    /* O0 in step 1: the IR is calls and branches, and compile time is the
     * gate's time. Measured before it is raised (§4). */
    char *triple = LLVMGetDefaultTargetTriple();
    LLVMTargetRef target;
    char *why = NULL;
    if (LLVMGetTargetFromTriple(triple, &target, &why)) {
        if (gb_jit_mode() == 2) {
            fprintf(stderr, "compile: no target for %s: %s\n", triple, why);
        }
        LLVMDisposeMessage(why);
        LLVMDisposeMessage(triple);
        return 0;
    }
    char *cpu = LLVMGetHostCPUName();
    char *features = LLVMGetHostCPUFeatures();
    LLVMTargetMachineRef tm = LLVMCreateTargetMachine(target, triple, cpu, features,
                                                      LLVMCodeGenLevelNone,
                                                      LLVMRelocDefault,
                                                      LLVMCodeModelJITDefault);
    LLVMDisposeMessage(features);
    LLVMDisposeMessage(cpu);
    LLVMDisposeMessage(triple);

    LLVMOrcLLJITBuilderRef builder = LLVMOrcCreateLLJITBuilder();
    LLVMOrcLLJITBuilderSetJITTargetMachineBuilder(
        builder, LLVMOrcJITTargetMachineBuilderCreateFromTargetMachine(tm));
    LLVMErrorRef err = LLVMOrcCreateLLJIT(&jit, builder);
    if (err) {
        jit_error("starting the JIT", err);
        jit = NULL;
        return 0;
    }

    /* THE RUNTIME BY ADDRESS, as absolute symbols: nothing is looked up by
     * the dynamic linker, so the binary needs no -rdynamic. */
    LLVMOrcCSymbolMapPair syms[] = {
        { LLVMOrcLLJITMangleAndIntern(jit, "gbrt_stmt"),
          { (LLVMOrcExecutorAddress)(uintptr_t)&gbrt_stmt,
            { LLVMJITSymbolGenericFlagsCallable, 0 } } },
        { LLVMOrcLLJITMangleAndIntern(jit, "gbrt_after"),
          { (LLVMOrcExecutorAddress)(uintptr_t)&gbrt_after,
            { LLVMJITSymbolGenericFlagsCallable, 0 } } },
    };
    LLVMOrcMaterializationUnitRef mu = LLVMOrcAbsoluteSymbols(syms, sizeof syms / sizeof syms[0]);
    err = LLVMOrcJITDylibDefine(LLVMOrcLLJITGetMainJITDylib(jit), mu);
    if (err) {
        LLVMOrcDisposeMaterializationUnit(mu);
        jit_error("defining the runtime", err);
        return 0;
    }
    jit_ctx = LLVMOrcCreateNewThreadSafeContext();
    jit_broken = 0;
    atexit(jit_stop);
    return 1;
}

/* The body's IR, in a module of its own named after it. */
static LLVMModuleRef jit_build(LLVMContextRef c, const char *sym, size_t count,
                               unsigned long *runs) {
    LLVMModuleRef m = LLVMModuleCreateWithNameInContext(sym, c);
    LLVMTypeRef ptr = LLVMPointerTypeInContext(c, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef vd = LLVMVoidTypeInContext(c);
    LLVMTypeRef stmt_params[] = { ptr, i64 };
    LLVMTypeRef stmt_ty = LLVMFunctionType(vd, stmt_params, 2, 0);
    LLVMTypeRef after_ty = LLVMFunctionType(i64, stmt_params, 2, 0);
    LLVMValueRef gbrt_stmt_fn = LLVMAddFunction(m, "gbrt_stmt", stmt_ty);
    LLVMValueRef gbrt_after_fn = LLVMAddFunction(m, "gbrt_after", after_ty);
    LLVMTypeRef body_params[] = { ptr };
    LLVMValueRef fn = LLVMAddFunction(m, sym, LLVMFunctionType(vd, body_params, 1, 0));
    LLVMValueRef loop = LLVMGetParam(fn, 0);

    LLVMBuilderRef b = LLVMCreateBuilderInContext(c);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef *s = malloc(sizeof *s * (count + 1));
    if (!s) {
        abort();
    }
    for (size_t i = 0; i < count; i++) {
        s[i] = LLVMAppendBasicBlockInContext(c, fn, "s");
    }
    LLVMBasicBlockRef dispatch = LLVMAppendBasicBlockInContext(c, fn, "dispatch");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(c, fn, "done");
    s[count] = done;

    LLVMPositionBuilderAtEnd(b, entry);
    if (runs) {
        LLVMTypeRef ul = LLVMIntTypeInContext(c, 8 * sizeof *runs);
        LLVMValueRef at = LLVMConstIntToPtr(LLVMConstInt(i64, (uintptr_t)runs, 0), ptr);
        LLVMValueRef was = LLVMBuildLoad2(b, ul, at, "runs");
        LLVMBuildStore(b, LLVMBuildAdd(b, was, LLVMConstInt(ul, 1, 0), ""), at);
    }
    LLVMBuildBr(b, s[0]);

    /* `n` reaches the dispatch block from every statement's block */
    LLVMPositionBuilderAtEnd(b, dispatch);
    LLVMValueRef n_phi = LLVMBuildPhi(b, i64, "n");
    LLVMValueRef sw = LLVMBuildSwitch(b, n_phi, done, (unsigned)count);
    for (size_t k = 0; k < count; k++) {
        LLVMAddCase(sw, LLVMConstInt(i64, k, 0), s[k]);
    }

    for (size_t i = 0; i < count; i++) {
        LLVMPositionBuilderAtEnd(b, s[i]);
        LLVMValueRef args[] = { loop, LLVMConstInt(i64, i, 0) };
        LLVMBuildCall2(b, stmt_ty, gbrt_stmt_fn, args, 2, "");
        LLVMValueRef n = LLVMBuildCall2(b, after_ty, gbrt_after_fn, args, 2, "next");
        LLVMValueRef straight = LLVMBuildICmp(b, LLVMIntEQ, n, LLVMConstInt(i64, i + 1, 0), "");
        LLVMBuildCondBr(b, straight, s[i + 1], dispatch);
        LLVMAddIncoming(n_phi, &n, &s[i], 1);
    }

    LLVMPositionBuilderAtEnd(b, done);
    LLVMBuildRetVoid(b);
    LLVMDisposeBuilder(b);
    free(s);
    return m;
}

GbJitBody gb_jit_body(void **slot, const char *name, size_t count) {
    if (*slot) {
        return *slot == (void *)&jit_interpreted ? NULL : (GbJitBody)(uintptr_t)*slot;
    }
    /* refused until it is compiled: a failure is tried once, not per call */
    *slot = &jit_interpreted;
    if (!jit_start()) {
        jit_note(name, count, NULL);
        return NULL;
    }
    unsigned long *runs = NULL;
    if (gb_jit_mode() == 2) {
        runs = calloc(1, sizeof *runs);
        if (!runs) {
            abort();
        }
    }
    char sym[48];
    snprintf(sym, sizeof sym, "gb_body_%lu", ++jit_serial);
    LLVMContextRef c = LLVMOrcThreadSafeContextGetContext(jit_ctx);
    LLVMModuleRef m = jit_build(c, sym, count, runs);
    char *why = NULL;
    if (LLVMVerifyModule(m, LLVMReturnStatusAction, &why)) {
        if (gb_jit_mode() == 2) {
            fprintf(stderr, "compile: %s: the IR did not verify: %s\n", name, why);
        }
        LLVMDisposeMessage(why);
        LLVMDisposeModule(m);
        free(runs);
        jit_note(name, count, NULL);
        return NULL;
    }
    LLVMDisposeMessage(why);
    LLVMOrcThreadSafeModuleRef tsm = LLVMOrcCreateNewThreadSafeModule(m, jit_ctx);
    LLVMErrorRef err = LLVMOrcLLJITAddLLVMIRModule(jit, LLVMOrcLLJITGetMainJITDylib(jit), tsm);
    if (err) {
        LLVMOrcDisposeThreadSafeModule(tsm);
        jit_error(name, err);
        free(runs);
        jit_note(name, count, NULL);
        return NULL;
    }
    LLVMOrcExecutorAddress addr = 0;
    err = LLVMOrcLLJITLookup(jit, &addr, sym);
    if (err || !addr) {
        if (err) {
            jit_error(name, err);
        }
        free(runs);
        jit_note(name, count, NULL);
        return NULL;
    }
    jit_note(name, count, runs);
    *slot = (void *)(uintptr_t)addr;
    return (GbJitBody)(uintptr_t)addr;
}

#else /* !HAVE_LLVM */

static int jit_report_armed;

GbJitBody gb_jit_body(void **slot, const char *name, size_t count) {
    (void)name;
    (void)count;
    *slot = &jit_interpreted;
    if (gb_jit_mode() == 2 && !jit_report_armed) {
        jit_report_armed = 1;
        fprintf(stderr, "compile: not available in this build (needs LLVM 18 or later); "
                        "every function is interpreted\n");
    }
    return NULL;
}

#endif

