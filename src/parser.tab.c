/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */
#line 1 "src/parser.y"

#include "ast.h"
#include "builtins.h"
#include "lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Lexer *active_lexer;
static int lexer_error_reported;
static const char *active_parse_path;

AstStmtList parsed_program;

static void report_parse_issue(const char *kind, int line, int column, const char *message) {
    if (active_parse_path && active_parse_path[0]) {
        fprintf(stderr, "%s at %s:%d:%d: %s\n",
                kind,
                active_parse_path,
                line,
                column,
                message);
    } else {
        fprintf(stderr, "%s at %d:%d: %s\n",
                kind,
                line,
                column,
                message);
    }
}

static char *copy_text(const char *start, int length) {
    char *text = malloc((size_t)length + 1);
    if (!text) {
        abort();
    }
    memcpy(text, start, (size_t)length);
    text[length] = '\0';
    return text;
}

static int hex_digit_value(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

/* Encode a Unicode scalar value as UTF-8 into out (up to 4 bytes); returns the
 * byte count. Caller guarantees a valid scalar (0..0x10FFFF, no surrogate). */
static int utf8_encode_literal(unsigned cp, char out[4]) {
    if (cp <= 0x7fu) {
        out[0] = (char)cp;
        return 1;
    } else if (cp <= 0x7ffu) {
        out[0] = (char)(0xc0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3fu));
        return 2;
    } else if (cp <= 0xffffu) {
        out[0] = (char)(0xe0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3fu));
        out[2] = (char)(0x80u | (cp & 0x3fu));
        return 3;
    }
    out[0] = (char)(0xf0u | (cp >> 18));
    out[1] = (char)(0x80u | ((cp >> 12) & 0x3fu));
    out[2] = (char)(0x80u | ((cp >> 6) & 0x3fu));
    out[3] = (char)(0x80u | (cp & 0x3fu));
    return 4;
}

static char *copy_string_literal(const char *start, int length, int line, int column, int *ok) {
    *ok = 1;
    if (length < 2) {
        return copy_text("", 0);
    }

    char *text = malloc((size_t)length - 1);
    if (!text) {
        abort();
    }
    int out = 0;
    for (int i = 1; i < length - 1; i++) {
        if (start[i] == '\\') {
            if (i + 1 >= length - 1) {
                report_parse_issue("runtime error", line, column, "unterminated escape sequence");
                *ok = 0;
                free(text);
                return NULL;
            }
            i++;
            if (start[i] == 'n') {
                text[out++] = '\n';
            } else if (start[i] == 't') {
                text[out++] = '\t';
            } else if (start[i] == '"' || start[i] == '\\') {
                text[out++] = start[i];
            } else if (start[i] == 'u') {
                /* \u{HHHH}: decode a Unicode scalar to UTF-8. The lexer already
                 * guaranteed the { hexdigits } shape, so just read it. */
                i++; /* the '{' */
                unsigned cp = 0;
                int digits = 0;
                i++; /* first hex digit */
                while (i < length - 1 && start[i] != '}') {
                    cp = cp * 16u + (unsigned)hex_digit_value(start[i]);
                    digits++;
                    i++;
                }
                /* i now points at '}', which the for-loop's i++ will consume. */
                if (digits > 6 || cp > 0x10FFFFu) {
                    report_parse_issue("runtime error", line, column,
                                       "invalid unicode escape: codepoint must be between 0 and 0x10FFFF");
                    *ok = 0;
                    free(text);
                    return NULL;
                }
                if (cp >= 0xD800u && cp <= 0xDFFFu) {
                    report_parse_issue("runtime error", line, column,
                                       "invalid unicode escape: surrogate codepoints (0xD800..0xDFFF) are not valid");
                    *ok = 0;
                    free(text);
                    return NULL;
                }
                if (cp == 0) {
                    report_parse_issue("runtime error", line, column,
                                       "invalid unicode escape: \\u{0} is not allowed in a literal; use chr(0)");
                    *ok = 0;
                    free(text);
                    return NULL;
                }
                char utf8[4];
                int n = utf8_encode_literal(cp, utf8);
                for (int b = 0; b < n; b++) {
                    text[out++] = utf8[b];
                }
            } else {
                char message[64];
                snprintf(message, sizeof(message), "invalid escape sequence: \\%c", start[i]);
                report_parse_issue("runtime error", line, column, message);
                *ok = 0;
                free(text);
                return NULL;
            }
        } else {
            text[out++] = start[i];
        }
    }
    text[out] = '\0';
    return text;
}

static char *copy_const(const char *text) {
    return copy_text(text, (int)strlen(text));
}

static char *join_watch_path(char *left, char *right) {
    size_t left_len = strlen(left);
    size_t right_len = strlen(right);
    char *text = malloc(left_len + 1 + right_len + 1);
    if (!text) {
        abort();
    }
    memcpy(text, left, left_len);
    text[left_len] = '.';
    memcpy(text + left_len + 1, right, right_len + 1);
    free(left);
    free(right);
    return text;
}

static void split_qualified_ident(char *text, char **library, char **name) {
    char *dot = strchr(text, '.');
    if (!dot) {
        *library = text;
        *name = copy_const("");
        return;
    }
    *dot = '\0';
    *library = text;
    *name = copy_const(dot + 1);
}

static int ascii_lower(int ch) {
    return ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch;
}

static int is_ident_start_char(char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_';
}

static int is_ident_char(char ch) {
    return is_ident_start_char(ch) || (ch >= '0' && ch <= '9');
}

static int keyword_at(const char *p, const char *keyword) {
    const char *k = keyword;
    while (*k) {
        if (ascii_lower((unsigned char)*p) != *k) {
            return 0;
        }
        p++;
        k++;
    }
    return !is_ident_char(*p);
}

static int source_declares_function(const char *name) {
    const char *p = active_lexer->source;
    size_t name_len = strlen(name);
    int in_string = 0;
    int in_comment = 0;

    while (*p) {
        if (in_comment) {
            if (*p == '\n') {
                in_comment = 0;
            }
            p++;
            continue;
        }
        if (in_string) {
            if (*p == '\\' && p[1]) {
                p += 2;
                continue;
            }
            if (*p == '"') {
                in_string = 0;
            }
            p++;
            continue;
        }
        if (*p == '\'') {
            in_comment = 1;
            p++;
            continue;
        }
        if (*p == '"') {
            in_string = 1;
            p++;
            continue;
        }
        if ((p == active_lexer->source || !is_ident_char(p[-1])) &&
            keyword_at(p, "function")) {
            p += strlen("function");
            while (*p == ' ' || *p == '\t' || *p == '\r') {
                p++;
            }
            const char *start = p;
            if (!is_ident_start_char(*p)) {
                continue;
            }
            while (is_ident_char(*p)) {
                p++;
            }
            if ((size_t)(p - start) == name_len && strncmp(start, name, name_len) == 0) {
                return 1;
            }
            continue;
        }
        p++;
    }

    return 0;
}

static int is_modifier_target_expr(AstExpr *expr) {
    if (!expr) {
        return 0;
    }
    if (expr->kind == AST_EXPR_IDENT) {
        return 1;
    }
    if (expr->kind == AST_EXPR_FIELD) {
        return is_modifier_target_expr(expr->as.field.object);
    }
    if (expr->kind == AST_EXPR_INDEX) {
        return is_modifier_target_expr(expr->as.index.array);
    }
    return 0;
}

static AstExpr *expr_at(AstExpr *expr, int line, int column) {
    return ast_expr_position(expr, line, column);
}

static char *join_words(char *left, char *right) {
    size_t left_len = strlen(left);
    size_t right_len = strlen(right);
    char *joined = malloc(left_len + right_len + 2);
    if (!joined) {
        abort();
    }
    memcpy(joined, left, left_len);
    joined[left_len] = ' ';
    memcpy(joined + left_len + 1, right, right_len + 1);
    free(left);
    free(right);
    return joined;
}

static AstModifierUse parse_modifier_use(char *text) {
    AstModifierUse modifier = ast_modifier_use(text, ast_expr_list_empty());
    char *dot = strchr(modifier.name, '.');
    if (!dot) {
        return modifier;
    }

    char *start = modifier.name;
    while (*start == ' ' || *start == '\t') {
        start++;
    }
    char *library_end = dot;
    while (library_end > start && (library_end[-1] == ' ' || library_end[-1] == '\t')) {
        library_end--;
    }
    char *name_start = dot + 1;
    while (*name_start == ' ' || *name_start == '\t') {
        name_start++;
    }
    if (library_end == start || *name_start == '\0') {
        return modifier;
    }

    size_t library_len = (size_t)(library_end - start);
    char *library = malloc(library_len + 1);
    if (!library) {
        abort();
    }
    memcpy(library, start, library_len);
    library[library_len] = '\0';

    char *name = copy_const(name_start);
    free(modifier.name);
    modifier.library = library;
    modifier.name = name;
    return modifier;
}

static int unit_is(const char *text, const char *unit) {
    return strcmp(text, unit) == 0;
}

static AstDuration duration_add_unit(AstDuration duration, double amount, char *unit) {
    int value = (int)amount;
    if (unit_is(unit, "year") || unit_is(unit, "years")) {
        duration.years += value;
    } else if (unit_is(unit, "month") || unit_is(unit, "months")) {
        duration.months += value;
    } else if (unit_is(unit, "week") || unit_is(unit, "weeks")) {
        duration.weeks += value;
    } else if (unit_is(unit, "day") || unit_is(unit, "days")) {
        duration.days += value;
    } else if (unit_is(unit, "hour") || unit_is(unit, "hours")) {
        duration.hours += value;
    } else if (unit_is(unit, "minute") || unit_is(unit, "minutes")) {
        duration.minutes += value;
    } else if (unit_is(unit, "second") || unit_is(unit, "seconds")) {
        duration.seconds += value;
    } else {
        fprintf(stderr, "unknown duration unit: %s\n", unit);
    }
    free(unit);
    return duration;
}

static int modifier_lparen_ahead(const char *start) {
    const char *p = start + 1;
    int saw_term = 0;

    while (*p == ' ' || *p == '\t' || *p == '\r') {
        p++;
    }
    while (*p && *p != ')' && *p != '\n') {
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || *p == '_') {
            saw_term = 1;
            while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
                   (*p >= '0' && *p <= '9') || *p == '_') {
                p++;
            }
        } else if ((*p >= '0' && *p <= '9') || *p == '"') {
            saw_term = 1;
            if (*p == '"') {
                p++;
                while (*p && *p != '"' && *p != '\n') {
                    p++;
                }
                if (*p != '"') {
                    return 0;
                }
                p++;
            } else {
                while (*p >= '0' && *p <= '9') {
                    p++;
                }
                if (*p == '.') {
                    p++;
                    while (*p >= '0' && *p <= '9') {
                        p++;
                    }
                }
            }
        } else if (*p == ' ' || *p == '\t' || *p == '\r') {
            p++;
        } else if (*p == ',') {
            return 0;
        } else if (*p == '+' || *p == '-' || *p == '*' || *p == '/' ||
                   *p == '.' || *p == '[' || *p == ']') {
            p++;
        } else {
            return 0;
        }
    }
    if (*p != ')' || !saw_term) {
        return 0;
    }
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r') {
        p++;
    }
    int followed_by_comparison = *p == '=' || *p == '>' || *p == '<' ||
        (*p == '!' && (p[1] == '=' || p[1] == '>' || p[1] == '<'));
    if (!followed_by_comparison) {
        return 0;
    }

    const char *name_end = start;
    while (name_end > active_lexer->source &&
           (name_end[-1] == ' ' || name_end[-1] == '\t' || name_end[-1] == '\r')) {
        name_end--;
    }
    const char *name_start = name_end;
    while (name_start > active_lexer->source &&
           ((name_start[-1] >= 'A' && name_start[-1] <= 'Z') ||
            (name_start[-1] >= 'a' && name_start[-1] <= 'z') ||
            (name_start[-1] >= '0' && name_start[-1] <= '9') ||
            name_start[-1] == '_')) {
        name_start--;
    }
    if (name_start < name_end) {
        char *name = copy_text(name_start, (int)(name_end - name_start));
        int is_function = gbasic_builtin_function(name) || source_declares_function(name);
        free(name);
        if (is_function) {
            return 0;
        }
    }

    return 1;
}

static int yylex(void);
static void yyerror(const char *message);

#line 527 "src/parser.tab.c"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

