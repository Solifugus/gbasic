#ifndef GBASIC_EVAL_H
#define GBASIC_EVAL_H

#include "ast.h"

int eval_program(AstStmtList program);
void eval_set_source_path(const char *path);

#endif
