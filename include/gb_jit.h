#ifndef GB_JIT_H
#define GB_JIT_H

#include <stddef.h>
#include <stdio.h>

#include "ast.h"

/* The compiled body of a function (docs/llvm_backend_design.md §4, increment
 * 2). src/jit.c is the only file that includes LLVM's headers; the
 * interpreter sees this seam and nothing else.
 *
 * A compiled body replaces invoke_function's statement loop and nothing
 * around it. It is handed the loop's state, which only src/eval.c can read,
 * and calls back into the runtime below for everything it does. */
typedef struct GbBodyLoop GbBodyLoop;
typedef void (*GbJitBody)(GbBodyLoop *loop);

/* THE RUNTIME a compiled body calls, defined in src/eval.c and given to the
 * JIT by address. gbrt_stmt runs statement `index` of the body, as the loop
 * does; gbrt_after then decides what the loop decides after it, and answers
 * the index of the statement to run next (the body's count to end normally)
 * or -1 to leave. */
void gbrt_stmt(GbBodyLoop *loop, size_t index);
long gbrt_after(GbBodyLoop *loop, size_t index);

/* STEP 2: NESTED LISTS AND LOWERED STATEMENTS. A nested list (an `if`'s
 * branch, a `while`'s body) is unrolled as the body is, over a GbList the
 * compiled code keeps on its own stack (gbrt_list_bytes() bytes): begin
 * answers 0 to run it or -1 to leave (an interrupt), and gbrt_list_stmt and
 * gbrt_list_after are gbrt_stmt and gbrt_after for a list, the same calls
 * eval_stmt_list makes.
 *
 * A lowered statement leaves its result where gbrt_stmt would, at `at`: the
 * state of the list it is in (a GbBodyLoop or a GbList, whose result comes
 * first), and the list's after-call then runs as for any statement. `saved`
 * is two ints of the compiled code's, the line and column put back at the
 * statement's end. Under --profile the first call runs the whole statement
 * interpreted, so the profiler times it as it does any other.
 *
 * The interpreted `if`, `while` and `for` call the same functions after
 * their first (eval_stmt has announced them already), so a lowered
 * statement cannot decide anything differently. */
typedef struct GbList GbList;
size_t gbrt_list_bytes(void);
int gbrt_list_begin(GbList *l, AstStmtList *list);
void gbrt_list_stmt(GbList *l, size_t index);
long gbrt_list_after(GbList *l, size_t index);
/* `if`: enter (0, or -1 done with the result at `at`), the condition
 * evaluated (below), then truth: 1 the body, 0 the else body, -1 done; then
 * gbrt_if_done once the branch has run */
int gbrt_if_enter(AstStmt *stmt, void *at, int *saved);
int gbrt_if_truth(void *at, const int *saved, int before_error, void *value);
void gbrt_if_done(void *at, GbList *branch, const int *saved);
/* `while`: enter (0, or -1 done), then test (1 the body, else done) and,
 * after each pass of the body, next (1 test again, else done) */
int gbrt_while_enter(AstStmt *stmt, void *at, int *saved);
int gbrt_while_test(AstStmt *stmt, void *at, const int *saved);
int gbrt_while_next(void *at, GbList *body, const int *saved);
/* the test with the condition evaluated by the compiled code */
int gbrt_while_truth(void *at, const int *saved, int before_error, void *value);
/* `for ... to`, over a GbFor of gbrt_for_bytes() bytes the compiled code
 * keeps: enter (0, or -1 done), then test (1 the body, the counter set, else
 * done) and, after each pass, next (1 test again, the counter stepped, else
 * done). The interpreted `for` calls the same three. */
