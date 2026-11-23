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
 * calls too, and cannot come out differently here.
 *
 * STEP 2: `if`, `while` AND `for ... to` LOWERED. Their conditions, and a
 * `for`'s bounds, test and step, are still the runtime's (gbrt_if,
 * gbrt_while_test, gbrt_for_*: the calls the interpreted statements make),
 * but the branch and the loop are IR, and the lists inside them are unrolled
 * as the body is, each statement followed by gbrt_list_after, the call
 * eval_stmt_list makes. Any other statement is still gbrt_stmt, and a list
 * inside one (a `for each`'s body, a `do`'s) is interpreted. */
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
    X(LLVMArrayType2) \
    X(LLVMBuildAlloca) \
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
    X(LLVMGetBasicBlockParent) \
    X(LLVMGetHostCPUName) \
    X(LLVMGetInsertBlock) \
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
    X(LLVMSetAlignment) \
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
    size_t lowered;
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
    size_t built = 0, statements = 0, lowered = 0;
    for (size_t i = 0; i < jit_row_count; i++) {
        JitRow *r = &jit_rows[i];
        if (r->failed) {
            fprintf(stderr, "compile: %s: not compiled, interpreted\n", r->name);
        } else {
            built++;
            statements += r->statements;
            lowered += r->lowered;
            fprintf(stderr, "compile: %s: %zu statement%s, %zu lowered, ran %lu time%s\n",
                    r->name, r->statements, r->statements == 1 ? "" : "s", r->lowered,
                    *r->runs, *r->runs == 1 ? "" : "s");
        }
    }
    fprintf(stderr, "compile: %zu bod%s compiled, %zu statement%s, %zu lowered\n",
            built, built == 1 ? "y" : "ies", statements, statements == 1 ? "" : "s", lowered);
}

