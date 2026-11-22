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
 * interpreted, so the profiler times it as it does any other. */
typedef struct GbList GbList;
size_t gbrt_list_bytes(void);
int gbrt_list_begin(GbList *l, AstStmtList *list);
void gbrt_list_stmt(GbList *l, size_t index);
long gbrt_list_after(GbList *l, size_t index);
/* `if`: 1 the body, 0 the else body, -1 done with the result at `at`; then
 * gbrt_if_done once the branch has run */
int gbrt_if(AstStmt *stmt, void *at, int *saved);
void gbrt_if_done(void *at, GbList *branch, const int *saved);
/* `while`: enter (0, or -1 done), then test (1 the body, else done) and,
 * after each pass of the body, next (1 test again, else done) */
int gbrt_while_enter(AstStmt *stmt, void *at, int *saved);
int gbrt_while_test(AstStmt *stmt, void *at, const int *saved);
int gbrt_while_next(void *at, GbList *body, const int *saved);

/* GBASIC_COMPILE, read once: 0 nothing compiled, 1 `all`, 2 `report`. */
int gb_jit_mode(void);

/* The compiled entry for a function's body, compiled on its first call and
 * kept in fn->jit; NULL when nothing is to be compiled, or compiling failed
 * (the loop then runs as it always has). */
GbJitBody gb_jit_body(AstStmt *fn);

#endif
