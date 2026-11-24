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
    X(LLVMBuildAnd) \
    X(LLVMBuildExtractValue) \
    X(LLVMBuildBr) \
    X(LLVMBuildCall2) \
    X(LLVMBuildCondBr) \
    X(LLVMBuildICmp) \
    X(LLVMBuildInBoundsGEP2) \
    X(LLVMBuildLoad2) \
    X(LLVMBuildOr) \
    X(LLVMBuildPhi) \
    X(LLVMBuildRetVoid) \
    X(LLVMBuildSIToFP) \
    X(LLVMBuildStore) \
    X(LLVMBuildSwitch) \
    X(LLVMBuildZExt) \
    X(LLVMConstInt) \
    X(LLVMConstIntToPtr) \
    X(LLVMConsumeError) \
    X(LLVMCreateBuilderInContext) \
    X(LLVMCreateTargetMachine) \
    X(LLVMDisposeBuilder) \
    X(LLVMDisposeErrorMessage) \
    X(LLVMDisposeMessage) \
    X(LLVMDisposeModule) \
    X(LLVMDoubleTypeInContext) \
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
    X(LLVMStructTypeInContext) \
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
    size_t expressions;
    char *held;              /* the names held, sorted, or NULL */
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
    size_t built = 0, statements = 0, lowered = 0, expressions = 0;
    for (size_t i = 0; i < jit_row_count; i++) {
        JitRow *r = &jit_rows[i];
        if (r->failed) {
            fprintf(stderr, "compile: %s: not compiled, interpreted\n", r->name);
        } else {
            built++;
            statements += r->statements;
            lowered += r->lowered;
            expressions += r->expressions;
            fprintf(stderr, "compile: %s: %zu statement%s, %zu lowered, %zu expression%s, %s%s%sran %lu time%s\n",
                    r->name, r->statements, r->statements == 1 ? "" : "s", r->lowered,
                    r->expressions, r->expressions == 1 ? "" : "s",
                    r->held ? "holds " : "", r->held ? r->held : "", r->held ? ", " : "",
                    *r->runs, *r->runs == 1 ? "" : "s");
        }
    }
    fprintf(stderr, "compile: %zu bod%s compiled, %zu statement%s, %zu lowered, %zu expression%s\n",
            built, built == 1 ? "y" : "ies", statements, statements == 1 ? "" : "s", lowered,
            expressions, expressions == 1 ? "" : "s");
}