typedef struct GbFor GbFor;
size_t gbrt_for_bytes(void);
int gbrt_for_enter(AstStmt *stmt, void *at, int *saved, GbFor *f);
int gbrt_for_test(AstStmt *stmt, void *at, const int *saved, GbFor *f);
int gbrt_for_next(AstStmt *stmt, void *at, GbList *body, const int *saved, GbFor *f);
/* `do ... until`: gbrt_while_enter, the body, gbrt_while_next (a pass ends
 * as a `while`'s does), then the stop condition evaluated by the compiled
 * code and read by gbrt_do_truth: 0 to run the body again, else done.
 * `for each`, over a GbEach of gbrt_each_bytes(): enter (0, or -1 done),
 * then test (1 the body, the element assigned, else done) and, after each
 * pass, next (1 test again, else done). The interpreted loops call the
 * same functions. */
int gbrt_do_truth(void *at, const int *saved, int before_error, void *value);
typedef struct GbEach GbEach;
size_t gbrt_each_bytes(void);
int gbrt_each_enter(AstStmt *stmt, void *at, int *saved, GbEach *each);
int gbrt_each_test(AstStmt *stmt, void *at, const int *saved, GbEach *each);
int gbrt_each_next(AstStmt *stmt, void *at, GbList *body, const int *saved, GbEach *each);

/* STEP 3: EXPRESSIONS. A lowered expression is evaluated into a value the
 * compiled code keeps (gbrt_value_bytes() bytes, a Value only src/eval.c
 * reads), one node at a time by its shape: a constant's bytes, made at
 * compile time by gbrt_constant, stored as they are; an identifier by
 * gbrt_ident; an operator by gbrt_binary_enter, its left operand,
 * gbrt_binary_left (1: a raise, *out is the value, the right is skipped),
 * its right, then gbrt_binary_apply, the calls the interpreted operator
 * makes; anything else by gbrt_eval, which is the interpreter's. Each takes
 * its operands' values. `before_error` for a condition or a return is read
 * from gbrt_error_generation() just before the expression, as the
 * interpreted statement reads it. */
enum { GB_EXPR_OTHER, GB_EXPR_CONSTANT, GB_EXPR_IDENT, GB_EXPR_BINARY, GB_EXPR_CALL,
       GB_EXPR_NEGATE };
size_t gbrt_value_bytes(void);
int gbrt_expr_shape(AstExpr *x);
void gbrt_constant(AstExpr *x, void *out);
void gbrt_ident(AstExpr *x, void *out);
void gbrt_eval(AstExpr *x, void *out);
int *gbrt_error_generation(void);
void gbrt_binary_enter(AstExpr *x, int *saved);
int gbrt_binary_left(void *left, void *out, const int *saved);
void gbrt_binary_apply(AstExpr *x, void *left, void *right, void *out, const int *saved);
/* `-x` (GB_EXPR_NEGATE): the operand, then an exact integer other than the
 * least negated by the compiled code, anything else by gbrt_negate, the
 * interpreted operator's own end, which takes the operand */
void gbrt_negate(AstExpr *x, void *value, void *out);
/* A call by name (GB_EXPR_CALL): gbrt_call_enter answers 1 with the gBASIC
 * function it reaches at target[0] (its statement, since an argument may
 * move the function table), a method's receiver at target[1] and a function
 * value's `bound` context at target[2], 0 when it reaches none (gbrt_eval it
 * whole), -1 with *out `nothing` when the count of arguments is refused or
 * the method is unknown; then each argument into the array `args`,
 * gbrt_call_abandon(args, evaluated, out) at the first raise, else
 * gbrt_call_invoke(target, args, count, out), which takes the arguments and
 * appends the context, so `args` has room for one past `count`. */
int gbrt_call_enter(AstExpr *x, void **target, void *out);
void gbrt_call_abandon(void *args, size_t count, void *out);
void gbrt_call_invoke(void **target, void *args, size_t count, void *out);
/* `name = expression` with no operator, modifier or lock (gbrt_assign_plain),
 * over a GbAssign of gbrt_assign_bytes(): begin (0, or -1 done), the value,
 * end. `return expression`: begin (0, or -1 done), the value, end. The
 * interpreted statements end in the same functions end calls. */
