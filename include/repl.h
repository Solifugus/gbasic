#ifndef GBASIC_REPL_H
#define GBASIC_REPL_H

/* The gBASIC prompt (src/repl.c). Reads chunks from stdin and runs them against
 * ONE session, so a variable assigned on one line is there on the next. Returns
 * the process exit status: 0 unless a line asked for another with `exit(n)`, or
 * a prompt command could not do what it was told. */
int repl_main(int json_diagnostics);

#endif