#include "parser.tab.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_NUMBER = 3,                     /* NUMBER  */
  YYSYMBOL_IDENT = 4,                      /* IDENT  */
  YYSYMBOL_STRING = 5,                     /* STRING  */
  YYSYMBOL_MOD_CONTENT = 6,                /* MOD_CONTENT  */
  YYSYMBOL_LENS_CONTENT = 7,               /* LENS_CONTENT  */
  YYSYMBOL_QUALIFIED_IDENT = 8,            /* QUALIFIED_IDENT  */
  YYSYMBOL_IF = 9,                         /* IF  */
  YYSYMBOL_CONSIDER_IF = 10,               /* CONSIDER_IF  */
  YYSYMBOL_THEN = 11,                      /* THEN  */
  YYSYMBOL_ELSE = 12,                      /* ELSE  */
  YYSYMBOL_CONSIDER_ELSE = 13,             /* CONSIDER_ELSE  */
  YYSYMBOL_END = 14,                       /* END  */
  YYSYMBOL_END_CONSIDER = 15,              /* END_CONSIDER  */
  YYSYMBOL_PRINT = 16,                     /* PRINT  */
  YYSYMBOL_TRUE = 17,                      /* TRUE  */
  YYSYMBOL_FALSE = 18,                     /* FALSE  */
  YYSYMBOL_NOTHING = 19,                   /* NOTHING  */
  YYSYMBOL_UNKNOWN_VALUE = 20,             /* UNKNOWN_VALUE  */
  YYSYMBOL_AND = 21,                       /* AND  */
  YYSYMBOL_OR = 22,                        /* OR  */
  YYSYMBOL_NOT = 23,                       /* NOT  */
  YYSYMBOL_WITH = 24,                      /* WITH  */
  YYSYMBOL_NEW = 25,                       /* NEW  */
  YYSYMBOL_SPAWN = 26,                     /* SPAWN  */
  YYSYMBOL_FOR = 27,                       /* FOR  */
  YYSYMBOL_TO = 28,                        /* TO  */
  YYSYMBOL_IN = 29,                        /* IN  */
  YYSYMBOL_EACH = 30,                      /* EACH  */
  YYSYMBOL_WHILE = 31,                     /* WHILE  */
  YYSYMBOL_CONSIDER = 32,                  /* CONSIDER  */
  YYSYMBOL_BREAK = 33,                     /* BREAK  */
  YYSYMBOL_CONTINUE = 34,                  /* CONTINUE  */
  YYSYMBOL_FUNCTION = 35,                  /* FUNCTION  */
  YYSYMBOL_RETURN = 36,                    /* RETURN  */
  YYSYMBOL_GOTO = 37,                      /* GOTO  */
  YYSYMBOL_GOSUB = 38,                     /* GOSUB  */
  YYSYMBOL_WATCH = 39,                     /* WATCH  */
  YYSYMBOL_WITHOUT = 40,                   /* WITHOUT  */
  YYSYMBOL_WATCHERS = 41,                  /* WATCHERS  */
  YYSYMBOL_ON = 42,                        /* ON  */
  YYSYMBOL_RESUME = 43,                    /* RESUME  */
  YYSYMBOL_NEXT = 44,                      /* NEXT  */
  YYSYMBOL_STOP = 45,                      /* STOP  */
  YYSYMBOL_ERROR_VALUE = 46,               /* ERROR_VALUE  */
  YYSYMBOL_MODIFIER = 47,                  /* MODIFIER  */
  YYSYMBOL_PROGRAM = 48,                   /* PROGRAM  */
  YYSYMBOL_LIBRARY = 49,                   /* LIBRARY  */
  YYSYMBOL_LOAD = 50,                      /* LOAD  */
  YYSYMBOL_USE = 51,                       /* USE  */
  YYSYMBOL_EXPORT = 52,                    /* EXPORT  */
  YYSYMBOL_OP_EQ = 53,                     /* OP_EQ  */
  YYSYMBOL_OP_NE = 54,                     /* OP_NE  */
  YYSYMBOL_OP_GT = 55,                     /* OP_GT  */
  YYSYMBOL_OP_LT = 56,                     /* OP_LT  */
  YYSYMBOL_OP_GE = 57,                     /* OP_GE  */
  YYSYMBOL_OP_LE = 58,                     /* OP_LE  */
  YYSYMBOL_OP_NGT = 59,                    /* OP_NGT  */
  YYSYMBOL_OP_NLT = 60,                    /* OP_NLT  */
  YYSYMBOL_OP_NGE = 61,                    /* OP_NGE  */
  YYSYMBOL_OP_NLE = 62,                    /* OP_NLE  */
  YYSYMBOL_PLUS = 63,                      /* PLUS  */
  YYSYMBOL_MINUS = 64,                     /* MINUS  */
  YYSYMBOL_STAR = 65,                      /* STAR  */
  YYSYMBOL_SLASH = 66,                     /* SLASH  */
  YYSYMBOL_LPAREN = 67,                    /* LPAREN  */
  YYSYMBOL_MOD_LPAREN = 68,                /* MOD_LPAREN  */
  YYSYMBOL_RPAREN = 69,                    /* RPAREN  */
  YYSYMBOL_LBRACKET = 70,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 71,                  /* RBRACKET  */
  YYSYMBOL_LBRACE = 72,                    /* LBRACE  */
  YYSYMBOL_RBRACE = 73,                    /* RBRACE  */
  YYSYMBOL_COMMA = 74,                     /* COMMA  */
  YYSYMBOL_COLON = 75,                     /* COLON  */
  YYSYMBOL_NEWLINE = 76,                   /* NEWLINE  */
  YYSYMBOL_IF_WITHOUT_ELSE = 77,           /* IF_WITHOUT_ELSE  */
  YYSYMBOL_NO_DOT = 78,                    /* NO_DOT  */
  YYSYMBOL_DOT = 79,                       /* DOT  */
  YYSYMBOL_YYACCEPT = 80,                  /* $accept  */
  YYSYMBOL_program = 81,                   /* program  */
  YYSYMBOL_statement_list = 82,            /* statement_list  */
  YYSYMBOL_statement = 83,                 /* statement  */
  YYSYMBOL_assignment = 84,                /* assignment  */
  YYSYMBOL_lvalue = 85,                    /* lvalue  */
  YYSYMBOL_variable_name = 86,             /* variable_name  */
  YYSYMBOL_modifier = 87,                  /* modifier  */
  YYSYMBOL_comparison_lens = 88,           /* comparison_lens  */
  YYSYMBOL_89_1 = 89,                      /* $@1  */
  YYSYMBOL_modifier_name = 90,             /* modifier_name  */
  YYSYMBOL_modifier_word = 91,             /* modifier_word  */
  YYSYMBOL_print_statement = 92,           /* print_statement  */
  YYSYMBOL_call_statement = 93,            /* call_statement  */
  YYSYMBOL_with_lock_statement = 94,       /* with_lock_statement  */
  YYSYMBOL_for_each_statement = 95,        /* for_each_statement  */
  YYSYMBOL_while_statement = 96,           /* while_statement  */
  YYSYMBOL_consider_statement = 97,        /* consider_statement  */
  YYSYMBOL_consider_branch_list = 98,      /* consider_branch_list  */
  YYSYMBOL_consider_else_opt = 99,         /* consider_else_opt  */
  YYSYMBOL_consider_statement_list = 100,  /* consider_statement_list  */
  YYSYMBOL_consider_body_statement = 101,  /* consider_body_statement  */
  YYSYMBOL_function_statement = 102,       /* function_statement  */
  YYSYMBOL_modifier_statement = 103,       /* modifier_statement  */
  YYSYMBOL_program_statement = 104,        /* program_statement  */
  YYSYMBOL_library_statement = 105,        /* library_statement  */
  YYSYMBOL_use_statement = 106,            /* use_statement  */
  YYSYMBOL_modifier_signature = 107,       /* modifier_signature  */
  YYSYMBOL_modifier_context = 108,         /* modifier_context  */
  YYSYMBOL_watch_statement = 109,          /* watch_statement  */
  YYSYMBOL_watch_target_list = 110,        /* watch_target_list  */
  YYSYMBOL_watch_target_path = 111,        /* watch_target_path  */
  YYSYMBOL_without_watchers_statement = 112, /* without_watchers_statement  */
  YYSYMBOL_on_error_statement = 113,       /* on_error_statement  */
  YYSYMBOL_error_statement = 114,          /* error_statement  */
  YYSYMBOL_return_statement = 115,         /* return_statement  */
  YYSYMBOL_label_statement = 116,          /* label_statement  */
  YYSYMBOL_goto_statement = 117,           /* goto_statement  */
  YYSYMBOL_gosub_statement = 118,          /* gosub_statement  */
  YYSYMBOL_break_statement = 119,          /* break_statement  */
  YYSYMBOL_continue_statement = 120,       /* continue_statement  */
  YYSYMBOL_if_statement = 121,             /* if_statement  */
  YYSYMBOL_if_block_tail = 122,            /* if_block_tail  */
  YYSYMBOL_if_inline_tail = 123,           /* if_inline_tail  */
  YYSYMBOL_inline_statement = 124,         /* inline_statement  */
  YYSYMBOL_expression = 125,               /* expression  */
  YYSYMBOL_or_expression = 126,            /* or_expression  */
  YYSYMBOL_and_expression = 127,           /* and_expression  */
  YYSYMBOL_comparison_expression = 128,    /* comparison_expression  */
  YYSYMBOL_additive_expression = 129,      /* additive_expression  */
  YYSYMBOL_multiplicative_expression = 130, /* multiplicative_expression  */
  YYSYMBOL_unary_expression = 131,         /* unary_expression  */
  YYSYMBOL_postfix_expression = 132,       /* postfix_expression  */
  YYSYMBOL_comparison_operator = 133,      /* comparison_operator  */
  YYSYMBOL_primary = 134,                  /* primary  */
  YYSYMBOL_record_literal = 135,           /* record_literal  */
  YYSYMBOL_ident_suffix = 136,             /* ident_suffix  */
  YYSYMBOL_ident_dot_suffix = 137,         /* ident_dot_suffix  */
  YYSYMBOL_duration_terms = 138,           /* duration_terms  */
  YYSYMBOL_argument_list_opt = 139,        /* argument_list_opt  */
  YYSYMBOL_argument_list = 140,            /* argument_list  */
  YYSYMBOL_array_argument_list = 141,      /* array_argument_list  */
  YYSYMBOL_parameter_list_opt = 142,       /* parameter_list_opt  */
  YYSYMBOL_parameter_list = 143,           /* parameter_list  */
  YYSYMBOL_record_field_list = 144,        /* record_field_list  */
  YYSYMBOL_field_policy = 145,             /* field_policy  */
  YYSYMBOL_optional_newlines = 146         /* optional_newlines  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_int16 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if 1

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* 1 */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL \
             && defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
  YYLTYPE yyls_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE) \
             + YYSIZEOF (YYLTYPE)) \
      + 2 * YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  3
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   1475

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  80
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  67
/* YYNRULES -- Number of rules.  */
#define YYNRULES  212
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  457

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   334


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   527,   527,   531,   532,   533,   537,   538,   539,   540,
     541,   542,   543,   544,   545,   546,   547,   548,   549,   550,
     551,   552,   553,   554,   555,   556,   557,   558,   559,   563,
     564,   574,   575,   576,   580,   581,   582,   586,   590,   590,
     596,   597,   601,   602,   603,   604,   608,   612,   613,   619,
     632,   644,   647,   653,   659,   665,   668,   674,   675,   679,
     680,   681,   685,   686,   687,   688,   689,   690,   691,   692,
     693,   694,   695,   696,   697,   698,   699,   700,   701,   702,
     703,   704,   705,   706,   707,   711,   717,   720,   726,   732,
     738,   739,   740,   741,   742,   753,   767,   768,   772,   776,
     779,   785,   786,   790,   791,   795,   801,   802,   803,   807,
     811,   812,   816,   820,   824,   828,   832,   836,   840,   847,
     850,   853,   859,   862,   865,   871,   872,   873,   874,   875,
     876,   877,   878,   879,   880,   881,   885,   889,   890,   894,
     895,   899,   900,   901,   904,   914,   915,   916,   920,   921,
     922,   926,   927,   928,   929,   930,   931,   935,   936,   937,
     941,   942,   943,   944,   945,   946,   947,   948,   949,   950,
     954,   955,   956,   957,   968,   974,   975,   976,   977,   978,
     979,   980,   981,   982,   986,   987,   991,   996,  1001,  1008,
    1013,  1021,  1025,  1031,  1032,  1036,  1037,  1041,  1042,  1046,
    1047,  1051,  1052,  1056,  1057,  1058,  1059,  1060,  1061,  1069,
    1090,  1106,  1107
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if 1
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "NUMBER", "IDENT",
  "STRING", "MOD_CONTENT", "LENS_CONTENT", "QUALIFIED_IDENT", "IF",
  "CONSIDER_IF", "THEN", "ELSE", "CONSIDER_ELSE", "END", "END_CONSIDER",
  "PRINT", "TRUE", "FALSE", "NOTHING", "UNKNOWN_VALUE", "AND", "OR", "NOT",
  "WITH", "NEW", "SPAWN", "FOR", "TO", "IN", "EACH", "WHILE", "CONSIDER",
  "BREAK", "CONTINUE", "FUNCTION", "RETURN", "GOTO", "GOSUB", "WATCH",
  "WITHOUT", "WATCHERS", "ON", "RESUME", "NEXT", "STOP", "ERROR_VALUE",
  "MODIFIER", "PROGRAM", "LIBRARY", "LOAD", "USE", "EXPORT", "OP_EQ",
  "OP_NE", "OP_GT", "OP_LT", "OP_GE", "OP_LE", "OP_NGT", "OP_NLT",
  "OP_NGE", "OP_NLE", "PLUS", "MINUS", "STAR", "SLASH", "LPAREN",
  "MOD_LPAREN", "RPAREN", "LBRACKET", "RBRACKET", "LBRACE", "RBRACE",
  "COMMA", "COLON", "NEWLINE", "IF_WITHOUT_ELSE", "NO_DOT", "DOT",
  "$accept", "program", "statement_list", "statement", "assignment",
  "lvalue", "variable_name", "modifier", "comparison_lens", "$@1",
  "modifier_name", "modifier_word", "print_statement", "call_statement",
  "with_lock_statement", "for_each_statement", "while_statement",
  "consider_statement", "consider_branch_list", "consider_else_opt",
  "consider_statement_list", "consider_body_statement",
  "function_statement", "modifier_statement", "program_statement",
  "library_statement", "use_statement", "modifier_signature",
  "modifier_context", "watch_statement", "watch_target_list",
  "watch_target_path", "without_watchers_statement", "on_error_statement",
  "error_statement", "return_statement", "label_statement",
  "goto_statement", "gosub_statement", "break_statement",
  "continue_statement", "if_statement", "if_block_tail", "if_inline_tail",
  "inline_statement", "expression", "or_expression", "and_expression",
  "comparison_expression", "additive_expression",
  "multiplicative_expression", "unary_expression", "postfix_expression",
  "comparison_operator", "primary", "record_literal", "ident_suffix",
  "ident_dot_suffix", "duration_terms", "argument_list_opt",
  "argument_list", "array_argument_list", "parameter_list_opt",
  "parameter_list", "record_field_list", "field_policy",
  "optional_newlines", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-354)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -354,    29,   528,  -354,   -40,   -24,  1403,  -354,  1403,    54,
      46,  1403,  1403,  -354,  -354,    83,  1403,   111,   127,    26,
      51,    93,  -354,    34,   136,   162,   173,    58,   219,   134,
    -354,  -354,   115,    81,   126,   128,   133,  -354,  -354,  -354,
    -354,  -354,  -354,  -354,  -354,   138,  -354,  -354,   140,   145,
     149,   159,   164,   166,   177,   183,  -354,  1403,  1403,   207,
    -354,  -354,   171,  -354,  -354,  -354,  -354,  1403,   400,   259,
    -354,  1403,  1403,  -354,  -354,   -53,   255,   250,   253,  -354,
    1378,   163,  -354,    33,  -354,  -354,   273,  -354,   212,   251,
     277,   208,   209,   216,  -354,  -354,  -354,    27,  -354,    91,
     213,   210,   175,   283,  -354,  -354,  -354,  -354,  -354,    21,
    -354,   264,   226,   223,   296,  -354,   297,  -354,   136,  -354,
    1403,   298,  1403,   299,   252,  -354,  -354,  -354,  -354,  -354,
    -354,  -354,  -354,  -354,  -354,  -354,  -354,  -354,   238,   237,
     256,  -354,  1403,  -354,    65,   257,  -354,   258,   330,     9,
    1403,   324,  -354,    86,  1403,  1403,  -354,  -354,  -354,  -354,
    -354,  -354,  -354,  -354,  -354,  -354,  1403,  1403,  -354,   260,
     260,  1403,  1403,  1403,  1403,   325,   327,  1403,  1403,   303,
    -354,   326,   333,   -13,    27,  -354,   336,  -354,   337,   301,
    -354,   275,   333,  -354,   342,   333,  -354,   346,   347,   331,
    -354,  -354,   289,  -354,  1403,  -354,  1403,  -354,   285,   290,
    1403,  -354,  -354,  -354,  -354,   287,    88,  -354,   292,   295,
     300,  -354,  -354,  -354,  -354,  -354,  -354,  -354,  -354,  -354,
    -354,  -354,  -354,  -354,   302,   253,  -354,   163,   163,   368,
    1403,  1403,   167,  -354,  -354,   308,  -354,  -354,   312,   307,
    1403,   578,  1403,   135,  -354,   315,   314,   310,   213,   628,
    -354,   678,  -354,  -354,  1403,   320,  -354,   316,   321,   728,
    -354,  -354,   342,  -354,  -354,  -354,  -354,  -354,   322,  -354,
       8,  1403,   389,  1403,  -354,   100,  -354,  1403,  -354,   478,
     383,   323,   167,   167,  -354,   334,  -354,   335,   367,   388,
    1403,   340,   397,   345,   409,  -354,   384,   385,   358,  -354,
    -354,   352,   380,   355,  -354,   431,  -354,  -354,  1403,   363,
    -354,     5,  -354,   369,  1327,   428,  -354,  1354,  -354,  -354,
    -354,   778,  -354,   364,   365,   432,  -354,   366,  -354,  -354,
     828,   376,   377,  -354,   878,  -354,   379,  -354,  -354,  -354,
     386,    94,  -354,  -354,   382,   387,  -354,   390,   928,   433,
     978,  -354,  -354,   392,  1028,  -354,  1078,   420,  -354,  -354,
     415,  1128,  -354,  1178,  1403,  1403,   389,  1403,  1228,  -354,
    -354,  1278,  -354,   440,   393,   438,  1028,  -354,  -354,   395,
     398,   402,  -354,  -354,  -354,  -354,  -354,  -354,  -354,  -354,
    -354,   403,  -354,  -354,   404,   405,   407,   408,   412,   413,
     417,   421,  -354,   441,   423,   424,   425,   444,  -354,  -354,
     416,  -354,   487,   495,   430,  -354,   443,  1028,  -354,  -354,
    -354,  -354,  -354,  -354,  -354,  -354,  -354,  -354,  -354,  -354,
     445,  -354,  -354,   447,   455,   458,   459,   462,  -354,  -354,
    -354,  -354,  -354,  1403,  -354,  -354,  -354
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       3,     0,     2,     1,    34,     0,     0,    35,     0,     0,
       0,     0,     0,   115,   116,     0,   110,     0,     0,     0,
       0,     0,    36,     0,     0,     0,     0,     0,     0,     0,
       4,     5,     0,     0,    31,     0,     0,     9,    10,    11,
      12,    13,    14,    15,    16,     0,    18,    19,     0,     0,
       0,     0,     0,     0,     0,     0,    28,   193,   193,   170,
      34,   172,     0,   176,   177,   178,   179,     0,     0,     0,
     175,     0,     0,   211,   211,   186,     0,   136,   137,   139,
     141,   145,   148,   151,   157,   183,   171,    46,     0,     0,
       0,     0,     0,     0,   111,   113,   114,     0,   103,     0,
     101,     0,     0,     0,   109,    42,    44,    43,    45,    96,
      40,     0,     0,     0,    91,    93,    90,    92,     0,     6,
       0,     0,     0,     0,     0,   112,     7,     8,    17,    20,
      21,    22,    23,    24,    25,    26,    27,   195,     0,   194,
       0,   191,   193,   152,   154,     0,   153,     0,     0,     0,
     193,     0,   173,     0,     0,     0,   160,   161,   162,   163,
     164,   165,   166,   167,   168,   169,     0,     0,    38,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       3,     0,   199,     0,     0,     3,     0,     3,     0,     0,
     108,     0,   199,    41,     0,   199,     3,     0,     0,     0,
      29,    37,     0,    33,     0,    47,     0,    48,     0,     0,
     193,   180,   181,   212,   197,   211,     0,   184,   211,     0,
     189,     3,   125,    31,   126,   127,   128,   129,   130,   131,
     132,   133,   134,   135,     0,   138,   140,   146,   147,     0,
       0,     0,   142,   149,   150,     0,   159,   192,     0,     0,
       0,     0,     0,    57,   201,     0,   200,     0,   102,     0,
     104,     0,   106,   107,   193,     0,    98,     0,     0,     0,
      95,    94,     0,    32,    30,   196,   174,   155,     0,   211,
       0,     0,     0,     0,   211,     0,   187,   193,   188,     0,
     122,     0,   144,   143,   158,     0,     3,     0,    35,     0,
       0,     0,     0,     0,     0,     3,    35,    35,     0,    97,
       3,     0,    35,     0,   156,     0,   182,   203,   209,     0,
     204,     0,   185,     0,     0,    35,   117,     0,   118,    39,
       3,     0,     3,     0,     0,     0,    59,     0,     3,   202,
       0,     0,     0,    49,     0,     3,     0,     3,   198,   210,
       0,     0,   190,     3,     0,     0,     3,     0,     0,    35,
       0,    53,    59,     0,    58,    54,     0,    35,   100,   105,
      35,     0,    89,     0,     0,     0,     0,     0,     0,   120,
     119,     0,   123,    35,     0,    35,    55,    59,    60,     0,
       0,     0,    65,    66,    67,    68,    61,    69,    70,    71,
      72,     0,    74,    75,     0,     0,     0,     0,     0,     0,
       0,     0,    84,    35,     0,     0,    35,    35,   205,   206,
       0,   207,    35,    35,     0,    51,     0,    56,    62,    63,
      64,    73,    76,    77,    78,    79,    80,    81,    82,    83,
       0,    99,    86,     0,     0,     0,     0,     0,    50,    52,
      85,    88,    87,     0,   121,   124,   208
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -354,  -354,    77,  -354,  -149,  -354,    -1,   454,  -354,  -354,
    -354,   399,  -145,  -138,  -353,  -319,  -318,  -317,  -354,  -354,
    -343,  -354,  -309,  -300,  -289,  -281,  -137,   422,   267,  -269,
     446,   357,  -257,  -132,  -131,  -130,  -253,  -125,  -121,  -119,
    -117,  -243,  -354,  -354,  -170,    -6,  -354,   391,   394,  -168,
      85,   -47,   479,   101,  -354,   339,  -354,  -354,  -354,   -51,
    -354,  -354,    -7,  -354,  -354,   170,   -62
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,     2,    31,    32,    33,    75,   124,   170,   239,
     109,   110,    35,    36,    37,    38,    39,    40,   253,   302,
     364,   396,    41,    42,    43,    44,    45,   111,   267,    46,
      99,   100,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,   326,   328,   234,   137,    77,    78,    79,    80,
      81,    82,    83,   171,    84,    85,   152,   288,    86,   138,
     139,   215,   255,   256,   218,   319,   148
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      76,    34,    87,   242,   222,    91,    92,   140,   224,   351,
      94,   392,   149,   216,   150,   225,   226,   104,    98,   386,
     143,   227,   228,   229,   146,   105,   151,    57,   230,     3,
      60,    60,   231,   392,   232,   106,   233,    59,    60,    61,
       7,     7,    62,    58,   427,   393,   394,   395,     7,   107,
      89,    63,    64,    65,    66,   397,   257,    67,    88,    68,
      69,   184,   114,   115,   398,   108,   147,   393,   394,   395,
      22,    22,   292,   293,   392,   399,    90,   397,    22,   316,
      70,   213,   217,   400,   213,   213,   398,    93,   192,   209,
       4,   208,   101,    97,     5,   402,    98,   399,    71,   219,
       7,    72,     8,   174,    73,   400,    74,   403,   393,   394,
     395,   407,   175,   103,   200,    95,   202,   402,   397,    13,
      14,   412,    16,    17,    18,   243,   244,   398,    21,   403,
      22,    96,    23,   407,   120,   174,    27,    28,   399,   102,
     105,   281,   214,   412,   175,   300,   400,   375,   301,   121,
     106,   122,   223,   280,   354,   282,   285,   357,   402,   278,
     123,   376,   221,   283,   107,   184,   112,   185,   245,   377,
     403,   248,   249,   322,   407,   222,   213,   113,   222,   224,
     108,   118,   224,    98,   412,   265,   225,   226,   268,   225,
     226,   119,   227,   228,   229,   227,   228,   229,   274,   230,
     275,   125,   230,   231,   126,   232,   231,   233,   232,   127,
     233,   141,   188,   308,   128,   389,   129,   315,   189,   390,
     190,   130,   321,   116,   117,   131,   391,   401,   172,   173,
     166,   167,   404,   405,   406,   132,   323,   389,   142,   408,
     133,   390,   134,   409,   297,   410,   299,   411,   391,   401,
      34,   237,   238,   135,   404,   405,   406,   251,    34,   136,
      34,   408,   259,   145,   261,   409,   153,   410,    34,   411,
     240,   241,   154,   269,   155,   317,   176,   320,   389,   177,
     178,   179,   390,   182,   180,   181,   187,   191,    34,   391,
     401,   194,   186,   195,   335,   404,   405,   406,   289,   196,
     197,   198,   408,   203,   201,   204,   409,   205,   410,   348,
     411,   206,   349,   156,   157,   158,   159,   160,   161,   162,
     163,   164,   165,   223,   210,   207,   223,   211,   220,   246,
      34,   247,   250,    59,    60,    61,   252,   254,    62,    34,
     260,   262,   264,    34,     7,   263,   266,    63,    64,    65,
      66,   270,   271,    67,   276,    68,    69,    34,   272,    34,
     273,   279,    74,    34,   286,    34,   284,   287,   418,   419,
      34,   421,    34,   331,    22,   291,    70,    34,   290,   294,
      34,   295,   340,   296,   303,    34,   305,   344,   304,   309,
     311,   314,   310,   318,    71,   327,   329,    72,   333,   334,
      73,   212,    74,    59,    60,    61,   213,   358,    62,   360,
     330,   332,   337,   339,     7,   366,   336,    63,    64,    65,
      66,   338,   371,   341,   373,   342,    34,   343,   345,   346,
     378,   347,   350,   381,    59,    60,    61,   355,   352,    62,
     361,   362,   365,   363,    22,     7,    70,   456,    63,    64,
      65,    66,   368,   369,    67,   372,    68,    69,   379,   414,
     384,   374,   415,   380,   424,   426,   382,    72,   387,   425,
      73,   428,    74,   443,   429,    22,   440,    70,   430,   431,
     432,   433,     4,   434,   435,   445,     5,     6,   436,   437,
     324,   444,   325,   438,     8,    71,   446,   439,    72,   441,
     442,    73,     9,    74,   447,    10,   448,   213,   193,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,   449,
      21,   450,    22,   451,    23,    24,    25,    26,    27,    28,
      29,   452,     4,   453,   169,   454,     5,     6,   455,   313,
     199,   258,     7,   183,     8,   235,   420,   144,   277,   236,
       0,     0,     9,     0,    30,    10,     0,     0,     0,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,     0,
      21,     0,    22,     0,    23,    24,    25,    26,    27,    28,
      29,     0,     4,     0,     0,     0,     5,     6,     0,     0,
       0,     0,   298,     0,     8,     0,     0,     0,     0,     0,
       0,     0,     9,     0,    30,    10,     0,     0,     0,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,     0,
      21,     0,    22,     0,    23,    24,    25,    26,    27,    28,
      29,     0,     4,     0,     0,     0,     5,     6,     0,     0,
       0,     0,   306,     0,     8,     0,     0,     0,     0,     0,
       0,     0,     9,     0,    30,    10,     0,     0,     0,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,     0,
      21,     0,    22,     0,    23,    24,    25,    26,    27,    28,
      29,     0,     4,     0,     0,     0,     5,     6,     0,     0,
       0,     0,   307,     0,     8,     0,     0,     0,     0,     0,
       0,     0,     9,     0,    30,    10,     0,     0,     0,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,     0,
      21,     0,    22,     0,    23,    24,    25,    26,    27,    28,
      29,     0,     4,     0,     0,     0,     5,     6,     0,     0,
       0,     0,   312,     0,     8,     0,     0,     0,     0,     0,
       0,     0,     9,     0,    30,    10,     0,     0,     0,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,     0,
      21,     0,    22,     0,    23,    24,    25,    26,    27,    28,
      29,     0,     4,     0,     0,     0,     5,     6,     0,     0,
       0,     0,   359,     0,     8,     0,     0,     0,     0,     0,
       0,     0,     9,     0,    30,    10,     0,     0,     0,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,     0,
      21,     0,    22,     0,    23,    24,    25,    26,    27,    28,
      29,     0,     4,     0,     0,     0,     5,     6,     0,     0,
       0,     0,   367,     0,     8,     0,     0,     0,     0,     0,
       0,     0,     9,     0,    30,    10,     0,     0,     0,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,     0,
      21,     0,    22,     0,    23,    24,    25,    26,    27,    28,
      29,     0,     4,     0,     0,     0,     5,     6,     0,     0,
       0,     0,   370,     0,     8,     0,     0,     0,     0,     0,
       0,     0,     9,     0,    30,    10,     0,     0,     0,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,     0,
      21,     0,    22,     0,    23,    24,    25,    26,    27,    28,
      29,     0,     4,     0,     0,     0,     5,     6,     0,     0,
       0,     0,   383,     0,     8,     0,     0,     0,     0,     0,
       0,     0,     9,     0,    30,    10,     0,     0,     0,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,     0,
      21,     0,    22,     0,    23,    24,    25,    26,    27,    28,
      29,     0,     4,     0,     0,     0,     5,     6,     0,     0,
       0,     0,   385,     0,     8,     0,     0,     0,     0,     0,
       0,     0,     9,     0,    30,    10,     0,     0,     0,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,     0,
      21,     0,    22,     0,    23,    24,    25,    26,    27,    28,
      29,     0,     4,     0,     0,     0,     5,     6,     0,     0,
       0,     0,     7,     0,     8,     0,     0,     0,     0,     0,
       0,     0,     9,     0,    30,    10,     0,     0,     0,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,     0,
      21,     0,    22,     0,    23,    24,    25,    26,    27,    28,
      29,     0,     4,     0,     0,     0,     5,     6,     0,     0,
       0,     0,   413,     0,     8,     0,     0,     0,     0,     0,
       0,     0,     9,     0,   388,    10,     0,     0,     0,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,     0,
      21,     0,    22,     0,    23,    24,    25,    26,    27,    28,
      29,     0,     4,     0,     0,     0,     5,     6,     0,     0,
       0,     0,   416,     0,     8,     0,     0,     0,     0,     0,
       0,     0,     9,     0,    30,    10,     0,     0,     0,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,     0,
      21,     0,    22,     0,    23,    24,    25,    26,    27,    28,
      29,     0,     4,     0,     0,     0,     5,     6,     0,     0,
       0,     0,   417,     0,     8,     0,     0,     0,     0,     0,
       0,     0,     9,     0,    30,    10,     0,     0,     0,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,     0,
      21,     0,    22,     0,    23,    24,    25,    26,    27,    28,
      29,     0,     4,     0,     0,     0,     5,     6,     0,     0,
       0,     0,   422,     0,     8,     0,     0,     0,     0,     0,
       0,     0,     9,     0,    30,    10,     0,     0,     0,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,     0,
      21,     0,    22,     0,    23,    24,    25,    26,    27,    28,
      29,     0,     4,     0,     0,     0,     5,     6,     0,     0,
       0,     0,   423,     0,     8,     0,     0,     0,     0,     0,
       0,     0,     9,     0,    30,    10,     0,     0,     0,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,     0,
      21,     0,    22,     0,    23,    24,    25,    26,    27,    28,
      29,     4,     0,     0,     0,     5,     0,     0,     0,     0,
       0,     7,     0,     8,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    30,     0,     0,     0,     4,     0,
      13,    14,     5,    16,    17,    18,     0,     0,     7,    21,
       8,    22,     0,    23,     0,     0,     0,    27,    28,     0,
       0,     0,     0,     0,     0,     0,     0,    13,    14,     0,
      16,    17,    18,     0,     0,     0,    21,     0,    22,     0,
      23,     0,     0,   353,    27,    28,    59,    60,    61,     0,
       0,    62,     0,     0,     0,     0,     0,     7,     0,     0,
      63,    64,    65,    66,     0,     0,    67,     0,    68,    69,
     356,   156,   157,   158,   159,   160,   161,   162,   163,   164,
     165,   166,   167,     0,     0,     0,   121,    22,     0,    70,
     168,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    71,     0,     0,
      72,     0,     0,    73,     0,    74
};

