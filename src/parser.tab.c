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
  YYSYMBOL_FOR = 25,                       /* FOR  */
  YYSYMBOL_TO = 26,                        /* TO  */
  YYSYMBOL_IN = 27,                        /* IN  */
  YYSYMBOL_EACH = 28,                      /* EACH  */
  YYSYMBOL_WHILE = 29,                     /* WHILE  */
  YYSYMBOL_CONSIDER = 30,                  /* CONSIDER  */
  YYSYMBOL_BREAK = 31,                     /* BREAK  */
  YYSYMBOL_CONTINUE = 32,                  /* CONTINUE  */
  YYSYMBOL_FUNCTION = 33,                  /* FUNCTION  */
  YYSYMBOL_RETURN = 34,                    /* RETURN  */
  YYSYMBOL_GOTO = 35,                      /* GOTO  */
  YYSYMBOL_GOSUB = 36,                     /* GOSUB  */
  YYSYMBOL_WATCH = 37,                     /* WATCH  */
  YYSYMBOL_WITHOUT = 38,                   /* WITHOUT  */
  YYSYMBOL_WATCHERS = 39,                  /* WATCHERS  */
  YYSYMBOL_ON = 40,                        /* ON  */
  YYSYMBOL_RESUME = 41,                    /* RESUME  */
  YYSYMBOL_NEXT = 42,                      /* NEXT  */
  YYSYMBOL_STOP = 43,                      /* STOP  */
  YYSYMBOL_ERROR_VALUE = 44,               /* ERROR_VALUE  */
  YYSYMBOL_MODIFIER = 45,                  /* MODIFIER  */
  YYSYMBOL_PROGRAM = 46,                   /* PROGRAM  */
  YYSYMBOL_LIBRARY = 47,                   /* LIBRARY  */
  YYSYMBOL_LOAD = 48,                      /* LOAD  */
  YYSYMBOL_USE = 49,                       /* USE  */
  YYSYMBOL_EXPORT = 50,                    /* EXPORT  */
  YYSYMBOL_OP_EQ = 51,                     /* OP_EQ  */
  YYSYMBOL_OP_NE = 52,                     /* OP_NE  */
  YYSYMBOL_OP_GT = 53,                     /* OP_GT  */
  YYSYMBOL_OP_LT = 54,                     /* OP_LT  */
  YYSYMBOL_OP_GE = 55,                     /* OP_GE  */
  YYSYMBOL_OP_LE = 56,                     /* OP_LE  */
  YYSYMBOL_OP_NGT = 57,                    /* OP_NGT  */
  YYSYMBOL_OP_NLT = 58,                    /* OP_NLT  */
  YYSYMBOL_OP_NGE = 59,                    /* OP_NGE  */
  YYSYMBOL_OP_NLE = 60,                    /* OP_NLE  */
  YYSYMBOL_PLUS = 61,                      /* PLUS  */
  YYSYMBOL_MINUS = 62,                     /* MINUS  */
  YYSYMBOL_STAR = 63,                      /* STAR  */
  YYSYMBOL_SLASH = 64,                     /* SLASH  */
  YYSYMBOL_LPAREN = 65,                    /* LPAREN  */
  YYSYMBOL_MOD_LPAREN = 66,                /* MOD_LPAREN  */
  YYSYMBOL_RPAREN = 67,                    /* RPAREN  */
  YYSYMBOL_LBRACKET = 68,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 69,                  /* RBRACKET  */
  YYSYMBOL_LBRACE = 70,                    /* LBRACE  */
  YYSYMBOL_RBRACE = 71,                    /* RBRACE  */
  YYSYMBOL_COMMA = 72,                     /* COMMA  */
  YYSYMBOL_COLON = 73,                     /* COLON  */
  YYSYMBOL_NEWLINE = 74,                   /* NEWLINE  */
  YYSYMBOL_IF_WITHOUT_ELSE = 75,           /* IF_WITHOUT_ELSE  */
  YYSYMBOL_NO_DOT = 76,                    /* NO_DOT  */
  YYSYMBOL_DOT = 77,                       /* DOT  */
  YYSYMBOL_YYACCEPT = 78,                  /* $accept  */
  YYSYMBOL_program = 79,                   /* program  */
  YYSYMBOL_statement_list = 80,            /* statement_list  */
  YYSYMBOL_statement = 81,                 /* statement  */
  YYSYMBOL_assignment = 82,                /* assignment  */
  YYSYMBOL_lvalue = 83,                    /* lvalue  */
  YYSYMBOL_variable_name = 84,             /* variable_name  */
  YYSYMBOL_modifier = 85,                  /* modifier  */
  YYSYMBOL_comparison_lens = 86,           /* comparison_lens  */
  YYSYMBOL_87_1 = 87,                      /* $@1  */
  YYSYMBOL_modifier_name = 88,             /* modifier_name  */
  YYSYMBOL_modifier_word = 89,             /* modifier_word  */
  YYSYMBOL_print_statement = 90,           /* print_statement  */
  YYSYMBOL_call_statement = 91,            /* call_statement  */
  YYSYMBOL_with_lock_statement = 92,       /* with_lock_statement  */
  YYSYMBOL_for_each_statement = 93,        /* for_each_statement  */
  YYSYMBOL_while_statement = 94,           /* while_statement  */
  YYSYMBOL_consider_statement = 95,        /* consider_statement  */
  YYSYMBOL_consider_branch_list = 96,      /* consider_branch_list  */
  YYSYMBOL_consider_else_opt = 97,         /* consider_else_opt  */
  YYSYMBOL_consider_statement_list = 98,   /* consider_statement_list  */
  YYSYMBOL_consider_body_statement = 99,   /* consider_body_statement  */
  YYSYMBOL_function_statement = 100,       /* function_statement  */
  YYSYMBOL_modifier_statement = 101,       /* modifier_statement  */
  YYSYMBOL_program_statement = 102,        /* program_statement  */
  YYSYMBOL_library_statement = 103,        /* library_statement  */
  YYSYMBOL_use_statement = 104,            /* use_statement  */
  YYSYMBOL_modifier_signature = 105,       /* modifier_signature  */
  YYSYMBOL_modifier_context = 106,         /* modifier_context  */
  YYSYMBOL_watch_statement = 107,          /* watch_statement  */
  YYSYMBOL_watch_target_list = 108,        /* watch_target_list  */
  YYSYMBOL_watch_target_path = 109,        /* watch_target_path  */
  YYSYMBOL_without_watchers_statement = 110, /* without_watchers_statement  */
  YYSYMBOL_on_error_statement = 111,       /* on_error_statement  */
  YYSYMBOL_error_statement = 112,          /* error_statement  */
  YYSYMBOL_return_statement = 113,         /* return_statement  */
  YYSYMBOL_label_statement = 114,          /* label_statement  */
  YYSYMBOL_goto_statement = 115,           /* goto_statement  */
  YYSYMBOL_gosub_statement = 116,          /* gosub_statement  */
  YYSYMBOL_break_statement = 117,          /* break_statement  */
  YYSYMBOL_continue_statement = 118,       /* continue_statement  */
  YYSYMBOL_if_statement = 119,             /* if_statement  */
  YYSYMBOL_if_block_tail = 120,            /* if_block_tail  */
  YYSYMBOL_if_inline_tail = 121,           /* if_inline_tail  */
  YYSYMBOL_inline_statement = 122,         /* inline_statement  */
  YYSYMBOL_expression = 123,               /* expression  */
  YYSYMBOL_or_expression = 124,            /* or_expression  */
  YYSYMBOL_and_expression = 125,           /* and_expression  */
  YYSYMBOL_comparison_expression = 126,    /* comparison_expression  */
  YYSYMBOL_additive_expression = 127,      /* additive_expression  */
  YYSYMBOL_multiplicative_expression = 128, /* multiplicative_expression  */
  YYSYMBOL_unary_expression = 129,         /* unary_expression  */
  YYSYMBOL_postfix_expression = 130,       /* postfix_expression  */
  YYSYMBOL_comparison_operator = 131,      /* comparison_operator  */
  YYSYMBOL_primary = 132,                  /* primary  */
  YYSYMBOL_ident_suffix = 133,             /* ident_suffix  */
  YYSYMBOL_ident_dot_suffix = 134,         /* ident_dot_suffix  */
  YYSYMBOL_duration_terms = 135,           /* duration_terms  */
  YYSYMBOL_argument_list_opt = 136,        /* argument_list_opt  */
  YYSYMBOL_argument_list = 137,            /* argument_list  */
  YYSYMBOL_array_argument_list = 138,      /* array_argument_list  */
  YYSYMBOL_parameter_list_opt = 139,       /* parameter_list_opt  */
  YYSYMBOL_parameter_list = 140,           /* parameter_list  */
  YYSYMBOL_record_field_list = 141,        /* record_field_list  */
  YYSYMBOL_field_policy = 142,             /* field_policy  */
  YYSYMBOL_optional_newlines = 143         /* optional_newlines  */
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
#define YYLAST   1438

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  78
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  66
/* YYNRULES -- Number of rules.  */
#define YYNRULES  208
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  447

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   332


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
      75,    76,    77
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   458,   458,   462,   463,   464,   468,   469,   470,   471,
     472,   473,   474,   475,   476,   477,   478,   479,   480,   481,
     482,   483,   484,   485,   486,   487,   488,   489,   490,   494,
     495,   505,   506,   507,   511,   512,   513,   517,   521,   521,
     527,   528,   532,   533,   534,   535,   539,   543,   544,   550,
     563,   575,   578,   584,   590,   596,   599,   605,   606,   610,
     611,   612,   616,   617,   618,   619,   620,   621,   622,   623,
     624,   625,   626,   627,   628,   629,   630,   631,   632,   633,
     634,   635,   636,   637,   638,   642,   648,   651,   657,   663,
     669,   670,   671,   672,   673,   684,   698,   699,   703,   707,
     710,   716,   717,   721,   722,   726,   732,   733,   734,   738,
     742,   743,   747,   751,   755,   759,   763,   767,   771,   778,
     781,   784,   790,   793,   796,   802,   803,   804,   805,   806,
     807,   808,   809,   810,   811,   812,   816,   820,   821,   825,
     826,   830,   831,   832,   835,   845,   846,   847,   851,   852,
     853,   857,   858,   859,   863,   864,   865,   869,   870,   871,
     872,   873,   874,   875,   876,   877,   878,   882,   883,   884,
     885,   896,   902,   903,   904,   905,   906,   907,   908,   909,
     910,   911,   915,   920,   925,   932,   937,   945,   949,   955,
     956,   960,   961,   965,   966,   970,   971,   975,   976,   980,
     981,   982,   983,   984,   985,   993,  1014,  1030,  1031
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
  "WITH", "FOR", "TO", "IN", "EACH", "WHILE", "CONSIDER", "BREAK",
  "CONTINUE", "FUNCTION", "RETURN", "GOTO", "GOSUB", "WATCH", "WITHOUT",
  "WATCHERS", "ON", "RESUME", "NEXT", "STOP", "ERROR_VALUE", "MODIFIER",
  "PROGRAM", "LIBRARY", "LOAD", "USE", "EXPORT", "OP_EQ", "OP_NE", "OP_GT",
  "OP_LT", "OP_GE", "OP_LE", "OP_NGT", "OP_NLT", "OP_NGE", "OP_NLE",
  "PLUS", "MINUS", "STAR", "SLASH", "LPAREN", "MOD_LPAREN", "RPAREN",
  "LBRACKET", "RBRACKET", "LBRACE", "RBRACE", "COMMA", "COLON", "NEWLINE",
  "IF_WITHOUT_ELSE", "NO_DOT", "DOT", "$accept", "program",
  "statement_list", "statement", "assignment", "lvalue", "variable_name",
  "modifier", "comparison_lens", "$@1", "modifier_name", "modifier_word",
  "print_statement", "call_statement", "with_lock_statement",
  "for_each_statement", "while_statement", "consider_statement",
  "consider_branch_list", "consider_else_opt", "consider_statement_list",
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
  "record_field_list", "field_policy", "optional_newlines", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-332)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -332,    27,   499,  -332,   -18,     8,  1368,  -332,  1368,    87,
      84,  1368,  1368,  -332,  -332,   111,  1368,   115,   126,   110,
     103,   104,  -332,    32,   250,   151,   167,    53,   160,   131,
    -332,  -332,   118,   -12,   123,   128,   133,  -332,  -332,  -332,
    -332,  -332,  -332,  -332,  -332,   143,  -332,  -332,   156,   159,
     161,   171,   172,   176,   178,   181,  -332,  1368,  1368,   180,
    -332,  -332,   192,  -332,  -332,  -332,  -332,  1368,  -332,  1368,
    1368,  -332,  -332,   -35,   204,   236,   238,  -332,   284,   141,
    -332,    -7,  -332,   260,  -332,   200,   242,   266,   197,   201,
     209,  -332,  -332,  -332,    29,  -332,   -43,   203,   207,   100,
     274,  -332,  -332,  -332,  -332,  -332,   202,  -332,   259,   221,
     213,   289,  -332,   290,  -332,   250,  -332,  1368,   294,  1368,
     291,   253,  -332,  -332,  -332,  -332,  -332,  -332,  -332,  -332,
    -332,  -332,  -332,  -332,  -332,   239,   233,   241,  -332,  1368,
    -332,  -332,   243,    64,    21,  1368,   305,  -332,  1251,  1368,
    1368,  -332,  -332,  -332,  -332,  -332,  -332,  -332,  -332,  -332,
    -332,  1368,  1368,  -332,   333,   333,  1368,  1368,  1368,  1368,
     307,   308,  1368,  1368,   287,  -332,   309,   311,   -56,    29,
    -332,   313,  -332,   314,   279,  -332,   257,   311,  -332,   319,
     311,  -332,   320,   321,   299,  -332,  -332,   258,  -332,  1368,
    -332,  1368,  -332,   261,  -332,  -332,  -332,  -332,   262,    52,
    -332,   276,   264,   267,  -332,  -332,  -332,  -332,  -332,  -332,
    -332,  -332,  -332,  -332,  -332,  -332,  -332,   277,   238,  -332,
     141,   141,   345,  1368,  1368,   150,  -332,  -332,   288,  -332,
    -332,   302,   282,  1368,   546,  1368,    49,  -332,   304,   300,
     292,   203,   593,  -332,   640,  -332,  -332,  1368,   306,  -332,
     322,   315,   687,  -332,  -332,   319,  -332,  -332,  -332,  -332,
    -332,    70,  1368,   372,  1368,  -332,    75,  -332,  1368,  -332,
     452,   369,   323,   150,   150,  -332,   324,  -332,   325,   366,
     386,  1368,   326,   390,   327,   402,  -332,   370,   371,   341,
    -332,  -332,   336,   364,   339,   360,  -332,  -332,  1368,   347,
    -332,     3,  -332,   348,  1280,   409,  -332,  1326,  -332,  -332,
    -332,   734,  -332,   346,   349,   408,  -332,   350,  -332,  -332,
     781,   352,   353,  -332,   828,  -332,   355,  -332,  -332,  -332,
     358,    80,  -332,  -332,   359,   361,  -332,   365,   875,   396,
     922,  -332,  -332,   367,   969,  -332,  1016,   395,  -332,  -332,
     397,  1063,  -332,  1110,  1368,  1368,   372,  1368,  1157,  -332,
    -332,  1204,  -332,   416,   373,   418,   969,  -332,  -332,   374,
     375,   376,  -332,  -332,  -332,  -332,  -332,  -332,  -332,  -332,
    -332,   378,  -332,  -332,   380,   381,   383,   384,   389,   391,
     393,   398,  -332,   412,   399,   400,   423,   401,  -332,  -332,
     403,  -332,   462,   466,   404,  -332,   405,   969,  -332,  -332,
    -332,  -332,  -332,  -332,  -332,  -332,  -332,  -332,  -332,  -332,
     406,  -332,  -332,   417,   419,   422,   430,   431,  -332,  -332,
    -332,  -332,  -332,  1368,  -332,  -332,  -332
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
       0,     0,     0,     0,     0,     0,    28,   189,   189,   167,
      34,   169,     0,   173,   174,   175,   176,     0,   172,     0,
       0,   207,   207,   182,     0,   136,   137,   139,   141,   145,
     148,   151,   154,   168,    46,     0,     0,     0,     0,     0,
       0,   111,   113,   114,     0,   103,     0,   101,     0,     0,
       0,   109,    42,    44,    43,    45,    96,    40,     0,     0,
       0,    91,    93,    90,    92,     0,     6,     0,     0,     0,
       0,     0,   112,     7,     8,    17,    20,    21,    22,    23,
      24,    25,    26,    27,   191,     0,   190,     0,   187,   189,
     152,   153,     0,     0,     0,   189,     0,   170,     0,     0,
       0,   157,   158,   159,   160,   161,   162,   163,   164,   165,
     166,     0,     0,    38,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     3,     0,   195,     0,     0,
       3,     0,     3,     0,     0,   108,     0,   195,    41,     0,
     195,     3,     0,     0,     0,    29,    37,     0,    33,     0,
      47,     0,    48,     0,   177,   178,   208,   193,   207,     0,
     180,   207,     0,   185,     3,   125,    31,   126,   127,   128,
     129,   130,   131,   132,   133,   134,   135,     0,   138,   140,
     146,   147,     0,     0,     0,   142,   149,   150,     0,   156,
     188,     0,     0,     0,     0,     0,    57,   197,     0,   196,
       0,   102,     0,   104,     0,   106,   107,   189,     0,    98,
       0,     0,     0,    95,    94,     0,    32,    30,   192,   171,
     207,     0,     0,     0,     0,   207,     0,   183,   189,   184,
       0,   122,     0,   144,   143,   155,     0,     3,     0,    35,
       0,     0,     0,     0,     0,     0,     3,    35,    35,     0,
      97,     3,     0,    35,     0,     0,   179,   199,   205,     0,
     200,     0,   181,     0,     0,    35,   117,     0,   118,    39,
       3,     0,     3,     0,     0,     0,    59,     0,     3,   198,
       0,     0,     0,    49,     0,     3,     0,     3,   194,   206,
       0,     0,   186,     3,     0,     0,     3,     0,     0,    35,
       0,    53,    59,     0,    58,    54,     0,    35,   100,   105,
      35,     0,    89,     0,     0,     0,     0,     0,     0,   120,
     119,     0,   123,    35,     0,    35,    55,    59,    60,     0,
       0,     0,    65,    66,    67,    68,    61,    69,    70,    71,
      72,     0,    74,    75,     0,     0,     0,     0,     0,     0,
       0,     0,    84,    35,     0,     0,    35,    35,   201,   202,
       0,   203,    35,    35,     0,    51,     0,    56,    62,    63,
      64,    73,    76,    77,    78,    79,    80,    81,    82,    83,
       0,    99,    86,     0,     0,     0,     0,     0,    50,    52,
      85,    88,    87,     0,   121,   124,   204
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -332,  -332,   116,  -332,  -145,  -332,    -1,   428,  -332,  -332,
    -332,   410,  -144,  -140,  -331,  -328,  -316,  -313,  -332,  -332,
    -267,  -332,  -310,  -301,  -256,  -255,  -135,   394,   245,  -249,
     420,   332,  -236,  -134,  -129,  -128,  -226,  -127,  -120,  -116,
    -114,  -218,  -332,  -332,  -158,    -6,  -332,   363,   368,  -154,
      60,   -45,  -332,    59,  -332,  -332,  -332,  -332,   -49,  -332,
    -332,   -30,  -332,  -332,   153,   -57
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,     2,    31,    32,    33,    73,   121,   165,   232,
     106,   107,    35,    36,    37,    38,    39,    40,   246,   293,
     354,   386,    41,    42,    43,    44,    45,   108,   260,    46,
      96,    97,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,   316,   318,   227,   134,    75,    76,    77,    78,
      79,    80,    81,   166,    82,   147,   279,    83,   135,   136,
     208,   248,   249,   211,   309,   143
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      74,    34,    84,   215,   217,    88,    89,   341,   218,   137,
      91,   250,   235,   219,   220,   144,   179,   101,    95,   221,
     222,   223,   140,   382,   141,   209,   383,     3,   224,   179,
     145,   180,   225,    60,   226,    59,    60,    61,   384,   117,
      62,   385,   146,     7,   387,   382,     7,    57,   383,    63,
      64,    65,    66,   388,   118,    67,   119,   111,   112,   291,
     384,   169,   292,   385,   142,   120,   387,    59,    60,    61,
     170,    22,    62,    58,    22,   388,    68,   206,     7,   283,
     284,    63,    64,    65,    66,   376,   382,    67,    86,   383,
     203,    85,   210,    95,    69,   206,   212,    70,   389,   390,
      71,   384,    72,   272,   385,   392,    22,   387,    68,   100,
     417,   195,    87,   197,    60,    90,   388,   273,   393,    92,
     389,   390,   236,   237,     7,   274,    69,   392,   397,    70,
      93,   365,    71,   205,    72,   183,   402,   207,   206,   306,
     393,   184,    98,   185,   206,   366,   312,   216,    99,   206,
     397,   271,    22,   367,   276,   109,   344,   258,   402,   347,
     261,   389,   390,   238,   113,   114,   241,   242,   392,   215,
     217,   110,   215,   217,   218,    94,   115,   218,    95,   219,
     220,   393,   219,   220,   138,   221,   222,   223,   221,   222,
     223,   397,   116,   267,   224,   268,   122,   224,   225,   402,
     226,   225,   123,   226,   167,   168,   102,   124,   299,   379,
     380,   161,   162,   305,   381,   148,   103,   125,   311,   391,
     394,   230,   231,   233,   234,   395,   396,   398,   104,   313,
     126,   379,   380,   127,   399,   128,   381,   288,   400,   290,
     401,   391,   394,    34,   105,   129,   130,   395,   396,   398,
     131,    34,   132,    34,   102,   133,   399,   139,   149,   150,
     400,    34,   401,   171,   103,   172,   307,   187,   310,   173,
     174,   175,   379,   380,   177,   176,   104,   381,   186,    34,
     181,   182,   391,   394,   189,   325,   190,   191,   395,   396,
     398,   244,   105,   192,   193,   198,   252,   399,   254,   338,
     196,   400,   339,   401,   199,   201,   200,   262,   202,   213,
     204,   239,   240,   216,   243,   247,   216,   253,   255,   245,
      34,   256,   257,   259,   265,   263,   264,   266,   269,    34,
     280,   277,   278,    34,   270,   151,   152,   153,   154,   155,
     156,   157,   158,   159,   160,   161,   162,    34,   275,    34,
     118,   281,   282,    34,   163,    34,   287,   285,   408,   409,
      34,   411,    34,    59,    60,    61,   296,    34,    62,   286,
      34,   294,   295,   300,     7,    34,   308,    63,    64,    65,
      66,   317,   302,    67,   151,   152,   153,   154,   155,   156,
     157,   158,   159,   160,   319,   323,   301,   324,   320,   322,
     326,   328,    22,   321,    68,   327,   329,   331,   333,   332,
     335,   336,   330,   337,   340,   342,    34,   334,   345,   353,
     351,   374,    69,   352,   355,    70,   358,   359,    71,   362,
      72,   364,   404,   369,   206,   370,   348,   446,   350,   372,
     414,   377,   405,   416,   356,   430,   434,   415,   418,   419,
     420,   361,   421,   363,   422,   423,     4,   424,   425,   368,
       5,     6,   371,   426,   314,   427,   315,   428,     8,   433,
     435,   436,   429,   431,   432,   437,     9,    10,   438,   439,
     440,    11,    12,    13,    14,    15,    16,    17,    18,    19,
      20,   441,    21,   442,    22,   443,    23,    24,    25,    26,
      27,    28,    29,     4,   444,   445,   164,     5,     6,   194,
     304,   251,   228,     7,   178,     8,   188,     0,   229,   410,
       0,     0,     0,     9,    10,     0,    30,     0,    11,    12,
      13,    14,    15,    16,    17,    18,    19,    20,     0,    21,
       0,    22,     0,    23,    24,    25,    26,    27,    28,    29,
       4,     0,     0,     0,     5,     6,     0,     0,     0,     0,
     289,     0,     8,     0,     0,     0,     0,     0,     0,     0,
       9,    10,     0,    30,     0,    11,    12,    13,    14,    15,
      16,    17,    18,    19,    20,     0,    21,     0,    22,     0,
      23,    24,    25,    26,    27,    28,    29,     4,     0,     0,
       0,     5,     6,     0,     0,     0,     0,   297,     0,     8,
       0,     0,     0,     0,     0,     0,     0,     9,    10,     0,
      30,     0,    11,    12,    13,    14,    15,    16,    17,    18,
      19,    20,     0,    21,     0,    22,     0,    23,    24,    25,
      26,    27,    28,    29,     4,     0,     0,     0,     5,     6,
       0,     0,     0,     0,   298,     0,     8,     0,     0,     0,
       0,     0,     0,     0,     9,    10,     0,    30,     0,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,     0,
      21,     0,    22,     0,    23,    24,    25,    26,    27,    28,
      29,     4,     0,     0,     0,     5,     6,     0,     0,     0,
       0,   303,     0,     8,     0,     0,     0,     0,     0,     0,
       0,     9,    10,     0,    30,     0,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,     0,    21,     0,    22,
       0,    23,    24,    25,    26,    27,    28,    29,     4,     0,
       0,     0,     5,     6,     0,     0,     0,     0,   349,     0,
       8,     0,     0,     0,     0,     0,     0,     0,     9,    10,
       0,    30,     0,    11,    12,    13,    14,    15,    16,    17,
      18,    19,    20,     0,    21,     0,    22,     0,    23,    24,
      25,    26,    27,    28,    29,     4,     0,     0,     0,     5,
       6,     0,     0,     0,     0,   357,     0,     8,     0,     0,
       0,     0,     0,     0,     0,     9,    10,     0,    30,     0,
      11,    12,    13,    14,    15,    16,    17,    18,    19,    20,
       0,    21,     0,    22,     0,    23,    24,    25,    26,    27,
      28,    29,     4,     0,     0,     0,     5,     6,     0,     0,
       0,     0,   360,     0,     8,     0,     0,     0,     0,     0,
       0,     0,     9,    10,     0,    30,     0,    11,    12,    13,
      14,    15,    16,    17,    18,    19,    20,     0,    21,     0,
      22,     0,    23,    24,    25,    26,    27,    28,    29,     4,
       0,     0,     0,     5,     6,     0,     0,     0,     0,   373,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
      10,     0,    30,     0,    11,    12,    13,    14,    15,    16,
      17,    18,    19,    20,     0,    21,     0,    22,     0,    23,
      24,    25,    26,    27,    28,    29,     4,     0,     0,     0,
       5,     6,     0,     0,     0,     0,   375,     0,     8,     0,
       0,     0,     0,     0,     0,     0,     9,    10,     0,    30,
       0,    11,    12,    13,    14,    15,    16,    17,    18,    19,
      20,     0,    21,     0,    22,     0,    23,    24,    25,    26,
      27,    28,    29,     4,     0,     0,     0,     5,     6,     0,
       0,     0,     0,     7,     0,     8,     0,     0,     0,     0,
       0,     0,     0,     9,    10,     0,    30,     0,    11,    12,
      13,    14,    15,    16,    17,    18,    19,    20,     0,    21,
       0,    22,     0,    23,    24,    25,    26,    27,    28,    29,
       4,     0,     0,     0,     5,     6,     0,     0,     0,     0,
     403,     0,     8,     0,     0,     0,     0,     0,     0,     0,
       9,    10,     0,   378,     0,    11,    12,    13,    14,    15,
      16,    17,    18,    19,    20,     0,    21,     0,    22,     0,
      23,    24,    25,    26,    27,    28,    29,     4,     0,     0,
       0,     5,     6,     0,     0,     0,     0,   406,     0,     8,
       0,     0,     0,     0,     0,     0,     0,     9,    10,     0,
      30,     0,    11,    12,    13,    14,    15,    16,    17,    18,
      19,    20,     0,    21,     0,    22,     0,    23,    24,    25,
      26,    27,    28,    29,     4,     0,     0,     0,     5,     6,
       0,     0,     0,     0,   407,     0,     8,     0,     0,     0,
       0,     0,     0,     0,     9,    10,     0,    30,     0,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,     0,
      21,     0,    22,     0,    23,    24,    25,    26,    27,    28,
      29,     4,     0,     0,     0,     5,     6,     0,     0,     0,
       0,   412,     0,     8,     0,     0,     0,     0,     0,     0,
       0,     9,    10,     0,    30,     0,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,     0,    21,     0,    22,
       0,    23,    24,    25,    26,    27,    28,    29,     4,     0,
       0,     0,     5,     6,     0,     0,     0,     0,   413,     0,
       8,     0,     0,     0,     0,     0,     0,     0,     9,    10,
       0,    30,     0,    11,    12,    13,    14,    15,    16,    17,
      18,    19,    20,     0,    21,     0,    22,     0,    23,    24,
      25,    26,    27,    28,    29,     4,     0,     0,     0,     5,
       0,     0,     0,     0,     0,     7,     0,     8,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    30,     0,
       0,     0,    13,    14,     4,    16,    17,    18,     5,     0,
       0,    21,     0,    22,     7,    23,     8,     0,     0,    27,
      28,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    13,    14,     0,    16,    17,    18,     0,     0,     0,
      21,     0,    22,     0,    23,   214,     0,     0,    27,    28,
       4,     0,     0,     0,     5,     0,     0,     0,     0,     0,
       7,     0,     8,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   343,     0,     0,    13,    14,     0,
      16,    17,    18,     0,     0,     0,    21,     0,    22,     0,
      23,    59,    60,    61,    27,    28,    62,     0,     0,     0,
       0,     0,     7,     0,     0,    63,    64,    65,    66,     0,
       0,    67,     0,     0,     0,     0,     0,     0,     0,     0,
     346,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      22,     0,    68,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      69,     0,     0,    70,     0,     0,    71,     0,    72
};

