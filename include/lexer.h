#ifndef GBASIC_LEXER_H
#define GBASIC_LEXER_H

typedef enum {
    TOKEN_EOF,
    TOKEN_IDENT,
    TOKEN_QUALIFIED_IDENT,
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_LENS_CONTENT,
    TOKEN_PROGRAM,
    TOKEN_LIBRARY,
    TOKEN_LOAD,
    TOKEN_USE,
    TOKEN_EXPORT,
    TOKEN_IF,
    TOKEN_CONSIDER_IF,
    TOKEN_THEN,
    TOKEN_ELSE,
    TOKEN_CONSIDER_ELSE,
    TOKEN_END,
    TOKEN_END_CONSIDER,
    TOKEN_FOR,
    TOKEN_TO,
    TOKEN_STEP,
    TOKEN_DO,
    TOKEN_UNTIL,
    TOKEN_WHILE,
    TOKEN_CONSIDER,
    TOKEN_BREAK,
    TOKEN_CONTINUE,
    TOKEN_FUNCTION,
    TOKEN_RETURN,
    TOKEN_PRINT,
    TOKEN_DIM,
    TOKEN_AS,
    TOKEN_WATCH,
    TOKEN_UNWATCH,
    TOKEN_WITHOUT,
    TOKEN_WATCHERS,
    TOKEN_MODIFIER,
    TOKEN_GOTO,
    TOKEN_GOSUB,
    TOKEN_WITH,
    TOKEN_NEW,
    TOKEN_SPAWN,
    TOKEN_ON,
    TOKEN_NEXT,
    TOKEN_STOP,
    TOKEN_ERROR_VALUE,
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_NOTHING,
    TOKEN_UNKNOWN,
    TOKEN_AND,
    TOKEN_OR,
    TOKEN_NOT,
    TOKEN_IN,
    TOKEN_EACH,
    TOKEN_OP_EQ,
    TOKEN_OP_NE,
    TOKEN_OP_GT,
    TOKEN_OP_LT,
    TOKEN_OP_GE,
    TOKEN_OP_LE,
    TOKEN_OP_NGT,
    TOKEN_OP_NLT,
    TOKEN_OP_NGE,
    TOKEN_OP_NLE,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    /* Compound assignment. `x op= e` means exactly `x = x op e`, so these
     * carry no semantics of their own -- they name which operator the
     * assignment folds in. */
    TOKEN_PLUS_EQ,
    TOKEN_MINUS_EQ,
    TOKEN_STAR_EQ,
    TOKEN_SLASH_EQ,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_COMMA,
    TOKEN_DOT,
    TOKEN_COLON,
    TOKEN_NEWLINE,
    TOKEN_ERROR
} TokenType;

#define GBASIC_BRACKET_STACK 64

typedef struct {
    TokenType type;
    const char *start;
    int length;
    int line;
    int column;
} Token;

typedef struct {
    const char *source;
    const char *current;
    char error_message[96];
    int line;
    int column;
    int lens_content_mode;
    int consider_depth;
    int consider_columns[64];
    /* Line continuation: a newline inside an unclosed (, [ or { is not a
     * statement terminator. No new syntax and no trailing marker to forget --
     * the brackets the author already wrote say where the statement ends.
     * Only the depth decides; matching ')' to '(' is the grammar's job. The
     * stack records where each opener was so an unclosed one can be named at
     * end of file rather than reported as a puzzling EOF. */
    int bracket_depth;
    int bracket_lines[GBASIC_BRACKET_STACK];
    char bracket_chars[GBASIC_BRACKET_STACK];
} Lexer;

void lexer_init(Lexer *lexer, const char *source);
void lexer_begin_lens_content(Lexer *lexer);
Token lexer_next(Lexer *lexer);
const char *token_type_name(TokenType type);

/* Classify an identifier-shaped span: TOKEN_IDENT, or the keyword's token. */
TokenType lexer_identifier_type(const char *start, int length);

#endif