static const yytype_int16 yycheck[] =
{
       6,     2,     8,   171,   153,    11,    12,    58,   153,     4,
      16,   364,    74,     4,    67,   153,   153,    23,    19,   362,
      67,   153,   153,   153,    71,     4,    79,    67,   153,     0,
       4,     4,   153,   386,   153,    14,   153,     3,     4,     5,
      14,    14,     8,    67,   387,   364,   364,   364,    14,    28,
       4,    17,    18,    19,    20,   364,    69,    23,     4,    25,
      26,    74,     4,     5,   364,    44,    72,   386,   386,   386,
      44,    44,   240,   241,   427,   364,    30,   386,    44,    71,
      46,    76,    73,   364,    76,    76,   386,     4,    67,    24,
       4,   142,    41,    67,     8,   364,    97,   386,    64,   150,
      14,    67,    16,    70,    70,   386,    72,   364,   427,   427,
     427,   364,    79,    79,   120,     4,   122,   386,   427,    33,
      34,   364,    36,    37,    38,   172,   173,   427,    42,   386,
      44,     4,    46,   386,    53,    70,    50,    51,   427,    46,
       4,    53,   148,   386,    79,    10,   427,    53,    13,    68,
      14,    70,   153,   215,   324,    67,   218,   327,   427,   210,
      79,    67,    76,    75,    28,    74,     4,    76,   174,    75,
     427,   177,   178,    73,   427,   324,    76,     4,   327,   324,
      44,    47,   327,   184,   427,   192,   324,   324,   195,   327,
     327,    76,   324,   324,   324,   327,   327,   327,   204,   324,
     206,    75,   327,   324,    76,   324,   327,   324,   327,    76,
     327,     4,    37,   264,    76,   364,    76,   279,    43,   364,
      45,    76,   284,     4,     5,    76,   364,   364,    65,    66,
      63,    64,   364,   364,   364,    76,   287,   386,    67,   364,
      76,   386,    76,   364,   250,   364,   252,   364,   386,   386,
     251,   166,   167,    76,   386,   386,   386,   180,   259,    76,
     261,   386,   185,     4,   187,   386,    11,   386,   269,   386,
     169,   170,    22,   196,    21,   281,     3,   283,   427,    67,
      29,     4,   427,    67,    76,    76,    76,     4,   289,   427,
     427,    27,    79,    67,   300,   427,   427,   427,   221,    76,
       4,     4,   427,     4,     6,    53,   427,    69,   427,   315,
     427,    74,   318,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,   324,    67,    69,   327,    69,     4,     4,
     331,     4,    29,     3,     4,     5,    10,     4,     8,   340,
       4,     4,    67,   344,    14,    44,     4,    17,    18,    19,
      20,     5,     5,    23,    69,    25,    26,   358,    27,   360,
      71,    74,    72,   364,    69,   366,    74,    67,   374,   375,
     371,   377,   373,   296,    44,     7,    46,   378,    76,    71,
     381,    69,   305,    76,    69,   386,    76,   310,    74,    69,
      69,    69,    76,     4,    64,    12,    73,    67,    31,    11,
      70,    71,    72,     3,     4,     5,    76,   330,     8,   332,
      76,    76,    15,     4,    14,   338,    76,    17,    18,    19,
      20,    76,   345,    39,   347,    40,   427,    69,    76,    49,
     353,    76,    69,   356,     3,     4,     5,     9,    69,     8,
      76,    76,    76,    11,    44,    14,    46,   453,    17,    18,
      19,    20,    76,    76,    23,    76,    25,    26,    76,    39,
      27,    75,    47,    76,    24,    27,    76,    67,    76,    76,
      70,    76,    72,    48,    76,    44,    35,    46,    76,    76,
      76,    76,     4,    76,    76,    69,     8,     9,    76,    76,
      12,    47,    14,    76,    16,    64,     9,    76,    67,    76,
      76,    70,    24,    72,     9,    27,    76,    76,   109,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    76,
      42,    76,    44,    76,    46,    47,    48,    49,    50,    51,
      52,    76,     4,    75,    80,    76,     8,     9,    76,   272,
     118,   184,    14,    97,    16,   154,   376,    68,   209,   155,
      -1,    -1,    24,    -1,    76,    27,    -1,    -1,    -1,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    -1,
      42,    -1,    44,    -1,    46,    47,    48,    49,    50,    51,
      52,    -1,     4,    -1,    -1,    -1,     8,     9,    -1,    -1,
      -1,    -1,    14,    -1,    16,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    24,    -1,    76,    27,    -1,    -1,    -1,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    -1,
      42,    -1,    44,    -1,    46,    47,    48,    49,    50,    51,
      52,    -1,     4,    -1,    -1,    -1,     8,     9,    -1,    -1,
      -1,    -1,    14,    -1,    16,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    24,    -1,    76,    27,    -1,    -1,    -1,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    -1,
      42,    -1,    44,    -1,    46,    47,    48,    49,    50,    51,
      52,    -1,     4,    -1,    -1,    -1,     8,     9,    -1,    -1,
      -1,    -1,    14,    -1,    16,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    24,    -1,    76,    27,    -1,    -1,    -1,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    -1,
      42,    -1,    44,    -1,    46,    47,    48,    49,    50,    51,
      52,    -1,     4,    -1,    -1,    -1,     8,     9,    -1,    -1,
      -1,    -1,    14,    -1,    16,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    24,    -1,    76,    27,    -1,    -1,    -1,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    -1,
      42,    -1,    44,    -1,    46,    47,    48,    49,    50,    51,
      52,    -1,     4,    -1,    -1,    -1,     8,     9,    -1,    -1,
      -1,    -1,    14,    -1,    16,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    24,    -1,    76,    27,    -1,    -1,    -1,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    -1,
      42,    -1,    44,    -1,    46,    47,    48,    49,    50,    51,
      52,    -1,     4,    -1,    -1,    -1,     8,     9,    -1,    -1,
      -1,    -1,    14,    -1,    16,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    24,    -1,    76,    27,    -1,    -1,    -1,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    -1,
      42,    -1,    44,    -1,    46,    47,    48,    49,    50,    51,
      52,    -1,     4,    -1,    -1,    -1,     8,     9,    -1,    -1,
      -1,    -1,    14,    -1,    16,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    24,    -1,    76,    27,    -1,    -1,    -1,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    -1,
      42,    -1,    44,    -1,    46,    47,    48,    49,    50,    51,
      52,    -1,     4,    -1,    -1,    -1,     8,     9,    -1,    -1,
      -1,    -1,    14,    -1,    16,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    24,    -1,    76,    27,    -1,    -1,    -1,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    -1,
      42,    -1,    44,    -1,    46,    47,    48,    49,    50,    51,
      52,    -1,     4,    -1,    -1,    -1,     8,     9,    -1,    -1,
      -1,    -1,    14,    -1,    16,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    24,    -1,    76,    27,    -1,    -1,    -1,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    -1,
      42,    -1,    44,    -1,    46,    47,    48,    49,    50,    51,
      52,    -1,     4,    -1,    -1,    -1,     8,     9,    -1,    -1,
      -1,    -1,    14,    -1,    16,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    24,    -1,    76,    27,    -1,    -1,    -1,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    -1,
      42,    -1,    44,    -1,    46,    47,    48,    49,    50,    51,
      52,    -1,     4,    -1,    -1,    -1,     8,     9,    -1,    -1,
      -1,    -1,    14,    -1,    16,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    24,    -1,    76,    27,    -1,    -1,    -1,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    -1,
      42,    -1,    44,    -1,    46,    47,    48,    49,    50,    51,
      52,    -1,     4,    -1,    -1,    -1,     8,     9,    -1,    -1,
      -1,    -1,    14,    -1,    16,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    24,    -1,    76,    27,    -1,    -1,    -1,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    -1,
      42,    -1,    44,    -1,    46,    47,    48,    49,    50,    51,
      52,    -1,     4,    -1,    -1,    -1,     8,     9,    -1,    -1,
      -1,    -1,    14,    -1,    16,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    24,    -1,    76,    27,    -1,    -1,    -1,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    -1,
      42,    -1,    44,    -1,    46,    47,    48,    49,    50,    51,
      52,    -1,     4,    -1,    -1,    -1,     8,     9,    -1,    -1,
      -1,    -1,    14,    -1,    16,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    24,    -1,    76,    27,    -1,    -1,    -1,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    -1,
      42,    -1,    44,    -1,    46,    47,    48,    49,    50,    51,
      52,    -1,     4,    -1,    -1,    -1,     8,     9,    -1,    -1,
      -1,    -1,    14,    -1,    16,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    24,    -1,    76,    27,    -1,    -1,    -1,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    -1,
      42,    -1,    44,    -1,    46,    47,    48,    49,    50,    51,
      52,     4,    -1,    -1,    -1,     8,    -1,    -1,    -1,    -1,
      -1,    14,    -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    76,    -1,    -1,    -1,     4,    -1,
      33,    34,     8,    36,    37,    38,    -1,    -1,    14,    42,
      16,    44,    -1,    46,    -1,    -1,    -1,    50,    51,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    33,    34,    -1,
      36,    37,    38,    -1,    -1,    -1,    42,    -1,    44,    -1,
      46,    -1,    -1,    76,    50,    51,     3,     4,     5,    -1,
      -1,     8,    -1,    -1,    -1,    -1,    -1,    14,    -1,    -1,
      17,    18,    19,    20,    -1,    -1,    23,    -1,    25,    26,
      76,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    63,    64,    -1,    -1,    -1,    68,    44,    -1,    46,
      72,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    64,    -1,    -1,
      67,    -1,    -1,    70,    -1,    72
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    81,    82,     0,     4,     8,     9,    14,    16,    24,
      27,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    42,    44,    46,    47,    48,    49,    50,    51,    52,
      76,    83,    84,    85,    86,    92,    93,    94,    95,    96,
      97,   102,   103,   104,   105,   106,   109,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   121,    67,    67,     3,
       4,     5,     8,    17,    18,    19,    20,    23,    25,    26,
      46,    64,    67,    70,    72,    86,   125,   126,   127,   128,
     129,   130,   131,   132,   134,   135,   138,   125,     4,     4,
      30,   125,   125,     4,   125,     4,     4,    67,    86,   110,
     111,    41,    46,    79,   125,     4,    14,    28,    44,    90,
      91,   107,     4,     4,     4,     5,     4,     5,    47,    76,
      53,    68,    70,    79,    87,    75,    76,    76,    76,    76,
      76,    76,    76,    76,    76,    76,    76,   125,   139,   140,
     139,     4,    67,   131,   132,     4,   131,   125,   146,   146,
      67,    79,   136,    11,    22,    21,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    72,    87,
      88,   133,    65,    66,    70,    79,     3,    67,    29,     4,
      76,    76,    67,   110,    74,    76,    79,    76,    37,    43,
      45,     4,    67,    91,    27,    67,    76,     4,     4,   107,
     125,     6,   125,     4,    53,    69,    74,    69,   139,    24,
      67,    69,    71,    76,   125,   141,     4,    73,   144,   139,
       4,    76,    84,    86,    92,    93,   106,   113,   114,   115,
     117,   118,   119,   120,   124,   127,   128,   130,   130,    89,
     133,   133,   129,   131,   131,   125,     4,     4,   125,   125,
      29,    82,    10,    98,     4,   142,   143,    69,   111,    82,
       4,    82,     4,    44,    67,   142,     4,   108,   142,    82,
       5,     5,    27,    71,   125,   125,    69,   135,   139,    74,
     146,    53,    67,    75,    74,   146,    69,    67,   137,    82,
      76,     7,   129,   129,    71,    69,    76,   125,    14,   125,
      10,    13,    99,    69,    74,    76,    14,    14,   139,    69,
      76,    69,    14,   108,    69,   146,    71,   125,     4,   145,
     125,   146,    73,   139,    12,    14,   122,    12,   123,    73,
      76,    82,    76,    31,    11,   125,    76,    15,    76,     4,
      82,    39,    40,    69,    82,    76,    49,    76,   125,   125,
      69,     4,    69,    76,   124,     9,    76,   124,    82,    14,
      82,    76,    76,    11,   100,    76,    82,    14,    76,    76,
      14,    82,    76,    82,    75,    53,    67,    75,    82,    76,
      76,    82,    76,    14,    27,    14,   100,    76,    76,    84,
      92,    93,    94,    95,    96,    97,   101,   102,   103,   104,
     105,   106,   109,   112,   113,   114,   115,   116,   117,   118,
     119,   120,   121,    14,    39,    47,    14,    14,   125,   125,
     145,   125,    14,    14,    24,    76,    27,   100,    76,    76,
      76,    76,    76,    76,    76,    76,    76,    76,    76,    76,
      35,    76,    76,    48,    47,    69,     9,     9,    76,    76,
      76,    76,    76,    75,    76,    76,   125
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    80,    81,    82,    82,    82,    83,    83,    83,    83,
      83,    83,    83,    83,    83,    83,    83,    83,    83,    83,
      83,    83,    83,    83,    83,    83,    83,    83,    83,    84,
      84,    85,    85,    85,    86,    86,    86,    87,    89,    88,
      90,    90,    91,    91,    91,    91,    92,    93,    93,    93,
      94,    95,    95,    96,    97,    98,    98,    99,    99,   100,
     100,   100,   101,   101,   101,   101,   101,   101,   101,   101,
     101,   101,   101,   101,   101,   101,   101,   101,   101,   101,
     101,   101,   101,   101,   101,   102,   103,   103,   104,   105,
     106,   106,   106,   106,   106,   106,   107,   107,   108,   109,
     109,   110,   110,   111,   111,   112,   113,   113,   113,   114,
     115,   115,   116,   117,   118,   119,   120,   121,   121,   122,
     122,   122,   123,   123,   123,   124,   124,   124,   124,   124,
     124,   124,   124,   124,   124,   124,   125,   126,   126,   127,
     127,   128,   128,   128,   128,   129,   129,   129,   130,   130,
     130,   131,   131,   131,   131,   131,   131,   132,   132,   132,
     133,   133,   133,   133,   133,   133,   133,   133,   133,   133,
     134,   134,   134,   134,   134,   134,   134,   134,   134,   134,
     134,   134,   134,   134,   135,   135,   136,   136,   136,   137,
     137,   138,   138,   139,   139,   140,   140,   141,   141,   142,
     142,   143,   143,   144,   144,   144,   144,   144,   144,   145,
     145,   146,   146
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     2,     2,     2,     2,     1,
       1,     1,     1,     1,     1,     1,     1,     2,     1,     1,
       2,     2,     2,     2,     2,     2,     2,     2,     1,     3,
       4,     1,     4,     3,     1,     1,     1,     2,     0,     4,
       1,     2,     1,     1,     1,     1,     2,     4,     4,     6,
      10,     9,    10,     7,     7,     5,     6,     0,     3,     0,
       2,     2,     2,     2,     2,     1,     1,     1,     1,     1,
       1,     1,     1,     2,     1,     1,     2,     2,     2,     2,
       2,     2,     2,     2,     1,    10,     9,    10,    10,     7,
       2,     2,     2,     2,     4,     4,     1,     4,     1,     9,
       7,     1,     3,     1,     3,     7,     4,     4,     3,     2,
       1,     2,     2,     2,     2,     1,     1,     6,     6,     3,
       3,     6,     0,     3,     6,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     3,     1,
       3,     1,     3,     4,     4,     1,     3,     3,     1,     3,
       3,     1,     2,     2,     2,     4,     5,     1,     4,     3,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     2,     4,     1,     1,     1,     1,     1,
       3,     3,     5,     1,     3,     5,     0,     3,     3,     0,
       3,     2,     3,     0,     1,     1,     3,     1,     4,     0,
       1,     1,     3,     3,     3,     6,     6,     6,     9,     1,
       2,     0,     2
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF

/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)                                \
    do                                                                  \
      if (N)                                                            \
        {                                                               \
          (Current).first_line   = YYRHSLOC (Rhs, 1).first_line;        \
          (Current).first_column = YYRHSLOC (Rhs, 1).first_column;      \
          (Current).last_line    = YYRHSLOC (Rhs, N).last_line;         \
          (Current).last_column  = YYRHSLOC (Rhs, N).last_column;       \
        }                                                               \
      else                                                              \
        {                                                               \
          (Current).first_line   = (Current).last_line   =              \
            YYRHSLOC (Rhs, 0).last_line;                                \
          (Current).first_column = (Current).last_column =              \
            YYRHSLOC (Rhs, 0).last_column;                              \
        }                                                               \
    while (0)