static const yytype_int16 yycheck[] =
{
       6,     2,     8,   148,   148,    11,    12,     4,   148,    58,
      16,    67,   166,   148,   148,    72,    72,    23,    19,   148,
     148,   148,    67,   354,    69,     4,   354,     0,   148,    72,
      65,    74,   148,     4,   148,     3,     4,     5,   354,    51,
       8,   354,    77,    14,   354,   376,    14,    65,   376,    17,
      18,    19,    20,   354,    66,    23,    68,     4,     5,    10,
     376,    68,    13,   376,    70,    77,   376,     3,     4,     5,
      77,    42,     8,    65,    42,   376,    44,    74,    14,   233,
     234,    17,    18,    19,    20,   352,   417,    23,     4,   417,
     139,     4,    71,    94,    62,    74,   145,    65,   354,   354,
      68,   417,    70,    51,   417,   354,    42,   417,    44,    77,
     377,   117,    28,   119,     4,     4,   417,    65,   354,     4,
     376,   376,   167,   168,    14,    73,    62,   376,   354,    65,
       4,    51,    68,    69,    70,    35,   354,   143,    74,    69,
     376,    41,    39,    43,    74,    65,    71,   148,    44,    74,
     376,   208,    42,    73,   211,     4,   314,   187,   376,   317,
     190,   417,   417,   169,     4,     5,   172,   173,   417,   314,
     314,     4,   317,   317,   314,    65,    45,   317,   179,   314,
     314,   417,   317,   317,     4,   314,   314,   314,   317,   317,
     317,   417,    74,   199,   314,   201,    73,   317,   314,   417,
     314,   317,    74,   317,    63,    64,     4,    74,   257,   354,
     354,    61,    62,   270,   354,    11,    14,    74,   275,   354,
     354,   161,   162,   164,   165,   354,   354,   354,    26,   278,
      74,   376,   376,    74,   354,    74,   376,   243,   354,   245,
     354,   376,   376,   244,    42,    74,    74,   376,   376,   376,
      74,   252,    74,   254,     4,    74,   376,    65,    22,    21,
     376,   262,   376,     3,    14,    65,   272,    65,   274,    27,
       4,    74,   417,   417,    65,    74,    26,   417,     4,   280,
      77,    74,   417,   417,    25,   291,    65,    74,   417,   417,
     417,   175,    42,     4,     4,     4,   180,   417,   182,   305,
       6,   417,   308,   417,    51,    72,    67,   191,    67,     4,
      67,     4,     4,   314,    27,     4,   317,     4,     4,    10,
     321,    42,    65,     4,    25,     5,     5,    69,    67,   330,
     214,    67,    65,   334,    72,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,   348,    72,   350,
      66,    74,     7,   354,    70,   356,    74,    69,   364,   365,
     361,   367,   363,     3,     4,     5,    74,   368,     8,    67,
     371,    67,    72,    67,    14,   376,     4,    17,    18,    19,
      20,    12,    67,    23,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    71,    29,    74,    11,    74,    74,
      74,    74,    42,   287,    44,    15,     4,    37,    67,    38,
      74,    47,   296,    74,    67,    67,   417,   301,     9,    11,
      74,    25,    62,    74,    74,    65,    74,    74,    68,    74,
      70,    73,    37,    74,    74,    74,   320,   443,   322,    74,
      24,    74,    45,    25,   328,    33,    45,    74,    74,    74,
      74,   335,    74,   337,    74,    74,     4,    74,    74,   343,
       8,     9,   346,    74,    12,    74,    14,    74,    16,    46,
      67,     9,    74,    74,    74,     9,    24,    25,    74,    74,
      74,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    74,    40,    74,    42,    73,    44,    45,    46,    47,
      48,    49,    50,     4,    74,    74,    78,     8,     9,   115,
     265,   179,   149,    14,    94,    16,   106,    -1,   150,   366,
      -1,    -1,    -1,    24,    25,    -1,    74,    -1,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    -1,    40,
      -1,    42,    -1,    44,    45,    46,    47,    48,    49,    50,
       4,    -1,    -1,    -1,     8,     9,    -1,    -1,    -1,    -1,
      14,    -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      24,    25,    -1,    74,    -1,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    -1,    40,    -1,    42,    -1,
      44,    45,    46,    47,    48,    49,    50,     4,    -1,    -1,
      -1,     8,     9,    -1,    -1,    -1,    -1,    14,    -1,    16,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,    25,    -1,
      74,    -1,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    -1,    40,    -1,    42,    -1,    44,    45,    46,
      47,    48,    49,    50,     4,    -1,    -1,    -1,     8,     9,
      -1,    -1,    -1,    -1,    14,    -1,    16,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    24,    25,    -1,    74,    -1,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    -1,
      40,    -1,    42,    -1,    44,    45,    46,    47,    48,    49,
      50,     4,    -1,    -1,    -1,     8,     9,    -1,    -1,    -1,
      -1,    14,    -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    24,    25,    -1,    74,    -1,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    -1,    40,    -1,    42,
      -1,    44,    45,    46,    47,    48,    49,    50,     4,    -1,
      -1,    -1,     8,     9,    -1,    -1,    -1,    -1,    14,    -1,
      16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,    25,
      -1,    74,    -1,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    -1,    40,    -1,    42,    -1,    44,    45,
      46,    47,    48,    49,    50,     4,    -1,    -1,    -1,     8,
       9,    -1,    -1,    -1,    -1,    14,    -1,    16,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    24,    25,    -1,    74,    -1,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      -1,    40,    -1,    42,    -1,    44,    45,    46,    47,    48,
      49,    50,     4,    -1,    -1,    -1,     8,     9,    -1,    -1,
      -1,    -1,    14,    -1,    16,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    24,    25,    -1,    74,    -1,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    -1,    40,    -1,
      42,    -1,    44,    45,    46,    47,    48,    49,    50,     4,
      -1,    -1,    -1,     8,     9,    -1,    -1,    -1,    -1,    14,
      -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,
      25,    -1,    74,    -1,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    -1,    40,    -1,    42,    -1,    44,
      45,    46,    47,    48,    49,    50,     4,    -1,    -1,    -1,
       8,     9,    -1,    -1,    -1,    -1,    14,    -1,    16,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    24,    25,    -1,    74,
      -1,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    -1,    40,    -1,    42,    -1,    44,    45,    46,    47,
      48,    49,    50,     4,    -1,    -1,    -1,     8,     9,    -1,
      -1,    -1,    -1,    14,    -1,    16,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    24,    25,    -1,    74,    -1,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    -1,    40,
      -1,    42,    -1,    44,    45,    46,    47,    48,    49,    50,
       4,    -1,    -1,    -1,     8,     9,    -1,    -1,    -1,    -1,
      14,    -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      24,    25,    -1,    74,    -1,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    -1,    40,    -1,    42,    -1,
      44,    45,    46,    47,    48,    49,    50,     4,    -1,    -1,
      -1,     8,     9,    -1,    -1,    -1,    -1,    14,    -1,    16,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,    25,    -1,
      74,    -1,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    -1,    40,    -1,    42,    -1,    44,    45,    46,
      47,    48,    49,    50,     4,    -1,    -1,    -1,     8,     9,
      -1,    -1,    -1,    -1,    14,    -1,    16,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    24,    25,    -1,    74,    -1,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    -1,
      40,    -1,    42,    -1,    44,    45,    46,    47,    48,    49,
      50,     4,    -1,    -1,    -1,     8,     9,    -1,    -1,    -1,
      -1,    14,    -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    24,    25,    -1,    74,    -1,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    -1,    40,    -1,    42,
      -1,    44,    45,    46,    47,    48,    49,    50,     4,    -1,
      -1,    -1,     8,     9,    -1,    -1,    -1,    -1,    14,    -1,
      16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    24,    25,
      -1,    74,    -1,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    -1,    40,    -1,    42,    -1,    44,    45,
      46,    47,    48,    49,    50,     4,    -1,    -1,    -1,     8,
      -1,    -1,    -1,    -1,    -1,    14,    -1,    16,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    74,    -1,
      -1,    -1,    31,    32,     4,    34,    35,    36,     8,    -1,
      -1,    40,    -1,    42,    14,    44,    16,    -1,    -1,    48,
      49,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    31,    32,    -1,    34,    35,    36,    -1,    -1,    -1,
      40,    -1,    42,    -1,    44,    74,    -1,    -1,    48,    49,
       4,    -1,    -1,    -1,     8,    -1,    -1,    -1,    -1,    -1,
      14,    -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    74,    -1,    -1,    31,    32,    -1,
      34,    35,    36,    -1,    -1,    -1,    40,    -1,    42,    -1,
      44,     3,     4,     5,    48,    49,     8,    -1,    -1,    -1,
      -1,    -1,    14,    -1,    -1,    17,    18,    19,    20,    -1,
      -1,    23,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      74,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      42,    -1,    44,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      62,    -1,    -1,    65,    -1,    -1,    68,    -1,    70
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    79,    80,     0,     4,     8,     9,    14,    16,    24,
      25,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    40,    42,    44,    45,    46,    47,    48,    49,    50,
      74,    81,    82,    83,    84,    90,    91,    92,    93,    94,
      95,   100,   101,   102,   103,   104,   107,   110,   111,   112,
     113,   114,   115,   116,   117,   118,   119,    65,    65,     3,
       4,     5,     8,    17,    18,    19,    20,    23,    44,    62,
      65,    68,    70,    84,   123,   124,   125,   126,   127,   128,
     129,   130,   132,   135,   123,     4,     4,    28,   123,   123,
       4,   123,     4,     4,    65,    84,   108,   109,    39,    44,
      77,   123,     4,    14,    26,    42,    88,    89,   105,     4,
       4,     4,     5,     4,     5,    45,    74,    51,    66,    68,
      77,    85,    73,    74,    74,    74,    74,    74,    74,    74,
      74,    74,    74,    74,   123,   136,   137,   136,     4,    65,
     129,   129,   123,   143,   143,    65,    77,   133,    11,    22,
      21,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    70,    85,    86,   131,    63,    64,    68,
      77,     3,    65,    27,     4,    74,    74,    65,   108,    72,
      74,    77,    74,    35,    41,    43,     4,    65,    89,    25,
      65,    74,     4,     4,   105,   123,     6,   123,     4,    51,
      67,    72,    67,   136,    67,    69,    74,   123,   138,     4,
      71,   141,   136,     4,    74,    82,    84,    90,    91,   104,
     111,   112,   113,   115,   116,   117,   118,   122,   125,   126,
     128,   128,    87,   131,   131,   127,   129,   129,   123,     4,
       4,   123,   123,    27,    80,    10,    96,     4,   139,   140,
      67,   109,    80,     4,    80,     4,    42,    65,   139,     4,
     106,   139,    80,     5,     5,    25,    69,   123,   123,    67,
      72,   143,    51,    65,    73,    72,   143,    67,    65,   134,
      80,    74,     7,   127,   127,    69,    67,    74,   123,    14,
     123,    10,    13,    97,    67,    72,    74,    14,    14,   136,
      67,    74,    67,    14,   106,   143,    69,   123,     4,   142,
     123,   143,    71,   136,    12,    14,   120,    12,   121,    71,
      74,    80,    74,    29,    11,   123,    74,    15,    74,     4,
      80,    37,    38,    67,    80,    74,    47,    74,   123,   123,
      67,     4,    67,    74,   122,     9,    74,   122,    80,    14,
      80,    74,    74,    11,    98,    74,    80,    14,    74,    74,
      14,    80,    74,    80,    73,    51,    65,    73,    80,    74,
      74,    80,    74,    14,    25,    14,    98,    74,    74,    82,
      90,    91,    92,    93,    94,    95,    99,   100,   101,   102,
     103,   104,   107,   110,   111,   112,   113,   114,   115,   116,
     117,   118,   119,    14,    37,    45,    14,    14,   123,   123,
     142,   123,    14,    14,    24,    74,    25,    98,    74,    74,
      74,    74,    74,    74,    74,    74,    74,    74,    74,    74,
      33,    74,    74,    46,    45,    67,     9,     9,    74,    74,
      74,    74,    74,    73,    74,    74,   123
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    78,    79,    80,    80,    80,    81,    81,    81,    81,
      81,    81,    81,    81,    81,    81,    81,    81,    81,    81,
      81,    81,    81,    81,    81,    81,    81,    81,    81,    82,
      82,    83,    83,    83,    84,    84,    84,    85,    87,    86,
      88,    88,    89,    89,    89,    89,    90,    91,    91,    91,
      92,    93,    93,    94,    95,    96,    96,    97,    97,    98,
      98,    98,    99,    99,    99,    99,    99,    99,    99,    99,
      99,    99,    99,    99,    99,    99,    99,    99,    99,    99,
      99,    99,    99,    99,    99,   100,   101,   101,   102,   103,
     104,   104,   104,   104,   104,   104,   105,   105,   106,   107,
     107,   108,   108,   109,   109,   110,   111,   111,   111,   112,
     113,   113,   114,   115,   116,   117,   118,   119,   119,   120,
     120,   120,   121,   121,   121,   122,   122,   122,   122,   122,
     122,   122,   122,   122,   122,   122,   123,   124,   124,   125,
     125,   126,   126,   126,   126,   127,   127,   127,   128,   128,
     128,   129,   129,   129,   130,   130,   130,   131,   131,   131,
     131,   131,   131,   131,   131,   131,   131,   132,   132,   132,
     132,   132,   132,   132,   132,   132,   132,   132,   132,   132,
     132,   132,   133,   133,   133,   134,   134,   135,   135,   136,
     136,   137,   137,   138,   138,   139,   139,   140,   140,   141,
     141,   141,   141,   141,   141,   142,   142,   143,   143
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
       3,     1,     2,     2,     1,     4,     3,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       2,     4,     1,     1,     1,     1,     1,     3,     3,     5,
       3,     5,     0,     3,     3,     0,     3,     2,     3,     0,
       1,     1,     3,     1,     4,     0,     1,     1,     3,     3,
       3,     6,     6,     6,     9,     1,     2,     0,     2
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
#line 458 "src/parser.y"
                     { parsed_program = (yyvsp[0].stmt_list); (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 2488 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 462 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 2494 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 463 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 2500 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 464 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 2506 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 468 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2512 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 469 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2518 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 470 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2524 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 471 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2530 "src/parser.tab.c"
    break;

  case 10: /* statement: for_each_statement  */
#line 472 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2536 "src/parser.tab.c"
    break;

  case 11: /* statement: while_statement  */
#line 473 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2542 "src/parser.tab.c"
    break;

  case 12: /* statement: consider_statement  */
#line 474 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2548 "src/parser.tab.c"
    break;

  case 13: /* statement: function_statement  */
#line 475 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2554 "src/parser.tab.c"
    break;

  case 14: /* statement: modifier_statement  */
#line 476 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2560 "src/parser.tab.c"
    break;

  case 15: /* statement: program_statement  */
#line 477 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2566 "src/parser.tab.c"
    break;

  case 16: /* statement: library_statement  */
#line 478 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2572 "src/parser.tab.c"
    break;

  case 17: /* statement: use_statement NEWLINE  */
#line 479 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2578 "src/parser.tab.c"
    break;

  case 18: /* statement: watch_statement  */
#line 480 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2584 "src/parser.tab.c"
    break;

  case 19: /* statement: without_watchers_statement  */
#line 481 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2590 "src/parser.tab.c"
    break;

  case 20: /* statement: on_error_statement NEWLINE  */
#line 482 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2596 "src/parser.tab.c"
    break;

  case 21: /* statement: error_statement NEWLINE  */
#line 483 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2602 "src/parser.tab.c"
    break;

  case 22: /* statement: return_statement NEWLINE  */
#line 484 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2608 "src/parser.tab.c"
    break;

  case 23: /* statement: label_statement NEWLINE  */
#line 485 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2614 "src/parser.tab.c"
    break;

  case 24: /* statement: goto_statement NEWLINE  */
#line 486 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2620 "src/parser.tab.c"
    break;

  case 25: /* statement: gosub_statement NEWLINE  */
#line 487 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2626 "src/parser.tab.c"
    break;

  case 26: /* statement: break_statement NEWLINE  */
#line 488 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2632 "src/parser.tab.c"
    break;

  case 27: /* statement: continue_statement NEWLINE  */
#line 489 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2638 "src/parser.tab.c"
    break;

  case 28: /* statement: if_statement  */
#line 490 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2644 "src/parser.tab.c"
    break;

  case 29: /* assignment: lvalue OP_EQ expression  */
#line 494 "src/parser.y"
                              { (yyval.stmt) = ast_assign((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr)); }
#line 2650 "src/parser.tab.c"
    break;

  case 30: /* assignment: lvalue modifier OP_EQ expression  */
#line 495 "src/parser.y"
                                       {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            yyerror("modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr));
      }
#line 2662 "src/parser.tab.c"
    break;

  case 31: /* lvalue: variable_name  */
#line 505 "src/parser.y"
                                 { (yyval.expr) = expr_at(ast_ident((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2668 "src/parser.tab.c"
    break;

  case 32: /* lvalue: lvalue LBRACKET expression RBRACKET  */
#line 506 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 2674 "src/parser.tab.c"
    break;

  case 33: /* lvalue: lvalue DOT IDENT  */
#line 507 "src/parser.y"
                                    { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2680 "src/parser.tab.c"
    break;

  case 34: /* variable_name: IDENT  */
#line 511 "src/parser.y"
                         { (yyval.text) = (yyvsp[0].text); }
#line 2686 "src/parser.tab.c"
    break;

  case 35: /* variable_name: END  */
#line 512 "src/parser.y"
                       { (yyval.text) = copy_const("end"); }
#line 2692 "src/parser.tab.c"
    break;

  case 36: /* variable_name: NEXT  */
#line 513 "src/parser.y"
                        { (yyval.text) = copy_const("next"); }
#line 2698 "src/parser.tab.c"
    break;

  case 37: /* modifier: MOD_LPAREN MOD_CONTENT  */
#line 517 "src/parser.y"
                             { (yyval.modifier) = parse_modifier_use((yyvsp[0].text)); }
#line 2704 "src/parser.tab.c"
    break;

  case 38: /* $@1: %empty  */
#line 521 "src/parser.y"
             { lexer_begin_lens_content(active_lexer); }
#line 2710 "src/parser.tab.c"
    break;

  case 39: /* comparison_lens: LBRACE $@1 LENS_CONTENT RBRACE  */
#line 521 "src/parser.y"
                                                                             {
        (yyval.modifier) = parse_modifier_use((yyvsp[-1].text));
      }
#line 2718 "src/parser.tab.c"
    break;

  case 40: /* modifier_name: modifier_word  */
#line 527 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 2724 "src/parser.tab.c"
    break;

  case 41: /* modifier_name: modifier_name modifier_word  */
#line 528 "src/parser.y"
                                  { (yyval.text) = join_words((yyvsp[-1].text), (yyvsp[0].text)); }
#line 2730 "src/parser.tab.c"
    break;

  case 42: /* modifier_word: IDENT  */
#line 532 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 2736 "src/parser.tab.c"
    break;

  case 43: /* modifier_word: TO  */
#line 533 "src/parser.y"
         { (yyval.text) = copy_const("to"); }
#line 2742 "src/parser.tab.c"
    break;

  case 44: /* modifier_word: END  */
#line 534 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 2748 "src/parser.tab.c"
    break;

  case 45: /* modifier_word: NEXT  */
#line 535 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 2754 "src/parser.tab.c"
    break;

  case 46: /* print_statement: PRINT expression  */
#line 539 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 2760 "src/parser.tab.c"
    break;

  case 47: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 543 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 2766 "src/parser.tab.c"
    break;

  case 48: /* call_statement: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 544 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.stmt) = ast_expr_stmt(ast_qualified_call(library, name, (yyvsp[-1].expr_list)));
      }
#line 2777 "src/parser.tab.c"
    break;

  case 49: /* call_statement: ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 550 "src/parser.y"
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
#line 2792 "src/parser.tab.c"
    break;

  case 50: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 563 "src/parser.y"
                                                                                  {
        if (strcmp((yyvsp[-8].text), "lock") != 0) {
            yyerror("expected lock in with lock block");
            free((yyvsp[-8].text));
            YYERROR;
        }
        free((yyvsp[-8].text));
        (yyval.stmt) = ast_with_lock((yyvsp[-6].expr), (yyvsp[-3].stmt_list));
      }
#line 2806 "src/parser.tab.c"
    break;

  case 51: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list END FOR NEWLINE  */
#line 575 "src/parser.y"
                                                                     {
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 2814 "src/parser.tab.c"
    break;

  case 52: /* for_each_statement: FOR EACH IDENT IN expression NEWLINE statement_list END FOR NEWLINE  */
#line 578 "src/parser.y"
                                                                          {
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 2822 "src/parser.tab.c"
    break;

  case 53: /* while_statement: WHILE expression NEWLINE statement_list END WHILE NEWLINE  */
#line 584 "src/parser.y"
                                                                {
        (yyval.stmt) = ast_while((yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 2830 "src/parser.tab.c"
    break;

  case 54: /* consider_statement: CONSIDER expression NEWLINE consider_branch_list consider_else_opt END_CONSIDER NEWLINE  */
#line 590 "src/parser.y"
                                                                                              {
        (yyval.stmt) = ast_consider((yyvsp[-5].expr), (yyvsp[-3].consider_branch_list), (yyvsp[-2].stmt_list));
      }
#line 2838 "src/parser.tab.c"
    break;

  case 55: /* consider_branch_list: CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 596 "src/parser.y"
                                                                  {
        (yyval.consider_branch_list) = ast_consider_branch_list_append(ast_consider_branch_list_empty(), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 2846 "src/parser.tab.c"
    break;

  case 56: /* consider_branch_list: consider_branch_list CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 599 "src/parser.y"
                                                                                       {
        (yyval.consider_branch_list) = ast_consider_branch_list_append((yyvsp[-5].consider_branch_list), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 2854 "src/parser.tab.c"
    break;

  case 57: /* consider_else_opt: %empty  */
#line 605 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 2860 "src/parser.tab.c"
    break;

  case 58: /* consider_else_opt: CONSIDER_ELSE NEWLINE consider_statement_list  */
#line 606 "src/parser.y"
                                                    { (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 2866 "src/parser.tab.c"
    break;

  case 59: /* consider_statement_list: %empty  */
#line 610 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 2872 "src/parser.tab.c"
    break;

  case 60: /* consider_statement_list: consider_statement_list NEWLINE  */
#line 611 "src/parser.y"
                                      { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 2878 "src/parser.tab.c"
    break;

  case 61: /* consider_statement_list: consider_statement_list consider_body_statement  */
#line 612 "src/parser.y"
                                                      { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 2884 "src/parser.tab.c"
    break;

  case 62: /* consider_body_statement: assignment NEWLINE  */
#line 616 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2890 "src/parser.tab.c"
    break;

  case 63: /* consider_body_statement: print_statement NEWLINE  */
#line 617 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2896 "src/parser.tab.c"
    break;

  case 64: /* consider_body_statement: call_statement NEWLINE  */
#line 618 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2902 "src/parser.tab.c"
    break;

  case 65: /* consider_body_statement: with_lock_statement  */
#line 619 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2908 "src/parser.tab.c"
    break;

  case 66: /* consider_body_statement: for_each_statement  */
#line 620 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2914 "src/parser.tab.c"
    break;

  case 67: /* consider_body_statement: while_statement  */
#line 621 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2920 "src/parser.tab.c"
    break;

  case 68: /* consider_body_statement: consider_statement  */
#line 622 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2926 "src/parser.tab.c"
    break;

  case 69: /* consider_body_statement: function_statement  */
#line 623 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2932 "src/parser.tab.c"
    break;

  case 70: /* consider_body_statement: modifier_statement  */
#line 624 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2938 "src/parser.tab.c"
    break;

  case 71: /* consider_body_statement: program_statement  */
#line 625 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2944 "src/parser.tab.c"
    break;

  case 72: /* consider_body_statement: library_statement  */
#line 626 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2950 "src/parser.tab.c"
    break;

  case 73: /* consider_body_statement: use_statement NEWLINE  */
#line 627 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2956 "src/parser.tab.c"
    break;

  case 74: /* consider_body_statement: watch_statement  */
#line 628 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2962 "src/parser.tab.c"
    break;

  case 75: /* consider_body_statement: without_watchers_statement  */
#line 629 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2968 "src/parser.tab.c"
    break;

  case 76: /* consider_body_statement: on_error_statement NEWLINE  */
#line 630 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2974 "src/parser.tab.c"
    break;

  case 77: /* consider_body_statement: error_statement NEWLINE  */
#line 631 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2980 "src/parser.tab.c"
    break;

  case 78: /* consider_body_statement: return_statement NEWLINE  */
#line 632 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2986 "src/parser.tab.c"
    break;

  case 79: /* consider_body_statement: label_statement NEWLINE  */
#line 633 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2992 "src/parser.tab.c"
    break;

  case 80: /* consider_body_statement: goto_statement NEWLINE  */
#line 634 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2998 "src/parser.tab.c"
    break;

  case 81: /* consider_body_statement: gosub_statement NEWLINE  */
#line 635 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3004 "src/parser.tab.c"
    break;

  case 82: /* consider_body_statement: break_statement NEWLINE  */
#line 636 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3010 "src/parser.tab.c"
    break;

  case 83: /* consider_body_statement: continue_statement NEWLINE  */
#line 637 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3016 "src/parser.tab.c"
    break;

  case 84: /* consider_body_statement: if_statement  */
#line 638 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3022 "src/parser.tab.c"
    break;

  case 85: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 642 "src/parser.y"
                                                                                                  {
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 3030 "src/parser.tab.c"
    break;

  case 86: /* modifier_statement: MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 648 "src/parser.y"
                                                                                                   {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 0, (yyvsp[-3].stmt_list));
      }
#line 3038 "src/parser.tab.c"
    break;

  case 87: /* modifier_statement: EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 651 "src/parser.y"
                                                                                                          {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 1, (yyvsp[-3].stmt_list));
      }
#line 3046 "src/parser.tab.c"
    break;

  case 88: /* program_statement: PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE  */
#line 657 "src/parser.y"
                                                                                                {
        (yyval.stmt) = ast_program((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 3054 "src/parser.tab.c"
    break;

  case 89: /* library_statement: LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE  */
#line 663 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_library((yyvsp[-5].text), (yyvsp[-3].stmt_list));
      }
#line 3062 "src/parser.tab.c"
    break;

  case 90: /* use_statement: USE IDENT  */
#line 669 "src/parser.y"
                { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 3068 "src/parser.tab.c"
    break;

  case 91: /* use_statement: LOAD IDENT  */
#line 670 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 3074 "src/parser.tab.c"
    break;

  case 92: /* use_statement: USE STRING  */
#line 671 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 3080 "src/parser.tab.c"
    break;

  case 93: /* use_statement: LOAD STRING  */
#line 672 "src/parser.y"
                  { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 3086 "src/parser.tab.c"
    break;

  case 94: /* use_statement: USE IDENT IDENT STRING  */
#line 673 "src/parser.y"
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
#line 3102 "src/parser.tab.c"
    break;

  case 95: /* use_statement: LOAD IDENT IDENT STRING  */
#line 684 "src/parser.y"
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
#line 3118 "src/parser.tab.c"
    break;

  case 96: /* modifier_signature: modifier_name  */
#line 698 "src/parser.y"
                    { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[0].text), ast_name_list_empty()); }
#line 3124 "src/parser.tab.c"
    break;

  case 97: /* modifier_signature: modifier_name LPAREN parameter_list_opt RPAREN  */
#line 699 "src/parser.y"
                                                     { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[-3].text), (yyvsp[-1].name_list)); }
#line 3130 "src/parser.tab.c"
    break;

  case 98: /* modifier_context: IDENT  */
#line 703 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 3136 "src/parser.tab.c"
    break;

  case 99: /* watch_statement: WATCH LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 707 "src/parser.y"
                                                                                     {
        (yyval.stmt) = ast_watch((yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 3144 "src/parser.tab.c"
    break;

  case 100: /* watch_statement: WATCH watch_target_list NEWLINE statement_list END WATCH NEWLINE  */
#line 710 "src/parser.y"
                                                                       {
        (yyval.stmt) = ast_watch((yyvsp[-5].name_list), (yyvsp[-3].stmt_list));
      }
#line 3152 "src/parser.tab.c"
    break;

  case 101: /* watch_target_list: watch_target_path  */
#line 716 "src/parser.y"
                        { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 3158 "src/parser.tab.c"
    break;

  case 102: /* watch_target_list: watch_target_list COMMA watch_target_path  */
#line 717 "src/parser.y"
                                                { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 3164 "src/parser.tab.c"
    break;

  case 103: /* watch_target_path: variable_name  */
#line 721 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 3170 "src/parser.tab.c"
    break;

  case 104: /* watch_target_path: watch_target_path DOT IDENT  */
#line 722 "src/parser.y"
                                  { (yyval.text) = join_watch_path((yyvsp[-2].text), (yyvsp[0].text)); }
#line 3176 "src/parser.tab.c"
    break;

  case 105: /* without_watchers_statement: WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE  */
#line 726 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_without_watchers((yyvsp[-3].stmt_list));
      }
#line 3184 "src/parser.tab.c"
    break;

  case 106: /* on_error_statement: ON ERROR_VALUE GOTO IDENT  */
#line 732 "src/parser.y"
                                { (yyval.stmt) = ast_on_error_goto((yyvsp[0].text)); }
#line 3190 "src/parser.tab.c"
    break;

  case 107: /* on_error_statement: ON ERROR_VALUE RESUME NEXT  */
#line 733 "src/parser.y"
                                 { (yyval.stmt) = ast_on_error_resume_next(); }
#line 3196 "src/parser.tab.c"
    break;

  case 108: /* on_error_statement: ON ERROR_VALUE STOP  */
#line 734 "src/parser.y"
                          { (yyval.stmt) = ast_on_error_stop(); }
#line 3202 "src/parser.tab.c"
    break;

  case 109: /* error_statement: ERROR_VALUE expression  */
#line 738 "src/parser.y"
                             { (yyval.stmt) = ast_error((yyvsp[0].expr)); }
#line 3208 "src/parser.tab.c"
    break;

  case 110: /* return_statement: RETURN  */
#line 742 "src/parser.y"
             { (yyval.stmt) = ast_return(NULL); }
#line 3214 "src/parser.tab.c"
    break;

  case 111: /* return_statement: RETURN expression  */
#line 743 "src/parser.y"
                        { (yyval.stmt) = ast_return((yyvsp[0].expr)); }
#line 3220 "src/parser.tab.c"
    break;

  case 112: /* label_statement: variable_name COLON  */
#line 747 "src/parser.y"
                          { (yyval.stmt) = ast_label((yyvsp[-1].text)); }
#line 3226 "src/parser.tab.c"
    break;

  case 113: /* goto_statement: GOTO IDENT  */
#line 751 "src/parser.y"
                 { (yyval.stmt) = ast_goto((yyvsp[0].text)); }
#line 3232 "src/parser.tab.c"
    break;

  case 114: /* gosub_statement: GOSUB IDENT  */
#line 755 "src/parser.y"
                  { (yyval.stmt) = ast_gosub((yyvsp[0].text)); }
#line 3238 "src/parser.tab.c"
    break;

  case 115: /* break_statement: BREAK  */
#line 759 "src/parser.y"
            { (yyval.stmt) = ast_break(); }
#line 3244 "src/parser.tab.c"
    break;

  case 116: /* continue_statement: CONTINUE  */
#line 763 "src/parser.y"
               { (yyval.stmt) = ast_continue(); }
#line 3250 "src/parser.tab.c"
    break;

  case 117: /* if_statement: IF expression THEN NEWLINE statement_list if_block_tail  */
#line 767 "src/parser.y"
                                                              {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), (yyvsp[-1].stmt_list));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 3259 "src/parser.tab.c"
    break;

  case 118: /* if_statement: IF expression THEN inline_statement NEWLINE if_inline_tail  */
#line 771 "src/parser.y"
                                                                 {
        (yyval.stmt) = ast_if((yyvsp[-4].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-2].stmt)));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[0].stmt_list);
      }
#line 3268 "src/parser.tab.c"
    break;

  case 119: /* if_block_tail: END IF NEWLINE  */
#line 778 "src/parser.y"
                     {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 3276 "src/parser.tab.c"
    break;

  case 120: /* if_block_tail: ELSE inline_statement NEWLINE  */
#line 781 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 3284 "src/parser.tab.c"
    break;

  case 121: /* if_block_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 784 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 3292 "src/parser.tab.c"
    break;

  case 122: /* if_inline_tail: %empty  */
#line 790 "src/parser.y"
                                   {
        (yyval.stmt_list) = ast_stmt_list_empty();
      }
#line 3300 "src/parser.tab.c"
    break;

  case 123: /* if_inline_tail: ELSE inline_statement NEWLINE  */
#line 793 "src/parser.y"
                                    {
        (yyval.stmt_list) = ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt));
      }
#line 3308 "src/parser.tab.c"
    break;

  case 124: /* if_inline_tail: ELSE NEWLINE statement_list END IF NEWLINE  */
#line 796 "src/parser.y"
                                                 {
        (yyval.stmt_list) = (yyvsp[-3].stmt_list);
      }
#line 3316 "src/parser.tab.c"
    break;

  case 125: /* inline_statement: assignment  */
#line 802 "src/parser.y"
                 { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3322 "src/parser.tab.c"
    break;

  case 126: /* inline_statement: print_statement  */
#line 803 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3328 "src/parser.tab.c"
    break;

  case 127: /* inline_statement: call_statement  */
#line 804 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3334 "src/parser.tab.c"
    break;

  case 128: /* inline_statement: use_statement  */
#line 805 "src/parser.y"
                    { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3340 "src/parser.tab.c"
    break;

  case 129: /* inline_statement: on_error_statement  */
#line 806 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3346 "src/parser.tab.c"
    break;

  case 130: /* inline_statement: error_statement  */
#line 807 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3352 "src/parser.tab.c"
    break;

  case 131: /* inline_statement: return_statement  */
#line 808 "src/parser.y"
                       { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3358 "src/parser.tab.c"
    break;

  case 132: /* inline_statement: goto_statement  */
#line 809 "src/parser.y"
                     { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3364 "src/parser.tab.c"
    break;

  case 133: /* inline_statement: gosub_statement  */
#line 810 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3370 "src/parser.tab.c"
    break;

  case 134: /* inline_statement: break_statement  */
#line 811 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3376 "src/parser.tab.c"
    break;

  case 135: /* inline_statement: continue_statement  */
#line 812 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3382 "src/parser.tab.c"
    break;

  case 136: /* expression: or_expression  */
#line 816 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 3388 "src/parser.tab.c"
    break;

  case 137: /* or_expression: and_expression  */
#line 820 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 3394 "src/parser.tab.c"
    break;

  case 138: /* or_expression: or_expression OR and_expression  */
#line 821 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_binary(copy_const("or"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3400 "src/parser.tab.c"
    break;

  case 139: /* and_expression: comparison_expression  */
#line 825 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 3406 "src/parser.tab.c"
    break;

  case 140: /* and_expression: and_expression AND comparison_expression  */
#line 826 "src/parser.y"
                                               { (yyval.expr) = expr_at(ast_binary(copy_const("and"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3412 "src/parser.tab.c"
    break;

  case 141: /* comparison_expression: additive_expression  */
#line 830 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 3418 "src/parser.tab.c"
    break;

  case 142: /* comparison_expression: additive_expression comparison_operator additive_expression  */
#line 831 "src/parser.y"
                                                                  { (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3424 "src/parser.tab.c"
    break;

  case 143: /* comparison_expression: additive_expression comparison_lens comparison_operator additive_expression  */
#line 832 "src/parser.y"
                                                                                  {
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 3432 "src/parser.tab.c"
    break;

  case 144: /* comparison_expression: additive_expression modifier comparison_operator additive_expression  */
#line 835 "src/parser.y"
                                                                           {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            yyerror("modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.expr) = expr_at(ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column);
      }
#line 3444 "src/parser.tab.c"
    break;

  case 145: /* additive_expression: multiplicative_expression  */
#line 845 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 3450 "src/parser.tab.c"
    break;

  case 146: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 846 "src/parser.y"
                                                         { (yyval.expr) = expr_at(ast_binary(copy_const("+"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3456 "src/parser.tab.c"
    break;

  case 147: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 847 "src/parser.y"
                                                          { (yyval.expr) = expr_at(ast_binary(copy_const("-"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3462 "src/parser.tab.c"
    break;

  case 148: /* multiplicative_expression: unary_expression  */
#line 851 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 3468 "src/parser.tab.c"
    break;

  case 149: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 852 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_binary(copy_const("*"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3474 "src/parser.tab.c"
    break;

  case 150: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 853 "src/parser.y"
                                                       { (yyval.expr) = expr_at(ast_binary(copy_const("/"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3480 "src/parser.tab.c"
    break;

  case 151: /* unary_expression: postfix_expression  */
#line 857 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 3486 "src/parser.tab.c"
    break;

  case 152: /* unary_expression: NOT unary_expression  */
#line 858 "src/parser.y"
                           { (yyval.expr) = expr_at(ast_unary(copy_const("not"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3492 "src/parser.tab.c"
    break;

  case 153: /* unary_expression: MINUS unary_expression  */
#line 859 "src/parser.y"
                             { (yyval.expr) = expr_at(ast_unary(copy_const("-"), (yyvsp[0].expr)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3498 "src/parser.tab.c"
    break;

  case 154: /* postfix_expression: primary  */
#line 863 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 3504 "src/parser.tab.c"
    break;

  case 155: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 864 "src/parser.y"
                                                      { (yyval.expr) = expr_at(ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 3510 "src/parser.tab.c"
    break;

  case 156: /* postfix_expression: postfix_expression DOT IDENT  */
#line 865 "src/parser.y"
                                   { (yyval.expr) = expr_at(ast_field((yyvsp[-2].expr), (yyvsp[0].text)), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 3516 "src/parser.tab.c"
    break;

  case 157: /* comparison_operator: OP_EQ  */
#line 869 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 3522 "src/parser.tab.c"
    break;

  case 158: /* comparison_operator: OP_NE  */
#line 870 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 3528 "src/parser.tab.c"
    break;

  case 159: /* comparison_operator: OP_GT  */
#line 871 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 3534 "src/parser.tab.c"
    break;

  case 160: /* comparison_operator: OP_LT  */
#line 872 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 3540 "src/parser.tab.c"
    break;

  case 161: /* comparison_operator: OP_GE  */
#line 873 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 3546 "src/parser.tab.c"
    break;

  case 162: /* comparison_operator: OP_LE  */
#line 874 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 3552 "src/parser.tab.c"
    break;

  case 163: /* comparison_operator: OP_NGT  */
#line 875 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 3558 "src/parser.tab.c"
    break;

  case 164: /* comparison_operator: OP_NLT  */
#line 876 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 3564 "src/parser.tab.c"
    break;

  case 165: /* comparison_operator: OP_NGE  */
#line 877 "src/parser.y"
             { (yyval.text) = copy_const("!>="); }
#line 3570 "src/parser.tab.c"
    break;

  case 166: /* comparison_operator: OP_NLE  */
#line 878 "src/parser.y"
             { (yyval.text) = copy_const("!<="); }
#line 3576 "src/parser.tab.c"
    break;

  case 167: /* primary: NUMBER  */
#line 882 "src/parser.y"
             { (yyval.expr) = expr_at(ast_number((yyvsp[0].number)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3582 "src/parser.tab.c"
    break;

  case 168: /* primary: duration_terms  */
#line 883 "src/parser.y"
                     { (yyval.expr) = expr_at(ast_duration((yyvsp[0].duration)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3588 "src/parser.tab.c"
    break;

  case 169: /* primary: STRING  */
#line 884 "src/parser.y"
             { (yyval.expr) = expr_at(ast_string((yyvsp[0].text)), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3594 "src/parser.tab.c"
    break;

  case 170: /* primary: variable_name ident_suffix  */
#line 885 "src/parser.y"
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
#line 3610 "src/parser.tab.c"
    break;

  case 171: /* primary: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 896 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.expr) = expr_at(ast_qualified_call(library, name, (yyvsp[-1].expr_list)), (yylsp[-3]).first_line, (yylsp[-3]).first_column);
      }
#line 3621 "src/parser.tab.c"
    break;

  case 172: /* primary: ERROR_VALUE  */
#line 902 "src/parser.y"
                  { (yyval.expr) = expr_at(ast_ident(copy_const("error")), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3627 "src/parser.tab.c"
    break;

  case 173: /* primary: TRUE  */
#line 903 "src/parser.y"
           { (yyval.expr) = expr_at(ast_bool(1), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3633 "src/parser.tab.c"
    break;

  case 174: /* primary: FALSE  */
#line 904 "src/parser.y"
            { (yyval.expr) = expr_at(ast_bool(0), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3639 "src/parser.tab.c"
    break;

  case 175: /* primary: NOTHING  */
#line 905 "src/parser.y"
              { (yyval.expr) = expr_at(ast_null(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3645 "src/parser.tab.c"
    break;

  case 176: /* primary: UNKNOWN_VALUE  */
#line 906 "src/parser.y"
                    { (yyval.expr) = expr_at(ast_unknown(), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 3651 "src/parser.tab.c"
    break;

  case 177: /* primary: LPAREN expression RPAREN  */
#line 907 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); }
#line 3657 "src/parser.tab.c"
    break;

  case 178: /* primary: LBRACKET optional_newlines RBRACKET  */
#line 908 "src/parser.y"
                                          { (yyval.expr) = expr_at(ast_array(ast_expr_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 3663 "src/parser.tab.c"
    break;

  case 179: /* primary: LBRACKET optional_newlines array_argument_list optional_newlines RBRACKET  */
#line 909 "src/parser.y"
                                                                                { (yyval.expr) = expr_at(ast_array((yyvsp[-2].expr_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 3669 "src/parser.tab.c"
    break;

  case 180: /* primary: LBRACE optional_newlines RBRACE  */
#line 910 "src/parser.y"
                                      { (yyval.expr) = expr_at(ast_record(ast_record_field_list_empty()), (yylsp[-2]).first_line, (yylsp[-2]).first_column); }
#line 3675 "src/parser.tab.c"
    break;

  case 181: /* primary: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 911 "src/parser.y"
                                                                          { (yyval.expr) = expr_at(ast_record((yyvsp[-2].record_field_list)), (yylsp[-4]).first_line, (yylsp[-4]).first_column); }
#line 3681 "src/parser.tab.c"
    break;

  case 182: /* ident_suffix: %empty  */
#line 915 "src/parser.y"
                          {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_NONE;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 3691 "src/parser.tab.c"
    break;

  case 183: /* ident_suffix: LPAREN argument_list_opt RPAREN  */
#line 920 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 3701 "src/parser.tab.c"
    break;

  case 184: /* ident_suffix: DOT IDENT ident_dot_suffix  */
#line 925 "src/parser.y"
                                 {
        (yyval.ident_suffix) = (yyvsp[0].ident_suffix);
        (yyval.ident_suffix).name = (yyvsp[-1].text);
      }
#line 3710 "src/parser.tab.c"
    break;

  case 185: /* ident_dot_suffix: %empty  */
#line 932 "src/parser.y"
             {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_FIELD;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 3720 "src/parser.tab.c"
    break;

  case 186: /* ident_dot_suffix: LPAREN argument_list_opt RPAREN  */
#line 937 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_QUALIFIED_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 3730 "src/parser.tab.c"
    break;

  case 187: /* duration_terms: NUMBER IDENT  */
#line 945 "src/parser.y"
                   {
        AstDuration duration = {0};
        (yyval.duration) = duration_add_unit(duration, (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 3739 "src/parser.tab.c"
    break;

  case 188: /* duration_terms: duration_terms NUMBER IDENT  */
#line 949 "src/parser.y"
                                  {
        (yyval.duration) = duration_add_unit((yyvsp[-2].duration), (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 3747 "src/parser.tab.c"
    break;

  case 189: /* argument_list_opt: %empty  */
#line 955 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 3753 "src/parser.tab.c"
    break;

  case 190: /* argument_list_opt: argument_list  */
#line 956 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 3759 "src/parser.tab.c"
    break;

  case 191: /* argument_list: expression  */
#line 960 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 3765 "src/parser.tab.c"
    break;

  case 192: /* argument_list: argument_list COMMA expression  */
#line 961 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 3771 "src/parser.tab.c"
    break;

  case 193: /* array_argument_list: expression  */
#line 965 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 3777 "src/parser.tab.c"
    break;

  case 194: /* array_argument_list: array_argument_list COMMA optional_newlines expression  */
#line 966 "src/parser.y"
                                                             { (yyval.expr_list) = ast_expr_list_append((yyvsp[-3].expr_list), (yyvsp[0].expr)); }
#line 3783 "src/parser.tab.c"
    break;

  case 195: /* parameter_list_opt: %empty  */
#line 970 "src/parser.y"
             { (yyval.name_list) = ast_name_list_empty(); }
#line 3789 "src/parser.tab.c"
    break;

  case 196: /* parameter_list_opt: parameter_list  */
#line 971 "src/parser.y"
                     { (yyval.name_list) = (yyvsp[0].name_list); }
#line 3795 "src/parser.tab.c"
    break;

  case 197: /* parameter_list: IDENT  */
#line 975 "src/parser.y"
            { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 3801 "src/parser.tab.c"
    break;

  case 198: /* parameter_list: parameter_list COMMA IDENT  */
#line 976 "src/parser.y"
                                 { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 3807 "src/parser.tab.c"
    break;

  case 199: /* record_field_list: IDENT OP_EQ expression  */
#line 980 "src/parser.y"
                             { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 3813 "src/parser.tab.c"
    break;

  case 200: /* record_field_list: IDENT COLON expression  */
#line 981 "src/parser.y"
                             { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 3819 "src/parser.tab.c"
    break;

  case 201: /* record_field_list: IDENT LPAREN field_policy RPAREN COLON expression  */
#line 982 "src/parser.y"
                                                        { (yyval.record_field_list) = ast_record_field_list_append_policy(ast_record_field_list_empty(), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 3825 "src/parser.tab.c"
    break;

  case 202: /* record_field_list: record_field_list COMMA optional_newlines IDENT OP_EQ expression  */
#line 983 "src/parser.y"
                                                                       { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 3831 "src/parser.tab.c"
    break;

  case 203: /* record_field_list: record_field_list COMMA optional_newlines IDENT COLON expression  */
#line 984 "src/parser.y"
                                                                       { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 3837 "src/parser.tab.c"
    break;

  case 204: /* record_field_list: record_field_list COMMA optional_newlines IDENT LPAREN field_policy RPAREN COLON expression  */
#line 985 "src/parser.y"
                                                                                                  { (yyval.record_field_list) = ast_record_field_list_append_policy((yyvsp[-8].record_field_list), (yyvsp[-5].text), (yyvsp[0].expr), (yyvsp[-3].field_policy).policy, (yyvsp[-3].field_policy).reset_expr); }
#line 3843 "src/parser.tab.c"
    break;

  case 205: /* field_policy: IDENT  */
#line 993 "src/parser.y"
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
#line 3869 "src/parser.tab.c"
    break;

  case 206: /* field_policy: IDENT expression  */
#line 1014 "src/parser.y"
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
#line 3887 "src/parser.tab.c"
    break;


#line 3891 "src/parser.tab.c"

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

#line 1034 "src/parser.y"


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
