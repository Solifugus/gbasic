#ifndef GBASIC_EVAL_H
#define GBASIC_EVAL_H

#include "ast.h"

int eval_program(AstStmtList program);
void eval_set_source_path(const char *path);

/* Command-line arguments that follow the script path. When the program declares
 * a `program NAME(param)` block, its first parameter binds to these as a 0-based
 * array of strings. `items` must remain valid for the program's lifetime (argv
 * does). Call before eval_program; unset (count 0) if never called. */
void eval_set_program_args(char *const *items, size_t count);

/* Run this process as a spawned actor: register the program's top-level
 * definitions, adopt the inherited mailbox/control fds, and run `entry`
 * (docs/multiprocessing_design.md §3). Returns a process exit status. */
int eval_run_actor(AstStmtList program, const char *entry,
                   int inbox_fd, int self_fd, int control_fd);

#endif