#endif

#define YYRHSLOC(Rhs, K) ((Rhs)[K])


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)


/* YYLOCATION_PRINT -- Print the location on the stream.
   This macro was not mandated originally: define only if we know
   we won't break user code: when these are the locations we know.  */

# ifndef YYLOCATION_PRINT

#  if defined YY_LOCATION_PRINT

   /* Temporary convenience wrapper in case some people defined the
      undocumented and private YY_LOCATION_PRINT macros.  */
#   define YYLOCATION_PRINT(File, Loc)  YY_LOCATION_PRINT(File, *(Loc))

#  elif defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL

/* Print *YYLOCP on YYO.  Private, do not rely on its existence. */

YY_ATTRIBUTE_UNUSED
static int
yy_location_print_ (FILE *yyo, YYLTYPE const * const yylocp)
{
  int res = 0;
  int end_col = 0 != yylocp->last_column ? yylocp->last_column - 1 : 0;
  if (0 <= yylocp->first_line)
    {
      res += YYFPRINTF (yyo, "%d", yylocp->first_line);
      if (0 <= yylocp->first_column)
        res += YYFPRINTF (yyo, ".%d", yylocp->first_column);
    }
  if (0 <= yylocp->last_line)
    {
      if (yylocp->first_line < yylocp->last_line)
        {
          res += YYFPRINTF (yyo, "-%d", yylocp->last_line);
          if (0 <= end_col)
            res += YYFPRINTF (yyo, ".%d", end_col);
        }
      else if (0 <= end_col && yylocp->first_column < end_col)
        res += YYFPRINTF (yyo, "-%d", end_col);
    }
  return res;
}