typedef struct GbAssign GbAssign;
size_t gbrt_assign_bytes(void);
int gbrt_assign_plain(AstStmt *stmt);
int gbrt_assign_begin(AstStmt *stmt, void *at, int *saved, GbAssign *a);
void gbrt_assign_end(AstStmt *stmt, void *at, const int *saved, GbAssign *a, void *value);
int gbrt_return_begin(AstStmt *stmt, void *at, int *saved);
void gbrt_return_end(AstStmt *stmt, void *at, const int *saved, int before_error, void *value);

/* INCREMENT 3: NUMBERS HELD IN THE COMPILED FRAME. gbrt_slot_names lists
 * the locals a body may hold (interned names; a malloc'd array the caller
 * frees, or 0 and NULL), the first *moving of them names the body may
 * assign and the rest parameters it never does, each in a GbSlot of gbrt_slot_bytes() on the
 * compiled code's stack, which starts unset. The compiled code reads and
 * writes a set slot's number itself (gbrt_layout's offsets), and calls the
 * runtime for anything else: gbrt_slot_read for a read of an unset slot,
 * gbrt_slot_assign for an assignment it cannot do alone (begun by the
 * compiled code as gbrt_assign_begin begins one), gbrt_slots_spill before
 * anything that may read a variable by name, gbrt_slots_retake after a
 * statement the interpreter ran (which may have assigned one), both over the
 * first *moving only, and the `for`
 * calls taking the counter's slot. An operator gbrt_fast_op names is done by the compiled
 * code on two exact integers, and by gbrt_binary_apply otherwise. */
typedef struct GbSlot GbSlot;
typedef struct {
    size_t kind, num_value, num_exact, num_is_exact, num_scale, boolean;
    size_t slot_value, slot_set, slot_dirty;
    size_t result_bytes;
    int number_kind, bool_kind;
    int *line, *column, *error_generation, *raise_in_flight, *runtime_stopped;
    size_t *watcher_count;
} GbLayout;
enum {
    GB_FAST_NONE, GB_FAST_ADD, GB_FAST_SUB, GB_FAST_MUL,
    GB_FAST_EQ, GB_FAST_NE, GB_FAST_LT, GB_FAST_LE, GB_FAST_GT, GB_FAST_GE
};
size_t gbrt_slot_names(AstStmt *fn, const char ***out, size_t *moving);
size_t gbrt_slot_bytes(void);
int gbrt_slots_allowed(void);
const char *gbrt_ident_key(AstExpr *x);
const char *gbrt_for_key(AstStmt *stmt);
void gbrt_layout(GbLayout *l);
void gbrt_templates(void *no_result, void *number, void *boolean);
int gbrt_fast_op(AstExpr *x);
void gbrt_slot_read(AstExpr *x, GbSlot *s, void *out);
void gbrt_slots_spill(GbSlot *slots, const char *const *keys, size_t count);
void gbrt_slots_retake(GbSlot *slots, const char *const *keys, size_t count);
void gbrt_slot_assign(AstStmt *stmt, void *at, const int *saved, int before_error, void *value,
                      GbSlot *s);
int gbrt_for_test_slot(AstStmt *stmt, void *at, const int *saved, GbFor *f, GbSlot *s);
int gbrt_for_next_slot(AstStmt *stmt, void *at, GbList *body, const int *saved, GbFor *f,
                       GbSlot *s);

/* GBASIC_COMPILE, read once: 0 nothing compiled, 1 `all`, 2 `report`. */
int gb_jit_mode(void);

/* The compiled entry for a function's body, compiled on its first call and
 * kept in fn->jit; NULL when nothing is to be compiled, or compiling failed
 * (the loop then runs as it always has). */
GbJitBody gb_jit_body(AstStmt *fn);

/* A LIST COMPILED AS A WHOLE: a `program` block's body, or the top level
 * when there is none, which eval_stmt_list would otherwise run. The entry is
 * handed a GbList and leaves the list's result in it, beginning with
 * gbrt_list_begin as eval_stmt_list does. Kept in *slot as gb_jit_body keeps
 * a function's; `name` is the report's. NULL as for gb_jit_body. */
typedef void (*GbJitList)(GbList *l);
GbJitList gb_jit_list(AstStmtList *list, void **slot, const char *name);

#endif
