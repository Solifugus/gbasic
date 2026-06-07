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

#line 458 "src/parser.tab.c"

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
  YYSYMBOL_QUALIFIED_IDENT = 7,            /* QUALIFIED_IDENT  */
  YYSYMBOL_IF = 8,                         /* IF  */
  YYSYMBOL_CONSIDER_IF = 9,                /* CONSIDER_IF  */
  YYSYMBOL_THEN = 10,                      /* THEN  */
  YYSYMBOL_ELSE = 11,                      /* ELSE  */
  YYSYMBOL_CONSIDER_ELSE = 12,             /* CONSIDER_ELSE  */
  YYSYMBOL_END = 13,                       /* END  */
  YYSYMBOL_END_CONSIDER = 14,              /* END_CONSIDER  */
  YYSYMBOL_PRINT = 15,                     /* PRINT  */
  YYSYMBOL_TRUE = 16,                      /* TRUE  */
  YYSYMBOL_FALSE = 17,                     /* FALSE  */
  YYSYMBOL_NOTHING = 18,                   /* NOTHING  */
  YYSYMBOL_UNKNOWN_VALUE = 19,             /* UNKNOWN_VALUE  */
  YYSYMBOL_AND = 20,                       /* AND  */
  YYSYMBOL_OR = 21,                        /* OR  */
  YYSYMBOL_NOT = 22,                       /* NOT  */
  YYSYMBOL_WITH = 23,                      /* WITH  */
  YYSYMBOL_FOR = 24,                       /* FOR  */
  YYSYMBOL_TO = 25,                        /* TO  */
  YYSYMBOL_IN = 26,                        /* IN  */
  YYSYMBOL_EACH = 27,                      /* EACH  */
  YYSYMBOL_WHILE = 28,                     /* WHILE  */
  YYSYMBOL_CONSIDER = 29,                  /* CONSIDER  */
  YYSYMBOL_BREAK = 30,                     /* BREAK  */
  YYSYMBOL_CONTINUE = 31,                  /* CONTINUE  */
  YYSYMBOL_FUNCTION = 32,                  /* FUNCTION  */
  YYSYMBOL_RETURN = 33,                    /* RETURN  */
  YYSYMBOL_GOTO = 34,                      /* GOTO  */
  YYSYMBOL_GOSUB = 35,                     /* GOSUB  */
  YYSYMBOL_WATCH = 36,                     /* WATCH  */
  YYSYMBOL_WITHOUT = 37,                   /* WITHOUT  */
  YYSYMBOL_WATCHERS = 38,                  /* WATCHERS  */
  YYSYMBOL_ON = 39,                        /* ON  */
  YYSYMBOL_RESUME = 40,                    /* RESUME  */
  YYSYMBOL_NEXT = 41,                      /* NEXT  */
  YYSYMBOL_STOP = 42,                      /* STOP  */
  YYSYMBOL_ERROR_VALUE = 43,               /* ERROR_VALUE  */
  YYSYMBOL_MODIFIER = 44,                  /* MODIFIER  */
  YYSYMBOL_PROGRAM = 45,                   /* PROGRAM  */
  YYSYMBOL_LIBRARY = 46,                   /* LIBRARY  */
  YYSYMBOL_LOAD = 47,                      /* LOAD  */
  YYSYMBOL_USE = 48,                       /* USE  */
  YYSYMBOL_EXPORT = 49,                    /* EXPORT  */
  YYSYMBOL_OP_EQ = 50,                     /* OP_EQ  */
  YYSYMBOL_OP_NE = 51,                     /* OP_NE  */
  YYSYMBOL_OP_GT = 52,                     /* OP_GT  */
  YYSYMBOL_OP_LT = 53,                     /* OP_LT  */
  YYSYMBOL_OP_GE = 54,                     /* OP_GE  */
  YYSYMBOL_OP_LE = 55,                     /* OP_LE  */
  YYSYMBOL_OP_NGT = 56,                    /* OP_NGT  */
  YYSYMBOL_OP_NLT = 57,                    /* OP_NLT  */
  YYSYMBOL_OP_NGE = 58,                    /* OP_NGE  */
  YYSYMBOL_OP_NLE = 59,                    /* OP_NLE  */
  YYSYMBOL_PLUS = 60,                      /* PLUS  */
  YYSYMBOL_MINUS = 61,                     /* MINUS  */
  YYSYMBOL_STAR = 62,                      /* STAR  */
  YYSYMBOL_SLASH = 63,                     /* SLASH  */
  YYSYMBOL_LPAREN = 64,                    /* LPAREN  */
  YYSYMBOL_MOD_LPAREN = 65,                /* MOD_LPAREN  */
  YYSYMBOL_RPAREN = 66,                    /* RPAREN  */
  YYSYMBOL_LBRACKET = 67,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 68,                  /* RBRACKET  */
  YYSYMBOL_LBRACE = 69,                    /* LBRACE  */
  YYSYMBOL_RBRACE = 70,                    /* RBRACE  */
  YYSYMBOL_COMMA = 71,                     /* COMMA  */
  YYSYMBOL_COLON = 72,                     /* COLON  */
  YYSYMBOL_NEWLINE = 73,                   /* NEWLINE  */
  YYSYMBOL_IF_WITHOUT_ELSE = 74,           /* IF_WITHOUT_ELSE  */
  YYSYMBOL_NO_DOT = 75,                    /* NO_DOT  */
  YYSYMBOL_DOT = 76,                       /* DOT  */
  YYSYMBOL_YYACCEPT = 77,                  /* $accept  */
  YYSYMBOL_program = 78,                   /* program  */
  YYSYMBOL_statement_list = 79,            /* statement_list  */
  YYSYMBOL_statement = 80,                 /* statement  */
  YYSYMBOL_assignment = 81,                /* assignment  */
  YYSYMBOL_lvalue = 82,                    /* lvalue  */
  YYSYMBOL_variable_name = 83,             /* variable_name  */
  YYSYMBOL_modifier = 84,                  /* modifier  */
  YYSYMBOL_modifier_name = 85,             /* modifier_name  */
  YYSYMBOL_modifier_word = 86,             /* modifier_word  */
  YYSYMBOL_print_statement = 87,           /* print_statement  */
  YYSYMBOL_call_statement = 88,            /* call_statement  */
  YYSYMBOL_with_lock_statement = 89,       /* with_lock_statement  */
  YYSYMBOL_for_each_statement = 90,        /* for_each_statement  */
  YYSYMBOL_while_statement = 91,           /* while_statement  */
  YYSYMBOL_consider_statement = 92,        /* consider_statement  */
  YYSYMBOL_consider_branch_list = 93,      /* consider_branch_list  */
  YYSYMBOL_consider_else_opt = 94,         /* consider_else_opt  */
  YYSYMBOL_consider_statement_list = 95,   /* consider_statement_list  */
  YYSYMBOL_consider_body_statement = 96,   /* consider_body_statement  */
  YYSYMBOL_function_statement = 97,        /* function_statement  */
  YYSYMBOL_modifier_statement = 98,        /* modifier_statement  */
  YYSYMBOL_program_statement = 99,         /* program_statement  */
  YYSYMBOL_library_statement = 100,        /* library_statement  */
  YYSYMBOL_use_statement = 101,            /* use_statement  */
  YYSYMBOL_modifier_signature = 102,       /* modifier_signature  */
  YYSYMBOL_modifier_context = 103,         /* modifier_context  */
  YYSYMBOL_watch_statement = 104,          /* watch_statement  */
  YYSYMBOL_watch_target_list = 105,        /* watch_target_list  */
  YYSYMBOL_watch_target_path = 106,        /* watch_target_path  */
  YYSYMBOL_without_watchers_statement = 107, /* without_watchers_statement  */
  YYSYMBOL_on_error_statement = 108,       /* on_error_statement  */
  YYSYMBOL_error_statement = 109,          /* error_statement  */
  YYSYMBOL_return_statement = 110,         /* return_statement  */
  YYSYMBOL_label_statement = 111,          /* label_statement  */
  YYSYMBOL_goto_statement = 112,           /* goto_statement  */
  YYSYMBOL_gosub_statement = 113,          /* gosub_statement  */
  YYSYMBOL_break_statement = 114,          /* break_statement  */
  YYSYMBOL_continue_statement = 115,       /* continue_statement  */
  YYSYMBOL_if_statement = 116,             /* if_statement  */
  YYSYMBOL_if_block_tail = 117,            /* if_block_tail  */
  YYSYMBOL_if_inline_tail = 118,           /* if_inline_tail  */
  YYSYMBOL_inline_statement = 119,         /* inline_statement  */
  YYSYMBOL_expression = 120,               /* expression  */
  YYSYMBOL_or_expression = 121,            /* or_expression  */
  YYSYMBOL_and_expression = 122,           /* and_expression  */
  YYSYMBOL_comparison_expression = 123,    /* comparison_expression  */
  YYSYMBOL_additive_expression = 124,      /* additive_expression  */
  YYSYMBOL_multiplicative_expression = 125, /* multiplicative_expression  */
  YYSYMBOL_unary_expression = 126,         /* unary_expression  */
  YYSYMBOL_postfix_expression = 127,       /* postfix_expression  */
  YYSYMBOL_comparison_operator = 128,      /* comparison_operator  */
  YYSYMBOL_primary = 129,                  /* primary  */
  YYSYMBOL_ident_suffix = 130,             /* ident_suffix  */
  YYSYMBOL_ident_dot_suffix = 131,         /* ident_dot_suffix  */
  YYSYMBOL_duration_terms = 132,           /* duration_terms  */
  YYSYMBOL_argument_list_opt = 133,        /* argument_list_opt  */
  YYSYMBOL_argument_list = 134,            /* argument_list  */
  YYSYMBOL_array_argument_list = 135,      /* array_argument_list  */
  YYSYMBOL_parameter_list_opt = 136,       /* parameter_list_opt  */
  YYSYMBOL_parameter_list = 137,           /* parameter_list  */
  YYSYMBOL_record_field_list = 138,        /* record_field_list  */
  YYSYMBOL_optional_newlines = 139         /* optional_newlines  */
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
#define YYLAST   1416

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  77
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  63
/* YYNRULES -- Number of rules.  */
#define YYNRULES  201
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  428

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   331


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
      75,    76
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   450,   450,   454,   455,   456,   460,   461,   462,   463,
     464,   465,   466,   467,   468,   469,   470,   471,   472,   473,
     474,   475,   476,   477,   478,   479,   480,   481,   482,   486,
     487,   497,   498,   499,   503,   504,   505,   509,   513,   514,
     518,   519,   520,   521,   525,   529,   530,   536,   549,   561,
     564,   570,   576,   582,   585,   591,   592,   596,   597,   598,
     602,   603,   604,   605,   606,   607,   608,   609,   610,   611,
     612,   613,   614,   615,   616,   617,   618,   619,   620,   621,
     622,   623,   624,   628,   634,   637,   643,   649,   655,   656,
     657,   658,   659,   670,   684,   685,   689,   693,   696,   702,
     703,   707,   708,   712,   718,   719,   720,   724,   728,   729,
     733,   737,   741,   745,   749,   753,   757,   764,   767,   770,
     776,   779,   782,   788,   789,   790,   791,   792,   793,   794,
     795,   796,   797,   798,   802,   806,   807,   811,   812,   816,
     817,   818,   828,   829,   830,   834,   835,   836,   840,   841,
     842,   846,   847,   848,   852,   853,   854,   855,   856,   857,
     858,   859,   860,   861,   865,   866,   867,   868,   879,   885,
     886,   887,   888,   889,   890,   891,   892,   893,   894,   898,
     903,   908,   915,   920,   928,   932,   938,   939,   943,   944,
     948,   949,   953,   954,   958,   959,   963,   964,   965,   966,
     970,   971
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
  "STRING", "MOD_CONTENT", "QUALIFIED_IDENT", "IF", "CONSIDER_IF", "THEN",
  "ELSE", "CONSIDER_ELSE", "END", "END_CONSIDER", "PRINT", "TRUE", "FALSE",
  "NOTHING", "UNKNOWN_VALUE", "AND", "OR", "NOT", "WITH", "FOR", "TO",
  "IN", "EACH", "WHILE", "CONSIDER", "BREAK", "CONTINUE", "FUNCTION",
  "RETURN", "GOTO", "GOSUB", "WATCH", "WITHOUT", "WATCHERS", "ON",
  "RESUME", "NEXT", "STOP", "ERROR_VALUE", "MODIFIER", "PROGRAM",
  "LIBRARY", "LOAD", "USE", "EXPORT", "OP_EQ", "OP_NE", "OP_GT", "OP_LT",
  "OP_GE", "OP_LE", "OP_NGT", "OP_NLT", "OP_NGE", "OP_NLE", "PLUS",
  "MINUS", "STAR", "SLASH", "LPAREN", "MOD_LPAREN", "RPAREN", "LBRACKET",
  "RBRACKET", "LBRACE", "RBRACE", "COMMA", "COLON", "NEWLINE",
  "IF_WITHOUT_ELSE", "NO_DOT", "DOT", "$accept", "program",
  "statement_list", "statement", "assignment", "lvalue", "variable_name",
  "modifier", "modifier_name", "modifier_word", "print_statement",
  "call_statement", "with_lock_statement", "for_each_statement",
  "while_statement", "consider_statement", "consider_branch_list",
  "consider_else_opt", "consider_statement_list",
  "consider_body_statement", "function_statement", "modifier_statement",
  "program_statement", "library_statement", "use_statement",
  "modifier_signature", "modifier_context", "watch_statement",
  "watch_target_list", "watch_target_path", "without_watchers_statement",
  "on_error_statement", "error_statement", "return_statement",
  "label_statement", "goto_statement", "gosub_statement",
  "break_statement", "continue_statement", "if_statement", "if_block_tail",
  "if_inline_tail", "inline_statement", "expression", "or_expression",
  "and_expression", "comparison_expression", "additive_expression",
  "multiplicative_expression", "unary_expression", "postfix_expression",
  "comparison_operator", "primary", "ident_suffix", "ident_dot_suffix",
  "duration_terms", "argument_list_opt", "argument_list",
  "array_argument_list", "parameter_list_opt", "parameter_list",
  "record_field_list", "optional_newlines", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-316)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -316,    24,   488,  -316,   -11,     3,  1347,  -316,  1347,    78,
      16,  1347,  1347,  -316,  -316,    95,  1347,   102,   111,    21,
      81,   100,  -316,    33,    99,   147,   152,   121,   132,   113,
    -316,  -316,   105,   164,   101,   109,   112,  -316,  -316,  -316,
    -316,  -316,  -316,  -316,  -316,   119,  -316,  -316,   120,   124,
     131,   137,   138,   139,   144,   146,  -316,  1347,  1347,   190,
    -316,  -316,   141,  -316,  -316,  -316,  -316,  1347,  -316,  1347,
    1347,  -316,  -316,   -22,   217,   209,   212,  -316,  1320,    87,
    -316,    -4,  -316,   231,  -316,   171,   210,   233,   166,   168,
     178,  -316,  -316,  -316,    92,  -316,    -5,   174,   179,    80,
     243,  -316,  -316,  -316,  -316,  -316,    19,  -316,   229,   193,
     185,   258,  -316,   259,  -316,    99,  -316,  1347,   260,  1347,
     264,   214,  -316,  -316,  -316,  -316,  -316,  -316,  -316,  -316,
    -316,  -316,  -316,  -316,  -316,   203,   199,   205,  -316,  1347,
    -316,  -316,   208,   307,     0,  1347,   271,  -316,  1224,  1347,
    1347,  -316,  -316,  -316,  -316,  -316,  -316,  -316,  -316,  -316,
    -316,  1347,  1347,  1190,  1347,  1347,  1347,  1347,   272,   273,
    1347,  1347,   255,  -316,   277,   284,   -10,    92,  -316,   285,
    -316,   286,   250,  -316,   228,   284,  -316,   289,   284,  -316,
     290,   292,   274,  -316,  -316,   232,  -316,  1347,  -316,  1347,
    -316,   235,  -316,  -316,  -316,  -316,   234,   -37,  -316,   236,
     238,   249,  -316,  -316,  -316,  -316,  -316,  -316,  -316,  -316,
    -316,  -316,  -316,  -316,  -316,   226,   212,  -316,    87,    87,
    1347,    94,  -316,  -316,   247,  -316,  -316,   251,   248,  1347,
     534,  1347,   123,  -316,   261,   245,   257,   174,   580,  -316,
     626,  -316,  -316,  1347,   262,  -316,   265,   266,   672,  -316,
    -316,   289,  -316,  -316,  -316,  -316,  -316,    50,  1347,  1347,
    -316,    68,  -316,  1347,  -316,   442,   291,    94,  -316,   268,
    -316,   270,   303,   323,  1347,   278,   321,   280,   333,  -316,
     308,   302,   279,  -316,  -316,   281,   300,   283,   387,  -316,
    -316,  -316,     6,  -316,   293,  1269,   349,  -316,  1314,  -316,
    -316,   718,  -316,   288,   294,   348,  -316,   296,  -316,  -316,
     764,   297,   299,  -316,   810,  -316,   304,  -316,  -316,    38,
    -316,  -316,   305,   306,  -316,   309,   856,   340,   902,  -316,
    -316,   310,   948,  -316,   994,   329,  -316,  -316,   322,  1040,
    -316,  1086,  1347,  1347,  1132,  -316,  -316,  1178,  -316,   350,
     311,   357,   948,  -316,  -316,   312,   313,   315,  -316,  -316,
    -316,  -316,  -316,  -316,  -316,  -316,  -316,   316,  -316,  -316,
     320,   324,   325,   334,   335,   337,   338,   339,  -316,   363,
     341,   342,   368,   358,  -316,  -316,   408,   410,   347,  -316,
     351,   948,  -316,  -316,  -316,  -316,  -316,  -316,  -316,  -316,
    -316,  -316,  -316,  -316,   353,  -316,  -316,   354,   356,   360,
     362,  -316,  -316,  -316,  -316,  -316,  -316,  -316
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       3,     0,     2,     1,    34,     0,     0,    35,     0,     0,
       0,     0,     0,   113,   114,     0,   108,     0,     0,     0,
       0,     0,    36,     0,     0,     0,     0,     0,     0,     0,
       4,     5,     0,     0,    31,     0,     0,     9,    10,    11,
      12,    13,    14,    15,    16,     0,    18,    19,     0,     0,
       0,     0,     0,     0,     0,     0,    28,   186,   186,   164,
      34,   166,     0,   170,   171,   172,   173,     0,   169,     0,
       0,   200,   200,   179,     0,   134,   135,   137,   139,   142,
     145,   148,   151,   165,    44,     0,     0,     0,     0,     0,
       0,   109,   111,   112,     0,   101,     0,    99,     0,     0,
       0,   107,    40,    42,    41,    43,    94,    38,     0,     0,
       0,    89,    91,    88,    90,     0,     6,     0,     0,     0,
       0,     0,   110,     7,     8,    17,    20,    21,    22,    23,
      24,    25,    26,    27,   188,     0,   187,     0,   184,   186,
     149,   150,     0,     0,     0,   186,     0,   167,     0,     0,
       0,   154,   155,   156,   157,   158,   159,   160,   161,   162,
     163,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     3,     0,   192,     0,     0,     3,     0,
       3,     0,     0,   106,     0,   192,    39,     0,   192,     3,
       0,     0,     0,    29,    37,     0,    33,     0,    45,     0,
      46,     0,   174,   175,   201,   190,   200,     0,   177,   200,
       0,   182,     3,   123,    31,   124,   125,   126,   127,   128,
     129,   130,   131,   132,   133,     0,   136,   138,   143,   144,
       0,   140,   146,   147,     0,   153,   185,     0,     0,     0,
       0,     0,    55,   194,     0,   193,     0,   100,     0,   102,
       0,   104,   105,   186,     0,    96,     0,     0,     0,    93,
      92,     0,    32,    30,   189,   168,   200,     0,     0,     0,
     200,     0,   180,   186,   181,     0,   120,   141,   152,     0,
       3,     0,    35,     0,     0,     0,     0,     0,     0,     3,
      35,    35,     0,    95,     3,     0,    35,     0,     0,   176,
     196,   197,     0,   178,     0,     0,    35,   115,     0,   116,
       3,     0,     3,     0,     0,     0,    57,     0,     3,   195,
       0,     0,     0,    47,     0,     3,     0,     3,   191,     0,
     183,     3,     0,     0,     3,     0,     0,    35,     0,    51,
      57,     0,    56,    52,     0,    35,    98,   103,    35,     0,
      87,     0,     0,     0,     0,   118,   117,     0,   121,    35,
       0,    35,    53,    57,    58,     0,     0,     0,    63,    64,
      65,    66,    59,    67,    68,    69,    70,     0,    72,    73,
       0,     0,     0,     0,     0,     0,     0,     0,    82,    35,
       0,     0,    35,    35,   198,   199,    35,    35,     0,    49,
       0,    54,    60,    61,    62,    71,    74,    75,    76,    77,
      78,    79,    80,    81,     0,    97,    84,     0,     0,     0,
       0,    48,    50,    83,    86,    85,   119,   122
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -316,  -316,   107,  -316,  -147,  -316,    -2,   343,  -316,   317,
    -146,  -142,  -315,  -314,  -303,  -297,  -316,  -316,  -299,  -316,
    -285,  -284,  -273,  -271,  -141,   327,   161,  -267,   345,   254,
    -249,  -140,  -136,  -134,  -241,  -129,  -119,  -118,  -117,  -231,
    -316,  -316,  -161,    10,  -316,   287,   295,  -149,    22,   -58,
    -316,   298,  -316,  -316,  -316,  -316,   -55,  -316,  -316,   -40,
    -316,  -316,   -67
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,     2,    31,    32,    33,    73,   121,   106,   107,
      35,    36,    37,    38,    39,    40,   242,   286,   342,   372,
      41,    42,    43,    44,    45,   108,   256,    46,    96,    97,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
     307,   309,   225,   134,    75,    76,    77,    78,    79,    80,
      81,   164,    82,   147,   274,    83,   135,   136,   206,   244,
     245,   209,   143
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      34,   213,   215,   137,   207,   144,   216,   217,   218,   140,
     329,   141,   219,   268,   220,   231,    74,    95,    84,   221,
      86,    88,    89,   102,     3,    60,    91,   368,   369,   222,
     223,   224,   103,   101,     7,   269,    59,    60,    61,   370,
      62,   362,   145,    87,   104,   371,     7,   368,   369,    63,
      64,    65,    66,    57,   146,    67,   246,   373,   374,   370,
     105,   177,    22,   167,   401,   371,   177,    58,   178,   375,
     208,   376,   168,   204,    22,   378,    68,   373,   374,   204,
     142,   277,    85,   185,   201,    94,   368,   369,   352,   375,
     210,   376,    95,   379,    69,   378,    60,    70,   370,    90,
      71,   383,    72,   102,   371,     7,    92,   232,   233,   100,
     353,   388,   103,   379,   181,    93,   373,   374,   299,    98,
     182,   383,   183,   204,   104,   111,   112,   193,   375,   195,
     376,   388,   284,    22,   378,   285,   113,   114,   303,   267,
     105,   204,   271,    99,   332,   254,   214,   335,   257,   165,
     166,   109,   379,   205,   161,   162,   110,   115,   213,   215,
     383,   213,   215,   216,   217,   218,   216,   217,   218,   219,
     388,   220,   219,   122,   220,    95,   221,   234,   116,   221,
     237,   238,   123,   228,   229,   124,   222,   223,   224,   222,
     223,   224,   125,   126,   138,   365,   366,   127,   292,   298,
     367,   377,   380,   302,   128,   139,   381,   263,   382,   264,
     129,   130,   131,   384,   117,   365,   366,   132,   304,   133,
     367,   377,   380,   385,   386,   387,   381,   148,   382,   118,
     149,   119,   150,   384,   169,   170,   171,   172,    34,   173,
     120,   174,   175,   385,   386,   387,    34,   184,    34,   281,
     179,   283,   180,   187,   365,   366,    34,   188,   189,   367,
     377,   380,   190,   191,   197,   381,   194,   382,   196,   198,
     199,   200,   384,    34,   202,   211,   235,   236,   300,   301,
     240,   239,   385,   386,   387,   248,   241,   250,   243,   249,
     251,   252,   253,   255,   315,   259,   258,   260,   261,   276,
     262,   265,   308,   214,   272,   266,   214,   270,   328,    34,
      59,    60,    61,   273,    62,   278,   288,   279,    34,   275,
       7,   280,    34,    63,    64,    65,    66,   287,   293,    67,
     289,   313,   295,   314,    34,   317,    34,   319,   294,   322,
      34,   310,    34,   312,   321,   323,   326,    34,    22,    34,
      68,   316,    34,   318,   325,    34,   327,   333,   341,   330,
      34,   339,   394,   395,   360,   390,   391,   340,    69,   343,
     346,    70,   347,   398,    71,   203,    72,   350,   355,   356,
     204,   400,   358,   363,   399,   402,   403,   311,   404,   405,
      59,    60,    61,   406,    62,   414,   320,   407,   408,    34,
       7,   324,   418,    63,    64,    65,    66,   409,   410,    67,
     411,   412,   413,   417,   415,   416,   419,   336,   420,   338,
     421,   163,   297,   186,   422,   344,   423,   424,    22,   425,
      68,   247,   349,   426,   351,   427,   226,     0,   354,   176,
       0,   357,   192,     0,     0,   227,     4,     0,    69,     5,
       6,    70,     0,   305,    71,   306,    72,     8,     0,     0,
     204,   230,     0,     0,     0,     9,    10,     0,     0,     0,
      11,    12,    13,    14,    15,    16,    17,    18,    19,    20,
       0,    21,     0,    22,     0,    23,    24,    25,    26,    27,
      28,    29,     4,     0,     0,     5,     6,     0,     0,     0,
       0,     7,     0,     8,     0,     0,     0,     0,     0,     0,
       0,     9,    10,     0,     0,    30,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,     0,    21,     0,    22,
       0,    23,    24,    25,    26,    27,    28,    29,     4,     0,
       0,     5,     6,     0,     0,     0,     0,   282,     0,     8,
       0,     0,     0,     0,     0,     0,     0,     9,    10,     0,
       0,    30,    11,    12,    13,    14,    15,    16,    17,    18,
      19,    20,     0,    21,     0,    22,     0,    23,    24,    25,
      26,    27,    28,    29,     4,     0,     0,     5,     6,     0,
       0,     0,     0,   290,     0,     8,     0,     0,     0,     0,
       0,     0,     0,     9,    10,     0,     0,    30,    11,    12,
      13,    14,    15,    16,    17,    18,    19,    20,     0,    21,
       0,    22,     0,    23,    24,    25,    26,    27,    28,    29,
       4,     0,     0,     5,     6,     0,     0,     0,     0,   291,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
      10,     0,     0,    30,    11,    12,    13,    14,    15,    16,
      17,    18,    19,    20,     0,    21,     0,    22,     0,    23,
      24,    25,    26,    27,    28,    29,     4,     0,     0,     5,
       6,     0,     0,     0,     0,   296,     0,     8,     0,     0,
       0,     0,     0,     0,     0,     9,    10,     0,     0,    30,
      11,    12,    13,    14,    15,    16,    17,    18,    19,    20,
       0,    21,     0,    22,     0,    23,    24,    25,    26,    27,
      28,    29,     4,     0,     0,     5,     6,     0,     0,     0,
       0,   337,     0,     8,     0,     0,     0,     0,     0,     0,
       0,     9,    10,     0,     0,    30,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,     0,    21,     0,    22,
       0,    23,    24,    25,    26,    27,    28,    29,     4,     0,
       0,     5,     6,     0,     0,     0,     0,   345,     0,     8,
       0,     0,     0,     0,     0,     0,     0,     9,    10,     0,
       0,    30,    11,    12,    13,    14,    15,    16,    17,    18,
      19,    20,     0,    21,     0,    22,     0,    23,    24,    25,
      26,    27,    28,    29,     4,     0,     0,     5,     6,     0,
       0,     0,     0,   348,     0,     8,     0,     0,     0,     0,
       0,     0,     0,     9,    10,     0,     0,    30,    11,    12,
      13,    14,    15,    16,    17,    18,    19,    20,     0,    21,
       0,    22,     0,    23,    24,    25,    26,    27,    28,    29,
       4,     0,     0,     5,     6,     0,     0,     0,     0,   359,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
      10,     0,     0,    30,    11,    12,    13,    14,    15,    16,
      17,    18,    19,    20,     0,    21,     0,    22,     0,    23,
      24,    25,    26,    27,    28,    29,     4,     0,     0,     5,
       6,     0,     0,     0,     0,   361,     0,     8,     0,     0,
       0,     0,     0,     0,     0,     9,    10,     0,     0,    30,
      11,    12,    13,    14,    15,    16,    17,    18,    19,    20,
       0,    21,     0,    22,     0,    23,    24,    25,    26,    27,
      28,    29,     4,     0,     0,     5,     6,     0,     0,     0,
       0,     7,     0,     8,     0,     0,     0,     0,     0,     0,
       0,     9,    10,     0,     0,    30,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,     0,    21,     0,    22,
       0,    23,    24,    25,    26,    27,    28,    29,     4,     0,
       0,     5,     6,     0,     0,     0,     0,   389,     0,     8,
       0,     0,     0,     0,     0,     0,     0,     9,    10,     0,
       0,   364,    11,    12,    13,    14,    15,    16,    17,    18,
      19,    20,     0,    21,     0,    22,     0,    23,    24,    25,
      26,    27,    28,    29,     4,     0,     0,     5,     6,     0,
       0,     0,     0,   392,     0,     8,     0,     0,     0,     0,
       0,     0,     0,     9,    10,     0,     0,    30,    11,    12,
      13,    14,    15,    16,    17,    18,    19,    20,     0,    21,
       0,    22,     0,    23,    24,    25,    26,    27,    28,    29,
       4,     0,     0,     5,     6,     0,     0,     0,     0,   393,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
      10,     0,     0,    30,    11,    12,    13,    14,    15,    16,
      17,    18,    19,    20,     0,    21,     0,    22,     0,    23,
      24,    25,    26,    27,    28,    29,     4,     0,     0,     5,
       6,     0,     0,     0,     0,   396,     0,     8,     0,     0,
       0,     0,     0,     0,     0,     9,    10,     0,     0,    30,
      11,    12,    13,    14,    15,    16,    17,    18,    19,    20,
       0,    21,     0,    22,     0,    23,    24,    25,    26,    27,
      28,    29,     4,     0,     0,     5,     6,     0,     0,     0,
       0,   397,     0,     8,     0,     0,     0,     0,     0,     0,
       0,     9,    10,     0,     0,    30,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,     0,    21,     0,    22,
       0,    23,    24,    25,    26,    27,    28,    29,     4,     0,
       0,     5,     0,     0,     0,     0,     0,     7,     0,     8,
     151,   152,   153,   154,   155,   156,   157,   158,   159,   160,
       0,    30,     0,     0,    13,    14,     0,    16,    17,    18,
       0,     0,     0,    21,     0,    22,     0,    23,     0,     0,
       0,    27,    28,     4,     0,     0,     5,     0,     0,     0,
       0,     0,     7,     0,     8,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   212,     0,    13,
      14,     0,    16,    17,    18,     0,     0,     0,    21,     0,
      22,     0,    23,     0,     0,     0,    27,    28,     4,     0,
       0,     5,     0,     0,     0,     0,     0,     7,     0,     8,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   331,     0,    13,    14,     0,    16,    17,    18,
      59,    60,    61,    21,    62,    22,     0,    23,     0,     0,
       7,    27,    28,    63,    64,    65,    66,     0,     0,    67,
     151,   152,   153,   154,   155,   156,   157,   158,   159,   160,
     161,   162,     0,     0,     0,   118,     0,   334,    22,     0,
      68,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    69,     0,
       0,    70,     0,     0,    71,     0,    72
};