#   define YYLOCATION_PRINT  yy_location_print_

    /* Temporary convenience wrapper in case some people defined the
       undocumented and private YY_LOCATION_PRINT macros.  */
#   define YY_LOCATION_PRINT(File, Loc)  YYLOCATION_PRINT(File, &(Loc))

#  else

#   define YYLOCATION_PRINT(File, Loc) ((void) 0)
    /* Temporary convenience wrapper in case some people defined the
       undocumented and private YY_LOCATION_PRINT macros.  */
#   define YY_LOCATION_PRINT  YYLOCATION_PRINT

#  endif
# endif /* !defined YYLOCATION_PRINT */


# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value, Location); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  YY_USE (yylocationp);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  YYLOCATION_PRINT (yyo, yylocationp);
  YYFPRINTF (yyo, ": ");
  yy_symbol_value_print (yyo, yykind, yyvaluep, yylocationp);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp, YYLTYPE *yylsp,
                 int yyrule)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)],
                       &(yylsp[(yyi + 1) - (yynrhs)]));
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, yylsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif


/* Context of a parse error.  */
typedef struct
{
  yy_state_t *yyssp;
  yysymbol_kind_t yytoken;
  YYLTYPE *yylloc;
} yypcontext_t;

/* Put in YYARG at most YYARGN of the expected tokens given the
   current YYCTX, and return the number of tokens stored in YYARG.  If
   YYARG is null, return the number of expected tokens (guaranteed to
   be less than YYNTOKENS).  Return YYENOMEM on memory exhaustion.
   Return 0 if there are more than YYARGN expected tokens, yet fill
   YYARG up to YYARGN. */
static int
yypcontext_expected_tokens (const yypcontext_t *yyctx,
                            yysymbol_kind_t yyarg[], int yyargn)
{
  /* Actual size of YYARG. */
  int yycount = 0;
  int yyn = yypact[+*yyctx->yyssp];
  if (!yypact_value_is_default (yyn))
    {
      /* Start YYX at -YYN if negative to avoid negative indexes in
         YYCHECK.  In other words, skip the first -YYN actions for
         this state because they are default actions.  */
      int yyxbegin = yyn < 0 ? -yyn : 0;
      /* Stay within bounds of both yycheck and yytname.  */
      int yychecklim = YYLAST - yyn + 1;
      int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
      int yyx;
      for (yyx = yyxbegin; yyx < yyxend; ++yyx)
        if (yycheck[yyx + yyn] == yyx && yyx != YYSYMBOL_YYerror
            && !yytable_value_is_error (yytable[yyx + yyn]))
          {
            if (!yyarg)
              ++yycount;
            else if (yycount == yyargn)
              return 0;
            else
              yyarg[yycount++] = YY_CAST (yysymbol_kind_t, yyx);
          }
    }
  if (yyarg && yycount == 0 && 0 < yyargn)
    yyarg[0] = YYSYMBOL_YYEMPTY;
  return yycount;
}




#ifndef yystrlen
# if defined __GLIBC__ && defined _STRING_H
#  define yystrlen(S) (YY_CAST (YYPTRDIFF_T, strlen (S)))
# else
/* Return the length of YYSTR.  */
static YYPTRDIFF_T
yystrlen (const char *yystr)
{
  YYPTRDIFF_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
# endif
#endif

#ifndef yystpcpy
# if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#  define yystpcpy stpcpy
# else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
static char *
yystpcpy (char *yydest, const char *yysrc)
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
# endif
#endif

#ifndef yytnamerr
/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
   quotes and backslashes, so that it's suitable for yyerror.  The
   heuristic is that double-quoting is unnecessary unless the string
   contains an apostrophe, a comma, or backslash (other than
   backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
   null, do not copy; instead, return the length of what the result
   would have been.  */
static YYPTRDIFF_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYPTRDIFF_T yyn = 0;
      char const *yyp = yystr;
      for (;;)
        switch (*++yyp)
          {
          case '\'':
          case ',':
            goto do_not_strip_quotes;

          case '\\':
            if (*++yyp != '\\')
              goto do_not_strip_quotes;
            else
              goto append;

          append:
          default:
            if (yyres)
              yyres[yyn] = *yyp;
            yyn++;
            break;

          case '"':
            if (yyres)
              yyres[yyn] = '\0';
            return yyn;
          }
    do_not_strip_quotes: ;
    }

  if (yyres)
    return yystpcpy (yyres, yystr) - yyres;
  else
    return yystrlen (yystr);
}
#endif


static int
yy_syntax_error_arguments (const yypcontext_t *yyctx,
                           yysymbol_kind_t yyarg[], int yyargn)
{
  /* Actual size of YYARG. */
  int yycount = 0;
  /* There are many possibilities here to consider:
     - If this state is a consistent state with a default action, then
       the only way this function was invoked is if the default action
       is an error action.  In that case, don't check for expected
       tokens because there are none.
     - The only way there can be no lookahead present (in yychar) is if
       this state is a consistent state with a default action.  Thus,
       detecting the absence of a lookahead is sufficient to determine
       that there is no unexpected or expected token to report.  In that
       case, just report a simple "syntax error".
     - Don't assume there isn't a lookahead just because this state is a
       consistent state with a default action.  There might have been a
       previous inconsistent state, consistent state with a non-default
       action, or user semantic action that manipulated yychar.
     - Of course, the expected token list depends on states to have
       correct lookahead information, and it depends on the parser not
       to perform extra reductions after fetching a lookahead from the
       scanner and before detecting a syntax error.  Thus, state merging
       (from LALR or IELR) and default reductions corrupt the expected
       token list.  However, the list is correct for canonical LR with
       one exception: it will still contain any token that will not be
       accepted due to an error action in a later state.
  */
  if (yyctx->yytoken != YYSYMBOL_YYEMPTY)
    {
      int yyn;
      if (yyarg)
        yyarg[yycount] = yyctx->yytoken;
      ++yycount;
      yyn = yypcontext_expected_tokens (yyctx,
                                        yyarg ? yyarg + 1 : yyarg, yyargn - 1);
      if (yyn == YYENOMEM)
        return YYENOMEM;
      else
        yycount += yyn;
    }
  return yycount;
}

/* Copy into *YYMSG, which is of size *YYMSG_ALLOC, an error message
   about the unexpected token YYTOKEN for the state stack whose top is
   YYSSP.

   Return 0 if *YYMSG was successfully written.  Return -1 if *YYMSG is
   not large enough to hold the message.  In that case, also set
   *YYMSG_ALLOC to the required number of bytes.  Return YYENOMEM if the
   required number of bytes is too large to store.  */
