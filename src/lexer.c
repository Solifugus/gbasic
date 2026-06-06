#include "lexer.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int is_at_end(Lexer *lexer) {
    return *lexer->current == '\0';
}

static char advance(Lexer *lexer) {
    char ch = *lexer->current;
    if (ch != '\0') {
        lexer->current++;
        if (ch == '\n') {
            lexer->line++;
            lexer->column = 1;
        } else {
            lexer->column++;
        }
    }
    return ch;
}

static char peek(Lexer *lexer) {
    return *lexer->current;
}

static char peek_next(Lexer *lexer) {
    if (is_at_end(lexer)) {
        return '\0';
    }
    return lexer->current[1];
}

static Token make_token(Lexer *lexer, TokenType type, const char *start, int line, int column) {
    Token token;
    token.type = type;
    token.start = start;
    token.length = (int)(lexer->current - start);
    token.line = line;
    token.column = column;
    return token;
}

static Token error_token(Lexer *lexer, const char *start, int line, int column) {
    return make_token(lexer, TOKEN_ERROR, start, line, column);
}

static Token error_token_message(Lexer *lexer, const char *start, int line, int column, const char *message) {
    snprintf(lexer->error_message, sizeof(lexer->error_message), "%s", message);
    return error_token(lexer, start, line, column);
}

static int match(Lexer *lexer, char expected) {
    if (is_at_end(lexer) || *lexer->current != expected) {
        return 0;
    }
    advance(lexer);
    return 1;
}

static void skip_spaces_and_comments(Lexer *lexer) {
    for (;;) {
        char ch = peek(lexer);
        if (ch == ' ' || ch == '\r' || ch == '\t') {
            advance(lexer);
        } else if (ch == '\'') {
            while (peek(lexer) != '\n' && !is_at_end(lexer)) {
                advance(lexer);
            }
        } else {
            return;
        }
    }
}

static Token string_token(Lexer *lexer, const char *start, int line, int column) {
    while (!is_at_end(lexer)) {
        char ch = advance(lexer);
        if (ch == '"') {
            return make_token(lexer, TOKEN_STRING, start, line, column);
        }
        if (ch == '\\') {
            if (is_at_end(lexer) || peek(lexer) == '\n') {
                return error_token_message(lexer, start, line, column, "unterminated escape sequence");
            }
            char esc = advance(lexer);
            if (esc != 'n' && esc != 't' && esc != '\\' && esc != '"') {
                char message[96];
                snprintf(message, sizeof(message), "invalid escape sequence: \\%c", esc);
                return error_token_message(lexer, start, line, column, message);
            }
        }
    }

    return error_token_message(lexer, start, line, column, "unterminated string");
}

static Token modifier_content_token(Lexer *lexer) {
    const char *start = lexer->current;
    int line = lexer->line;
    int column = lexer->column;
    int in_string = 0;

    while (!is_at_end(lexer)) {
        char ch = peek(lexer);
        if (ch == '"' && (lexer->current == start || lexer->current[-1] != '\\')) {
            in_string = !in_string;
        }
        if (!in_string && ch == ')') {
            Token token = make_token(lexer, TOKEN_MOD_CONTENT, start, line, column);
            advance(lexer);
            lexer->modifier_content_mode = 0;
            return token;
        }
        if (ch == '\n') {
            break;
        }
        advance(lexer);
    }

    lexer->modifier_content_mode = 0;
    return error_token(lexer, start, line, column);
}

void lexer_begin_modifier_content(Lexer *lexer) {
    lexer->modifier_content_mode = 1;
}

static Token number_token(Lexer *lexer, const char *start, int line, int column) {
    while (isdigit((unsigned char)peek(lexer))) {
        advance(lexer);
    }

    if (peek(lexer) == '.' && isdigit((unsigned char)peek_next(lexer))) {
        advance(lexer);
        while (isdigit((unsigned char)peek(lexer))) {
            advance(lexer);
        }
    }

    return make_token(lexer, TOKEN_NUMBER, start, line, column);
}

static int keyword_equals(const char *start, int length, const char *keyword) {
    int keyword_length = (int)strlen(keyword);
    if (length != keyword_length) {
        return 0;
    }

    for (int i = 0; i < length; i++) {
        if (tolower((unsigned char)start[i]) != keyword[i]) {
            return 0;
        }
    }
    return 1;
}

static int text_equals_keyword(const char *start, const char *end, const char *keyword) {
    return keyword_equals(start, (int)(end - start), keyword);
}

