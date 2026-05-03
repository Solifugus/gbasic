#ifndef GBASIC_LEXER_H
#define GBASIC_LEXER_H

void lexer_init(const char *source);
int lexer_line(void);
int yylex(void);
void yyerror(const char *message);

#endif
