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

#include <dlfcn.h>

#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/Error.h>
#include <llvm-c/LLJIT.h>
#include <llvm-c/Orc.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

/* LLVM IS LOADED WHEN FIRST ASKED FOR, not linked. Linked, libLLVM's
 * relocations were done at every start: a program printing one line peaked
 * at 85 MB against 31 MB without it, GBASIC_COMPILE unset, and
 * run_read_at's memory ratio failed on that alone. So the binary carries the
 * library's path (GBASIC_LLVM_LIB, from llvm-config at build time) and the
 * first compile opens it; a program that compiles nothing never maps it.
 *
 * Every function used is in this list, its type taken from LLVM's own
 * declaration, so a call through the table is checked as a direct call is. */
#define JIT_LLVM_FUNCTIONS(X) \
    X(LLVMAddCase) \
    X(LLVMAddFunction) \
    X(LLVMAddIncoming) \
    X(LLVMAppendBasicBlockInContext) \
    X(LLVMBuildAdd) \
    X(LLVMBuildBr) \
    X(LLVMBuildCall2) \
    X(LLVMBuildCondBr) \
    X(LLVMBuildICmp) \
    X(LLVMBuildLoad2) \
    X(LLVMBuildPhi) \
    X(LLVMBuildRetVoid) \
    X(LLVMBuildStore) \
    X(LLVMBuildSwitch) \
    X(LLVMConstInt) \
    X(LLVMConstIntToPtr) \
    X(LLVMConsumeError) \
    X(LLVMCreateBuilderInContext) \
    X(LLVMCreateTargetMachine) \
    X(LLVMDisposeBuilder) \
    X(LLVMDisposeErrorMessage) \
    X(LLVMDisposeMessage) \
    X(LLVMDisposeModule) \
    X(LLVMFunctionType) \
    X(LLVMGetDefaultTargetTriple) \
    X(LLVMGetErrorMessage) \
    X(LLVMGetHostCPUFeatures) \
    X(LLVMGetHostCPUName) \
    X(LLVMGetParam) \
    X(LLVMGetTargetFromTriple) \
    X(LLVMInt64TypeInContext) \
    X(LLVMIntTypeInContext) \
    X(LLVMModuleCreateWithNameInContext) \
    X(LLVMOrcAbsoluteSymbols) \
    X(LLVMOrcCreateLLJIT) \
    X(LLVMOrcCreateLLJITBuilder) \
    X(LLVMOrcCreateNewThreadSafeContext) \
    X(LLVMOrcCreateNewThreadSafeModule) \
    X(LLVMOrcDisposeLLJIT) \
    X(LLVMOrcDisposeMaterializationUnit) \
    X(LLVMOrcDisposeThreadSafeContext) \
    X(LLVMOrcDisposeThreadSafeModule) \
    X(LLVMOrcJITDylibDefine) \
    X(LLVMOrcJITTargetMachineBuilderCreateFromTargetMachine) \
    X(LLVMOrcLLJITAddLLVMIRModule) \
    X(LLVMOrcLLJITBuilderSetJITTargetMachineBuilder) \
    X(LLVMOrcLLJITGetMainJITDylib) \
    X(LLVMOrcLLJITLookup) \
    X(LLVMOrcLLJITMangleAndIntern) \
    X(LLVMOrcThreadSafeContextGetContext) \
    X(LLVMPointerTypeInContext) \
    X(LLVMPositionBuilderAtEnd) \
    X(LLVMVerifyModule) \
    X(LLVMVoidTypeInContext)

#define JIT_LLVM_MEMBER(f) __typeof__(&f) f;
static struct {
    JIT_LLVM_FUNCTIONS(JIT_LLVM_MEMBER)
} llvm;

/* the native target's initialisers, by the names LLVM's configuration gives
 * them (LLVMInitializeX86Target, ...): the header's inline
 * LLVMInitializeNativeTarget would link them directly */
#define JIT_NAME_(x) #x
#define JIT_NAME(x) JIT_NAME_(x)
static const char *const jit_native_names[] = {
    JIT_NAME(LLVM_NATIVE_TARGETINFO), JIT_NAME(LLVM_NATIVE_TARGET),
    JIT_NAME(LLVM_NATIVE_TARGETMC), JIT_NAME(LLVM_NATIVE_ASMPRINTER),
};
static void (*jit_native[4])(void);

