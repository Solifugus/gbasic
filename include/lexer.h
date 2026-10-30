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
    TOKEN_EXCLUDING,
    TOKEN_INTERSECTING,
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
    /* `{ IDENT }` EXACTLY, decided by the LEXER, which is why the inline
     * type modifier costs zero grammar conflicts -- `{a}` and `{a: 1}` differ
     * at their THIRD token and LALR(1) cannot see that far from the brace.
     * The token's span is the NAME. */
    TOKEN_MODIFIER_PREFIX,
    /* `{IDENT}` where what follows the `}` says the modifier NAMES A TYPE
     * rather than applying to something -- a `,`, a `)` or a newline. The
     * LEXER decides, which is what makes it free: admitting a bare modifier as
     * a primary expression in the grammar costs FOUR shift/reduce conflicts
     * (measured 2026-10-17, the count that got `IDENT expression` rejected as a
     * statement form), because after `{USD}` the parser cannot tell a bare
     * modifier from one applied to what follows -- `{USD} -5` is the
     * discriminating case. The lexer may look past the `}` as far as it likes,
     * so it costs zero. See docs/qt_gui_design.md §5.2. */
    TOKEN_MODIFIER_VALUE,
    /* The opener of a `window` block, emitted only for the exact three-token
     * shape `window IDENT (`. NOTHING IS RESERVED: `window` stays an ordinary
     * identifier everywhere else, measured at 153 uses across the tree with
     * ZERO instances of that shape, so no existing program changes meaning.
     *
     * It needs its own token because sharing the `server` block's
     * `IDENT IDENT (` head costs 2 reduce/reduce -- after
     * `IDENT IDENT ( ... ) NEWLINE` the parser cannot tell which body is
     * coming (measured; bison's counterexample names exactly that). */
    TOKEN_WINDOW_OPEN,
    /* G-SQL's two declarations (docs/gsql_module_design.md §7), and nothing is
     * reserved for them either. `database IDENT (` opens a database
     * declaration; `sql IDENT on IDENT` at the end of a line opens a statement
     * block, whose BODY then arrives as ONE TOKEN_SQL_TEXT holding the raw SQL
     * up to the line reading `end sql`. The body is SQL, parsed by G-SQL's own
     * parser, so SQL's words are never gBASIC's. */
    TOKEN_DATABASE_OPEN,
    TOKEN_SQL_OPEN,
    TOKEN_SQL_TEXT,
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
    /* An `sql` block's body: 1 once the head is lexed, 2 once its line has
     * ended, when the next token is the raw body. */
    int sql_body_pending;
    int sql_open_line;
} Lexer;

void lexer_init(Lexer *lexer, const char *source);

/* Initialise for a buffer whose first character is at line `first_line` of some
 * larger text -- see the comment on the definition. lexer_init is this with 1. */
void lexer_init_at(Lexer *lexer, const char *source, int first_line);
void lexer_begin_lens_content(Lexer *lexer);
Token lexer_next(Lexer *lexer);
const char *token_type_name(TokenType type);

/* Classify an identifier-shaped span: TOKEN_IDENT, or the keyword's token. */
TokenType lexer_identifier_type(const char *start, int length);

#endif