static int
yysyntax_error (YYPTRDIFF_T *yymsg_alloc, char **yymsg,
                const yypcontext_t *yyctx)
{
  enum { YYARGS_MAX = 5 };
  /* Internationalized format string. */
  const char *yyformat = YY_NULLPTR;
  /* Arguments of yyformat: reported tokens (one for the "unexpected",
     one per "expected"). */
  yysymbol_kind_t yyarg[YYARGS_MAX];
  /* Cumulated lengths of YYARG.  */
  YYPTRDIFF_T yysize = 0;

  /* Actual size of YYARG. */
  int yycount = yy_syntax_error_arguments (yyctx, yyarg, YYARGS_MAX);
  if (yycount == YYENOMEM)
    return YYENOMEM;

  switch (yycount)
    {
#define YYCASE_(N, S)                       \
      case N:                               \
        yyformat = S;                       \
        break
    default: /* Avoid compiler warnings. */
      YYCASE_(0, YY_("syntax error"));
      YYCASE_(1, YY_("syntax error, unexpected %s"));
      YYCASE_(2, YY_("syntax error, unexpected %s, expecting %s"));
      YYCASE_(3, YY_("syntax error, unexpected %s, expecting %s or %s"));
      YYCASE_(4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
      YYCASE_(5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
#undef YYCASE_
    }

  /* Compute error message size.  Don't count the "%s"s, but reserve
     room for the terminator.  */
  yysize = yystrlen (yyformat) - 2 * yycount + 1;
  {
    int yyi;
    for (yyi = 0; yyi < yycount; ++yyi)
      {
        YYPTRDIFF_T yysize1
          = yysize + yytnamerr (YY_NULLPTR, yytname[yyarg[yyi]]);
        if (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM)
          yysize = yysize1;
        else
          return YYENOMEM;
      }
  }

  if (*yymsg_alloc < yysize)
    {
      *yymsg_alloc = 2 * yysize;
      if (! (yysize <= *yymsg_alloc
             && *yymsg_alloc <= YYSTACK_ALLOC_MAXIMUM))
        *yymsg_alloc = YYSTACK_ALLOC_MAXIMUM;
      return -1;
    }

  /* Avoid sprintf, as that infringes on the user's name space.
     Don't have undefined behavior even if the translation
     produced a string with the wrong number of "%s"s.  */
  {
    char *yyp = *yymsg;
    int yyi = 0;
    while ((*yyp = *yyformat) != '\0')
      if (*yyp == '%' && yyformat[1] == 's' && yyi < yycount)
        {
          yyp += yytnamerr (yyp, yytname[yyarg[yyi++]]);
          yyformat += 2;
        }
      else
        {
          ++yyp;
          ++yyformat;
        }
  }
  return 0;
}


/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep, YYLTYPE *yylocationp)
{
  YY_USE (yyvaluep);
  YY_USE (yylocationp);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/* Lookahead token kind.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Location data for the lookahead symbol.  */
YYLTYPE yylloc
# if defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL
  = { 1, 1, 1, 1 }
# endif
;
/* Number of syntax errors so far.  */
int yynerrs;




/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

    /* The location stack: array, bottom, top.  */
    YYLTYPE yylsa[YYINITDEPTH];
    YYLTYPE *yyls = yylsa;
    YYLTYPE *yylsp = yyls;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;
  YYLTYPE yyloc;

  /* The locations where the error started and ended.  */
  YYLTYPE yyerror_range[3];

  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYPTRDIFF_T yymsg_alloc = sizeof yymsgbuf;

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N), yylsp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */

  yylsp[0] = yylloc;
  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;
        YYLTYPE *yyls1 = yyls;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yyls1, yysize * YYSIZEOF (*yylsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
        yyls = yyls1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
        YYSTACK_RELOCATE (yyls_alloc, yyls);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;
      yylsp = yyls + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex ();
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      yyerror_range[1] = yylloc;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END
  *++yylsp = yylloc;

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];

  /* Default location. */
  YYLLOC_DEFAULT (yyloc, (yylsp - yylen), yylen);
  yyerror_range[1] = yyloc;
  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 2: /* program: statement_list  */
#line 527 "src/parser.y"
                     { parsed_program = (yyvsp[0].stmt_list); (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 2576 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 531 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 2582 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 532 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 2588 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 533 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 2594 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 537 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2600 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 538 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2606 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 539 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2612 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 540 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2618 "src/parser.tab.c"
    break;

  case 10: /* statement: for_each_statement  */
#line 541 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2624 "src/parser.tab.c"
    break;

  case 11: /* statement: while_statement  */
#line 542 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2630 "src/parser.tab.c"
    break;

  case 12: /* statement: consider_statement  */
#line 543 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2636 "src/parser.tab.c"
    break;

  case 13: /* statement: function_statement  */
#line 544 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2642 "src/parser.tab.c"
    break;

  case 14: /* statement: modifier_statement  */
#line 545 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2648 "src/parser.tab.c"
    break;

  case 15: /* statement: program_statement  */
#line 546 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2654 "src/parser.tab.c"
    break;

  case 16: /* statement: library_statement  */
#line 547 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2660 "src/parser.tab.c"
    break;

  case 17: /* statement: use_statement NEWLINE  */
#line 548 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2666 "src/parser.tab.c"
    break;

  case 18: /* statement: watch_statement  */
#line 549 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2672 "src/parser.tab.c"
    break;

  case 19: /* statement: without_watchers_statement  */
#line 550 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2678 "src/parser.tab.c"
    break;

  case 20: /* statement: on_error_statement NEWLINE  */
#line 551 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2684 "src/parser.tab.c"
    break;

  case 21: /* statement: error_statement NEWLINE  */
#line 552 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2690 "src/parser.tab.c"
    break;

  case 22: /* statement: return_statement NEWLINE  */
#line 553 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2696 "src/parser.tab.c"
    break;

  case 23: /* statement: label_statement NEWLINE  */
#line 554 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2702 "src/parser.tab.c"
    break;

  case 24: /* statement: goto_statement NEWLINE  */
#line 555 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2708 "src/parser.tab.c"
    break;

  case 25: /* statement: gosub_statement NEWLINE  */
#line 556 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2714 "src/parser.tab.c"
    break;

  case 26: /* statement: break_statement NEWLINE  */
#line 557 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2720 "src/parser.tab.c"
    break;

  case 27: /* statement: continue_statement NEWLINE  */
#line 558 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2726 "src/parser.tab.c"
    break;

  case 28: /* statement: if_statement  */
#line 559 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2732 "src/parser.tab.c"
    break;

  case 29: /* assignment: lvalue OP_EQ expression  */
#line 563 "src/parser.y"
                              { (yyval.stmt) = ast_assign((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr)); }
#line 2738 "src/parser.tab.c"
    break;

  case 30: /* assignment: lvalue modifier OP_EQ expression  */
#line 564 "src/parser.y"
                                       {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            yyerror("modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr));
      }
#line 2750 "src/parser.tab.c"
    break;

  case 31: /* lvalue: variable_name  */
#line 574 "src/parser.y"
                                 { (yyval.expr) = expr_at(ast_ident((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2756 "src/parser.tab.c"
    break;

  case 32: /* lvalue: lvalue LBRACKET expression RBRACKET  */
#line 575 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 2762 "src/parser.tab.c"
    break;

  case 33: /* lvalue: lvalue DOT IDENT  */
#line 576 "src/parser.y"
                                    { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2768 "src/parser.tab.c"
    break;

  case 34: /* variable_name: IDENT  */
#line 580 "src/parser.y"
                         { (yyval.text) = (yyvsp[0].text); }
#line 2774 "src/parser.tab.c"
    break;

  case 35: /* variable_name: END  */
#line 581 "src/parser.y"
                       { (yyval.text) = copy_const("end"); }
#line 2780 "src/parser.tab.c"
    break;

  case 36: /* variable_name: NEXT  */
#line 582 "src/parser.y"
                        { (yyval.text) = copy_const("next"); }
#line 2786 "src/parser.tab.c"
    break;

  case 37: /* modifier: MOD_LPAREN MOD_CONTENT  */
#line 586 "src/parser.y"
                             { (yyval.modifier) = parse_modifier_use((yyvsp[0].text)); }
#line 2792 "src/parser.tab.c"
    break;

  case 38: /* $@1: %empty  */
#line 590 "src/parser.y"
             { lexer_begin_lens_content(active_lexer); }
#line 2798 "src/parser.tab.c"
    break;

  case 39: /* comparison_lens: LBRACE $@1 LENS_CONTENT RBRACE  */
#line 590 "src/parser.y"
                                                                             {
        (yyval.modifier) = parse_modifier_use((yyvsp[-1].text));
      }
#line 2806 "src/parser.tab.c"
    break;

  case 40: /* modifier_name: modifier_word  */
#line 596 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 2812 "src/parser.tab.c"
    break;

  case 41: /* modifier_name: modifier_name modifier_word  */
#line 597 "src/parser.y"
                                  { (yyval.text) = join_words((yyvsp[-1].text), (yyvsp[0].text)); }
#line 2818 "src/parser.tab.c"
    break;

  case 42: /* modifier_word: IDENT  */
#line 601 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 2824 "src/parser.tab.c"
    break;

  case 43: /* modifier_word: TO  */
#line 602 "src/parser.y"
         { (yyval.text) = copy_const("to"); }
#line 2830 "src/parser.tab.c"
    break;

  case 44: /* modifier_word: END  */
#line 603 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 2836 "src/parser.tab.c"
    break;

  case 45: /* modifier_word: NEXT  */
#line 604 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 2842 "src/parser.tab.c"
    break;

  case 46: /* print_statement: PRINT expression  */
#line 608 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 2848 "src/parser.tab.c"
    break;

  case 47: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 612 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 2854 "src/parser.tab.c"
    break;

  case 48: /* call_statement: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 613 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.stmt) = ast_expr_stmt(ast_qualified_call(library, name, (yyvsp[-1].expr_list)));
      }
#line 2865 "src/parser.tab.c"
    break;

  case 49: /* call_statement: ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 619 "src/parser.y"
                                                            {
        size_t length = strlen("error.") + strlen((yyvsp[-3].text));
        char *name = malloc(length + 1);
        if (!name) {
            abort();
        }
        snprintf(name, length + 1, "error.%s", (yyvsp[-3].text));
        free((yyvsp[-3].text));
        (yyval.stmt) = ast_expr_stmt(ast_call(name, (yyvsp[-1].expr_list)));
      }
#line 2880 "src/parser.tab.c"
    break;

  case 50: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 632 "src/parser.y"
                                                                                  {
        if (strcmp((yyvsp[-8].text), "lock") != 0) {
            yyerror("expected lock in with lock block");
            free((yyvsp[-8].text));
            YYERROR;
        }
        free((yyvsp[-8].text));
        (yyval.stmt) = ast_with_lock((yyvsp[-6].expr), (yyvsp[-3].stmt_list));
      }
#line 2894 "src/parser.tab.c"
    break;

  case 51: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list END FOR NEWLINE  */
#line 644 "src/parser.y"
                                                                     {
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 2902 "src/parser.tab.c"
    break;

  case 52: /* for_each_statement: FOR EACH IDENT IN expression NEWLINE statement_list END FOR NEWLINE  */
#line 647 "src/parser.y"
                                                                          {
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 2910 "src/parser.tab.c"
    break;

  case 53: /* while_statement: WHILE expression NEWLINE statement_list END WHILE NEWLINE  */
#line 653 "src/parser.y"
                                                                {
        (yyval.stmt) = ast_while((yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 2918 "src/parser.tab.c"
    break;

  case 54: /* consider_statement: CONSIDER expression NEWLINE consider_branch_list consider_else_opt END_CONSIDER NEWLINE  */
#line 659 "src/parser.y"
                                                                                              {
        (yyval.stmt) = ast_consider((yyvsp[-5].expr), (yyvsp[-3].consider_branch_list), (yyvsp[-2].stmt_list));
      }
#line 2926 "src/parser.tab.c"
    break;

  case 55: /* consider_branch_list: CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 665 "src/parser.y"
                                                                  {
        (yyval.consider_branch_list) = ast_consider_branch_list_append(ast_consider_branch_list_empty(), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 2934 "src/parser.tab.c"
    break;

  case 56: /* consider_branch_list: consider_branch_list CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 668 "src/parser.y"
                                                                                       {
        (yyval.consider_branch_list) = ast_consider_branch_list_append((yyvsp[-5].consider_branch_list), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 2942 "src/parser.tab.c"
    break;

  case 57: /* consider_else_opt: %empty  */
#line 674 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 2948 "src/parser.tab.c"
    break;

  case 58: /* consider_else_opt: CONSIDER_ELSE NEWLINE consider_statement_list  */
#line 675 "src/parser.y"
                                                    { (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 2954 "src/parser.tab.c"
    break;

  case 59: /* consider_statement_list: %empty  */
#line 679 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 2960 "src/parser.tab.c"
    break;

  case 60: /* consider_statement_list: consider_statement_list NEWLINE  */
#line 680 "src/parser.y"
                                      { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 2966 "src/parser.tab.c"
    break;

  case 61: /* consider_statement_list: consider_statement_list consider_body_statement  */
#line 681 "src/parser.y"
                                                      { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 2972 "src/parser.tab.c"
    break;

  case 62: /* consider_body_statement: assignment NEWLINE  */
#line 685 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2978 "src/parser.tab.c"
    break;

  case 63: /* consider_body_statement: print_statement NEWLINE  */
#line 686 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2984 "src/parser.tab.c"
    break;

  case 64: /* consider_body_statement: call_statement NEWLINE  */
#line 687 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2990 "src/parser.tab.c"
    break;

  case 65: /* consider_body_statement: with_lock_statement  */
#line 688 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2996 "src/parser.tab.c"
    break;

  case 66: /* consider_body_statement: for_each_statement  */
#line 689 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3002 "src/parser.tab.c"
    break;

  case 67: /* consider_body_statement: while_statement  */
#line 690 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3008 "src/parser.tab.c"
    break;

  case 68: /* consider_body_statement: consider_statement  */
#line 691 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3014 "src/parser.tab.c"
    break;

  case 69: /* consider_body_statement: function_statement  */
#line 692 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3020 "src/parser.tab.c"
    break;

  case 70: /* consider_body_statement: modifier_statement  */
#line 693 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3026 "src/parser.tab.c"
    break;

  case 71: /* consider_body_statement: program_statement  */
#line 694 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3032 "src/parser.tab.c"
    break;

  case 72: /* consider_body_statement: library_statement  */
#line 695 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3038 "src/parser.tab.c"
    break;

  case 73: /* consider_body_statement: use_statement NEWLINE  */
#line 696 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3044 "src/parser.tab.c"
    break;

  case 74: /* consider_body_statement: watch_statement  */
#line 697 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3050 "src/parser.tab.c"
    break;

  case 75: /* consider_body_statement: without_watchers_statement  */
#line 698 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3056 "src/parser.tab.c"
    break;

  case 76: /* consider_body_statement: on_error_statement NEWLINE  */
#line 699 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3062 "src/parser.tab.c"
    break;

  case 77: /* consider_body_statement: error_statement NEWLINE  */
#line 700 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3068 "src/parser.tab.c"
    break;

  case 78: /* consider_body_statement: return_statement NEWLINE  */
#line 701 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3074 "src/parser.tab.c"
    break;

  case 79: /* consider_body_statement: label_statement NEWLINE  */
#line 702 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3080 "src/parser.tab.c"
    break;

  case 80: /* consider_body_statement: goto_statement NEWLINE  */
#line 703 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3086 "src/parser.tab.c"
    break;

  case 81: /* consider_body_statement: gosub_statement NEWLINE  */
#line 704 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3092 "src/parser.tab.c"
    break;

  case 82: /* consider_body_statement: break_statement NEWLINE  */
#line 705 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3098 "src/parser.tab.c"
    break;

  case 83: /* consider_body_statement: continue_statement NEWLINE  */
#line 706 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3104 "src/parser.tab.c"
    break;

  case 84: /* consider_body_statement: if_statement  */
#line 707 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3110 "src/parser.tab.c"
    break;

  case 85: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 711 "src/parser.y"
                                                                                                  {
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 3118 "src/parser.tab.c"
    break;

  case 86: /* modifier_statement: MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 717 "src/parser.y"
                                                                                                   {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 0, (yyvsp[-3].stmt_list));
      }
#line 3126 "src/parser.tab.c"
    break;

  case 87: /* modifier_statement: EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 720 "src/parser.y"
                                                                                                          {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 1, (yyvsp[-3].stmt_list));
      }
#line 3134 "src/parser.tab.c"
    break;

  case 88: /* program_statement: PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE  */
#line 726 "src/parser.y"
                                                                                                {
        (yyval.stmt) = ast_program((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 3142 "src/parser.tab.c"
    break;

  case 89: /* library_statement: LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE  */
#line 732 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_library((yyvsp[-5].text), (yyvsp[-3].stmt_list));
      }
#line 3150 "src/parser.tab.c"
    break;

  case 90: /* use_statement: USE IDENT  */
#line 738 "src/parser.y"
                { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 3156 "src/parser.tab.c"
    break;

  case 91: /* use_statement: LOAD IDENT  */
#line 739 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 3162 "src/parser.tab.c"
    break;

  case 92: /* use_statement: USE STRING  */
#line 740 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 3168 "src/parser.tab.c"
    break;

  case 93: /* use_statement: LOAD STRING  */
#line 741 "src/parser.y"
                  { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 3174 "src/parser.tab.c"
    break;

  case 94: /* use_statement: USE IDENT IDENT STRING  */
#line 742 "src/parser.y"
                             {
        if (strcmp((yyvsp[-1].text), "from") != 0) {
            yyerror("expected from in use statement");
            free((yyvsp[-2].text));
            free((yyvsp[-1].text));
            free((yyvsp[0].text));
            YYERROR;
        }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_use((yyvsp[-2].text), (yyvsp[0].text));
      }
#line 3190 "src/parser.tab.c"
    break;

  case 95: /* use_statement: LOAD IDENT IDENT STRING  */
#line 753 "src/parser.y"
                              {
        if (strcmp((yyvsp[-1].text), "from") != 0) {
            yyerror("expected from in load statement");
            free((yyvsp[-2].text));
            free((yyvsp[-1].text));
            free((yyvsp[0].text));
            YYERROR;
        }
        free((yyvsp[-1].text));
        (yyval.stmt) = ast_use((yyvsp[-2].text), (yyvsp[0].text));
      }
#line 3206 "src/parser.tab.c"
    break;

  case 96: /* modifier_signature: modifier_name  */
#line 767 "src/parser.y"
                    { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[0].text), ast_name_list_empty()); }
#line 3212 "src/parser.tab.c"
    break;

  case 97: /* modifier_signature: modifier_name LPAREN parameter_list_opt RPAREN  */
#line 768 "src/parser.y"
                                                     { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[-3].text), (yyvsp[-1].name_list)); }
#line 3218 "src/parser.tab.c"
    break;

  case 98: /* modifier_context: IDENT  */
#line 772 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 3224 "src/parser.tab.c"
    break;

  case 99: /* watch_statement: WATCH LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 776 "src/parser.y"
                                                                                     {
        (yyval.stmt) = ast_watch((yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 3232 "src/parser.tab.c"
    break;

  case 100: /* watch_statement: WATCH watch_target_list NEWLINE statement_list END WATCH NEWLINE  */
#line 779 "src/parser.y"
                                                                       {
        (yyval.stmt) = ast_watch((yyvsp[-5].name_list), (yyvsp[-3].stmt_list));
      }
#line 3240 "src/parser.tab.c"
    break;

  case 101: /* watch_target_list: watch_target_path  */
#line 785 "src/parser.y"
                        { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 3246 "src/parser.tab.c"
    break;

  case 102: /* watch_target_list: watch_target_list COMMA watch_target_path  */
#line 786 "src/parser.y"
                                                { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 3252 "src/parser.tab.c"
    break;

  case 103: /* watch_target_path: variable_name  */
#line 790 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 3258 "src/parser.tab.c"
    break;

  case 104: /* watch_target_path: watch_target_path DOT IDENT  */
#line 791 "src/parser.y"
                                  { (yyval.text) = join_watch_path((yyvsp[-2].text), (yyvsp[0].text)); }
#line 3264 "src/parser.tab.c"
    break;

  case 105: /* without_watchers_statement: WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE  */
#line 795 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_without_watchers((yyvsp[-3].stmt_list));
      }
#line 3272 "src/parser.tab.c"
    break;

  case 106: /* on_error_statement: ON ERROR_VALUE GOTO IDENT  */
#line 801 "src/parser.y"
                                { (yyval.stmt) = ast_on_error_goto((yyvsp[0].text)); }
#line 3278 "src/parser.tab.c"
    break;

  case 107: /* on_error_statement: ON ERROR_VALUE RESUME NEXT  */
#line 802 "src/parser.y"
                                 { (yyval.stmt) = ast_on_error_resume_next(); }
#line 3284 "src/parser.tab.c"
    break;

  case 108: /* on_error_statement: ON ERROR_VALUE STOP  */
#line 803 "src/parser.y"
                          { (yyval.stmt) = ast_on_error_stop(); }
#line 3290 "src/parser.tab.c"
    break;

  case 109: /* error_statement: ERROR_VALUE expression  */
#line 807 "src/parser.y"
                             { (yyval.stmt) = ast_error((yyvsp[0].expr)); }
#line 3296 "src/parser.tab.c"
    break;

  case 110: /* return_statement: RETURN  */
#line 811 "src/parser.y"
             { (yyval.stmt) = ast_return(NULL); }
#line 3302 "src/parser.tab.c"
    break;

  case 111: /* return_statement: RETURN expression  */
#line 812 "src/parser.y"
                        { (yyval.stmt) = ast_return((yyvsp[0].expr)); }
#line 3308 "src/parser.tab.c"
    break;

  case 112: /* label_statement: variable_name COLON  */
#line 816 "src/parser.y"
                          { (yyval.stmt) = ast_label((yyvsp[-1].text)); }
#line 3314 "src/parser.tab.c"
    break;

  case 113: /* goto_statement: GOTO IDENT  */
#line 820 "src/parser.y"
                 { (yyval.stmt) = ast_goto((yyvsp[0].text)); }
#line 3320 "src/parser.tab.c"
    break;

  case 114: /* gosub_statement: GOSUB IDENT  */
#line 824 "src/parser.y"
                  { (yyval.stmt) = ast_gosub((yyvsp[0].text)); }
#line 3326 "src/parser.tab.c"
    break;

  case 115: /* break_statement: BREAK  */
#line 828 "src/parser.y"
            { (yyval.stmt) = ast_break(); }
#line 3332 "src/parser.tab.c"
    break;

  case 116: /* continue_statement: CONTINUE  */
#line 832 "src/parser.y"
               { (yyval.stmt) = ast_continue(); }
#line 3338 "src/parser.tab.c"
    break;

  case 117: /* if_statement: IF expression THEN NEWLINE statement_list if_block_tail  */
#line 836 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 3347 "src/parser.tab.c"
    break;

  case 118: /* if_statement: IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 840 "src/parser.y"
                                                                 {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 3356 "src/parser.tab.c"
    break;

  case 119: /* if_block_tail: END IF NEWLINE  */
#line 847 "src/parser.y"
                     {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 3364 "src/parser.tab.c"
    break;

  case 120: /* if_block_tail: ELSE inline_statement NEWLINE  */
#line 850 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 3372 "src/parser.tab.c"
    break;

  case 121: /* if_block_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 853 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 3380 "src/parser.tab.c"
    break;

  case 122: /* if_inline_tail: %empty  */
#line 859 "src/parser.y"
                                   {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 3388 "src/parser.tab.c"
    break;

  case 123: /* if_inline_tail: ELSE inline_statement NEWLINE  */
#line 862 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 3396 "src/parser.tab.c"
    break;

  case 124: /* if_inline_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 865 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 3404 "src/parser.tab.c"
    break;

  case 125: /* inline_statement: assignment  */
#line 871 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3410 "src/parser.tab.c"
    break;

  case 126: /* inline_statement: print_statement  */
#line 872 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3416 "src/parser.tab.c"
    break;

  case 127: /* inline_statement: call_statement  */
#line 873 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3422 "src/parser.tab.c"
    break;

  case 128: /* inline_statement: use_statement  */
#line 874 "src/parser.y"
                    { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3428 "src/parser.tab.c"
    break;

  case 129: /* inline_statement: on_error_statement  */
#line 875 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3434 "src/parser.tab.c"
    break;

  case 130: /* inline_statement: error_statement  */
#line 876 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3440 "src/parser.tab.c"
    break;

  case 131: /* inline_statement: return_statement  */
#line 877 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3446 "src/parser.tab.c"
    break;

  case 132: /* inline_statement: goto_statement  */
#line 878 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3452 "src/parser.tab.c"
    break;

  case 133: /* inline_statement: gosub_statement  */
#line 879 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3458 "src/parser.tab.c"
    break;

  case 134: /* inline_statement: break_statement  */
#line 880 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3464 "src/parser.tab.c"
    break;

  case 135: /* inline_statement: continue_statement  */
#line 881 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3470 "src/parser.tab.c"
    break;

  case 136: /* expression: or_expression  */
#line 885 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 3476 "src/parser.tab.c"
    break;

  case 137: /* or_expression: and_expression  */
#line 889 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 3482 "src/parser.tab.c"
    break;

  case 138: /* or_expression: or_expression OR and_expression  */
#line 890 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_binary(copy_const("or"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3488 "src/parser.tab.c"
    break;

  case 139: /* and_expression: comparison_expression  */
#line 894 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 3494 "src/parser.tab.c"
    break;

  case 140: /* and_expression: and_expression AND comparison_expression  */
#line 895 "src/parser.y"
                                               { (yyval.expr) = expr_at(ast_binary(copy_const("and"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3500 "src/parser.tab.c"
    break;

  case 141: /* comparison_expression: additive_expression  */
#line 899 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 3506 "src/parser.tab.c"
    break;

  case 142: /* comparison_expression: additive_expression comparison_operator additive_expression  */
#line 900 "src/parser.y"
                                                                  { (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3512 "src/parser.tab.c"
    break;

  case 143: /* comparison_expression: additive_expression comparison_lens comparison_operator additive_expression  */
#line 901 "src/parser.y"
                                                                                  {
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 3520 "src/parser.tab.c"
    break;

  case 144: /* comparison_expression: additive_expression modifier comparison_operator additive_expression  */
#line 904 "src/parser.y"
                                                                           {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            yyerror("modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 3532 "src/parser.tab.c"
    break;

  case 145: /* additive_expression: multiplicative_expression  */
#line 914 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 3538 "src/parser.tab.c"
    break;

  case 146: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 915 "src/parser.y"
                                                         { (yyval.expr) = expr_at(ast_binary(copy_const("+"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3544 "src/parser.tab.c"
    break;

  case 147: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 916 "src/parser.y"
                                                          { (yyval.expr) = expr_at(ast_binary(copy_const("-"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3550 "src/parser.tab.c"
    break;

  case 148: /* multiplicative_expression: unary_expression  */
#line 920 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 3556 "src/parser.tab.c"
    break;

  case 149: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 921 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("*"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3562 "src/parser.tab.c"
    break;

  case 150: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 922 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_binary(copy_const("/"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3568 "src/parser.tab.c"
    break;

  case 151: /* unary_expression: postfix_expression  */
#line 926 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 3574 "src/parser.tab.c"
    break;

  case 152: /* unary_expression: NOT unary_expression  */
#line 927 "src/parser.y"
                           { (yyval.expr) = expr_at(ast_unary(copy_const("not"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3580 "src/parser.tab.c"
    break;

  case 153: /* unary_expression: MINUS unary_expression  */
#line 928 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_unary(copy_const("-"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3586 "src/parser.tab.c"
    break;

  case 154: /* unary_expression: NEW postfix_expression  */
#line 929 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_new((yyvsp[0].expr), NULL), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3592 "src/parser.tab.c"
    break;

  case 155: /* unary_expression: NEW postfix_expression WITH record_literal  */
#line 930 "src/parser.y"
                                                 { (yyval.expr) = expr_at(ast_new((yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-3]).first_line, (yylsp[-3]).first_column); }
#line 3598 "src/parser.tab.c"
    break;

  case 156: /* unary_expression: SPAWN IDENT LPAREN argument_list_opt RPAREN  */
#line 931 "src/parser.y"
                                                  { (yyval.expr) = expr_at(ast_spawn((yyvsp[-3].text), (yyvsp[-1].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 3604 "src/parser.tab.c"
    break;

  case 157: /* postfix_expression: primary  */
#line 935 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 3610 "src/parser.tab.c"
    break;

  case 158: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 936 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 3616 "src/parser.tab.c"
    break;

  case 159: /* postfix_expression: postfix_expression DOT IDENT  */
#line 937 "src/parser.y"
                                   { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3622 "src/parser.tab.c"
    break;

  case 160: /* comparison_operator: OP_EQ  */
#line 941 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 3628 "src/parser.tab.c"
    break;

  case 161: /* comparison_operator: OP_NE  */
#line 942 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 3634 "src/parser.tab.c"
    break;

  case 162: /* comparison_operator: OP_GT  */
#line 943 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 3640 "src/parser.tab.c"
    break;

  case 163: /* comparison_operator: OP_LT  */
#line 944 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 3646 "src/parser.tab.c"
    break;

  case 164: /* comparison_operator: OP_GE  */
#line 945 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 3652 "src/parser.tab.c"
    break;

  case 165: /* comparison_operator: OP_LE  */
#line 946 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 3658 "src/parser.tab.c"
    break;

  case 166: /* comparison_operator: OP_NGT  */
#line 947 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 3664 "src/parser.tab.c"
    break;

  case 167: /* comparison_operator: OP_NLT  */
#line 948 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 3670 "src/parser.tab.c"
    break;

  case 168: /* comparison_operator: OP_NGE  */
#line 949 "src/parser.y"
             { (yyval.text) = copy_const("!>="); }
#line 3676 "src/parser.tab.c"
    break;

  case 169: /* comparison_operator: OP_NLE  */
#line 950 "src/parser.y"
             { (yyval.text) = copy_const("!<="); }
#line 3682 "src/parser.tab.c"
    break;

  case 170: /* primary: NUMBER  */
#line 954 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].number)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3688 "src/parser.tab.c"
    break;

  case 171: /* primary: duration_terms  */
#line 955 "src/parser.y"
                     { (yyval.expr) = expr_at(ast_duration((yyvsp[0].duration)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3694 "src/parser.tab.c"
    break;

  case 172: /* primary: STRING  */
#line 956 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3700 "src/parser.tab.c"
    break;

  case 173: /* primary: variable_name ident_suffix  */
#line 957 "src/parser.y"
                                 {
        if ((yyvsp[0].ident_suffix).kind == IDENT_SUFFIX_CALL) {
            (yyval.expr) = expr_at(ast_call((yyvsp[-1].text), (yyvsp[0].ident_suffix).args), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
        } else if ((yyvsp[0].ident_suffix).kind == IDENT_SUFFIX_FIELD) {
            (yyval.expr) = expr_at(ast_field(expr_at(ast_ident((yyvsp[-1].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column), (yyvsp[0].ident_suffix).name), (yylsp[0]).first_line, (yylsp[0]).first_column);
        } else if ((yyvsp[0].ident_suffix).kind == IDENT_SUFFIX_QUALIFIED_CALL) {
            (yyval.expr) = expr_at(ast_qualified_call((yyvsp[-1].text), (yyvsp[0].ident_suffix).name, (yyvsp[0].ident_suffix).args), (yylsp[0]).first_line, (yylsp[0]).first_column);
        } else {
            (yyval.expr) = expr_at(ast_ident((yyvsp[-1].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
        }
      }
#line 3716 "src/parser.tab.c"
    break;

  case 174: /* primary: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 968 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.expr) = expr_at(ast_qualified_call(library, name, (yyvsp[-1].expr_list)), (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 3727 "src/parser.tab.c"
    break;

  case 175: /* primary: ERROR_VALUE  */
#line 974 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_ident(copy_const("error")), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3733 "src/parser.tab.c"
    break;

  case 176: /* primary: TRUE  */
#line 975 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3739 "src/parser.tab.c"
    break;

  case 177: /* primary: FALSE  */
#line 976 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3745 "src/parser.tab.c"
    break;

  case 178: /* primary: NOTHING  */
#line 977 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3751 "src/parser.tab.c"
    break;

  case 179: /* primary: UNKNOWN_VALUE  */
#line 978 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3757 "src/parser.tab.c"
    break;

  case 180: /* primary: LPAREN expression RPAREN  */
#line 979 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); }
#line 3763 "src/parser.tab.c"
    break;

  case 181: /* primary: LBRACKET optional_newlines RBRACKET  */
#line 980 "src/parser.y"
                                          { (yyval.expr) = expr_at(ast_array(ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 3769 "src/parser.tab.c"
    break;

  case 182: /* primary: LBRACKET optional_newlines array_argument_list optional_newlines RBRACKET  */
#line 981 "src/parser.y"
                                                                                { (yyval.expr) = expr_at(ast_array((yyvsp[-2].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 3775 "src/parser.tab.c"
    break;

  case 183: /* primary: record_literal  */
#line 982 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 3781 "src/parser.tab.c"
    break;

  case 184: /* record_literal: LBRACE optional_newlines RBRACE  */
#line 986 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_record(ast_record_field_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 3787 "src/parser.tab.c"
    break;

  case 185: /* record_literal: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 987 "src/parser.y"
                                                                          { (yyval.expr) = expr_at(ast_record((yyvsp[-2].record_field_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 3793 "src/parser.tab.c"
    break;

  case 186: /* ident_suffix: %empty  */
#line 991 "src/parser.y"
                          {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_NONE;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 3803 "src/parser.tab.c"
    break;

  case 187: /* ident_suffix: LPAREN argument_list_opt RPAREN  */
#line 996 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 3813 "src/parser.tab.c"
    break;

  case 188: /* ident_suffix: DOT IDENT ident_dot_suffix  */
#line 1001 "src/parser.y"
                                 {
        (yyval.ident_suffix) = (yyvsp[0].ident_suffix);
        (yyval.ident_suffix).name = (yyvsp[-1].text);
      }
#line 3822 "src/parser.tab.c"
    break;

  case 189: /* ident_dot_suffix: %empty  */
#line 1008 "src/parser.y"
             {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_FIELD;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 3832 "src/parser.tab.c"
    break;

  case 190: /* ident_dot_suffix: LPAREN argument_list_opt RPAREN  */
#line 1013 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_QUALIFIED_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 3842 "src/parser.tab.c"
    break;

  case 191: /* duration_terms: NUMBER IDENT  */
#line 1021 "src/parser.y"
                   {
        AstDuration duration = {0};
        (yyval.duration) = duration_add_unit(duration, (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 3851 "src/parser.tab.c"
    break;

  case 192: /* duration_terms: duration_terms NUMBER IDENT  */
#line 1025 "src/parser.y"
                                  {
        (yyval.duration) = duration_add_unit((yyvsp[-2].duration), (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 3859 "src/parser.tab.c"
    break;

  case 193: /* argument_list_opt: %empty  */
#line 1031 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 3865 "src/parser.tab.c"
    break;

  case 194: /* argument_list_opt: argument_list  */
#line 1032 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 3871 "src/parser.tab.c"
    break;

  case 195: /* argument_list: expression  */
#line 1036 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 3877 "src/parser.tab.c"
    break;

  case 196: /* argument_list: argument_list COMMA expression  */
#line 1037 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 3883 "src/parser.tab.c"
    break;

  case 197: /* array_argument_list: expression  */
#line 1041 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 3889 "src/parser.tab.c"
    break;

  case 198: /* array_argument_list: array_argument_list COMMA optional_newlines expression  */
#line 1042 "src/parser.y"
                                                             { (yyval.expr_list) = ast_expr_list_append((yyvsp[-3].expr_list), (yyvsp[0].expr)); }
#line 3895 "src/parser.tab.c"
    break;

  case 199: /* parameter_list_opt: %empty  */
#line 1046 "src/parser.y"
             { (yyval.name_list) = ast_name_list_empty(); }
#line 3901 "src/parser.tab.c"
    break;

  case 200: /* parameter_list_opt: parameter_list  */
#line 1047 "src/parser.y"
                     { (yyval.name_list) = (yyvsp[0].name_list); }
#line 3907 "src/parser.tab.c"
    break;

  case 201: /* parameter_list: IDENT  */
#line 1051 "src/parser.y"
            { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 3913 "src/parser.tab.c"
    break;

  case 202: /* parameter_list: parameter_list COMMA IDENT  */
#line 1052 "src/parser.y"
                                 { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 3919 "src/parser.tab.c"
    break;

  case 203: /* record_field_list: IDENT OP_EQ expression  */
#line 1056 "src/parser.y"
                             { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 3925 "src/parser.tab.c"
    break;

  case 204: /* record_field_list: IDENT COLON expression  */
#line 1057 "src/parser.y"
                             { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 3931 "src/parser.tab.c"
    break;

  case 205: /* record_field_list: IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1058 "src/parser.y"
                                                        { (yyval.record_field_list) = ast_record_field_list_append_policy(ast_record_field_list_empty(), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 3937 "src/parser.tab.c"
    break;

  case 206: /* record_field_list: record_field_list COMMA optional_newlines IDENT OP_EQ expression  */
#line 1059 "src/parser.y"
                                                                       { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 3943 "src/parser.tab.c"
    break;

  case 207: /* record_field_list: record_field_list COMMA optional_newlines IDENT COLON expression  */
#line 1060 "src/parser.y"
                                                                       { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 3949 "src/parser.tab.c"
    break;

  case 208: /* record_field_list: record_field_list COMMA optional_newlines IDENT LPAREN field_policy RPAREN COLON expression  */
#line 1061 "src/parser.y"
                                                                                                  { (yyval.record_field_list) = ast_record_field_list_append_policy((yyvsp[-8].record_field_list), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 3955 "src/parser.tab.c"
    break;

  case 209: /* field_policy: IDENT  */
#line 1069 "src/parser.y"
            {
        FieldPolicySpec spec;
        spec.reset_expr = NULL;
        if (strcmp((yyvsp[0].text), "copy") == 0) {
            spec.policy = AST_FIELD_POLICY_COPY;
        } else if (strcmp((yyvsp[0].text), "link") == 0) {
            spec.policy = AST_FIELD_POLICY_LINK;
        } else if (strcmp((yyvsp[0].text), "exclude") == 0) {
            spec.policy = AST_FIELD_POLICY_EXCLUDE;
        } else if (strcmp((yyvsp[0].text), "reset") == 0) {
            free((yyvsp[0].text));
            yyerror("reset policy requires a value, e.g. (reset 0)");
            YYERROR;
        } else {
            yyerror("unknown field policy (expected copy, link, reset, or exclude)");
            free((yyvsp[0].text));
            YYERROR;
        }
        free((yyvsp[0].text));
        (yyval.field_policy) = spec;
      }
#line 3981 "src/parser.tab.c"
    break;

  case 210: /* field_policy: IDENT expression  */
#line 1090 "src/parser.y"
                       {
        FieldPolicySpec spec;
        if (strcmp((yyvsp[-1].text), "reset") == 0) {
            spec.policy = AST_FIELD_POLICY_RESET;
            spec.reset_expr = (yyvsp[0].expr);
        } else {
            free((yyvsp[-1].text));
            yyerror("only the reset policy takes a value");
            YYERROR;
        }
        free((yyvsp[-1].text));
        (yyval.field_policy) = spec;
      }
#line 3999 "src/parser.tab.c"
    break;


#line 4003 "src/parser.tab.c"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;
  *++yylsp = yyloc;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      {
        yypcontext_t yyctx
          = {yyssp, yytoken, &yylloc};
        char const *yymsgp = YY_("syntax error");
        int yysyntax_error_status;
        yysyntax_error_status = yysyntax_error (&yymsg_alloc, &yymsg, &yyctx);
        if (yysyntax_error_status == 0)
          yymsgp = yymsg;
        else if (yysyntax_error_status == -1)
          {
            if (yymsg != yymsgbuf)
              YYSTACK_FREE (yymsg);
            yymsg = YY_CAST (char *,
                             YYSTACK_ALLOC (YY_CAST (YYSIZE_T, yymsg_alloc)));
            if (yymsg)
              {
                yysyntax_error_status
                  = yysyntax_error (&yymsg_alloc, &yymsg, &yyctx);
                yymsgp = yymsg;
              }
            else
              {
                yymsg = yymsgbuf;
                yymsg_alloc = sizeof yymsgbuf;
                yysyntax_error_status = YYENOMEM;
              }
          }
        yyerror (yymsgp);
        if (yysyntax_error_status == YYENOMEM)
          YYNOMEM;
      }
    }

  yyerror_range[1] = yylloc;
  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval, &yylloc);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;

      yyerror_range[1] = *yylsp;
      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp, yylsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  yyerror_range[2] = yylloc;
  ++yylsp;
  YYLLOC_DEFAULT (*yylsp, yyerror_range, 2);

  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval, &yylloc);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp, yylsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
  return yyresult;
}

#line 1110 "src/parser.y"


int parse_source(const char *source, AstStmtList *out_program) {
    Lexer lexer;
    lexer_init(&lexer, source);
    active_lexer = &lexer;
    lexer_error_reported = 0;
    parsed_program = ast_stmt_list_empty();

    int result = yyparse();
    active_lexer = NULL;
    if (result != 0) {
        return result;
    }

    *out_program = parsed_program;
    return 0;
}

void parse_set_source_path(const char *path) {
    active_parse_path = path;
}

static int yylex(void) {
    Token token = lexer_next(active_lexer);
    yylloc.first_line = token.line;
    yylloc.first_column = token.column;
    yylloc.last_line = token.line;
    yylloc.last_column = token.column + token.length;

    switch (token.type) {
    case TOKEN_EOF: return 0;
    case TOKEN_IDENT:
        yylval.text = copy_text(token.start, token.length);
        return IDENT;
    case TOKEN_QUALIFIED_IDENT:
        yylval.text = copy_text(token.start, token.length);
        return QUALIFIED_IDENT;
    case TOKEN_NUMBER:
        yylval.number = strtod(token.start, NULL);
        return NUMBER;
    case TOKEN_STRING:
    {
        int ok = 0;
        yylval.text = copy_string_literal(token.start, token.length, token.line, token.column, &ok);
        if (!ok) {
            lexer_error_reported = 1;
            return 0;
        }
        return STRING;
    }
    case TOKEN_MOD_CONTENT:
        yylval.text = copy_text(token.start, token.length);
        return MOD_CONTENT;
    case TOKEN_LENS_CONTENT:
        yylval.text = copy_text(token.start, token.length);
        return LENS_CONTENT;
    case TOKEN_IF: return IF;
    case TOKEN_CONSIDER_IF: return CONSIDER_IF;
    case TOKEN_THEN: return THEN;
    case TOKEN_ELSE: return ELSE;
    case TOKEN_CONSIDER_ELSE: return CONSIDER_ELSE;
    case TOKEN_END: return END;
    case TOKEN_END_CONSIDER: return END_CONSIDER;
    case TOKEN_PRINT: return PRINT;
    case TOKEN_TRUE: return TRUE;
    case TOKEN_FALSE: return FALSE;
    case TOKEN_NOTHING: return NOTHING;
    case TOKEN_UNKNOWN: return UNKNOWN_VALUE;
    case TOKEN_AND: return AND;
    case TOKEN_OR: return OR;
    case TOKEN_NOT: return NOT;
    case TOKEN_WITH: return WITH;
    case TOKEN_NEW: return NEW;
    case TOKEN_SPAWN: return SPAWN;
    case TOKEN_FOR: return FOR;
    case TOKEN_TO: return TO;
    case TOKEN_IN: return IN;
    case TOKEN_EACH: return EACH;
    case TOKEN_WHILE: return WHILE;
    case TOKEN_CONSIDER: return CONSIDER;
    case TOKEN_BREAK: return BREAK;
    case TOKEN_CONTINUE: return CONTINUE;
    case TOKEN_FUNCTION: return FUNCTION;
    case TOKEN_RETURN: return RETURN;
    case TOKEN_GOTO: return GOTO;
    case TOKEN_GOSUB: return GOSUB;
    case TOKEN_WATCH: return WATCH;
    case TOKEN_WITHOUT: return WITHOUT;
    case TOKEN_WATCHERS: return WATCHERS;
    case TOKEN_ON: return ON;
    case TOKEN_RESUME: return RESUME;
    case TOKEN_NEXT: return NEXT;
    case TOKEN_STOP: return STOP;
    case TOKEN_ERROR_VALUE: return ERROR_VALUE;
    case TOKEN_MODIFIER: return MODIFIER;
    case TOKEN_PROGRAM: return PROGRAM;
    case TOKEN_LIBRARY: return LIBRARY;
    case TOKEN_LOAD: return LOAD;
    case TOKEN_USE: return USE;
    case TOKEN_EXPORT: return EXPORT;
    case TOKEN_OP_EQ: return OP_EQ;
    case TOKEN_OP_NE: return OP_NE;
    case TOKEN_OP_GT: return OP_GT;
    case TOKEN_OP_LT: return OP_LT;
    case TOKEN_OP_GE: return OP_GE;
    case TOKEN_OP_LE: return OP_LE;
    case TOKEN_OP_NGT: return OP_NGT;
    case TOKEN_OP_NLT: return OP_NLT;
    case TOKEN_OP_NGE: return OP_NGE;
    case TOKEN_OP_NLE: return OP_NLE;
    case TOKEN_PLUS: return PLUS;
    case TOKEN_MINUS: return MINUS;
    case TOKEN_STAR: return STAR;
    case TOKEN_SLASH: return SLASH;
    case TOKEN_LPAREN:
        if (modifier_lparen_ahead(token.start)) {
            lexer_begin_modifier_content(active_lexer);
            return MOD_LPAREN;
        }
        return LPAREN;
    case TOKEN_RPAREN: return RPAREN;
    case TOKEN_LBRACKET: return LBRACKET;
    case TOKEN_RBRACKET: return RBRACKET;
    case TOKEN_COMMA: return COMMA;
    case TOKEN_LBRACE: return LBRACE;
    case TOKEN_RBRACE: return RBRACE;
    case TOKEN_DOT: return DOT;
    case TOKEN_COLON: return COLON;
    case TOKEN_NEWLINE: return NEWLINE;
    case TOKEN_ERROR:
        if (active_lexer->error_message[0]) {
            report_parse_issue("runtime error", token.line, token.column, active_lexer->error_message);
        } else {
            report_parse_issue("lexer error", token.line, token.column, "unexpected token");
        }
        lexer_error_reported = 1;
        return 0;
    default:
        fprintf(stderr, "unexpected token %s at %d:%d\n",
                token_type_name(token.type), token.line, token.column);
        return 0;
    }
}

static void yyerror(const char *message) {
    if (lexer_error_reported) {
        return;
    }
    int line = yylloc.first_line;
    int column = yylloc.first_column;
    if (line <= 0 && active_lexer) {
        line = active_lexer->line;
        column = active_lexer->column;
    }
    if (line <= 0) {
        line = 1;
    }
    if (column <= 0) {
        column = 1;
    }
    report_parse_issue("parse error", line, column, message);
}