static void jit_note(const char *name, size_t statements, size_t lowered,
                     unsigned long *runs) {
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
    jit_rows[jit_row_count].lowered = lowered;
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
#define JIT_RUNTIME(f) \
        { llvm.LLVMOrcLLJITMangleAndIntern(jit, #f), \
          { (LLVMOrcExecutorAddress)(uintptr_t)&f, { LLVMJITSymbolGenericFlagsCallable, 0 } } },
    LLVMOrcCSymbolMapPair syms[] = {
        JIT_RUNTIME(gbrt_stmt)
        JIT_RUNTIME(gbrt_after)
        JIT_RUNTIME(gbrt_list_begin)
        JIT_RUNTIME(gbrt_list_stmt)
        JIT_RUNTIME(gbrt_list_after)
        JIT_RUNTIME(gbrt_if)
        JIT_RUNTIME(gbrt_if_done)
        JIT_RUNTIME(gbrt_while_enter)
        JIT_RUNTIME(gbrt_while_test)
        JIT_RUNTIME(gbrt_while_next)
        JIT_RUNTIME(gbrt_for_enter)
        JIT_RUNTIME(gbrt_for_test)
        JIT_RUNTIME(gbrt_for_next)
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

/* A runtime function the IR calls: its type, and its declaration in the
 * module. */
typedef struct {
    LLVMTypeRef ty;
    LLVMValueRef fn;
} JitCall;

/* WHAT BUILDING A BODY CARRIES: the module, two builders (one at the end of
 * the entry block, where every alloca goes, so a loop never grows the
 * stack), the runtime, and the counts the report gives. */
typedef struct {
    LLVMContextRef c;
    LLVMModuleRef m;
    LLVMBuilderRef b;
    LLVMBuilderRef entry;
    LLVMTypeRef ptr, i32, size, lng;
    LLVMValueRef list_bytes;
    JitCall stmt, after, list_begin, list_stmt, list_after;
    JitCall if_, if_done, while_enter, while_test, while_next;
    JitCall for_enter, for_test, for_next;
    size_t statements;
    size_t lowered;
} JitEmit;

static JitCall jit_declare(JitEmit *e, const char *name, LLVMTypeRef ret,
                           LLVMTypeRef *params, unsigned count) {
    JitCall call;
    call.ty = llvm.LLVMFunctionType(ret, params, count, 0);
    call.fn = llvm.LLVMAddFunction(e->m, name, call.ty);
    return call;
}

static LLVMValueRef jit_call(JitEmit *e, JitCall call, LLVMValueRef *args, unsigned count) {
    return llvm.LLVMBuildCall2(e->b, call.ty, call.fn, args, count, "");
}

/* an address of the interpreter's (a node, a list), as a constant */
static LLVMValueRef jit_address(JitEmit *e, const void *p) {
    return llvm.LLVMConstIntToPtr(llvm.LLVMConstInt(e->size, (uintptr_t)p, 0), e->ptr);
}

static LLVMValueRef jit_alloca(JitEmit *e, LLVMTypeRef ty) {
    LLVMValueRef at = llvm.LLVMBuildAlloca(e->entry, ty, "");
    llvm.LLVMSetAlignment(at, 16);
    return at;
}

/* `n` bytes of the compiled code's stack, for a state only the runtime reads */
static LLVMValueRef jit_bytes(JitEmit *e, size_t n) {
    return jit_alloca(e, llvm.LLVMArrayType2(llvm.LLVMIntTypeInContext(e->c, 8), (uint64_t)n));
}

static LLVMBasicBlockRef jit_block(JitEmit *e, const char *name) {
    LLVMValueRef fn = llvm.LLVMGetBasicBlockParent(llvm.LLVMGetInsertBlock(e->b));
    return llvm.LLVMAppendBasicBlockInContext(e->c, fn, name);
}

static void jit_list(JitEmit *e, LLVMValueRef state, AstStmtList *list, int nested);

/* A nested list over a GbList of its own: entered by gbrt_list_begin, which
 * may leave at once (an interrupt). */
static void jit_nested(JitEmit *e, LLVMValueRef l, AstStmtList *list) {
    jit_list(e, l, list, 1);
}

/* ONE STATEMENT, leaving its result at `state` as gbrt_stmt would, with the
 * builder left where the list's after-call goes. */
static void jit_stmt(JitEmit *e, LLVMValueRef state, AstStmt *stmt, size_t index, int nested) {
    LLVMValueRef zero = llvm.LLVMConstInt(e->i32, 0, 0);
    LLVMValueRef one = llvm.LLVMConstInt(e->i32, 1, 0);
    if (stmt->kind == AST_STMT_IF) {
        e->lowered++;
        LLVMValueRef saved = jit_alloca(e, llvm.LLVMArrayType2(e->i32, 2));
        LLVMValueRef l = jit_bytes(e, gbrt_list_bytes());
        LLVMValueRef args[] = { jit_address(e, stmt), state, saved };
        LLVMValueRef truth = jit_call(e, e->if_, args, 3);
        LLVMBasicBlockRef then_b = jit_block(e, "then");
        LLVMBasicBlockRef else_b = jit_block(e, "else");
        LLVMBasicBlockRef join = jit_block(e, "endif");
        LLVMValueRef sw = llvm.LLVMBuildSwitch(e->b, truth, join, 2);
        llvm.LLVMAddCase(sw, one, then_b);
        llvm.LLVMAddCase(sw, zero, else_b);
        AstStmtList *branches[] = { &stmt->as.if_stmt.body, &stmt->as.if_stmt.else_body };
        LLVMBasicBlockRef blocks[] = { then_b, else_b };
        for (int k = 0; k < 2; k++) {
            llvm.LLVMPositionBuilderAtEnd(e->b, blocks[k]);
            jit_nested(e, l, branches[k]);
            LLVMValueRef done[] = { state, l, saved };
            jit_call(e, e->if_done, done, 3);
            llvm.LLVMBuildBr(e->b, join);
        }
        llvm.LLVMPositionBuilderAtEnd(e->b, join);
        return;
    }
    if (stmt->kind == AST_STMT_WHILE) {
        e->lowered++;
        LLVMValueRef saved = jit_alloca(e, llvm.LLVMArrayType2(e->i32, 2));
        LLVMValueRef l = jit_bytes(e, gbrt_list_bytes());
        LLVMValueRef args[] = { jit_address(e, stmt), state, saved };
        LLVMBasicBlockRef test = jit_block(e, "test");
        LLVMBasicBlockRef body = jit_block(e, "pass");
        LLVMBasicBlockRef join = jit_block(e, "endwhile");
        LLVMValueRef entered = jit_call(e, e->while_enter, args, 3);
        llvm.LLVMBuildCondBr(e->b, llvm.LLVMBuildICmp(e->b, LLVMIntEQ, entered, zero, ""), test, join);
        llvm.LLVMPositionBuilderAtEnd(e->b, test);
        LLVMValueRef truth = jit_call(e, e->while_test, args, 3);
        llvm.LLVMBuildCondBr(e->b, llvm.LLVMBuildICmp(e->b, LLVMIntEQ, truth, one, ""), body, join);
        llvm.LLVMPositionBuilderAtEnd(e->b, body);
        jit_nested(e, l, &stmt->as.while_stmt.body);
        LLVMValueRef next_args[] = { state, l, saved };
        LLVMValueRef again = jit_call(e, e->while_next, next_args, 3);
        llvm.LLVMBuildCondBr(e->b, llvm.LLVMBuildICmp(e->b, LLVMIntEQ, again, one, ""), test, join);
        llvm.LLVMPositionBuilderAtEnd(e->b, join);
        return;
    }
    if (stmt->kind == AST_STMT_FOR_RANGE) {
        e->lowered++;
        LLVMValueRef saved = jit_alloca(e, llvm.LLVMArrayType2(e->i32, 2));
        LLVMValueRef l = jit_bytes(e, gbrt_list_bytes());
        LLVMValueRef f = jit_bytes(e, gbrt_for_bytes());
        LLVMValueRef node = jit_address(e, stmt);
        LLVMValueRef args[] = { node, state, saved, f };
        LLVMBasicBlockRef test = jit_block(e, "test");
        LLVMBasicBlockRef body = jit_block(e, "pass");
        LLVMBasicBlockRef join = jit_block(e, "endfor");
        LLVMValueRef entered = jit_call(e, e->for_enter, args, 4);
        llvm.LLVMBuildCondBr(e->b, llvm.LLVMBuildICmp(e->b, LLVMIntEQ, entered, zero, ""), test, join);
        llvm.LLVMPositionBuilderAtEnd(e->b, test);
        LLVMValueRef truth = jit_call(e, e->for_test, args, 4);
        llvm.LLVMBuildCondBr(e->b, llvm.LLVMBuildICmp(e->b, LLVMIntEQ, truth, one, ""), body, join);
        llvm.LLVMPositionBuilderAtEnd(e->b, body);
        jit_nested(e, l, &stmt->as.for_range.body);
        LLVMValueRef next_args[] = { node, state, l, saved, f };
        LLVMValueRef again = jit_call(e, e->for_next, next_args, 5);
        llvm.LLVMBuildCondBr(e->b, llvm.LLVMBuildICmp(e->b, LLVMIntEQ, again, one, ""), test, join);
        llvm.LLVMPositionBuilderAtEnd(e->b, join);
        return;
    }
    LLVMValueRef args[] = { state, llvm.LLVMConstInt(e->size, index, 0) };
    jit_call(e, nested ? e->list_stmt : e->stmt, args, 2);
}

/* A LIST UNROLLED, one block a statement, each followed by the list's
 * after-call (gbrt_after for the body, gbrt_list_after for a nested list):
 * the next index straight on, anything else through a switch. The builder
 * is left at the list's end. */
static void jit_list(JitEmit *e, LLVMValueRef state, AstStmtList *list, int nested) {
    size_t count = list->count;
    e->statements += count;
    LLVMBasicBlockRef *s = malloc(sizeof *s * (count + 1));
    if (!s) {
        abort();
    }
    for (size_t i = 0; i < count; i++) {
        s[i] = jit_block(e, "s");
    }
    LLVMBasicBlockRef dispatch = jit_block(e, "dispatch");
    LLVMBasicBlockRef end = jit_block(e, "end");
    s[count] = end;

    if (nested) {
        LLVMValueRef args[] = { state, jit_address(e, list) };
        LLVMValueRef begun = jit_call(e, e->list_begin, args, 2);
        llvm.LLVMBuildCondBr(e->b, llvm.LLVMBuildICmp(e->b, LLVMIntEQ, begun,
                                                        llvm.LLVMConstInt(e->i32, 0, 0), ""),
                             s[0], end);
    } else {
        llvm.LLVMBuildBr(e->b, s[0]);
    }

    /* `n` reaches the dispatch block from every statement's after-call */
    llvm.LLVMPositionBuilderAtEnd(e->b, dispatch);
    LLVMValueRef n_phi = llvm.LLVMBuildPhi(e->b, e->lng, "n");
    LLVMValueRef sw = llvm.LLVMBuildSwitch(e->b, n_phi, end, (unsigned)count);
    for (size_t k = 0; k < count; k++) {
        llvm.LLVMAddCase(sw, llvm.LLVMConstInt(e->lng, k, 0), s[k]);
    }

    for (size_t i = 0; i < count; i++) {
        llvm.LLVMPositionBuilderAtEnd(e->b, s[i]);
        jit_stmt(e, state, list->items[i], i, nested);
        LLVMValueRef args[] = { state, llvm.LLVMConstInt(e->size, i, 0) };
        LLVMValueRef n = jit_call(e, nested ? e->list_after : e->after, args, 2);
        LLVMValueRef straight = llvm.LLVMBuildICmp(e->b, LLVMIntEQ, n,
                                                   llvm.LLVMConstInt(e->lng, i + 1, 0), "");
        llvm.LLVMBuildCondBr(e->b, straight, s[i + 1], dispatch);
        LLVMBasicBlockRef from = llvm.LLVMGetInsertBlock(e->b);
        llvm.LLVMAddIncoming(n_phi, &n, &from, 1);
    }
    llvm.LLVMPositionBuilderAtEnd(e->b, end);
    free(s);
}

/* The IR of a body (a GbBodyLoop's, `nested` 0) or of a list compiled as a
 * whole (a GbList's, `nested` 1), in a module of its own named after it. */
static LLVMModuleRef jit_build(LLVMContextRef c, const char *sym, AstStmtList *list, int nested,
                               unsigned long *runs, size_t *statements, size_t *lowered) {
    JitEmit e = { 0 };
    e.c = c;
    e.m = llvm.LLVMModuleCreateWithNameInContext(sym, c);
    e.ptr = llvm.LLVMPointerTypeInContext(c, 0);
    e.i32 = llvm.LLVMIntTypeInContext(c, 8 * sizeof(int));
    e.size = llvm.LLVMIntTypeInContext(c, 8 * sizeof(size_t));
    e.lng = llvm.LLVMIntTypeInContext(c, 8 * sizeof(long));
    LLVMTypeRef vd = llvm.LLVMVoidTypeInContext(c);
    LLVMTypeRef ptr_size[] = { e.ptr, e.size };
    LLVMTypeRef ptr2[] = { e.ptr, e.ptr };
    LLVMTypeRef ptr3[] = { e.ptr, e.ptr, e.ptr };
    e.stmt = jit_declare(&e, "gbrt_stmt", vd, ptr_size, 2);
    e.after = jit_declare(&e, "gbrt_after", e.lng, ptr_size, 2);
    e.list_begin = jit_declare(&e, "gbrt_list_begin", e.i32, ptr2, 2);
    e.list_stmt = jit_declare(&e, "gbrt_list_stmt", vd, ptr_size, 2);
    e.list_after = jit_declare(&e, "gbrt_list_after", e.lng, ptr_size, 2);
    e.if_ = jit_declare(&e, "gbrt_if", e.i32, ptr3, 3);
    e.if_done = jit_declare(&e, "gbrt_if_done", vd, ptr3, 3);
    e.while_enter = jit_declare(&e, "gbrt_while_enter", e.i32, ptr3, 3);
    e.while_test = jit_declare(&e, "gbrt_while_test", e.i32, ptr3, 3);
    e.while_next = jit_declare(&e, "gbrt_while_next", e.i32, ptr3, 3);
    LLVMTypeRef ptr4[] = { e.ptr, e.ptr, e.ptr, e.ptr };
    LLVMTypeRef ptr5[] = { e.ptr, e.ptr, e.ptr, e.ptr, e.ptr };
    e.for_enter = jit_declare(&e, "gbrt_for_enter", e.i32, ptr4, 4);
    e.for_test = jit_declare(&e, "gbrt_for_test", e.i32, ptr4, 4);
    e.for_next = jit_declare(&e, "gbrt_for_next", e.i32, ptr5, 5);
    LLVMTypeRef body_params[] = { e.ptr };
    LLVMValueRef fn = llvm.LLVMAddFunction(e.m, sym, llvm.LLVMFunctionType(vd, body_params, 1, 0));
    LLVMValueRef loop = llvm.LLVMGetParam(fn, 0);

    e.b = llvm.LLVMCreateBuilderInContext(c);
    e.entry = llvm.LLVMCreateBuilderInContext(c);
    LLVMBasicBlockRef entry = llvm.LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef start = llvm.LLVMAppendBasicBlockInContext(c, fn, "start");
    llvm.LLVMPositionBuilderAtEnd(e.entry, entry);
    if (runs) {
        LLVMTypeRef ul = llvm.LLVMIntTypeInContext(c, 8 * sizeof *runs);
        LLVMValueRef at = jit_address(&e, runs);
        LLVMValueRef was = llvm.LLVMBuildLoad2(e.entry, ul, at, "runs");
        llvm.LLVMBuildStore(e.entry, llvm.LLVMBuildAdd(e.entry, was, llvm.LLVMConstInt(ul, 1, 0), ""), at);
    }
    llvm.LLVMPositionBuilderAtEnd(e.b, start);
    jit_list(&e, loop, list, nested);
    llvm.LLVMBuildRetVoid(e.b);
    /* the entry block's allocas are all built: now it can end */
    llvm.LLVMBuildBr(e.entry, start);
    llvm.LLVMDisposeBuilder(e.entry);
    llvm.LLVMDisposeBuilder(e.b);
    *statements = e.statements;
    *lowered = e.lowered;
    return e.m;
}

/* Compiled once into *slot: the entry's address, or 0 to interpret. */
static uintptr_t jit_compile(void **slot, const char *name, AstStmtList *list, int nested) {
    size_t count = list->count;
    if (*slot) {
        return *slot == (void *)&jit_interpreted ? 0 : (uintptr_t)*slot;
    }
    /* refused until it is compiled: a failure is tried once, not per call */
    *slot = &jit_interpreted;
    if (!jit_start()) {
        jit_note(name, count, 0, NULL);
        return 0;
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
    size_t statements = 0, lowered = 0;
    LLVMModuleRef m = jit_build(c, sym, list, nested, runs, &statements, &lowered);
    char *why = NULL;
    if (llvm.LLVMVerifyModule(m, LLVMReturnStatusAction, &why)) {
        if (gb_jit_mode() == 2) {
            fprintf(stderr, "compile: %s: the IR did not verify: %s\n", name, why);
        }
        llvm.LLVMDisposeMessage(why);
        llvm.LLVMDisposeModule(m);
        free(runs);
        jit_note(name, count, 0, NULL);
        return 0;
    }
    llvm.LLVMDisposeMessage(why);
    LLVMOrcThreadSafeModuleRef tsm = llvm.LLVMOrcCreateNewThreadSafeModule(m, jit_ctx);
    LLVMErrorRef err = llvm.LLVMOrcLLJITAddLLVMIRModule(jit, llvm.LLVMOrcLLJITGetMainJITDylib(jit), tsm);
    if (err) {
        llvm.LLVMOrcDisposeThreadSafeModule(tsm);
        jit_error(name, err);
        free(runs);
        jit_note(name, count, 0, NULL);
        return 0;
    }
    LLVMOrcExecutorAddress addr = 0;
    err = llvm.LLVMOrcLLJITLookup(jit, &addr, sym);
    if (err || !addr) {
        if (err) {
            jit_error(name, err);
        }
        free(runs);
        jit_note(name, count, 0, NULL);
        return 0;
    }
    jit_note(name, statements, lowered, runs);
    *slot = (void *)(uintptr_t)addr;
    return (uintptr_t)addr;
}

GbJitBody gb_jit_body(AstStmt *fn) {
    return (GbJitBody)jit_compile(&fn->jit, fn->as.function.name, &fn->as.function.body, 0);
}

GbJitList gb_jit_list(AstStmtList *list, void **slot, const char *name) {
    return (GbJitList)jit_compile(slot, name, list, 1);
}

#else /* !HAVE_LLVM */

static int jit_report_armed;

static void jit_unavailable(void) {
    if (gb_jit_mode() == 2 && !jit_report_armed) {
        jit_report_armed = 1;
        fprintf(stderr, "compile: not available in this build (needs LLVM 18 or later); "
                        "every function is interpreted\n");
    }
}

GbJitBody gb_jit_body(AstStmt *fn) {
    fn->jit = &jit_interpreted;
    jit_unavailable();
    return NULL;
}

GbJitList gb_jit_list(AstStmtList *list, void **slot, const char *name) {
    (void)list;
    (void)name;
    *slot = &jit_interpreted;
    jit_unavailable();
    return NULL;
}

#endif