/* NULL once every function is bound, else what was missing */
static const char *jit_load(void) {
    static const char *missing;
    static int tried;
    if (tried) {
        return missing;
    }
    tried = 1;
    void *h = dlopen(GBASIC_LLVM_LIB, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        missing = dlerror();
        return missing;
    }
    void *sym;
#define JIT_LLVM_BIND(f) \
    if (!(sym = dlsym(h, #f))) { \
        return missing = #f; \
    } \
    memcpy(&llvm.f, &sym, sizeof sym);
    JIT_LLVM_FUNCTIONS(JIT_LLVM_BIND)
    for (size_t i = 0; i < sizeof jit_native / sizeof jit_native[0]; i++) {
        if (!(sym = dlsym(h, jit_native_names[i]))) {
            return missing = jit_native_names[i];
        }
        memcpy(&jit_native[i], &sym, sizeof sym);
    }
    return NULL;
}

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
    char *msg = llvm.LLVMGetErrorMessage(err);
    if (gb_jit_mode() == 2) {
        fprintf(stderr, "compile: %s: %s\n", what, msg);
    }
    llvm.LLVMDisposeErrorMessage(msg);
}

/* AT EXIT, THE JIT IS DISPOSED, which unregisters each body's unwinding
 * tables with libgcc: left to the process's end, valgrind reported each one
 * (48 bytes a function, malloc'd by __register_frame) as definitely lost, and
 * the compiled lane runs every suite's valgrind tier. */
static void jit_stop(void) {
    if (jit) {
        LLVMErrorRef err = llvm.LLVMOrcDisposeLLJIT(jit);
        if (err) {
            llvm.LLVMConsumeError(err);
        }
        jit = NULL;
    }
    if (jit_ctx) {
        llvm.LLVMOrcDisposeThreadSafeContext(jit_ctx);
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
    const char *missing = jit_load();
    if (missing) {
        if (gb_jit_mode() == 2) {
            fprintf(stderr, "compile: LLVM could not be loaded (%s); every function is interpreted\n",
                    missing);
        }
        return 0;
    }
    for (size_t i = 0; i < sizeof jit_native / sizeof jit_native[0]; i++) {
        jit_native[i]();
    }

    /* O0 in step 1: the IR is calls and branches, and compile time is the
     * gate's time. Measured before it is raised (§4). */
    char *triple = llvm.LLVMGetDefaultTargetTriple();
    LLVMTargetRef target;
    char *why = NULL;
    if (llvm.LLVMGetTargetFromTriple(triple, &target, &why)) {
        if (gb_jit_mode() == 2) {
            fprintf(stderr, "compile: no target for %s: %s\n", triple, why);
        }
        llvm.LLVMDisposeMessage(why);
        llvm.LLVMDisposeMessage(triple);
        return 0;
    }
    char *cpu = llvm.LLVMGetHostCPUName();
    char *features = llvm.LLVMGetHostCPUFeatures();
    LLVMTargetMachineRef tm = llvm.LLVMCreateTargetMachine(target, triple, cpu, features,
                                                      LLVMCodeGenLevelNone,
                                                      LLVMRelocDefault,
                                                      LLVMCodeModelJITDefault);
    llvm.LLVMDisposeMessage(features);
    llvm.LLVMDisposeMessage(cpu);
    llvm.LLVMDisposeMessage(triple);

    LLVMOrcLLJITBuilderRef builder = llvm.LLVMOrcCreateLLJITBuilder();
    llvm.LLVMOrcLLJITBuilderSetJITTargetMachineBuilder(
        builder, llvm.LLVMOrcJITTargetMachineBuilderCreateFromTargetMachine(tm));
    LLVMErrorRef err = llvm.LLVMOrcCreateLLJIT(&jit, builder);
    if (err) {
        jit_error("starting the JIT", err);
        jit = NULL;
        return 0;
    }

    /* THE RUNTIME BY ADDRESS, as absolute symbols: nothing is looked up by
     * the dynamic linker, so the binary needs no -rdynamic. */
    LLVMOrcCSymbolMapPair syms[] = {
        { llvm.LLVMOrcLLJITMangleAndIntern(jit, "gbrt_stmt"),
          { (LLVMOrcExecutorAddress)(uintptr_t)&gbrt_stmt,
            { LLVMJITSymbolGenericFlagsCallable, 0 } } },
        { llvm.LLVMOrcLLJITMangleAndIntern(jit, "gbrt_after"),
          { (LLVMOrcExecutorAddress)(uintptr_t)&gbrt_after,
            { LLVMJITSymbolGenericFlagsCallable, 0 } } },
    };
    LLVMOrcMaterializationUnitRef mu = llvm.LLVMOrcAbsoluteSymbols(syms, sizeof syms / sizeof syms[0]);
    err = llvm.LLVMOrcJITDylibDefine(llvm.LLVMOrcLLJITGetMainJITDylib(jit), mu);
    if (err) {
        llvm.LLVMOrcDisposeMaterializationUnit(mu);
        jit_error("defining the runtime", err);
        return 0;
    }
    jit_ctx = llvm.LLVMOrcCreateNewThreadSafeContext();
    jit_broken = 0;
    atexit(jit_stop);
    return 1;
}

/* The body's IR, in a module of its own named after it. */
static LLVMModuleRef jit_build(LLVMContextRef c, const char *sym, size_t count,
                               unsigned long *runs) {
    LLVMModuleRef m = llvm.LLVMModuleCreateWithNameInContext(sym, c);
    LLVMTypeRef ptr = llvm.LLVMPointerTypeInContext(c, 0);
    LLVMTypeRef i64 = llvm.LLVMInt64TypeInContext(c);
    LLVMTypeRef vd = llvm.LLVMVoidTypeInContext(c);
    LLVMTypeRef stmt_params[] = { ptr, i64 };
    LLVMTypeRef stmt_ty = llvm.LLVMFunctionType(vd, stmt_params, 2, 0);
    LLVMTypeRef after_ty = llvm.LLVMFunctionType(i64, stmt_params, 2, 0);
    LLVMValueRef gbrt_stmt_fn = llvm.LLVMAddFunction(m, "gbrt_stmt", stmt_ty);
    LLVMValueRef gbrt_after_fn = llvm.LLVMAddFunction(m, "gbrt_after", after_ty);
    LLVMTypeRef body_params[] = { ptr };
    LLVMValueRef fn = llvm.LLVMAddFunction(m, sym, llvm.LLVMFunctionType(vd, body_params, 1, 0));
    LLVMValueRef loop = llvm.LLVMGetParam(fn, 0);

    LLVMBuilderRef b = llvm.LLVMCreateBuilderInContext(c);
    LLVMBasicBlockRef entry = llvm.LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef *s = malloc(sizeof *s * (count + 1));
    if (!s) {
        abort();
    }
    for (size_t i = 0; i < count; i++) {
        s[i] = llvm.LLVMAppendBasicBlockInContext(c, fn, "s");
    }
    LLVMBasicBlockRef dispatch = llvm.LLVMAppendBasicBlockInContext(c, fn, "dispatch");
    LLVMBasicBlockRef done = llvm.LLVMAppendBasicBlockInContext(c, fn, "done");
    s[count] = done;

    llvm.LLVMPositionBuilderAtEnd(b, entry);
    if (runs) {
        LLVMTypeRef ul = llvm.LLVMIntTypeInContext(c, 8 * sizeof *runs);
        LLVMValueRef at = llvm.LLVMConstIntToPtr(llvm.LLVMConstInt(i64, (uintptr_t)runs, 0), ptr);
        LLVMValueRef was = llvm.LLVMBuildLoad2(b, ul, at, "runs");
        llvm.LLVMBuildStore(b, llvm.LLVMBuildAdd(b, was, llvm.LLVMConstInt(ul, 1, 0), ""), at);
    }
    llvm.LLVMBuildBr(b, s[0]);

    /* `n` reaches the dispatch block from every statement's block */
    llvm.LLVMPositionBuilderAtEnd(b, dispatch);
    LLVMValueRef n_phi = llvm.LLVMBuildPhi(b, i64, "n");
    LLVMValueRef sw = llvm.LLVMBuildSwitch(b, n_phi, done, (unsigned)count);
    for (size_t k = 0; k < count; k++) {
        llvm.LLVMAddCase(sw, llvm.LLVMConstInt(i64, k, 0), s[k]);
    }

    for (size_t i = 0; i < count; i++) {
        llvm.LLVMPositionBuilderAtEnd(b, s[i]);
        LLVMValueRef args[] = { loop, llvm.LLVMConstInt(i64, i, 0) };
        llvm.LLVMBuildCall2(b, stmt_ty, gbrt_stmt_fn, args, 2, "");
        LLVMValueRef n = llvm.LLVMBuildCall2(b, after_ty, gbrt_after_fn, args, 2, "next");
        LLVMValueRef straight = llvm.LLVMBuildICmp(b, LLVMIntEQ, n, llvm.LLVMConstInt(i64, i + 1, 0), "");
        llvm.LLVMBuildCondBr(b, straight, s[i + 1], dispatch);
        llvm.LLVMAddIncoming(n_phi, &n, &s[i], 1);
    }

    llvm.LLVMPositionBuilderAtEnd(b, done);
    llvm.LLVMBuildRetVoid(b);
    llvm.LLVMDisposeBuilder(b);
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
    LLVMContextRef c = llvm.LLVMOrcThreadSafeContextGetContext(jit_ctx);
    LLVMModuleRef m = jit_build(c, sym, count, runs);
    char *why = NULL;
    if (llvm.LLVMVerifyModule(m, LLVMReturnStatusAction, &why)) {
        if (gb_jit_mode() == 2) {
            fprintf(stderr, "compile: %s: the IR did not verify: %s\n", name, why);
        }
        llvm.LLVMDisposeMessage(why);
        llvm.LLVMDisposeModule(m);
        free(runs);
        jit_note(name, count, NULL);
        return NULL;
    }
    llvm.LLVMDisposeMessage(why);
    LLVMOrcThreadSafeModuleRef tsm = llvm.LLVMOrcCreateNewThreadSafeModule(m, jit_ctx);
    LLVMErrorRef err = llvm.LLVMOrcLLJITAddLLVMIRModule(jit, llvm.LLVMOrcLLJITGetMainJITDylib(jit), tsm);
    if (err) {
        llvm.LLVMOrcDisposeThreadSafeModule(tsm);
        jit_error(name, err);
        free(runs);
        jit_note(name, count, NULL);
        return NULL;
    }
    LLVMOrcExecutorAddress addr = 0;
    err = llvm.LLVMOrcLLJITLookup(jit, &addr, sym);
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

