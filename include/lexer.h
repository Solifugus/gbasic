#ifndef GBASIC_LEXER_H
#define GBASIC_LEXER_H

typedef enum {
    TOKEN_EOF,
    TOKEN_IDENT,
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_MOD_CONTENT,
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
    TOKEN_WITHOUT,
    TOKEN_WATCHERS,
    TOKEN_MODIFIER,
    TOKEN_GOTO,
    TOKEN_GOSUB,
    TOKEN_WITH,
    TOKEN_ON,
    TOKEN_RESUME,
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
    int modifier_content_mode;
    int consider_depth;
    int consider_columns[64];
} Lexer;

void lexer_init(Lexer *lexer, const char *source);
void lexer_begin_modifier_content(Lexer *lexer);
Token lexer_next(Lexer *lexer);
const char *token_type_name(TokenType type);

#endif
