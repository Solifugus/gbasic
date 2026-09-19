#ifndef GBASIC_EVAL_H
#define GBASIC_EVAL_H

#include <stdio.h>

#include "ast.h"

int eval_program(AstStmtList program);
void eval_set_source_path(const char *path);

/* The file a child process re-execs to BE this program -- what `spawn` hands an
 * actor, and what `process.self` reports as the script. Defaults to the source
 * path; the prompt points it at its session cache, which is a real file holding
 * exactly what has been typed. Separate from the source path because that is
 * also the DIAGNOSTIC name and the base a relative `load` resolves against. */
void gb_set_reexec_path(const char *path);

/* Command-line arguments that follow the script path. When the program declares
 * a `program NAME(param)` block, its first parameter binds to these as a 0-based
 * array of strings. `items` must remain valid for the program's lifetime (argv
 * does). Call before eval_program; unset (count 0) if never called. */
void eval_set_program_args(char *const *items, size_t count);

/* True for a native module qualifier that a `load` must name before its
 * dispatch works (`sqlite`, `pg`, `xml`, ...), false for one that answers
 * without one (`money`, `process`, `timer`, ...). Used by --add-loads. */
int eval_module_needs_load(const char *name);

/* True for ANY qualifier the evaluator intercepts by string before user
 * function resolution -- including the ones that need no `load`. */
int eval_is_native_module(const char *name);

/* Run this process as a spawned actor: register the program's top-level
 * definitions, adopt the inherited mailbox/control fds, and run `entry`
 * (docs/multiprocessing_design.md §3). Returns a process exit status. */
int eval_run_actor(AstStmtList program, const char *entry,
                   int inbox_fd, int self_fd, int control_fd);

/* ---- REPL session -----------------------------------------------------------
 *
 * `eval_program` opens an environment, runs one root and releases everything.
 * A prompt needs the middle of that repeated: open once, run many chunks
 * against ONE environment, close once. The session OWNS every chunk's AST until
 * it closes (a function declared on one line is called on the next, and the
 * registration stores the AST node, not a copy), so the caller must NOT free a
 * chunk it has handed to gb_session_run.
 *
 * gb_session_run returns 0 when the chunk ran to the end, non-zero when it
 * raised, stopped or escaped a loop -- reported on stderr exactly as a script's
 * failure is. Either way the next chunk starts clean: a prompt whose next line
 * is dead because the previous one failed is not a prompt. */
void gb_session_open(void);
int  gb_session_run(AstStmtList chunk);
/* Whether the chunk just run ANSWERED rather than acted: a single expression
 * statement that produced a value, which the session printed. A call returning
 * `nothing` is not an answer and is not reported as one. */
/* Ask the chunk currently running to stop, as Ctrl-C does at a prompt. Safe to
 * call from a signal handler: it sets one flag, and the evaluator unwinds
 * through the ordinary raise path so locks are released and frames unwound. */
void gb_session_interrupt(void);
int  gb_session_echoed(void);
int  gb_session_exit_code(void);
void gb_session_close(void);

/* The session's global names and their values, one per line -- the REPL's
 * `vars`. Rendered in display mode, so it cannot raise. */
void gb_session_list_vars(FILE *out);

#endif