static const yytype_int16 yycheck[] =
{
       2,   148,   148,    58,     4,    72,   148,   148,   148,    67,
       4,    69,   148,    50,   148,   164,     6,    19,     8,   148,
       4,    11,    12,     4,     0,     4,    16,   342,   342,   148,
     148,   148,    13,    23,    13,    72,     3,     4,     5,   342,
       7,   340,    64,    27,    25,   342,    13,   362,   362,    16,
      17,    18,    19,    64,    76,    22,    66,   342,   342,   362,
      41,    71,    41,    67,   363,   362,    71,    64,    73,   342,
      70,   342,    76,    73,    41,   342,    43,   362,   362,    73,
      70,   230,     4,    64,   139,    64,   401,   401,    50,   362,
     145,   362,    94,   342,    61,   362,     4,    64,   401,     4,
      67,   342,    69,     4,   401,    13,     4,   165,   166,    76,
      72,   342,    13,   362,    34,     4,   401,   401,    68,    38,
      40,   362,    42,    73,    25,     4,     5,   117,   401,   119,
     401,   362,     9,    41,   401,    12,     4,     5,    70,   206,
      41,    73,   209,    43,   305,   185,   148,   308,   188,    62,
      63,     4,   401,   143,    60,    61,     4,    44,   305,   305,
     401,   308,   308,   305,   305,   305,   308,   308,   308,   305,
     401,   305,   308,    72,   308,   177,   305,   167,    73,   308,
     170,   171,    73,   161,   162,    73,   305,   305,   305,   308,
     308,   308,    73,    73,     4,   342,   342,    73,   253,   266,
     342,   342,   342,   270,    73,    64,   342,   197,   342,   199,
      73,    73,    73,   342,    50,   362,   362,    73,   273,    73,
     362,   362,   362,   342,   342,   342,   362,    10,   362,    65,
      21,    67,    20,   362,     3,    64,    26,     4,   240,    73,
      76,    73,    64,   362,   362,   362,   248,     4,   250,   239,
      76,   241,    73,    24,   401,   401,   258,    64,    73,   401,
     401,   401,     4,     4,    50,   401,     6,   401,     4,    66,
      71,    66,   401,   275,    66,     4,     4,     4,   268,   269,
     173,    26,   401,   401,   401,   178,     9,   180,     4,     4,
       4,    41,    64,     4,   284,     5,   189,     5,    24,    73,
      68,    66,    11,   305,    66,    71,   308,    71,   298,   311,
       3,     4,     5,    64,     7,    68,    71,    66,   320,   212,
      13,    73,   324,    16,    17,    18,    19,    66,    66,    22,
      73,    28,    66,    10,   336,    14,   338,     4,    73,    37,
     342,    73,   344,    73,    36,    66,    46,   349,    41,   351,
      43,    73,   354,    73,    73,   357,    73,     8,    10,    66,
     362,    73,   352,   353,    24,    36,    44,    73,    61,    73,
      73,    64,    73,    23,    67,    68,    69,    73,    73,    73,
      73,    24,    73,    73,    73,    73,    73,   280,    73,    73,
       3,     4,     5,    73,     7,    32,   289,    73,    73,   401,
      13,   294,    44,    16,    17,    18,    19,    73,    73,    22,
      73,    73,    73,    45,    73,    73,     8,   310,     8,   312,
      73,    78,   261,   106,    73,   318,    73,    73,    41,    73,
      43,   177,   325,    73,   327,    73,   149,    -1,   331,    94,
      -1,   334,   115,    -1,    -1,   150,     4,    -1,    61,     7,
       8,    64,    -1,    11,    67,    13,    69,    15,    -1,    -1,
      73,   163,    -1,    -1,    -1,    23,    24,    -1,    -1,    -1,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      -1,    39,    -1,    41,    -1,    43,    44,    45,    46,    47,
      48,    49,     4,    -1,    -1,     7,     8,    -1,    -1,    -1,
      -1,    13,    -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    23,    24,    -1,    -1,    73,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    -1,    39,    -1,    41,
      -1,    43,    44,    45,    46,    47,    48,    49,     4,    -1,
      -1,     7,     8,    -1,    -1,    -1,    -1,    13,    -1,    15,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    23,    24,    -1,
      -1,    73,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    -1,    39,    -1,    41,    -1,    43,    44,    45,
      46,    47,    48,    49,     4,    -1,    -1,     7,     8,    -1,
      -1,    -1,    -1,    13,    -1,    15,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    23,    24,    -1,    -1,    73,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    -1,    39,
      -1,    41,    -1,    43,    44,    45,    46,    47,    48,    49,
       4,    -1,    -1,     7,     8,    -1,    -1,    -1,    -1,    13,
      -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    23,
      24,    -1,    -1,    73,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    -1,    39,    -1,    41,    -1,    43,
      44,    45,    46,    47,    48,    49,     4,    -1,    -1,     7,
       8,    -1,    -1,    -1,    -1,    13,    -1,    15,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    23,    24,    -1,    -1,    73,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      -1,    39,    -1,    41,    -1,    43,    44,    45,    46,    47,
      48,    49,     4,    -1,    -1,     7,     8,    -1,    -1,    -1,
      -1,    13,    -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    23,    24,    -1,    -1,    73,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    -1,    39,    -1,    41,
      -1,    43,    44,    45,    46,    47,    48,    49,     4,    -1,
      -1,     7,     8,    -1,    -1,    -1,    -1,    13,    -1,    15,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    23,    24,    -1,
      -1,    73,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    -1,    39,    -1,    41,    -1,    43,    44,    45,
      46,    47,    48,    49,     4,    -1,    -1,     7,     8,    -1,
      -1,    -1,    -1,    13,    -1,    15,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    23,    24,    -1,    -1,    73,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    -1,    39,
      -1,    41,    -1,    43,    44,    45,    46,    47,    48,    49,
       4,    -1,    -1,     7,     8,    -1,    -1,    -1,    -1,    13,
      -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    23,
      24,    -1,    -1,    73,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    -1,    39,    -1,    41,    -1,    43,
      44,    45,    46,    47,    48,    49,     4,    -1,    -1,     7,
       8,    -1,    -1,    -1,    -1,    13,    -1,    15,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    23,    24,    -1,    -1,    73,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      -1,    39,    -1,    41,    -1,    43,    44,    45,    46,    47,
      48,    49,     4,    -1,    -1,     7,     8,    -1,    -1,    -1,
      -1,    13,    -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    23,    24,    -1,    -1,    73,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    -1,    39,    -1,    41,
      -1,    43,    44,    45,    46,    47,    48,    49,     4,    -1,
      -1,     7,     8,    -1,    -1,    -1,    -1,    13,    -1,    15,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    23,    24,    -1,
      -1,    73,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    -1,    39,    -1,    41,    -1,    43,    44,    45,
      46,    47,    48,    49,     4,    -1,    -1,     7,     8,    -1,
      -1,    -1,    -1,    13,    -1,    15,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    23,    24,    -1,    -1,    73,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    -1,    39,
      -1,    41,    -1,    43,    44,    45,    46,    47,    48,    49,
       4,    -1,    -1,     7,     8,    -1,    -1,    -1,    -1,    13,
      -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    23,
      24,    -1,    -1,    73,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    -1,    39,    -1,    41,    -1,    43,
      44,    45,    46,    47,    48,    49,     4,    -1,    -1,     7,
       8,    -1,    -1,    -1,    -1,    13,    -1,    15,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    23,    24,    -1,    -1,    73,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      -1,    39,    -1,    41,    -1,    43,    44,    45,    46,    47,
      48,    49,     4,    -1,    -1,     7,     8,    -1,    -1,    -1,
      -1,    13,    -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    23,    24,    -1,    -1,    73,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    -1,    39,    -1,    41,
      -1,    43,    44,    45,    46,    47,    48,    49,     4,    -1,
      -1,     7,    -1,    -1,    -1,    -1,    -1,    13,    -1,    15,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      -1,    73,    -1,    -1,    30,    31,    -1,    33,    34,    35,
      -1,    -1,    -1,    39,    -1,    41,    -1,    43,    -1,    -1,
      -1,    47,    48,     4,    -1,    -1,     7,    -1,    -1,    -1,
      -1,    -1,    13,    -1,    15,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    73,    -1,    30,
      31,    -1,    33,    34,    35,    -1,    -1,    -1,    39,    -1,
      41,    -1,    43,    -1,    -1,    -1,    47,    48,     4,    -1,
      -1,     7,    -1,    -1,    -1,    -1,    -1,    13,    -1,    15,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    73,    -1,    30,    31,    -1,    33,    34,    35,
       3,     4,     5,    39,     7,    41,    -1,    43,    -1,    -1,
      13,    47,    48,    16,    17,    18,    19,    -1,    -1,    22,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    -1,    -1,    -1,    65,    -1,    73,    41,    -1,
      43,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    61,    -1,
      -1,    64,    -1,    -1,    67,    -1,    69
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    78,    79,     0,     4,     7,     8,    13,    15,    23,
      24,    28,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    39,    41,    43,    44,    45,    46,    47,    48,    49,
      73,    80,    81,    82,    83,    87,    88,    89,    90,    91,
      92,    97,    98,    99,   100,   101,   104,   107,   108,   109,
     110,   111,   112,   113,   114,   115,   116,    64,    64,     3,
       4,     5,     7,    16,    17,    18,    19,    22,    43,    61,
      64,    67,    69,    83,   120,   121,   122,   123,   124,   125,
     126,   127,   129,   132,   120,     4,     4,    27,   120,   120,
       4,   120,     4,     4,    64,    83,   105,   106,    38,    43,
      76,   120,     4,    13,    25,    41,    85,    86,   102,     4,
       4,     4,     5,     4,     5,    44,    73,    50,    65,    67,
      76,    84,    72,    73,    73,    73,    73,    73,    73,    73,
      73,    73,    73,    73,   120,   133,   134,   133,     4,    64,
     126,   126,   120,   139,   139,    64,    76,   130,    10,    21,
      20,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    84,   128,    62,    63,    67,    76,     3,
      64,    26,     4,    73,    73,    64,   105,    71,    73,    76,
      73,    34,    40,    42,     4,    64,    86,    24,    64,    73,
       4,     4,   102,   120,     6,   120,     4,    50,    66,    71,
      66,   133,    66,    68,    73,   120,   135,     4,    70,   138,
     133,     4,    73,    81,    83,    87,    88,   101,   108,   109,
     110,   112,   113,   114,   115,   119,   122,   123,   125,   125,
     128,   124,   126,   126,   120,     4,     4,   120,   120,    26,
      79,     9,    93,     4,   136,   137,    66,   106,    79,     4,
      79,     4,    41,    64,   136,     4,   103,   136,    79,     5,
       5,    24,    68,   120,   120,    66,    71,   139,    50,    72,
      71,   139,    66,    64,   131,    79,    73,   124,    68,    66,
      73,   120,    13,   120,     9,    12,    94,    66,    71,    73,
      13,    13,   133,    66,    73,    66,    13,   103,   139,    68,
     120,   120,   139,    70,   133,    11,    13,   117,    11,   118,
      73,    79,    73,    28,    10,   120,    73,    14,    73,     4,
      79,    36,    37,    66,    79,    73,    46,    73,   120,     4,
      66,    73,   119,     8,    73,   119,    79,    13,    79,    73,
      73,    10,    95,    73,    79,    13,    73,    73,    13,    79,
      73,    79,    50,    72,    79,    73,    73,    79,    73,    13,
      24,    13,    95,    73,    73,    81,    87,    88,    89,    90,
      91,    92,    96,    97,    98,    99,   100,   101,   104,   107,
     108,   109,   110,   111,   112,   113,   114,   115,   116,    13,
      36,    44,    13,    13,   120,   120,    13,    13,    23,    73,
      24,    95,    73,    73,    73,    73,    73,    73,    73,    73,
      73,    73,    73,    73,    32,    73,    73,    45,    44,     8,
       8,    73,    73,    73,    73,    73,    73,    73
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    77,    78,    79,    79,    79,    80,    80,    80,    80,
      80,    80,    80,    80,    80,    80,    80,    80,    80,    80,
      80,    80,    80,    80,    80,    80,    80,    80,    80,    81,
      81,    82,    82,    82,    83,    83,    83,    84,    85,    85,
      86,    86,    86,    86,    87,    88,    88,    88,    89,    90,
      90,    91,    92,    93,    93,    94,    94,    95,    95,    95,
      96,    96,    96,    96,    96,    96,    96,    96,    96,    96,
      96,    96,    96,    96,    96,    96,    96,    96,    96,    96,
      96,    96,    96,    97,    98,    98,    99,   100,   101,   101,
     101,   101,   101,   101,   102,   102,   103,   104,   104,   105,
     105,   106,   106,   107,   108,   108,   108,   109,   110,   110,
     111,   112,   113,   114,   115,   116,   116,   117,   117,   117,
     118,   118,   118,   119,   119,   119,   119,   119,   119,   119,
     119,   119,   119,   119,   120,   121,   121,   122,   122,   123,
     123,   123,   124,   124,   124,   125,   125,   125,   126,   126,
     126,   127,   127,   127,   128,   128,   128,   128,   128,   128,
     128,   128,   128,   128,   129,   129,   129,   129,   129,   129,
     129,   129,   129,   129,   129,   129,   129,   129,   129,   130,
     130,   130,   131,   131,   132,   132,   133,   133,   134,   134,
     135,   135,   136,   136,   137,   137,   138,   138,   138,   138,
     139,   139
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     2,     2,     2,     2,     1,
       1,     1,     1,     1,     1,     1,     1,     2,     1,     1,
       2,     2,     2,     2,     2,     2,     2,     2,     1,     3,
       4,     1,     4,     3,     1,     1,     1,     2,     1,     2,
       1,     1,     1,     1,     2,     4,     4,     6,    10,     9,
      10,     7,     7,     5,     6,     0,     3,     0,     2,     2,
       2,     2,     2,     1,     1,     1,     1,     1,     1,     1,
       1,     2,     1,     1,     2,     2,     2,     2,     2,     2,
       2,     2,     1,    10,     9,    10,    10,     7,     2,     2,
       2,     2,     4,     4,     1,     4,     1,     9,     7,     1,
       3,     1,     3,     7,     4,     4,     3,     2,     1,     2,
       2,     2,     2,     1,     1,     6,     6,     3,     3,     6,
       0,     3,     6,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     3,     1,     3,     1,
       3,     4,     1,     3,     3,     1,     3,     3,     1,     2,
       2,     1,     4,     3,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     2,     4,     1,
       1,     1,     1,     1,     3,     3,     5,     3,     5,     0,
       3,     3,     0,     3,     2,     3,     0,     1,     1,     3,
       1,     4,     0,     1,     1,     3,     3,     3,     6,     6,
       0,     2
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
#line 450 "src/parser.y"
                     { parsed_program = (yyvsp[0].stmt_list); (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 2474 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 454 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 2480 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 455 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 2486 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 456 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 2492 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 460 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2498 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 461 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2504 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 462 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2510 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 463 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2516 "src/parser.tab.c"
    break;

  case 10: /* statement: for_each_statement  */
#line 464 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2522 "src/parser.tab.c"
    break;

  case 11: /* statement: while_statement  */
#line 465 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2528 "src/parser.tab.c"
    break;

  case 12: /* statement: consider_statement  */
#line 466 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2534 "src/parser.tab.c"
    break;

  case 13: /* statement: function_statement  */
#line 467 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2540 "src/parser.tab.c"
    break;

  case 14: /* statement: modifier_statement  */
#line 468 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2546 "src/parser.tab.c"
    break;

  case 15: /* statement: program_statement  */
#line 469 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2552 "src/parser.tab.c"
    break;

  case 16: /* statement: library_statement  */
#line 470 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2558 "src/parser.tab.c"
    break;

  case 17: /* statement: use_statement NEWLINE  */
#line 471 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2564 "src/parser.tab.c"
    break;

  case 18: /* statement: watch_statement  */
#line 472 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2570 "src/parser.tab.c"
    break;

  case 19: /* statement: without_watchers_statement  */
#line 473 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2576 "src/parser.tab.c"
    break;

  case 20: /* statement: on_error_statement NEWLINE  */
#line 474 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2582 "src/parser.tab.c"
    break;

  case 21: /* statement: error_statement NEWLINE  */
#line 475 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2588 "src/parser.tab.c"
    break;

  case 22: /* statement: return_statement NEWLINE  */
#line 476 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2594 "src/parser.tab.c"
    break;

  case 23: /* statement: label_statement NEWLINE  */
#line 477 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2600 "src/parser.tab.c"
    break;

  case 24: /* statement: goto_statement NEWLINE  */
#line 478 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2606 "src/parser.tab.c"
    break;

  case 25: /* statement: gosub_statement NEWLINE  */
#line 479 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2612 "src/parser.tab.c"
    break;

  case 26: /* statement: break_statement NEWLINE  */
#line 480 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2618 "src/parser.tab.c"
    break;

  case 27: /* statement: continue_statement NEWLINE  */
#line 481 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2624 "src/parser.tab.c"
    break;

  case 28: /* statement: if_statement  */
#line 482 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2630 "src/parser.tab.c"
    break;

  case 29: /* assignment: lvalue OP_EQ expression  */
#line 486 "src/parser.y"
                              { (yyval.stmt) = ast_assign((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr)); }
#line 2636 "src/parser.tab.c"
    break;

  case 30: /* assignment: lvalue modifier OP_EQ expression  */
#line 487 "src/parser.y"
                                       {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            yyerror("modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr));
      }
#line 2648 "src/parser.tab.c"
    break;

  case 31: /* lvalue: variable_name  */
#line 497 "src/parser.y"
                                 { (yyval.expr) = expr_at(ast_ident((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2654 "src/parser.tab.c"
    break;

  case 32: /* lvalue: lvalue LBRACKET expression RBRACKET  */
#line 498 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 2660 "src/parser.tab.c"
    break;

  case 33: /* lvalue: lvalue DOT IDENT  */
#line 499 "src/parser.y"
                                    { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2666 "src/parser.tab.c"
    break;

  case 34: /* variable_name: IDENT  */
#line 503 "src/parser.y"
                         { (yyval.text) = (yyvsp[0].text); }
#line 2672 "src/parser.tab.c"
    break;

  case 35: /* variable_name: END  */
#line 504 "src/parser.y"
                       { (yyval.text) = copy_const("end"); }
#line 2678 "src/parser.tab.c"
    break;

  case 36: /* variable_name: NEXT  */
#line 505 "src/parser.y"
                        { (yyval.text) = copy_const("next"); }
#line 2684 "src/parser.tab.c"
    break;

  case 37: /* modifier: MOD_LPAREN MOD_CONTENT  */
#line 509 "src/parser.y"
                             { (yyval.modifier) = parse_modifier_use((yyvsp[0].text)); }
#line 2690 "src/parser.tab.c"
    break;

  case 38: /* modifier_name: modifier_word  */
#line 513 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 2696 "src/parser.tab.c"
    break;

  case 39: /* modifier_name: modifier_name modifier_word  */
#line 514 "src/parser.y"
                                  { (yyval.text) = join_words((yyvsp[-1].text), (yyvsp[0].text)); }
#line 2702 "src/parser.tab.c"
    break;

  case 40: /* modifier_word: IDENT  */
#line 518 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 2708 "src/parser.tab.c"
    break;

  case 41: /* modifier_word: TO  */
#line 519 "src/parser.y"
         { (yyval.text) = copy_const("to"); }
#line 2714 "src/parser.tab.c"
    break;

  case 42: /* modifier_word: END  */
#line 520 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 2720 "src/parser.tab.c"
    break;

  case 43: /* modifier_word: NEXT  */
#line 521 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 2726 "src/parser.tab.c"
    break;

  case 44: /* print_statement: PRINT expression  */
#line 525 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 2732 "src/parser.tab.c"
    break;

  case 45: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 529 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 2738 "src/parser.tab.c"
    break;

  case 46: /* call_statement: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 530 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.stmt) = ast_expr_stmt(ast_qualified_call(library, name, (yyvsp[-1].expr_list)));
      }
#line 2749 "src/parser.tab.c"
    break;

  case 47: /* call_statement: ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 536 "src/parser.y"
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
#line 2764 "src/parser.tab.c"
    break;

  case 48: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 549 "src/parser.y"
                                                                                  {
        if (strcmp((yyvsp[-8].text), "lock") != 0) {
            yyerror("expected lock in with lock block");
            free((yyvsp[-8].text));
            YYERROR;
        }
        free((yyvsp[-8].text));
        (yyval.stmt) = ast_with_lock((yyvsp[-6].expr), (yyvsp[-3].stmt_list));
      }
#line 2778 "src/parser.tab.c"
    break;

  case 49: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list END FOR NEWLINE  */
#line 561 "src/parser.y"
                                                                     {
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 2786 "src/parser.tab.c"
    break;

  case 50: /* for_each_statement: FOR EACH IDENT IN expression NEWLINE statement_list END FOR NEWLINE  */
#line 564 "src/parser.y"
                                                                          {
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 2794 "src/parser.tab.c"
    break;

  case 51: /* while_statement: WHILE expression NEWLINE statement_list END WHILE NEWLINE  */
#line 570 "src/parser.y"
                                                                {
        (yyval.stmt) = ast_while((yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 2802 "src/parser.tab.c"
    break;

  case 52: /* consider_statement: CONSIDER expression NEWLINE consider_branch_list consider_else_opt END_CONSIDER NEWLINE  */
#line 576 "src/parser.y"
                                                                                              {
        (yyval.stmt) = ast_consider((yyvsp[-5].expr), (yyvsp[-3].consider_branch_list), (yyvsp[-2].stmt_list));
      }
#line 2810 "src/parser.tab.c"
    break;

  case 53: /* consider_branch_list: CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 582 "src/parser.y"
                                                                  {
        (yyval.consider_branch_list) = ast_consider_branch_list_append(ast_consider_branch_list_empty(), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 2818 "src/parser.tab.c"
    break;

  case 54: /* consider_branch_list: consider_branch_list CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 585 "src/parser.y"
                                                                                       {
        (yyval.consider_branch_list) = ast_consider_branch_list_append((yyvsp[-5].consider_branch_list), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 2826 "src/parser.tab.c"
    break;

  case 55: /* consider_else_opt: %empty  */
#line 591 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 2832 "src/parser.tab.c"
    break;

  case 56: /* consider_else_opt: CONSIDER_ELSE NEWLINE consider_statement_list  */
#line 592 "src/parser.y"
                                                    { (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 2838 "src/parser.tab.c"
    break;

  case 57: /* consider_statement_list: %empty  */
#line 596 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 2844 "src/parser.tab.c"
    break;

  case 58: /* consider_statement_list: consider_statement_list NEWLINE  */
#line 597 "src/parser.y"
                                      { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 2850 "src/parser.tab.c"
    break;

  case 59: /* consider_statement_list: consider_statement_list consider_body_statement  */
#line 598 "src/parser.y"
                                                      { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 2856 "src/parser.tab.c"
    break;

  case 60: /* consider_body_statement: assignment NEWLINE  */
#line 602 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2862 "src/parser.tab.c"
    break;

  case 61: /* consider_body_statement: print_statement NEWLINE  */
#line 603 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2868 "src/parser.tab.c"
    break;

  case 62: /* consider_body_statement: call_statement NEWLINE  */
#line 604 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2874 "src/parser.tab.c"
    break;

  case 63: /* consider_body_statement: with_lock_statement  */
#line 605 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2880 "src/parser.tab.c"
    break;

  case 64: /* consider_body_statement: for_each_statement  */
#line 606 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2886 "src/parser.tab.c"
    break;

  case 65: /* consider_body_statement: while_statement  */
#line 607 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2892 "src/parser.tab.c"
    break;

  case 66: /* consider_body_statement: consider_statement  */
#line 608 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2898 "src/parser.tab.c"
    break;

  case 67: /* consider_body_statement: function_statement  */
#line 609 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2904 "src/parser.tab.c"
    break;

  case 68: /* consider_body_statement: modifier_statement  */
#line 610 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2910 "src/parser.tab.c"
    break;

  case 69: /* consider_body_statement: program_statement  */
#line 611 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2916 "src/parser.tab.c"
    break;

  case 70: /* consider_body_statement: library_statement  */
#line 612 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2922 "src/parser.tab.c"
    break;

  case 71: /* consider_body_statement: use_statement NEWLINE  */
#line 613 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2928 "src/parser.tab.c"
    break;

  case 72: /* consider_body_statement: watch_statement  */
#line 614 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2934 "src/parser.tab.c"
    break;

  case 73: /* consider_body_statement: without_watchers_statement  */
#line 615 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2940 "src/parser.tab.c"
    break;

  case 74: /* consider_body_statement: on_error_statement NEWLINE  */
#line 616 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2946 "src/parser.tab.c"
    break;

  case 75: /* consider_body_statement: error_statement NEWLINE  */
#line 617 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2952 "src/parser.tab.c"
    break;

  case 76: /* consider_body_statement: return_statement NEWLINE  */
#line 618 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2958 "src/parser.tab.c"
    break;

  case 77: /* consider_body_statement: label_statement NEWLINE  */
#line 619 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2964 "src/parser.tab.c"
    break;

  case 78: /* consider_body_statement: goto_statement NEWLINE  */
#line 620 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2970 "src/parser.tab.c"
    break;

  case 79: /* consider_body_statement: gosub_statement NEWLINE  */
#line 621 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2976 "src/parser.tab.c"
    break;

  case 80: /* consider_body_statement: break_statement NEWLINE  */
#line 622 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2982 "src/parser.tab.c"
    break;

  case 81: /* consider_body_statement: continue_statement NEWLINE  */
#line 623 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2988 "src/parser.tab.c"
    break;

  case 82: /* consider_body_statement: if_statement  */
#line 624 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2994 "src/parser.tab.c"
    break;

  case 83: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 628 "src/parser.y"
                                                                                                  {
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 3002 "src/parser.tab.c"
    break;

  case 84: /* modifier_statement: MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 634 "src/parser.y"
                                                                                                   {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 0, (yyvsp[-3].stmt_list));
      }
#line 3010 "src/parser.tab.c"
    break;

  case 85: /* modifier_statement: EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 637 "src/parser.y"
                                                                                                          {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 1, (yyvsp[-3].stmt_list));
      }
#line 3018 "src/parser.tab.c"
    break;

  case 86: /* program_statement: PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE  */
#line 643 "src/parser.y"
                                                                                                {
        (yyval.stmt) = ast_program((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 3026 "src/parser.tab.c"
    break;

  case 87: /* library_statement: LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE  */
#line 649 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_library((yyvsp[-5].text), (yyvsp[-3].stmt_list));
      }
#line 3034 "src/parser.tab.c"
    break;

  case 88: /* use_statement: USE IDENT  */
#line 655 "src/parser.y"
                { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 3040 "src/parser.tab.c"
    break;

  case 89: /* use_statement: LOAD IDENT  */
#line 656 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 3046 "src/parser.tab.c"
    break;

  case 90: /* use_statement: USE STRING  */
#line 657 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 3052 "src/parser.tab.c"
    break;

  case 91: /* use_statement: LOAD STRING  */
#line 658 "src/parser.y"
                  { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 3058 "src/parser.tab.c"
    break;

  case 92: /* use_statement: USE IDENT IDENT STRING  */
#line 659 "src/parser.y"
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
#line 3074 "src/parser.tab.c"
    break;

  case 93: /* use_statement: LOAD IDENT IDENT STRING  */
#line 670 "src/parser.y"
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
#line 3090 "src/parser.tab.c"
    break;

  case 94: /* modifier_signature: modifier_name  */
#line 684 "src/parser.y"
                    { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[0].text), ast_name_list_empty()); }
#line 3096 "src/parser.tab.c"
    break;

  case 95: /* modifier_signature: modifier_name LPAREN parameter_list_opt RPAREN  */
#line 685 "src/parser.y"
                                                     { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[-3].text), (yyvsp[-1].name_list)); }
#line 3102 "src/parser.tab.c"
    break;

  case 96: /* modifier_context: IDENT  */
#line 689 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 3108 "src/parser.tab.c"
    break;

  case 97: /* watch_statement: WATCH LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 693 "src/parser.y"
                                                                                     {
        (yyval.stmt) = ast_watch((yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 3116 "src/parser.tab.c"
    break;

  case 98: /* watch_statement: WATCH watch_target_list NEWLINE statement_list END WATCH NEWLINE  */
#line 696 "src/parser.y"
                                                                       {
        (yyval.stmt) = ast_watch((yyvsp[-5].name_list), (yyvsp[-3].stmt_list));
      }
#line 3124 "src/parser.tab.c"
    break;

  case 99: /* watch_target_list: watch_target_path  */
#line 702 "src/parser.y"
                        { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 3130 "src/parser.tab.c"
    break;

  case 100: /* watch_target_list: watch_target_list COMMA watch_target_path  */
#line 703 "src/parser.y"
                                                { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 3136 "src/parser.tab.c"
    break;

  case 101: /* watch_target_path: variable_name  */
#line 707 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 3142 "src/parser.tab.c"
    break;

  case 102: /* watch_target_path: watch_target_path DOT IDENT  */
#line 708 "src/parser.y"
                                  { (yyval.text) = join_watch_path((yyvsp[-2].text), (yyvsp[0].text)); }
#line 3148 "src/parser.tab.c"
    break;

  case 103: /* without_watchers_statement: WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE  */
#line 712 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_without_watchers((yyvsp[-3].stmt_list));
      }
#line 3156 "src/parser.tab.c"
    break;

  case 104: /* on_error_statement: ON ERROR_VALUE GOTO IDENT  */
#line 718 "src/parser.y"
                                { (yyval.stmt) = ast_on_error_goto((yyvsp[0].text)); }
#line 3162 "src/parser.tab.c"
    break;

  case 105: /* on_error_statement: ON ERROR_VALUE RESUME NEXT  */
#line 719 "src/parser.y"
                                 { (yyval.stmt) = ast_on_error_resume_next(); }
#line 3168 "src/parser.tab.c"
    break;

  case 106: /* on_error_statement: ON ERROR_VALUE STOP  */
#line 720 "src/parser.y"
                          { (yyval.stmt) = ast_on_error_stop(); }
#line 3174 "src/parser.tab.c"
    break;

  case 107: /* error_statement: ERROR_VALUE expression  */
#line 724 "src/parser.y"
                             { (yyval.stmt) = ast_error((yyvsp[0].expr)); }
#line 3180 "src/parser.tab.c"
    break;

  case 108: /* return_statement: RETURN  */
#line 728 "src/parser.y"
             { (yyval.stmt) = ast_return(NULL); }
#line 3186 "src/parser.tab.c"
    break;

  case 109: /* return_statement: RETURN expression  */
#line 729 "src/parser.y"
                        { (yyval.stmt) = ast_return((yyvsp[0].expr)); }
#line 3192 "src/parser.tab.c"
    break;

  case 110: /* label_statement: variable_name COLON  */
#line 733 "src/parser.y"
                          { (yyval.stmt) = ast_label((yyvsp[-1].text)); }
#line 3198 "src/parser.tab.c"
    break;

  case 111: /* goto_statement: GOTO IDENT  */
#line 737 "src/parser.y"
                 { (yyval.stmt) = ast_goto((yyvsp[0].text)); }
#line 3204 "src/parser.tab.c"
    break;

  case 112: /* gosub_statement: GOSUB IDENT  */
#line 741 "src/parser.y"
                  { (yyval.stmt) = ast_gosub((yyvsp[0].text)); }
#line 3210 "src/parser.tab.c"
    break;

  case 113: /* break_statement: BREAK  */
#line 745 "src/parser.y"
            { (yyval.stmt) = ast_break(); }
#line 3216 "src/parser.tab.c"
    break;

  case 114: /* continue_statement: CONTINUE  */
#line 749 "src/parser.y"
               { (yyval.stmt) = ast_continue(); }
#line 3222 "src/parser.tab.c"
    break;

  case 115: /* if_statement: IF expression THEN NEWLINE statement_list if_block_tail  */
#line 753 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 3231 "src/parser.tab.c"
    break;

  case 116: /* if_statement: IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 757 "src/parser.y"
                                                                 {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 3240 "src/parser.tab.c"
    break;

  case 117: /* if_block_tail: END IF NEWLINE  */
#line 764 "src/parser.y"
                     {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 3248 "src/parser.tab.c"
    break;

  case 118: /* if_block_tail: ELSE inline_statement NEWLINE  */
#line 767 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 3256 "src/parser.tab.c"
    break;

  case 119: /* if_block_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 770 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 3264 "src/parser.tab.c"
    break;

  case 120: /* if_inline_tail: %empty  */
#line 776 "src/parser.y"
                                   {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 3272 "src/parser.tab.c"
    break;

  case 121: /* if_inline_tail: ELSE inline_statement NEWLINE  */
#line 779 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 3280 "src/parser.tab.c"
    break;

  case 122: /* if_inline_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 782 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 3288 "src/parser.tab.c"
    break;

  case 123: /* inline_statement: assignment  */
#line 788 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3294 "src/parser.tab.c"
    break;

  case 124: /* inline_statement: print_statement  */
#line 789 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3300 "src/parser.tab.c"
    break;

  case 125: /* inline_statement: call_statement  */
#line 790 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3306 "src/parser.tab.c"
    break;

  case 126: /* inline_statement: use_statement  */
#line 791 "src/parser.y"
                    { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3312 "src/parser.tab.c"
    break;

  case 127: /* inline_statement: on_error_statement  */
#line 792 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3318 "src/parser.tab.c"
    break;

  case 128: /* inline_statement: error_statement  */
#line 793 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3324 "src/parser.tab.c"
    break;

  case 129: /* inline_statement: return_statement  */
#line 794 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3330 "src/parser.tab.c"
    break;

  case 130: /* inline_statement: goto_statement  */
#line 795 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3336 "src/parser.tab.c"
    break;

  case 131: /* inline_statement: gosub_statement  */
#line 796 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3342 "src/parser.tab.c"
    break;

  case 132: /* inline_statement: break_statement  */
#line 797 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3348 "src/parser.tab.c"
    break;

  case 133: /* inline_statement: continue_statement  */
#line 798 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3354 "src/parser.tab.c"
    break;

  case 134: /* expression: or_expression  */
#line 802 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 3360 "src/parser.tab.c"
    break;

  case 135: /* or_expression: and_expression  */
#line 806 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 3366 "src/parser.tab.c"
    break;

  case 136: /* or_expression: or_expression OR and_expression  */
#line 807 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_binary(copy_const("or"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3372 "src/parser.tab.c"
    break;

  case 137: /* and_expression: comparison_expression  */
#line 811 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 3378 "src/parser.tab.c"
    break;

  case 138: /* and_expression: and_expression AND comparison_expression  */
#line 812 "src/parser.y"
                                               { (yyval.expr) = expr_at(ast_binary(copy_const("and"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3384 "src/parser.tab.c"
    break;

  case 139: /* comparison_expression: additive_expression  */
#line 816 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 3390 "src/parser.tab.c"
    break;

  case 140: /* comparison_expression: additive_expression comparison_operator additive_expression  */
#line 817 "src/parser.y"
                                                                  { (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3396 "src/parser.tab.c"
    break;

  case 141: /* comparison_expression: additive_expression modifier comparison_operator additive_expression  */
#line 818 "src/parser.y"
                                                                           {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            yyerror("modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 3408 "src/parser.tab.c"
    break;

  case 142: /* additive_expression: multiplicative_expression  */
#line 828 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 3414 "src/parser.tab.c"
    break;

  case 143: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 829 "src/parser.y"
                                                         { (yyval.expr) = expr_at(ast_binary(copy_const("+"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3420 "src/parser.tab.c"
    break;

  case 144: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 830 "src/parser.y"
                                                          { (yyval.expr) = expr_at(ast_binary(copy_const("-"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3426 "src/parser.tab.c"
    break;

  case 145: /* multiplicative_expression: unary_expression  */
#line 834 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 3432 "src/parser.tab.c"
    break;

  case 146: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 835 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("*"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3438 "src/parser.tab.c"
    break;

  case 147: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 836 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_binary(copy_const("/"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3444 "src/parser.tab.c"
    break;

  case 148: /* unary_expression: postfix_expression  */
#line 840 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 3450 "src/parser.tab.c"
    break;

  case 149: /* unary_expression: NOT unary_expression  */
#line 841 "src/parser.y"
                           { (yyval.expr) = expr_at(ast_unary(copy_const("not"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3456 "src/parser.tab.c"
    break;

  case 150: /* unary_expression: MINUS unary_expression  */
#line 842 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_unary(copy_const("-"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3462 "src/parser.tab.c"
    break;

  case 151: /* postfix_expression: primary  */
#line 846 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 3468 "src/parser.tab.c"
    break;

  case 152: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 847 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 3474 "src/parser.tab.c"
    break;

  case 153: /* postfix_expression: postfix_expression DOT IDENT  */
#line 848 "src/parser.y"
                                   { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3480 "src/parser.tab.c"
    break;

  case 154: /* comparison_operator: OP_EQ  */
#line 852 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 3486 "src/parser.tab.c"
    break;

  case 155: /* comparison_operator: OP_NE  */
#line 853 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 3492 "src/parser.tab.c"
    break;

  case 156: /* comparison_operator: OP_GT  */
#line 854 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 3498 "src/parser.tab.c"
    break;

  case 157: /* comparison_operator: OP_LT  */
#line 855 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 3504 "src/parser.tab.c"
    break;

  case 158: /* comparison_operator: OP_GE  */
#line 856 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 3510 "src/parser.tab.c"
    break;

  case 159: /* comparison_operator: OP_LE  */
#line 857 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 3516 "src/parser.tab.c"
    break;

  case 160: /* comparison_operator: OP_NGT  */
#line 858 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 3522 "src/parser.tab.c"
    break;

  case 161: /* comparison_operator: OP_NLT  */
#line 859 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 3528 "src/parser.tab.c"
    break;

  case 162: /* comparison_operator: OP_NGE  */
#line 860 "src/parser.y"
             { (yyval.text) = copy_const("!>="); }
#line 3534 "src/parser.tab.c"
    break;

  case 163: /* comparison_operator: OP_NLE  */
#line 861 "src/parser.y"
             { (yyval.text) = copy_const("!<="); }
#line 3540 "src/parser.tab.c"
    break;

  case 164: /* primary: NUMBER  */
#line 865 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].number)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3546 "src/parser.tab.c"
    break;

  case 165: /* primary: duration_terms  */
#line 866 "src/parser.y"
                     { (yyval.expr) = expr_at(ast_duration((yyvsp[0].duration)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3552 "src/parser.tab.c"
    break;

  case 166: /* primary: STRING  */
#line 867 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3558 "src/parser.tab.c"
    break;

  case 167: /* primary: variable_name ident_suffix  */
#line 868 "src/parser.y"
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
#line 3574 "src/parser.tab.c"
    break;

  case 168: /* primary: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 879 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.expr) = expr_at(ast_qualified_call(library, name, (yyvsp[-1].expr_list)), (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 3585 "src/parser.tab.c"
    break;

  case 169: /* primary: ERROR_VALUE  */
#line 885 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_ident(copy_const("error")), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3591 "src/parser.tab.c"
    break;

  case 170: /* primary: TRUE  */
#line 886 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3597 "src/parser.tab.c"
    break;

  case 171: /* primary: FALSE  */
#line 887 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3603 "src/parser.tab.c"
    break;

  case 172: /* primary: NOTHING  */
#line 888 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3609 "src/parser.tab.c"
    break;

  case 173: /* primary: UNKNOWN_VALUE  */
#line 889 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3615 "src/parser.tab.c"
    break;

  case 174: /* primary: LPAREN expression RPAREN  */
#line 890 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); }
#line 3621 "src/parser.tab.c"
    break;

  case 175: /* primary: LBRACKET optional_newlines RBRACKET  */
#line 891 "src/parser.y"
                                          { (yyval.expr) = expr_at(ast_array(ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 3627 "src/parser.tab.c"
    break;

  case 176: /* primary: LBRACKET optional_newlines array_argument_list optional_newlines RBRACKET  */
#line 892 "src/parser.y"
                                                                                { (yyval.expr) = expr_at(ast_array((yyvsp[-2].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 3633 "src/parser.tab.c"
    break;

  case 177: /* primary: LBRACE optional_newlines RBRACE  */
#line 893 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_record(ast_record_field_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 3639 "src/parser.tab.c"
    break;

  case 178: /* primary: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 894 "src/parser.y"
                                                                          { (yyval.expr) = expr_at(ast_record((yyvsp[-2].record_field_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 3645 "src/parser.tab.c"
    break;

  case 179: /* ident_suffix: %empty  */
#line 898 "src/parser.y"
                          {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_NONE;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 3655 "src/parser.tab.c"
    break;

  case 180: /* ident_suffix: LPAREN argument_list_opt RPAREN  */
#line 903 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 3665 "src/parser.tab.c"
    break;

  case 181: /* ident_suffix: DOT IDENT ident_dot_suffix  */
#line 908 "src/parser.y"
                                 {
        (yyval.ident_suffix) = (yyvsp[0].ident_suffix);
        (yyval.ident_suffix).name = (yyvsp[-1].text);
      }
#line 3674 "src/parser.tab.c"
    break;

  case 182: /* ident_dot_suffix: %empty  */
#line 915 "src/parser.y"
             {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_FIELD;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 3684 "src/parser.tab.c"
    break;

  case 183: /* ident_dot_suffix: LPAREN argument_list_opt RPAREN  */
#line 920 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_QUALIFIED_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 3694 "src/parser.tab.c"
    break;

  case 184: /* duration_terms: NUMBER IDENT  */
#line 928 "src/parser.y"
                   {
        AstDuration duration = {0};
        (yyval.duration) = duration_add_unit(duration, (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 3703 "src/parser.tab.c"
    break;

  case 185: /* duration_terms: duration_terms NUMBER IDENT  */
#line 932 "src/parser.y"
                                  {
        (yyval.duration) = duration_add_unit((yyvsp[-2].duration), (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 3711 "src/parser.tab.c"
    break;

  case 186: /* argument_list_opt: %empty  */
#line 938 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 3717 "src/parser.tab.c"
    break;

  case 187: /* argument_list_opt: argument_list  */
#line 939 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 3723 "src/parser.tab.c"
    break;

  case 188: /* argument_list: expression  */
#line 943 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 3729 "src/parser.tab.c"
    break;

  case 189: /* argument_list: argument_list COMMA expression  */
#line 944 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 3735 "src/parser.tab.c"
    break;

  case 190: /* array_argument_list: expression  */
#line 948 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 3741 "src/parser.tab.c"
    break;

  case 191: /* array_argument_list: array_argument_list COMMA optional_newlines expression  */
#line 949 "src/parser.y"
                                                             { (yyval.expr_list) = ast_expr_list_append((yyvsp[-3].expr_list), (yyvsp[0].expr)); }
#line 3747 "src/parser.tab.c"
    break;

  case 192: /* parameter_list_opt: %empty  */
#line 953 "src/parser.y"
             { (yyval.name_list) = ast_name_list_empty(); }
#line 3753 "src/parser.tab.c"
    break;

  case 193: /* parameter_list_opt: parameter_list  */
#line 954 "src/parser.y"
                     { (yyval.name_list) = (yyvsp[0].name_list); }
#line 3759 "src/parser.tab.c"
    break;

  case 194: /* parameter_list: IDENT  */
#line 958 "src/parser.y"
            { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 3765 "src/parser.tab.c"
    break;

  case 195: /* parameter_list: parameter_list COMMA IDENT  */
#line 959 "src/parser.y"
                                 { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 3771 "src/parser.tab.c"
    break;

  case 196: /* record_field_list: IDENT OP_EQ expression  */
#line 963 "src/parser.y"
                             { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 3777 "src/parser.tab.c"
    break;

  case 197: /* record_field_list: IDENT COLON expression  */
#line 964 "src/parser.y"
                             { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 3783 "src/parser.tab.c"
    break;

  case 198: /* record_field_list: record_field_list COMMA optional_newlines IDENT OP_EQ expression  */
#line 965 "src/parser.y"
                                                                       { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 3789 "src/parser.tab.c"
    break;

  case 199: /* record_field_list: record_field_list COMMA optional_newlines IDENT COLON expression  */
#line 966 "src/parser.y"
                                                                       { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 3795 "src/parser.tab.c"
    break;


#line 3799 "src/parser.tab.c"

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

#line 974 "src/parser.y"


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
