#ifndef GB_JIT_H
#define GB_JIT_H

#include <stddef.h>
#include <stdio.h>

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

/* GBASIC_COMPILE, read once: 0 nothing compiled, 1 `all`, 2 `report`. */
int gb_jit_mode(void);

/* The compiled entry for a function body of `count` statements, compiled on
 * the first call; NULL when nothing is to be compiled, or compiling failed
 * (the loop then runs as it always has). `name` is for the report. */
GbJitBody gb_jit_body(void **slot, const char *name, size_t count);

#endif
