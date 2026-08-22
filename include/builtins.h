#ifndef GBASIC_BUILTINS_H
#define GBASIC_BUILTINS_H

int gbasic_builtin_function(const char *name);

/* The feature-probe answer: everything gbasic_builtin_function knows PLUS the
 * names eval.c dispatches at top level without registering (the file/dir call
 * families). has_builtin() reads this; see builtins.c for the maintenance
 * rule. */
int gbasic_has_builtin(const char *name);

#endif
