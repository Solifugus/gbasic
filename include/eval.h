#ifndef GBASIC_EVAL_H
#define GBASIC_EVAL_H

#include "ast.h"

int eval_program(AstStmtList program);
void eval_set_source_path(const char *path);

/* Run this process as a spawned actor: register the program's top-level
 * definitions, adopt the inherited mailbox/control fds, and run `entry`
 * (docs/multiprocessing_design.md §3). Returns a process exit status. */
int eval_run_actor(AstStmtList program, const char *entry,
                   int inbox_fd, int self_fd, int control_fd);

#endif