static TokenType identifier_type(const char *start, int length) {
    if (keyword_equals(start, length, "program")) return TOKEN_PROGRAM;
    if (keyword_equals(start, length, "library")) return TOKEN_LIBRARY;
    if (keyword_equals(start, length, "load")) return TOKEN_LOAD;
    if (keyword_equals(start, length, "use")) return TOKEN_USE;
    if (keyword_equals(start, length, "export")) return TOKEN_EXPORT;
    if (keyword_equals(start, length, "if")) return TOKEN_IF;
    if (keyword_equals(start, length, "then")) return TOKEN_THEN;
    if (keyword_equals(start, length, "else")) return TOKEN_ELSE;
    if (keyword_equals(start, length, "end")) return TOKEN_END;
    if (keyword_equals(start, length, "for")) return TOKEN_FOR;
    if (keyword_equals(start, length, "to")) return TOKEN_TO;
    if (keyword_equals(start, length, "step")) return TOKEN_STEP;
    if (keyword_equals(start, length, "while")) return TOKEN_WHILE;
    if (keyword_equals(start, length, "consider")) return TOKEN_CONSIDER;
    if (keyword_equals(start, length, "break")) return TOKEN_BREAK;
    if (keyword_equals(start, length, "continue")) return TOKEN_CONTINUE;
    if (keyword_equals(start, length, "function")) return TOKEN_FUNCTION;
    if (keyword_equals(start, length, "return")) return TOKEN_RETURN;
    if (keyword_equals(start, length, "print")) return TOKEN_PRINT;
    if (keyword_equals(start, length, "dim")) return TOKEN_DIM;
    if (keyword_equals(start, length, "as")) return TOKEN_AS;
    if (keyword_equals(start, length, "watch")) return TOKEN_WATCH;
    if (keyword_equals(start, length, "without")) return TOKEN_WITHOUT;
    if (keyword_equals(start, length, "watchers")) return TOKEN_WATCHERS;
    if (keyword_equals(start, length, "modifier")) return TOKEN_MODIFIER;
    if (keyword_equals(start, length, "goto")) return TOKEN_GOTO;
    if (keyword_equals(start, length, "gosub")) return TOKEN_GOSUB;
    if (keyword_equals(start, length, "with")) return TOKEN_WITH;
    if (keyword_equals(start, length, "on")) return TOKEN_ON;
    if (keyword_equals(start, length, "resume")) return TOKEN_RESUME;
    if (keyword_equals(start, length, "next")) return TOKEN_NEXT;
    if (keyword_equals(start, length, "stop")) return TOKEN_STOP;
    if (keyword_equals(start, length, "error")) return TOKEN_ERROR_VALUE;
    if (keyword_equals(start, length, "true")) return TOKEN_TRUE;
    if (keyword_equals(start, length, "false")) return TOKEN_FALSE;
    if (keyword_equals(start, length, "nothing")) return TOKEN_NOTHING;
    if (keyword_equals(start, length, "unknown")) return TOKEN_UNKNOWN;
    if (keyword_equals(start, length, "and")) return TOKEN_AND;
    if (keyword_equals(start, length, "or")) return TOKEN_OR;
    if (keyword_equals(start, length, "not")) return TOKEN_NOT;
    if (keyword_equals(start, length, "in")) return TOKEN_IN;
    if (keyword_equals(start, length, "each")) return TOKEN_EACH;
    return TOKEN_IDENT;
}