static int jit_name_order(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static void jit_note(const char *name, size_t statements, size_t lowered, size_t expressions,
                     unsigned long *runs, const char **held, size_t held_count) {
    if (gb_jit_mode() != 2) {
        return;
    }
    char *names = NULL;
    if (held_count) {
        const char **sorted = malloc(sizeof *sorted * held_count);
        size_t bytes = 1;
        if (!sorted) {
            abort();
        }
        for (size_t i = 0; i < held_count; i++) {
            sorted[i] = held[i];
            bytes += strlen(held[i]) + 1;
        }
        qsort(sorted, held_count, sizeof *sorted, jit_name_order);
        names = malloc(bytes);
        if (!names) {
            abort();
        }
        names[0] = '\0';
        for (size_t i = 0; i < held_count; i++) {
            strcat(names, i ? " " : "");
            strcat(names, sorted[i]);
        }
        free(sorted);
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
    jit_rows[jit_row_count].expressions = expressions;
    jit_rows[jit_row_count].held = names;
    jit_rows[jit_row_count].failed = runs == NULL;
    jit_rows[jit_row_count].runs = runs;
    jit_row_count++;
}

static LLVMOrcLLJITRef jit;
static LLVMOrcThreadSafeContextRef jit_ctx;
static int jit_broken;
/* what compiled code points at for the process's life (a body's held names),
 * freed when the JIT is */
static void **jit_kept;
static size_t jit_kept_count;
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
    for (size_t i = 0; i < jit_kept_count; i++) {
        free(jit_kept[i]);
    }
    free(jit_kept);
    jit_kept = NULL;
    jit_kept_count = 0;
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
        JIT_RUNTIME(gbrt_if_enter)
        JIT_RUNTIME(gbrt_if_truth)
        JIT_RUNTIME(gbrt_if_done)
        JIT_RUNTIME(gbrt_while_enter)
        JIT_RUNTIME(gbrt_while_truth)
        JIT_RUNTIME(gbrt_while_next)
        JIT_RUNTIME(gbrt_for_enter)
        JIT_RUNTIME(gbrt_for_test)
        JIT_RUNTIME(gbrt_for_next)
        JIT_RUNTIME(gbrt_for_test_slot)
        JIT_RUNTIME(gbrt_for_next_slot)
        JIT_RUNTIME(gbrt_slot_read)
        JIT_RUNTIME(gbrt_slots_spill)
        JIT_RUNTIME(gbrt_slot_assign)
        JIT_RUNTIME(gbrt_slots_retake)
        JIT_RUNTIME(gbrt_ident)
        JIT_RUNTIME(gbrt_eval)
        JIT_RUNTIME(gbrt_binary_enter)
        JIT_RUNTIME(gbrt_binary_left)
        JIT_RUNTIME(gbrt_binary_apply)
        JIT_RUNTIME(gbrt_call_enter)
        JIT_RUNTIME(gbrt_call_abandon)
        JIT_RUNTIME(gbrt_call_invoke)
        JIT_RUNTIME(gbrt_assign_begin)
        JIT_RUNTIME(gbrt_assign_end)
        JIT_RUNTIME(gbrt_return_begin)
        JIT_RUNTIME(gbrt_return_end)
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
    LLVMTypeRef ptr, i8, i32, i64, size, lng;
    LLVMValueRef list_bytes;
    JitCall stmt, after, list_begin, list_stmt, list_after;
    JitCall if_enter, if_truth, if_done, while_enter, while_truth, while_next;
    JitCall for_enter, for_test, for_next;
    JitCall ident, eval, binary_enter, binary_left, binary_apply;
    JitCall call_enter, call_abandon, call_invoke;
    JitCall assign_begin, assign_end, return_begin, return_end;
    JitCall slot_read, slots_spill, slot_assign, slots_retake, for_test_slot, for_next_slot;
    JitCall overflow[3];      /* llvm.s{add,sub,mul}.with.overflow.i64 */
    LLVMTypeRef dbl;
    GbLayout L;
    /* INCREMENT 3: the body's held names and their slots (none in a list
     * compiled as a whole, or under --profile) */
    const char **keys;
    size_t slot_count;
    size_t moving;            /* the first `moving` slots, spilled and retaken */
    LLVMValueRef *slot_at;
    LLVMValueRef slots;
    /* bytes made by the runtime at compile time: a statement's ordinary
     * result, an exact integer, `true` */
    unsigned char *no_result, *number, *boolean;
    size_t value_bytes;
    size_t statements;
    size_t lowered;
    size_t expressions;
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

/* a Value of the compiled code's own */
static LLVMValueRef jit_value(JitEmit *e) {
    return jit_bytes(e, e->value_bytes);
}

/* error_generation now, read as the interpreted statement reads it just
 * before evaluating */
static LLVMValueRef jit_error_mark(JitEmit *e) {
    return llvm.LLVMBuildLoad2(e->b, e->i32, jit_address(e, gbrt_error_generation()), "before");
}

/* the byte `off` bytes into `base` */
static LLVMValueRef jit_at(JitEmit *e, LLVMValueRef base, size_t off) {
    LLVMValueRef o = llvm.LLVMConstInt(e->size, off, 0);
    return llvm.LLVMBuildInBoundsGEP2(e->b, e->i8, base, &o, 1, "");
}

static LLVMValueRef jit_get(JitEmit *e, LLVMTypeRef ty, LLVMValueRef base, size_t off) {
    return llvm.LLVMBuildLoad2(e->b, ty, jit_at(e, base, off), "");
}

static void jit_store(JitEmit *e, LLVMValueRef v, LLVMValueRef base, size_t off) {
    llvm.LLVMBuildStore(e->b, v, jit_at(e, base, off));
}

/* an int of the interpreter's, read or written where it lives */
static LLVMValueRef jit_global(JitEmit *e, int *p) {
    return llvm.LLVMBuildLoad2(e->b, e->i32, jit_address(e, p), "");
}

static void jit_set_global(JitEmit *e, int *p, LLVMValueRef v) {
    llvm.LLVMBuildStore(e->b, v, jit_address(e, p));
}

static LLVMValueRef jit_i32(JitEmit *e, long v) {
    return llvm.LLVMConstInt(e->i32, (unsigned long long)v, 1);
}

static LLVMValueRef jit_is(JitEmit *e, LLVMValueRef v, long c) {
    return llvm.LLVMBuildICmp(e->b, LLVMIntEQ, v, jit_i32(e, c), "");
}

/* `n` bytes made at compile time, stored word by word: no call at all */
static void jit_store_bytes(JitEmit *e, const unsigned char *bytes, size_t n, LLVMValueRef out) {
    for (size_t k = 0; k < n; k += 8) {
        uint64_t w;
        memcpy(&w, bytes + k, sizeof w);
        jit_store(e, llvm.LLVMConstInt(e->i64, w, 0), out, k);
    }
}

/* a Value's bytes copied, word by word */
static void jit_copy_value(JitEmit *e, LLVMValueRef from, LLVMValueRef to) {
    for (size_t k = 0; k < e->value_bytes; k += 8) {
        jit_store(e, jit_get(e, e->i64, from, k), to, k);
    }
}

/* the slot holding `key`, or NULL when the body does not hold it */
static LLVMValueRef jit_slot(JitEmit *e, const char *key) {
    for (size_t i = 0; i < e->slot_count; i++) {
        if (e->keys[i] == key) {
            return e->slot_at[i];
        }
    }
    return NULL;
}

/* every held variable brought up to its slot: before anything that may
 * read one by name (an interpreted statement or expression, a `for`'s
 * bounds) */
static void jit_spill(JitEmit *e) {
    if (e->moving) {
        LLVMValueRef args[] = { e->slots, jit_address(e, e->keys),
                                llvm.LLVMConstInt(e->size, e->moving, 0) };
        jit_call(e, e->slots_spill, args, 3);
    }
}

/* control goes to `yes` when `cond`, else `no`; the builder is left in `yes` */
static void jit_if(JitEmit *e, LLVMValueRef cond, LLVMBasicBlockRef yes, LLVMBasicBlockRef no) {
    llvm.LLVMBuildCondBr(e->b, cond, yes, no);
    llvm.LLVMPositionBuilderAtEnd(e->b, yes);
}

/* raise_in_flight || runtime_stopped, error_action_pending's test */
static LLVMValueRef jit_pending(JitEmit *e) {
    LLVMValueRef either = llvm.LLVMBuildOr(e->b, jit_global(e, e->L.raise_in_flight),
                                           jit_global(e, e->L.runtime_stopped), "");
    return llvm.LLVMBuildICmp(e->b, LLVMIntNE, either, jit_i32(e, 0), "");
}

static void jit_expr(JitEmit *e, AstExpr *x, LLVMValueRef out);

/* A HELD NAME READ: the slot's number when it is set, else the runtime's
 * read of the frame's variable, which the slot may then take. */
static void jit_slot_read(JitEmit *e, AstExpr *x, LLVMValueRef s, LLVMValueRef out) {
    LLVMBasicBlockRef fast = jit_block(e, "held");
    LLVMBasicBlockRef slow = jit_block(e, "unheld");
    LLVMBasicBlockRef join = jit_block(e, "read");
    LLVMValueRef set = jit_get(e, e->i32, s, e->L.slot_set);
    jit_if(e, llvm.LLVMBuildICmp(e->b, LLVMIntNE, set, jit_i32(e, 0), ""), fast, slow);
    LLVMValueRef from = jit_at(e, s, e->L.slot_value);
    jit_copy_value(e, from, out);
    llvm.LLVMBuildBr(e->b, join);
    llvm.LLVMPositionBuilderAtEnd(e->b, slow);
    LLVMValueRef args[] = { jit_address(e, x), s, out };
    jit_call(e, e->slot_read, args, 3);
    llvm.LLVMBuildBr(e->b, join);
    llvm.LLVMPositionBuilderAtEnd(e->b, join);
}

/* AN OPERATOR: gbrt_binary_enter's work done here, the left operand, the
 * test gbrt_binary_left makes, the right; then, when gbrt_fast_op names the
 * operator and both values are exact integers with nothing pending, the
 * answer computed here as binary_apply computes it (an overflow goes to
 * binary_apply, which degrades with 2112); otherwise gbrt_binary_apply. */
static void jit_binary(JitEmit *e, AstExpr *x, LLVMValueRef out) {
    LLVMValueRef node = jit_address(e, x);
    LLVMValueRef saved = jit_alloca(e, llvm.LLVMArrayType2(e->i32, 2));
    LLVMValueRef l = jit_value(e);
    LLVMValueRef r = jit_value(e);
    LLVMValueRef line0 = jit_global(e, e->L.line);
    LLVMValueRef column0 = jit_global(e, e->L.column);
    jit_store(e, line0, saved, 0);
    jit_store(e, column0, saved, sizeof(int));
    if (x->line > 0) {
        jit_set_global(e, e->L.line, jit_i32(e, x->line));
        jit_set_global(e, e->L.column, x->column > 0 ? jit_i32(e, x->column) : column0);
    }
    jit_expr(e, x->as.binary.left, l);
    LLVMBasicBlockRef stop_b = jit_block(e, "stopped");
    LLVMBasicBlockRef right_b = jit_block(e, "right");
    LLVMBasicBlockRef join = jit_block(e, "applied");
    llvm.LLVMBuildCondBr(e->b, jit_pending(e), stop_b, right_b);
    llvm.LLVMPositionBuilderAtEnd(e->b, stop_b);
    LLVMValueRef left_args[] = { l, out, saved };
    jit_call(e, e->binary_left, left_args, 3);
    llvm.LLVMBuildBr(e->b, join);
    llvm.LLVMPositionBuilderAtEnd(e->b, right_b);
    jit_expr(e, x->as.binary.right, r);
    LLVMBasicBlockRef apply_b = jit_block(e, "apply");
    int fast_op = gbrt_fast_op(x);
    if (fast_op != GB_FAST_NONE && e->value_bytes % 8 == 0) {
        LLVMBasicBlockRef ints = jit_block(e, "ints");
        const GbLayout *L = &e->L;
        LLVMValueRef ok = llvm.LLVMBuildAnd(
            e->b,
            llvm.LLVMBuildAnd(e->b, jit_is(e, jit_get(e, e->i32, l, L->kind), L->number_kind),
                              jit_is(e, jit_get(e, e->i32, r, L->kind), L->number_kind), ""),
            llvm.LLVMBuildAnd(e->b,
                              llvm.LLVMBuildICmp(e->b, LLVMIntNE,
                                                 jit_get(e, e->i32, l, L->num_is_exact),
                                                 jit_i32(e, 0), ""),
                              llvm.LLVMBuildICmp(e->b, LLVMIntNE,
                                                 jit_get(e, e->i32, r, L->num_is_exact),
                                                 jit_i32(e, 0), ""), ""), "");
        LLVMValueRef none = llvm.LLVMBuildICmp(e->b, LLVMIntEQ,
                                               llvm.LLVMBuildOr(e->b, jit_global(e, L->raise_in_flight),
                                                                jit_global(e, L->runtime_stopped), ""),
                                               jit_i32(e, 0), "");
        jit_if(e, llvm.LLVMBuildAnd(e->b, ok, none, ""), ints, apply_b);
        LLVMValueRef a = jit_get(e, e->i64, l, L->num_exact);
        LLVMValueRef b = jit_get(e, e->i64, r, L->num_exact);
        if (fast_op == GB_FAST_ADD || fast_op == GB_FAST_SUB || fast_op == GB_FAST_MUL) {
            JitCall op = e->overflow[fast_op - GB_FAST_ADD];
            LLVMValueRef args[] = { a, b };
            LLVMValueRef pair = jit_call(e, op, args, 2);
            LLVMValueRef sum = llvm.LLVMBuildExtractValue(e->b, pair, 0, "");
            LLVMValueRef over = llvm.LLVMBuildExtractValue(e->b, pair, 1, "");
            LLVMBasicBlockRef fits = jit_block(e, "fits");
            jit_if(e, llvm.LLVMBuildICmp(e->b, LLVMIntEQ, llvm.LLVMBuildZExt(e->b, over, e->i32, ""),
                                         jit_i32(e, 0), ""), fits, apply_b);
            jit_store_bytes(e, e->number, e->value_bytes, out);
            jit_store(e, sum, out, L->num_exact);
            jit_store(e, llvm.LLVMBuildSIToFP(e->b, sum, e->dbl, ""), out, L->num_value);
        } else {
            static const LLVMIntPredicate preds[] = {
                [GB_FAST_EQ] = LLVMIntEQ, [GB_FAST_NE] = LLVMIntNE, [GB_FAST_LT] = LLVMIntSLT,
                [GB_FAST_LE] = LLVMIntSLE, [GB_FAST_GT] = LLVMIntSGT, [GB_FAST_GE] = LLVMIntSGE,
            };
            LLVMValueRef truth = llvm.LLVMBuildICmp(e->b, preds[fast_op], a, b, "");
            jit_store_bytes(e, e->boolean, e->value_bytes, out);
            jit_store(e, llvm.LLVMBuildZExt(e->b, truth, e->i32, ""), out, L->boolean);
        }
        jit_set_global(e, e->L.line, line0);
        jit_set_global(e, e->L.column, column0);
        llvm.LLVMBuildBr(e->b, join);
    } else {
        llvm.LLVMBuildBr(e->b, apply_b);
    }
    llvm.LLVMPositionBuilderAtEnd(e->b, apply_b);
    LLVMValueRef apply[] = { node, l, r, out, saved };
    jit_call(e, e->binary_apply, apply, 5);
    llvm.LLVMBuildBr(e->b, join);
    llvm.LLVMPositionBuilderAtEnd(e->b, join);
}

/* the most arguments a lowered call evaluates itself; past it, gbrt_eval */
#define JIT_CALL_ARGS 32

/* A PLAIN CALL: gbrt_call_enter finds the gBASIC function and checks the
 * count; its arguments evaluated here, by shape, into an array on the
 * compiled code's stack, each followed by eval_user_function_bound's test
 * for a raise; then gbrt_call_invoke. A call reaching no gBASIC function (a
 * builtin) is gbrt_eval's, after a spill, since it may read a name. Nothing
 * is spilled for the call itself: the callee's frame cannot see this one's
 * variables. */
static void jit_user_call(JitEmit *e, AstExpr *x, LLVMValueRef out) {
    LLVMValueRef node = jit_address(e, x);
    size_t n = x->as.call.args.count;
    LLVMValueRef fn = jit_alloca(e, e->ptr);
    LLVMValueRef enter_args[] = { node, fn, out };
    LLVMValueRef found = jit_call(e, e->call_enter, enter_args, 3);
    LLVMBasicBlockRef user = jit_block(e, "user");
    LLVMBasicBlockRef other = jit_block(e, "other");
    LLVMBasicBlockRef refused = jit_block(e, "refused");
    LLVMBasicBlockRef join = jit_block(e, "called");
    LLVMValueRef sw = llvm.LLVMBuildSwitch(e->b, found, refused, 2);
    llvm.LLVMAddCase(sw, jit_i32(e, 1), user);
    llvm.LLVMAddCase(sw, jit_i32(e, 0), other);
    llvm.LLVMPositionBuilderAtEnd(e->b, refused);
    llvm.LLVMBuildBr(e->b, join);
    llvm.LLVMPositionBuilderAtEnd(e->b, other);
    jit_spill(e);
    LLVMValueRef eval_args[] = { node, out };
    jit_call(e, e->eval, eval_args, 2);
    llvm.LLVMBuildBr(e->b, join);
    llvm.LLVMPositionBuilderAtEnd(e->b, user);
    LLVMValueRef args = jit_bytes(e, e->value_bytes * (n ? n : 1));
    for (size_t i = 0; i < n; i++) {
        jit_expr(e, x->as.call.args.items[i], jit_at(e, args, e->value_bytes * i));
        LLVMBasicBlockRef stop = jit_block(e, "abandoned");
        LLVMBasicBlockRef next = jit_block(e, "argument");
        llvm.LLVMBuildCondBr(e->b, jit_pending(e), stop, next);
        llvm.LLVMPositionBuilderAtEnd(e->b, stop);
        LLVMValueRef abandon[] = { args, llvm.LLVMConstInt(e->size, i + 1, 0), out };
        jit_call(e, e->call_abandon, abandon, 3);
        llvm.LLVMBuildBr(e->b, join);
        llvm.LLVMPositionBuilderAtEnd(e->b, next);
    }
    LLVMValueRef fn_now = llvm.LLVMBuildLoad2(e->b, e->ptr, fn, "");
    LLVMValueRef invoke[] = { fn_now, args, llvm.LLVMConstInt(e->size, n, 0), out };
    jit_call(e, e->call_invoke, invoke, 4);
    llvm.LLVMBuildBr(e->b, join);
    llvm.LLVMPositionBuilderAtEnd(e->b, join);
}

/* STEP 3: `x` EVALUATED into the Value at `out`, a node at a time by the
 * shape eval.c gives it (gbrt_expr_shape, gb_jit.h). */
static void jit_expr(JitEmit *e, AstExpr *x, LLVMValueRef out) {
    LLVMValueRef node = jit_address(e, x);
    int shape = gbrt_expr_shape(x);
    if (shape == GB_EXPR_CONSTANT && e->value_bytes % 8 == 0) {
        /* the literal's bytes, made once by eval_expr */
        e->expressions++;
        unsigned char *bytes = malloc(e->value_bytes);
        if (!bytes) {
            abort();
        }
        gbrt_constant(x, bytes);
        jit_store_bytes(e, bytes, e->value_bytes, out);
        free(bytes);
        return;
    }
    if (shape == GB_EXPR_IDENT) {
        e->expressions++;
        LLVMValueRef s = jit_slot(e, gbrt_ident_key(x));
        if (s) {
            jit_slot_read(e, x, s, out);
            return;
        }
        LLVMValueRef args[] = { node, out };
        jit_call(e, e->ident, args, 2);
        return;
    }
    if (shape == GB_EXPR_BINARY) {
        e->expressions++;
        jit_binary(e, x, out);
        return;
    }
    if (shape == GB_EXPR_CALL && x->as.call.args.count <= JIT_CALL_ARGS) {
        e->expressions++;
        jit_user_call(e, x, out);
        return;
    }
    jit_spill(e);
    LLVMValueRef args[] = { node, out };
    jit_call(e, e->eval, args, 2);
}

/* A CONDITION'S TRUTH: a boolean with no raise since `before` is read here,
 * as truth_of reads it with nothing else to do; anything else is the
 * runtime's `truth` call, which answers 1, 0 or -1. `only_true`: the
 * runtime is asked for every answer but 1 (a `while` ending has work to
 * do). */
static LLVMValueRef jit_truth(JitEmit *e, JitCall truth, LLVMValueRef state, LLVMValueRef saved,
                              LLVMValueRef before, LLVMValueRef v, int only_true) {
    LLVMBasicBlockRef fast = jit_block(e, "bool");
    LLVMBasicBlockRef slow = jit_block(e, "truth");
    LLVMBasicBlockRef join = jit_block(e, "known");
    LLVMValueRef same = llvm.LLVMBuildICmp(e->b, LLVMIntEQ, jit_global(e, e->L.error_generation),
                                           before, "");
    LLVMValueRef ok = llvm.LLVMBuildAnd(e->b, same,
                                        jit_is(e, jit_get(e, e->i32, v, e->L.kind), e->L.bool_kind), "");
    if (only_true) {
        ok = llvm.LLVMBuildAnd(e->b, ok,
                               llvm.LLVMBuildICmp(e->b, LLVMIntNE, jit_get(e, e->i32, v, e->L.boolean),
                                                  jit_i32(e, 0), ""), "");
    }
    jit_if(e, ok, fast, slow);
    LLVMValueRef known = llvm.LLVMBuildZExt(
        e->b, llvm.LLVMBuildICmp(e->b, LLVMIntNE, jit_get(e, e->i32, v, e->L.boolean), jit_i32(e, 0), ""),
        e->i32, "");
    llvm.LLVMBuildBr(e->b, join);
    LLVMBasicBlockRef fast_end = llvm.LLVMGetInsertBlock(e->b);
    llvm.LLVMPositionBuilderAtEnd(e->b, slow);
    LLVMValueRef args[] = { state, saved, before, v };
    LLVMValueRef asked = jit_call(e, truth, args, 4);
    llvm.LLVMBuildBr(e->b, join);
    LLVMBasicBlockRef slow_end = llvm.LLVMGetInsertBlock(e->b);
    llvm.LLVMPositionBuilderAtEnd(e->b, join);
    LLVMValueRef phi = llvm.LLVMBuildPhi(e->b, e->i32, "truth");
    LLVMValueRef values[] = { known, asked };
    LLVMBasicBlockRef from[] = { fast_end, slow_end };
    llvm.LLVMAddIncoming(phi, values, from, 2);
    return phi;
}

/* a statement lowered as begin, its expression, end: begin answers 0 to go
 * on, else the statement is done; the builder is left after it */
static LLVMBasicBlockRef jit_begun(JitEmit *e, LLVMValueRef begun) {
    LLVMBasicBlockRef body = jit_block(e, "begun");
    LLVMBasicBlockRef join = jit_block(e, "done");
    llvm.LLVMBuildCondBr(e->b, llvm.LLVMBuildICmp(e->b, LLVMIntEQ, begun,
                                                    llvm.LLVMConstInt(e->i32, 0, 0), ""),
                         body, join);
    llvm.LLVMPositionBuilderAtEnd(e->b, body);
    return join;
}

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
        LLVMValueRef v = jit_value(e);
        LLVMValueRef args[] = { jit_address(e, stmt), state, saved };
        LLVMBasicBlockRef join = jit_begun(e, jit_call(e, e->if_enter, args, 3));
        LLVMValueRef before = jit_error_mark(e);
        jit_expr(e, stmt->as.if_stmt.condition, v);
        LLVMValueRef truth = jit_truth(e, e->if_truth, state, saved, before, v, 0);
        LLVMBasicBlockRef then_b = jit_block(e, "then");
        LLVMBasicBlockRef else_b = jit_block(e, "else");
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
        LLVMValueRef v = jit_value(e);
        LLVMValueRef args[] = { jit_address(e, stmt), state, saved };
        LLVMBasicBlockRef test = jit_block(e, "test");
        LLVMBasicBlockRef body = jit_block(e, "pass");
        LLVMBasicBlockRef join = jit_block(e, "endwhile");
        LLVMValueRef entered = jit_call(e, e->while_enter, args, 3);
        llvm.LLVMBuildCondBr(e->b, llvm.LLVMBuildICmp(e->b, LLVMIntEQ, entered, zero, ""), test, join);
        llvm.LLVMPositionBuilderAtEnd(e->b, test);
        LLVMValueRef before = jit_error_mark(e);
        jit_expr(e, stmt->as.while_stmt.condition, v);
        LLVMValueRef truth = jit_truth(e, e->while_truth, state, saved, before, v, 1);
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
        /* the bounds are evaluated by name */
        jit_spill(e);
        LLVMValueRef entered = jit_call(e, e->for_enter, args, 4);
        llvm.LLVMBuildCondBr(e->b, llvm.LLVMBuildICmp(e->b, LLVMIntEQ, entered, zero, ""), test, join);
        llvm.LLVMPositionBuilderAtEnd(e->b, test);
        LLVMValueRef s = jit_slot(e, gbrt_for_key(stmt));
        LLVMValueRef test_args[] = { node, state, saved, f, s };
        LLVMValueRef truth = s ? jit_call(e, e->for_test_slot, test_args, 5)
                               : jit_call(e, e->for_test, args, 4);
        llvm.LLVMBuildCondBr(e->b, llvm.LLVMBuildICmp(e->b, LLVMIntEQ, truth, one, ""), body, join);
        llvm.LLVMPositionBuilderAtEnd(e->b, body);
        jit_nested(e, l, &stmt->as.for_range.body);
        LLVMValueRef next_args[] = { node, state, l, saved, f, s };
        LLVMValueRef again = s ? jit_call(e, e->for_next_slot, next_args, 6)
                               : jit_call(e, e->for_next, next_args, 5);
        llvm.LLVMBuildCondBr(e->b, llvm.LLVMBuildICmp(e->b, LLVMIntEQ, again, one, ""), test, join);
        llvm.LLVMPositionBuilderAtEnd(e->b, join);
        return;
    }
    LLVMValueRef held = gbrt_assign_plain(stmt)
        ? jit_slot(e, gbrt_ident_key(stmt->as.assign.target)) : NULL;
    if (held) {
        /* A HELD NAME ASSIGNED: announced as gbrt_assign_begin announces it,
         * the value, then a number with no raise and no watch anywhere is
         * the slot's, the statement's result the ordinary one; anything else
         * is gbrt_slot_assign, the interpreted assignment's own end */
        e->lowered++;
        LLVMValueRef saved = jit_alloca(e, llvm.LLVMArrayType2(e->i32, 2));
        LLVMValueRef v = jit_value(e);
        LLVMValueRef node = jit_address(e, stmt);
        jit_store(e, jit_global(e, e->L.line), saved, 0);
        jit_store(e, jit_global(e, e->L.column), saved, sizeof(int));
        jit_set_global(e, e->L.line, jit_i32(e, stmt->line));
        jit_set_global(e, e->L.column, jit_i32(e, stmt->column));
        LLVMValueRef before = jit_error_mark(e);
        jit_expr(e, stmt->as.assign.value, v);
        LLVMBasicBlockRef fast = jit_block(e, "kept");
        LLVMBasicBlockRef slow = jit_block(e, "assign");
        LLVMBasicBlockRef join = jit_block(e, "assigned");
        LLVMValueRef ok = llvm.LLVMBuildAnd(
            e->b,
            llvm.LLVMBuildICmp(e->b, LLVMIntEQ, jit_global(e, e->L.error_generation), before, ""),
            jit_is(e, jit_get(e, e->i32, v, e->L.kind), e->L.number_kind), "");
        ok = llvm.LLVMBuildAnd(e->b, ok,
                               llvm.LLVMBuildICmp(e->b, LLVMIntNE,
                                                  jit_get(e, e->i32, held, e->L.slot_set),
                                                  jit_i32(e, 0), ""), "");
        ok = llvm.LLVMBuildAnd(e->b, ok,
                               llvm.LLVMBuildICmp(e->b, LLVMIntEQ,
                                                  llvm.LLVMBuildLoad2(e->b, e->size,
                                                                      jit_address(e, e->L.watcher_count), ""),
                                                  llvm.LLVMConstInt(e->size, 0, 0), ""), "");
        jit_if(e, ok, fast, slow);
        jit_copy_value(e, v, jit_at(e, held, e->L.slot_value));
        jit_store(e, jit_i32(e, 1), held, e->L.slot_dirty);
        jit_set_global(e, e->L.line, jit_get(e, e->i32, saved, 0));
        jit_set_global(e, e->L.column, jit_get(e, e->i32, saved, sizeof(int)));
        jit_store_bytes(e, e->no_result, e->L.result_bytes, state);
        llvm.LLVMBuildBr(e->b, join);
        llvm.LLVMPositionBuilderAtEnd(e->b, slow);
        LLVMValueRef args[] = { node, state, saved, before, v, held };
        jit_call(e, e->slot_assign, args, 6);
        llvm.LLVMBuildBr(e->b, join);
        llvm.LLVMPositionBuilderAtEnd(e->b, join);
        return;
    }
    if (gbrt_assign_plain(stmt)) {
        e->lowered++;
        LLVMValueRef saved = jit_alloca(e, llvm.LLVMArrayType2(e->i32, 2));
        LLVMValueRef a = jit_bytes(e, gbrt_assign_bytes());
        LLVMValueRef v = jit_value(e);
        LLVMValueRef node = jit_address(e, stmt);
        LLVMValueRef args[] = { node, state, saved, a };
        LLVMBasicBlockRef join = jit_begun(e, jit_call(e, e->assign_begin, args, 4));
        jit_expr(e, stmt->as.assign.value, v);
        LLVMValueRef end[] = { node, state, saved, a, v };
        jit_call(e, e->assign_end, end, 5);
        llvm.LLVMBuildBr(e->b, join);
        llvm.LLVMPositionBuilderAtEnd(e->b, join);
        return;
    }
    if (stmt->kind == AST_STMT_RETURN && stmt->as.return_expr) {
        e->lowered++;
        LLVMValueRef saved = jit_alloca(e, llvm.LLVMArrayType2(e->i32, 2));
        LLVMValueRef v = jit_value(e);
        LLVMValueRef node = jit_address(e, stmt);
        LLVMValueRef args[] = { node, state, saved };
        LLVMBasicBlockRef join = jit_begun(e, jit_call(e, e->return_begin, args, 3));
        LLVMValueRef before = jit_error_mark(e);
        jit_expr(e, stmt->as.return_expr, v);
        LLVMValueRef end[] = { node, state, saved, before, v };
        jit_call(e, e->return_end, end, 5);
        llvm.LLVMBuildBr(e->b, join);
        llvm.LLVMPositionBuilderAtEnd(e->b, join);
        return;
    }
    jit_spill(e);
    LLVMValueRef args[] = { state, llvm.LLVMConstInt(e->size, index, 0) };
    jit_call(e, nested ? e->list_stmt : e->stmt, args, 2);
    /* the interpreter may have assigned a held name (`s += 1`, anything in a
     * `for each`'s body): every slot takes its variable again */
    if (e->moving) {
        LLVMValueRef retake[] = { e->slots, jit_address(e, e->keys),
                                  llvm.LLVMConstInt(e->size, e->moving, 0) };
        jit_call(e, e->slots_retake, retake, 3);
    }
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
static LLVMModuleRef jit_build(LLVMContextRef c, const char *sym, AstStmt *fn, AstStmtList *list,
                               int nested, unsigned long *runs, size_t *statements, size_t *lowered,
                               size_t *expressions, const char ***keys, size_t *held) {
    JitEmit e = { 0 };
    e.c = c;
    e.m = llvm.LLVMModuleCreateWithNameInContext(sym, c);
    e.ptr = llvm.LLVMPointerTypeInContext(c, 0);
    e.i8 = llvm.LLVMIntTypeInContext(c, 8);
    e.i32 = llvm.LLVMIntTypeInContext(c, 8 * sizeof(int));
    e.i64 = llvm.LLVMIntTypeInContext(c, 64);
    e.value_bytes = gbrt_value_bytes();
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
    e.if_enter = jit_declare(&e, "gbrt_if_enter", e.i32, ptr3, 3);
    e.if_done = jit_declare(&e, "gbrt_if_done", vd, ptr3, 3);
    e.while_enter = jit_declare(&e, "gbrt_while_enter", e.i32, ptr3, 3);

    e.while_next = jit_declare(&e, "gbrt_while_next", e.i32, ptr3, 3);
    LLVMTypeRef ptr4[] = { e.ptr, e.ptr, e.ptr, e.ptr };
    LLVMTypeRef ptr5[] = { e.ptr, e.ptr, e.ptr, e.ptr, e.ptr };
    e.for_enter = jit_declare(&e, "gbrt_for_enter", e.i32, ptr4, 4);
    e.for_test = jit_declare(&e, "gbrt_for_test", e.i32, ptr4, 4);
    e.for_next = jit_declare(&e, "gbrt_for_next", e.i32, ptr5, 5);
    LLVMTypeRef truth_params[] = { e.ptr, e.ptr, e.i32, e.ptr };
    e.if_truth = jit_declare(&e, "gbrt_if_truth", e.i32, truth_params, 4);
    e.while_truth = jit_declare(&e, "gbrt_while_truth", e.i32, truth_params, 4);
    e.ident = jit_declare(&e, "gbrt_ident", vd, ptr2, 2);
    e.eval = jit_declare(&e, "gbrt_eval", vd, ptr2, 2);
    e.binary_enter = jit_declare(&e, "gbrt_binary_enter", vd, ptr2, 2);
    e.binary_left = jit_declare(&e, "gbrt_binary_left", e.i32, ptr3, 3);
    e.binary_apply = jit_declare(&e, "gbrt_binary_apply", vd, ptr5, 5);
    e.call_enter = jit_declare(&e, "gbrt_call_enter", e.i32, ptr3, 3);
    LLVMTypeRef call_params[] = { e.ptr, e.ptr, e.size, e.ptr };
    LLVMTypeRef abandon_params[] = { e.ptr, e.size, e.ptr };
    e.call_abandon = jit_declare(&e, "gbrt_call_abandon", vd, abandon_params, 3);
    e.call_invoke = jit_declare(&e, "gbrt_call_invoke", vd, call_params, 4);
    e.assign_begin = jit_declare(&e, "gbrt_assign_begin", e.i32, ptr4, 4);
    e.assign_end = jit_declare(&e, "gbrt_assign_end", vd, ptr5, 5);
    e.return_begin = jit_declare(&e, "gbrt_return_begin", e.i32, ptr3, 3);
    LLVMTypeRef return_end_params[] = { e.ptr, e.ptr, e.ptr, e.i32, e.ptr };
    e.return_end = jit_declare(&e, "gbrt_return_end", vd, return_end_params, 5);
    e.slot_read = jit_declare(&e, "gbrt_slot_read", vd, ptr3, 3);
    LLVMTypeRef spill_params[] = { e.ptr, e.ptr, e.size };
    e.slots_spill = jit_declare(&e, "gbrt_slots_spill", vd, spill_params, 3);
    LLVMTypeRef slot_assign_params[] = { e.ptr, e.ptr, e.ptr, e.i32, e.ptr, e.ptr };
    e.slot_assign = jit_declare(&e, "gbrt_slot_assign", vd, slot_assign_params, 6);
    e.slots_retake = jit_declare(&e, "gbrt_slots_retake", vd, spill_params, 3);
    e.for_test_slot = jit_declare(&e, "gbrt_for_test_slot", e.i32, ptr5, 5);
    LLVMTypeRef ptr6[] = { e.ptr, e.ptr, e.ptr, e.ptr, e.ptr, e.ptr };
    e.for_next_slot = jit_declare(&e, "gbrt_for_next_slot", e.i32, ptr6, 6);
    LLVMTypeRef i64x2[] = { e.i64, e.i64 };
    LLVMTypeRef pair_fields[] = { e.i64, llvm.LLVMIntTypeInContext(c, 1) };
    LLVMTypeRef pair = llvm.LLVMStructTypeInContext(c, pair_fields, 2, 0);
    e.overflow[0] = jit_declare(&e, "llvm.sadd.with.overflow.i64", pair, i64x2, 2);
    e.overflow[1] = jit_declare(&e, "llvm.ssub.with.overflow.i64", pair, i64x2, 2);
    e.overflow[2] = jit_declare(&e, "llvm.smul.with.overflow.i64", pair, i64x2, 2);
    e.dbl = llvm.LLVMDoubleTypeInContext(c);
    gbrt_layout(&e.L);
    e.no_result = malloc(e.L.result_bytes);
    e.number = malloc(e.value_bytes);
    e.boolean = malloc(e.value_bytes);
    if (!e.no_result || !e.number || !e.boolean) {
        abort();
    }
    gbrt_templates(e.no_result, e.number, e.boolean);
    /* INCREMENT 3: a function's held names, each a slot on this frame's
     * stack, unset at every call */
    *keys = NULL;
    if (fn && gbrt_slots_allowed() && e.value_bytes % 8 == 0 && e.L.result_bytes % 8 == 0) {
        e.slot_count = gbrt_slot_names(fn, &e.keys, &e.moving);
        *keys = e.keys;
    }
    *held = e.slot_count;
    LLVMTypeRef body_params[] = { e.ptr };
    LLVMValueRef body = llvm.LLVMAddFunction(e.m, sym, llvm.LLVMFunctionType(vd, body_params, 1, 0));
    LLVMValueRef loop = llvm.LLVMGetParam(body, 0);

    e.b = llvm.LLVMCreateBuilderInContext(c);
    e.entry = llvm.LLVMCreateBuilderInContext(c);
    LLVMBasicBlockRef entry = llvm.LLVMAppendBasicBlockInContext(c, body, "entry");
    LLVMBasicBlockRef start = llvm.LLVMAppendBasicBlockInContext(c, body, "start");
    llvm.LLVMPositionBuilderAtEnd(e.entry, entry);
    if (runs) {
        LLVMTypeRef ul = llvm.LLVMIntTypeInContext(c, 8 * sizeof *runs);
        LLVMValueRef at = jit_address(&e, runs);
        LLVMValueRef was = llvm.LLVMBuildLoad2(e.entry, ul, at, "runs");
        llvm.LLVMBuildStore(e.entry, llvm.LLVMBuildAdd(e.entry, was, llvm.LLVMConstInt(ul, 1, 0), ""), at);
    }
    if (e.slot_count) {
        size_t bytes = gbrt_slot_bytes();
        e.slots = jit_alloca(&e, llvm.LLVMArrayType2(e.i8, (uint64_t)(bytes * e.slot_count)));
        e.slot_at = malloc(sizeof *e.slot_at * e.slot_count);
        if (!e.slot_at) {
            abort();
        }
        for (size_t i = 0; i < e.slot_count; i++) {
            LLVMValueRef off = llvm.LLVMConstInt(e.size, bytes * i, 0);
            e.slot_at[i] = llvm.LLVMBuildInBoundsGEP2(e.entry, e.i8, e.slots, &off, 1, "");
            LLVMValueRef zero = llvm.LLVMConstInt(e.i32, 0, 0);
            LLVMValueRef set_off = llvm.LLVMConstInt(e.size, e.L.slot_set, 0);
            LLVMValueRef dirty_off = llvm.LLVMConstInt(e.size, e.L.slot_dirty, 0);
            llvm.LLVMBuildStore(e.entry, zero,
                                llvm.LLVMBuildInBoundsGEP2(e.entry, e.i8, e.slot_at[i], &set_off, 1, ""));
            llvm.LLVMBuildStore(e.entry, zero,
                                llvm.LLVMBuildInBoundsGEP2(e.entry, e.i8, e.slot_at[i], &dirty_off, 1, ""));
        }
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
    *expressions = e.expressions;
    free(e.slot_at);
    free(e.no_result);
    free(e.number);
    free(e.boolean);
    return e.m;
}

/* Compiled once into *slot: the entry's address, or 0 to interpret. */
static uintptr_t jit_compile(void **slot, const char *name, AstStmt *fn, AstStmtList *list,
                             int nested) {
    size_t count = list->count;
    if (*slot) {
        return *slot == (void *)&jit_interpreted ? 0 : (uintptr_t)*slot;
    }
    /* refused until it is compiled: a failure is tried once, not per call */
    *slot = &jit_interpreted;
    if (!jit_start()) {
        jit_note(name, count, 0, 0, NULL, NULL, 0);
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
    size_t statements = 0, lowered = 0, expressions = 0;
    const char **keys = NULL;
    size_t held = 0;
    LLVMModuleRef m = jit_build(c, sym, fn, list, nested, runs, &statements, &lowered, &expressions,
                                &keys, &held);
    /* the held names are read by the compiled code while it lives */
    if (keys) {
        void **grown = realloc(jit_kept, sizeof *grown * (jit_kept_count + 1));
        if (!grown) {
            abort();
        }
        grown[jit_kept_count++] = (void *)keys;
        jit_kept = grown;
    }
    char *why = NULL;
    if (llvm.LLVMVerifyModule(m, LLVMReturnStatusAction, &why)) {
        if (gb_jit_mode() == 2) {
            fprintf(stderr, "compile: %s: the IR did not verify: %s\n", name, why);
        }
        llvm.LLVMDisposeMessage(why);
        llvm.LLVMDisposeModule(m);
        free(runs);
        jit_note(name, count, 0, 0, NULL, NULL, 0);
        return 0;
    }
    llvm.LLVMDisposeMessage(why);
    LLVMOrcThreadSafeModuleRef tsm = llvm.LLVMOrcCreateNewThreadSafeModule(m, jit_ctx);
    LLVMErrorRef err = llvm.LLVMOrcLLJITAddLLVMIRModule(jit, llvm.LLVMOrcLLJITGetMainJITDylib(jit), tsm);
    if (err) {
        llvm.LLVMOrcDisposeThreadSafeModule(tsm);
        jit_error(name, err);
        free(runs);
        jit_note(name, count, 0, 0, NULL, NULL, 0);
        return 0;
    }
    LLVMOrcExecutorAddress addr = 0;
    err = llvm.LLVMOrcLLJITLookup(jit, &addr, sym);
    if (err || !addr) {
        if (err) {
            jit_error(name, err);
        }
        free(runs);
        jit_note(name, count, 0, 0, NULL, NULL, 0);
        return 0;
    }
    jit_note(name, statements, lowered, expressions, runs, keys, held);
    *slot = (void *)(uintptr_t)addr;
    return (uintptr_t)addr;
}

GbJitBody gb_jit_body(AstStmt *fn) {
    return (GbJitBody)jit_compile(&fn->jit, fn->as.function.name, fn, &fn->as.function.body, 0);
}

GbJitList gb_jit_list(AstStmtList *list, void **slot, const char *name) {
    return (GbJitList)jit_compile(slot, name, NULL, list, 1);
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

