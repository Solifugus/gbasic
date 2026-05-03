#include "lexer.h"
#include "parser.tab.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *input;
static size_t pos;
static int line_no;

void lexer_init(const char *source) {
    input = source;
    pos = 0;
    line_no = 1;
}

int lexer_line(void) {
    return line_no;
}

static int peek(void) {
    return input[pos];
}

static int advance(void) {
    int ch = input[pos];
    if (ch) {
        pos++;
    }
    return ch;
}

static char *slice(size_t start, size_t end) {
    size_t len = end - start;
    char *text = malloc(len + 1);
    if (!text) {
        abort();
    }
    memcpy(text, input + start, len);
    text[len] = '\0';
    return text;
}

static int parenthesized_terms_before_operator(void) {
    size_t i = pos + 1;
    int depth = 1;

    while (input[i]) {
        if (input[i] == '"') {
            i++;
            while (input[i] && input[i] != '"') {
                if (input[i] == '\\' && input[i + 1]) {
                    i += 2;
                } else {
                    i++;
                }
            }
            if (input[i] == '"') {
                i++;
            }
            continue;
        }
        if (input[i] == '(') {
            depth++;
        } else if (input[i] == ')') {
            depth--;
            if (depth == 0) {
                i++;
                while (input[i] == ' ' || input[i] == '\t' || input[i] == '\r') {
                    i++;
                }
                return input[i] == '=' ||
                    input[i] == '>' ||
                    input[i] == '<' ||
                    (input[i] == '!' && (input[i + 1] == '=' || input[i + 1] == '>' || input[i + 1] == '<'));
            }
        }
        i++;
    }

    return 0;
}

static int keyword_or_ident(char *text) {
    for (char *p = text; *p; p++) {
        *p = (char)tolower((unsigned char)*p);
    }

    if (strcmp(text, "if") == 0) return IF;
    if (strcmp(text, "then") == 0) return THEN;
    if (strcmp(text, "else") == 0) return ELSE;
    if (strcmp(text, "end") == 0) return END;
    if (strcmp(text, "print") == 0) return PRINT;
    if (strcmp(text, "true") == 0) return TRUE;
    if (strcmp(text, "false") == 0) return FALSE;
    if (strcmp(text, "and") == 0) return AND;
    if (strcmp(text, "or") == 0) return OR;
    if (strcmp(text, "not") == 0) return NOT;

    yylval.string = text;
    return IDENT;
}

int yylex(void) {
    for (;;) {
        int ch = peek();
        if (ch == ' ' || ch == '\t' || ch == '\r') {
            advance();
            continue;
        }
        if (ch == '\'') {
            while (peek() && peek() != '\n') {
                advance();
            }
            continue;
        }
        break;
    }

    int ch = peek();
    if (!ch) {
        return 0;
    }

    if (ch == '\n') {
        advance();
        line_no++;
        return NEWLINE;
    }

    if (isalpha((unsigned char)ch) || ch == '_') {
        size_t start = pos;
        while (isalnum((unsigned char)peek()) || peek() == '_') {
            advance();
        }
        return keyword_or_ident(slice(start, pos));
    }

    if (isdigit((unsigned char)ch) || (ch == '.' && isdigit((unsigned char)input[pos + 1]))) {
        char *end = NULL;
        yylval.number = strtod(input + pos, &end);
        pos = (size_t)(end - input);
        return NUMBER;
    }

    if (ch == '"') {
        advance();
        size_t cap = 16;
        size_t len = 0;
        char *text = malloc(cap);
        if (!text) {
            abort();
        }
        while (peek() && peek() != '"') {
            char out = (char)advance();
            if (out == '\\') {
                int esc = advance();
                if (esc == 'n') {
                    out = '\n';
                } else if (esc == 't') {
                    out = '\t';
                } else if (esc == '"' || esc == '\\') {
                    out = (char)esc;
                } else {
                    out = (char)esc;
                }
            }
            if (len + 1 >= cap) {
                cap *= 2;
                text = realloc(text, cap);
                if (!text) {
                    abort();
                }
            }
            text[len++] = out;
        }
        if (peek() == '"') {
            advance();
        }
        text[len] = '\0';
        yylval.string = text;
        return STRING;
    }

    advance();
    switch (ch) {
    case '=': return OP_EQ;
    case '!':
        if (peek() == '=') {
            advance();
            return OP_NE;
        }
        if (peek() == '>') {
            advance();
            return OP_NGT;
        }
        if (peek() == '<') {
            advance();
            return OP_NLT;
        }
        break;
    case '>':
        if (peek() == '=') {
            advance();
            return OP_GE;
        }
        return OP_GT;
    case '<':
        if (peek() == '=') {
            advance();
            return OP_LE;
        }
        return OP_LT;
    case '+': return PLUS;
    case '-': return MINUS;
    case '*': return STAR;
    case '/': return SLASH;
    case '(':
        pos--;
        if (parenthesized_terms_before_operator()) {
            advance();
            return MOD_LPAREN;
        }
        advance();
        return LPAREN;
    case ')': return RPAREN;
    case '[': return LBRACKET;
    case ']': return RBRACKET;
    case ',': return COMMA;
    }

    fprintf(stderr, "unexpected character '%c' at line %d\n", ch, line_no);
    return 0;
}

void yyerror(const char *message) {
    fprintf(stderr, "parse error at line %d: %s\n", line_no, message);
}