static Token identifier_token(Lexer *lexer, const char *start, int line, int column) {
    while (isalnum((unsigned char)peek(lexer)) || peek(lexer) == '_') {
        advance(lexer);
    }
    if (text_equals_keyword(start, lexer->current, "end")) {
        const char *saved_current = lexer->current;
        int saved_line = lexer->line;
        int saved_column = lexer->column;
        while (peek(lexer) == ' ' || peek(lexer) == '\t' || peek(lexer) == '\r') {
            advance(lexer);
        }
        const char *word_start = lexer->current;
        while (isalnum((unsigned char)peek(lexer)) || peek(lexer) == '_') {
            advance(lexer);
        }
        if (text_equals_keyword(word_start, lexer->current, "consider")) {
            if (lexer->consider_depth > 0) {
                lexer->consider_depth--;
            }
            return make_token(lexer, TOKEN_END_CONSIDER, start, line, column);
        }
        lexer->current = saved_current;
        lexer->line = saved_line;
        lexer->column = saved_column;
    }
    TokenType type = identifier_type(start, (int)(lexer->current - start));
    if (type == TOKEN_IDENT && peek(lexer) == '.' &&
        (isalpha((unsigned char)peek_next(lexer)) || peek_next(lexer) == '_')) {
        const char *saved_current = lexer->current;
        int saved_line = lexer->line;
        int saved_column = lexer->column;

        advance(lexer);
        while (isalnum((unsigned char)peek(lexer)) || peek(lexer) == '_') {
            advance(lexer);
        }

        const char *after_second_ident = lexer->current;
        int after_second_line = lexer->line;
        int after_second_column = lexer->column;
        while (peek(lexer) == ' ' || peek(lexer) == '\t' || peek(lexer) == '\r') {
            advance(lexer);
        }
        if (peek(lexer) == '(') {
            lexer->current = after_second_ident;
            lexer->line = after_second_line;
            lexer->column = after_second_column;
            return make_token(lexer, TOKEN_QUALIFIED_IDENT, start, line, column);
        }

        lexer->current = saved_current;
        lexer->line = saved_line;
        lexer->column = saved_column;
    }
    if (type == TOKEN_CONSIDER) {
        if (lexer->consider_depth < 64) {
            lexer->consider_columns[lexer->consider_depth] = column;
        }
        lexer->consider_depth++;
    } else if (lexer->consider_depth > 0 &&
               column == lexer->consider_columns[lexer->consider_depth - 1] &&
               type == TOKEN_IF) {
        type = TOKEN_CONSIDER_IF;
    } else if (lexer->consider_depth > 0 &&
               column == lexer->consider_columns[lexer->consider_depth - 1] &&
               type == TOKEN_ELSE) {
        type = TOKEN_CONSIDER_ELSE;
    }
    return make_token(lexer, type, start, line, column);
}

void lexer_init(Lexer *lexer, const char *source) {
    lexer->source = source;
    lexer->current = source;
    lexer->error_message[0] = '\0';
    lexer->line = 1;
    lexer->column = 1;
    lexer->modifier_content_mode = 0;
    lexer->consider_depth = 0;
}

Token lexer_next(Lexer *lexer) {
    if (lexer->modifier_content_mode) {
        return modifier_content_token(lexer);
    }

    skip_spaces_and_comments(lexer);

    const char *start = lexer->current;
    int line = lexer->line;
    int column = lexer->column;
    if (is_at_end(lexer)) {
        return make_token(lexer, TOKEN_EOF, start, line, column);
    }

    char ch = advance(lexer);
    if (ch == '\n') {
        return make_token(lexer, TOKEN_NEWLINE, start, line, column);
    }
    if (isalpha((unsigned char)ch) || ch == '_') {
        return identifier_token(lexer, start, line, column);
    }
    if (isdigit((unsigned char)ch)) {
        return number_token(lexer, start, line, column);
    }

    switch (ch) {
    case '"': return string_token(lexer, start, line, column);
    case '=': return make_token(lexer, TOKEN_OP_EQ, start, line, column);
    case '!':
        if (match(lexer, '=')) return make_token(lexer, TOKEN_OP_NE, start, line, column);
        if (match(lexer, '>')) {
            if (match(lexer, '=')) return make_token(lexer, TOKEN_OP_NGE, start, line, column);
            return make_token(lexer, TOKEN_OP_NGT, start, line, column);
        }
        if (match(lexer, '<')) {
            if (match(lexer, '=')) return make_token(lexer, TOKEN_OP_NLE, start, line, column);
            return make_token(lexer, TOKEN_OP_NLT, start, line, column);
        }
        break;
    case '>':
        if (match(lexer, '=')) return make_token(lexer, TOKEN_OP_GE, start, line, column);
        return make_token(lexer, TOKEN_OP_GT, start, line, column);
    case '<':
        if (match(lexer, '=')) return make_token(lexer, TOKEN_OP_LE, start, line, column);
        return make_token(lexer, TOKEN_OP_LT, start, line, column);
    case '+': return make_token(lexer, TOKEN_PLUS, start, line, column);
    case '-': return make_token(lexer, TOKEN_MINUS, start, line, column);
    case '*': return make_token(lexer, TOKEN_STAR, start, line, column);
    case '/': return make_token(lexer, TOKEN_SLASH, start, line, column);
    case '(': return make_token(lexer, TOKEN_LPAREN, start, line, column);
    case ')': return make_token(lexer, TOKEN_RPAREN, start, line, column);
    case '[': return make_token(lexer, TOKEN_LBRACKET, start, line, column);
    case ']': return make_token(lexer, TOKEN_RBRACKET, start, line, column);
    case '{': return make_token(lexer, TOKEN_LBRACE, start, line, column);
    case '}': return make_token(lexer, TOKEN_RBRACE, start, line, column);
    case ',': return make_token(lexer, TOKEN_COMMA, start, line, column);
    case '.': return make_token(lexer, TOKEN_DOT, start, line, column);
    case ':': return make_token(lexer, TOKEN_COLON, start, line, column);
    }

    return error_token(lexer, start, line, column);
}

const char *token_type_name(TokenType type) {
    switch (type) {
    case TOKEN_EOF: return "EOF";
    case TOKEN_IDENT: return "IDENT";
    case TOKEN_QUALIFIED_IDENT: return "QUALIFIED_IDENT";
    case TOKEN_NUMBER: return "NUMBER";
    case TOKEN_STRING: return "STRING";
    case TOKEN_MOD_CONTENT: return "MOD_CONTENT";
    case TOKEN_PROGRAM: return "PROGRAM";
    case TOKEN_LIBRARY: return "LIBRARY";
    case TOKEN_LOAD: return "LOAD";
    case TOKEN_USE: return "USE";
    case TOKEN_EXPORT: return "EXPORT";
    case TOKEN_IF: return "IF";
    case TOKEN_CONSIDER_IF: return "CONSIDER_IF";
    case TOKEN_THEN: return "THEN";
    case TOKEN_ELSE: return "ELSE";
    case TOKEN_CONSIDER_ELSE: return "CONSIDER_ELSE";
    case TOKEN_END: return "END";
    case TOKEN_END_CONSIDER: return "END_CONSIDER";
    case TOKEN_FOR: return "FOR";
    case TOKEN_TO: return "TO";
    case TOKEN_STEP: return "STEP";
    case TOKEN_WHILE: return "WHILE";
    case TOKEN_CONSIDER: return "CONSIDER";
    case TOKEN_BREAK: return "BREAK";
    case TOKEN_CONTINUE: return "CONTINUE";
    case TOKEN_FUNCTION: return "FUNCTION";
    case TOKEN_RETURN: return "RETURN";
    case TOKEN_PRINT: return "PRINT";
    case TOKEN_DIM: return "DIM";
    case TOKEN_AS: return "AS";
    case TOKEN_WATCH: return "WATCH";
    case TOKEN_WITHOUT: return "WITHOUT";
    case TOKEN_WATCHERS: return "WATCHERS";
    case TOKEN_MODIFIER: return "MODIFIER";
    case TOKEN_GOTO: return "GOTO";
    case TOKEN_GOSUB: return "GOSUB";
    case TOKEN_WITH: return "WITH";
    case TOKEN_ON: return "ON";
    case TOKEN_RESUME: return "RESUME";
    case TOKEN_NEXT: return "NEXT";
    case TOKEN_STOP: return "STOP";
    case TOKEN_ERROR_VALUE: return "ERROR_VALUE";
    case TOKEN_TRUE: return "TRUE";
    case TOKEN_FALSE: return "FALSE";
    case TOKEN_NOTHING: return "NOTHING";
    case TOKEN_UNKNOWN: return "UNKNOWN";
    case TOKEN_AND: return "AND";
    case TOKEN_OR: return "OR";
    case TOKEN_NOT: return "NOT";
    case TOKEN_IN: return "IN";
    case TOKEN_OP_EQ: return "OP_EQ";
    case TOKEN_OP_NE: return "OP_NE";
    case TOKEN_OP_GT: return "OP_GT";
    case TOKEN_OP_LT: return "OP_LT";
    case TOKEN_OP_GE: return "OP_GE";
    case TOKEN_OP_LE: return "OP_LE";
    case TOKEN_OP_NGT: return "OP_NGT";
    case TOKEN_OP_NLT: return "OP_NLT";
    case TOKEN_OP_NGE: return "OP_NGE";
    case TOKEN_OP_NLE: return "OP_NLE";
    case TOKEN_PLUS: return "PLUS";
    case TOKEN_MINUS: return "MINUS";
    case TOKEN_STAR: return "STAR";
    case TOKEN_SLASH: return "SLASH";
    case TOKEN_LPAREN: return "LPAREN";
    case TOKEN_RPAREN: return "RPAREN";
    case TOKEN_LBRACKET: return "LBRACKET";
    case TOKEN_RBRACKET: return "RBRACKET";
    case TOKEN_LBRACE: return "LBRACE";
    case TOKEN_RBRACE: return "RBRACE";
    case TOKEN_COMMA: return "COMMA";
    case TOKEN_DOT: return "DOT";
    case TOKEN_COLON: return "COLON";
    case TOKEN_NEWLINE: return "NEWLINE";
    case TOKEN_ERROR: return "ERROR";
    }
    return "UNKNOWN";
}
