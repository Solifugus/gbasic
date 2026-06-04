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

AstStmtList parsed_program;

static char *copy_text(const char *start, int length) {
    char *text = malloc((size_t)length + 1);
    if (!text) {
        abort();
    }
    memcpy(text, start, (size_t)length);
    text[length] = '\0';
    return text;
}

static char *copy_string_literal(const char *start, int length, int *ok) {
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
                fprintf(stderr, "runtime error: unterminated escape sequence\n");
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
                fprintf(stderr, "runtime error: invalid escape sequence: \\%c\n", start[i]);
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

#line 419 "src/parser.tab.c"

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
  YYSYMBOL_WHILE = 27,                     /* WHILE  */
  YYSYMBOL_CONSIDER = 28,                  /* CONSIDER  */
  YYSYMBOL_BREAK = 29,                     /* BREAK  */
  YYSYMBOL_CONTINUE = 30,                  /* CONTINUE  */
  YYSYMBOL_FUNCTION = 31,                  /* FUNCTION  */
  YYSYMBOL_RETURN = 32,                    /* RETURN  */
  YYSYMBOL_GOTO = 33,                      /* GOTO  */
  YYSYMBOL_GOSUB = 34,                     /* GOSUB  */
  YYSYMBOL_WATCH = 35,                     /* WATCH  */
  YYSYMBOL_WITHOUT = 36,                   /* WITHOUT  */
  YYSYMBOL_WATCHERS = 37,                  /* WATCHERS  */
  YYSYMBOL_ON = 38,                        /* ON  */
  YYSYMBOL_RESUME = 39,                    /* RESUME  */
  YYSYMBOL_NEXT = 40,                      /* NEXT  */
  YYSYMBOL_STOP = 41,                      /* STOP  */
  YYSYMBOL_ERROR_VALUE = 42,               /* ERROR_VALUE  */
  YYSYMBOL_MODIFIER = 43,                  /* MODIFIER  */
  YYSYMBOL_PROGRAM = 44,                   /* PROGRAM  */
  YYSYMBOL_LIBRARY = 45,                   /* LIBRARY  */
  YYSYMBOL_LOAD = 46,                      /* LOAD  */
  YYSYMBOL_USE = 47,                       /* USE  */
  YYSYMBOL_EXPORT = 48,                    /* EXPORT  */
  YYSYMBOL_OP_EQ = 49,                     /* OP_EQ  */
  YYSYMBOL_OP_NE = 50,                     /* OP_NE  */
  YYSYMBOL_OP_GT = 51,                     /* OP_GT  */
  YYSYMBOL_OP_LT = 52,                     /* OP_LT  */
  YYSYMBOL_OP_GE = 53,                     /* OP_GE  */
  YYSYMBOL_OP_LE = 54,                     /* OP_LE  */
  YYSYMBOL_OP_NGT = 55,                    /* OP_NGT  */
  YYSYMBOL_OP_NLT = 56,                    /* OP_NLT  */
  YYSYMBOL_OP_NGE = 57,                    /* OP_NGE  */
  YYSYMBOL_OP_NLE = 58,                    /* OP_NLE  */
  YYSYMBOL_PLUS = 59,                      /* PLUS  */
  YYSYMBOL_MINUS = 60,                     /* MINUS  */
  YYSYMBOL_STAR = 61,                      /* STAR  */
  YYSYMBOL_SLASH = 62,                     /* SLASH  */
  YYSYMBOL_LPAREN = 63,                    /* LPAREN  */
  YYSYMBOL_MOD_LPAREN = 64,                /* MOD_LPAREN  */
  YYSYMBOL_RPAREN = 65,                    /* RPAREN  */
  YYSYMBOL_LBRACKET = 66,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 67,                  /* RBRACKET  */
  YYSYMBOL_LBRACE = 68,                    /* LBRACE  */
  YYSYMBOL_RBRACE = 69,                    /* RBRACE  */
  YYSYMBOL_COMMA = 70,                     /* COMMA  */
  YYSYMBOL_COLON = 71,                     /* COLON  */
  YYSYMBOL_NEWLINE = 72,                   /* NEWLINE  */
  YYSYMBOL_NO_DOT = 73,                    /* NO_DOT  */
  YYSYMBOL_DOT = 74,                       /* DOT  */
  YYSYMBOL_YYACCEPT = 75,                  /* $accept  */
  YYSYMBOL_program = 76,                   /* program  */
  YYSYMBOL_statement_list = 77,            /* statement_list  */
  YYSYMBOL_statement = 78,                 /* statement  */
  YYSYMBOL_assignment = 79,                /* assignment  */
  YYSYMBOL_lvalue = 80,                    /* lvalue  */
  YYSYMBOL_variable_name = 81,             /* variable_name  */
  YYSYMBOL_modifier = 82,                  /* modifier  */
  YYSYMBOL_modifier_name = 83,             /* modifier_name  */
  YYSYMBOL_modifier_word = 84,             /* modifier_word  */
  YYSYMBOL_print_statement = 85,           /* print_statement  */
  YYSYMBOL_call_statement = 86,            /* call_statement  */
  YYSYMBOL_with_lock_statement = 87,       /* with_lock_statement  */
  YYSYMBOL_for_each_statement = 88,        /* for_each_statement  */
  YYSYMBOL_while_statement = 89,           /* while_statement  */
  YYSYMBOL_consider_statement = 90,        /* consider_statement  */
  YYSYMBOL_consider_branch_list = 91,      /* consider_branch_list  */
  YYSYMBOL_consider_else_opt = 92,         /* consider_else_opt  */
  YYSYMBOL_consider_statement_list = 93,   /* consider_statement_list  */
  YYSYMBOL_consider_body_statement = 94,   /* consider_body_statement  */
  YYSYMBOL_function_statement = 95,        /* function_statement  */
  YYSYMBOL_modifier_statement = 96,        /* modifier_statement  */
  YYSYMBOL_program_statement = 97,         /* program_statement  */
  YYSYMBOL_library_statement = 98,         /* library_statement  */
  YYSYMBOL_use_statement = 99,             /* use_statement  */
  YYSYMBOL_modifier_signature = 100,       /* modifier_signature  */
  YYSYMBOL_modifier_context = 101,         /* modifier_context  */
  YYSYMBOL_watch_statement = 102,          /* watch_statement  */
  YYSYMBOL_without_watchers_statement = 103, /* without_watchers_statement  */
  YYSYMBOL_on_error_statement = 104,       /* on_error_statement  */
  YYSYMBOL_error_statement = 105,          /* error_statement  */
  YYSYMBOL_return_statement = 106,         /* return_statement  */
  YYSYMBOL_label_statement = 107,          /* label_statement  */
  YYSYMBOL_goto_statement = 108,           /* goto_statement  */
  YYSYMBOL_gosub_statement = 109,          /* gosub_statement  */
  YYSYMBOL_break_statement = 110,          /* break_statement  */
  YYSYMBOL_continue_statement = 111,       /* continue_statement  */
  YYSYMBOL_if_statement = 112,             /* if_statement  */
  YYSYMBOL_inline_statement = 113,         /* inline_statement  */
  YYSYMBOL_expression = 114,               /* expression  */
  YYSYMBOL_or_expression = 115,            /* or_expression  */
  YYSYMBOL_and_expression = 116,           /* and_expression  */
  YYSYMBOL_comparison_expression = 117,    /* comparison_expression  */
  YYSYMBOL_additive_expression = 118,      /* additive_expression  */
  YYSYMBOL_multiplicative_expression = 119, /* multiplicative_expression  */
  YYSYMBOL_unary_expression = 120,         /* unary_expression  */
  YYSYMBOL_postfix_expression = 121,       /* postfix_expression  */
  YYSYMBOL_comparison_operator = 122,      /* comparison_operator  */
  YYSYMBOL_primary = 123,                  /* primary  */
  YYSYMBOL_ident_suffix = 124,             /* ident_suffix  */
  YYSYMBOL_ident_dot_suffix = 125,         /* ident_dot_suffix  */
  YYSYMBOL_duration_terms = 126,           /* duration_terms  */
  YYSYMBOL_argument_list_opt = 127,        /* argument_list_opt  */
  YYSYMBOL_argument_list = 128,            /* argument_list  */
  YYSYMBOL_array_argument_list = 129,      /* array_argument_list  */
  YYSYMBOL_parameter_list_opt = 130,       /* parameter_list_opt  */
  YYSYMBOL_parameter_list = 131,           /* parameter_list  */
  YYSYMBOL_record_field_list = 132,        /* record_field_list  */
  YYSYMBOL_optional_newlines = 133         /* optional_newlines  */
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
#define YYLAST   1000

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  75
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  59
/* YYNRULES -- Number of rules.  */
#define YYNRULES  181
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  385

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   329


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
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   409,   409,   413,   414,   415,   419,   420,   421,   422,
     423,   424,   425,   426,   427,   428,   429,   430,   431,   432,
     433,   434,   435,   436,   437,   438,   439,   440,   441,   445,
     446,   456,   457,   458,   462,   463,   464,   468,   472,   473,
     477,   478,   479,   480,   484,   488,   489,   495,   508,   520,
     526,   532,   538,   541,   547,   548,   552,   553,   554,   558,
     559,   560,   561,   562,   563,   564,   565,   566,   567,   568,
     569,   570,   571,   572,   573,   574,   575,   576,   577,   578,
     579,   580,   584,   590,   593,   599,   605,   611,   612,   613,
     614,   615,   626,   640,   641,   645,   649,   655,   661,   662,
     663,   667,   671,   672,   676,   680,   684,   688,   692,   696,
     699,   703,   709,   710,   714,   718,   719,   723,   724,   728,
     729,   730,   740,   741,   742,   746,   747,   748,   752,   753,
     754,   758,   759,   760,   764,   765,   766,   767,   768,   769,
     770,   771,   772,   773,   777,   778,   779,   780,   791,   797,
     798,   799,   800,   801,   802,   803,   804,   805,   806,   810,
     815,   820,   827,   832,   840,   844,   850,   851,   855,   856,
     860,   861,   865,   866,   870,   871,   875,   876,   877,   878,
     882,   883
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
  "IN", "WHILE", "CONSIDER", "BREAK", "CONTINUE", "FUNCTION", "RETURN",
  "GOTO", "GOSUB", "WATCH", "WITHOUT", "WATCHERS", "ON", "RESUME", "NEXT",
  "STOP", "ERROR_VALUE", "MODIFIER", "PROGRAM", "LIBRARY", "LOAD", "USE",
  "EXPORT", "OP_EQ", "OP_NE", "OP_GT", "OP_LT", "OP_GE", "OP_LE", "OP_NGT",
  "OP_NLT", "OP_NGE", "OP_NLE", "PLUS", "MINUS", "STAR", "SLASH", "LPAREN",
  "MOD_LPAREN", "RPAREN", "LBRACKET", "RBRACKET", "LBRACE", "RBRACE",
  "COMMA", "COLON", "NEWLINE", "NO_DOT", "DOT", "$accept", "program",
  "statement_list", "statement", "assignment", "lvalue", "variable_name",
  "modifier", "modifier_name", "modifier_word", "print_statement",
  "call_statement", "with_lock_statement", "for_each_statement",
  "while_statement", "consider_statement", "consider_branch_list",
  "consider_else_opt", "consider_statement_list",
  "consider_body_statement", "function_statement", "modifier_statement",
  "program_statement", "library_statement", "use_statement",
  "modifier_signature", "modifier_context", "watch_statement",
  "without_watchers_statement", "on_error_statement", "error_statement",
  "return_statement", "label_statement", "goto_statement",
  "gosub_statement", "break_statement", "continue_statement",
  "if_statement", "inline_statement", "expression", "or_expression",
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

#define YYPACT_NINF (-299)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -299,    29,   366,  -299,   -47,   -21,   240,  -299,   240,    40,
      42,   240,   240,  -299,  -299,    44,   240,    62,    69,    26,
      56,    55,  -299,    18,   116,   139,   168,   122,   199,   158,
    -299,  -299,   135,   151,   138,   147,   154,  -299,  -299,  -299,
    -299,  -299,  -299,  -299,  -299,   162,  -299,  -299,   165,   169,
     174,   182,   183,   189,   192,   195,  -299,   240,   240,   207,
    -299,  -299,   153,  -299,  -299,  -299,  -299,   240,  -299,   240,
     240,  -299,  -299,   -22,   258,   248,   250,  -299,   919,   144,
    -299,   -17,  -299,   268,  -299,   209,   247,   202,   203,   214,
    -299,  -299,  -299,   274,   212,    82,   275,  -299,  -299,  -299,
    -299,  -299,    14,  -299,   257,   222,   215,   282,  -299,   284,
    -299,   116,  -299,   240,   283,   240,   287,   243,  -299,  -299,
    -299,  -299,  -299,  -299,  -299,  -299,  -299,  -299,  -299,  -299,
    -299,   229,   225,   231,  -299,   240,  -299,  -299,   232,    91,
      11,   240,   294,  -299,    34,   240,   240,  -299,  -299,  -299,
    -299,  -299,  -299,  -299,  -299,  -299,  -299,   240,   240,   942,
     240,   240,   240,   240,   295,   297,   240,   240,  -299,   296,
     274,  -299,    -1,  -299,   300,   270,  -299,   249,   274,  -299,
     307,   274,  -299,   309,   311,   293,  -299,  -299,   254,  -299,
     240,  -299,   240,  -299,   260,  -299,  -299,  -299,  -299,   253,
     -39,  -299,   256,   264,   267,  -299,  -299,  -299,   262,   250,
    -299,   144,   144,   240,   180,  -299,  -299,   265,  -299,  -299,
     271,   266,   412,   240,   113,   272,   269,   273,   336,   458,
    -299,  -299,   240,   276,  -299,   285,   277,   504,  -299,  -299,
     307,  -299,  -299,  -299,  -299,  -299,     3,   240,   240,  -299,
      67,  -299,   240,  -299,   320,  -299,   180,  -299,   289,  -299,
     319,   359,   240,   299,   358,   303,  -299,  -299,   340,   312,
    -299,  -299,   306,   335,   310,   170,  -299,  -299,  -299,    16,
    -299,   318,   313,   376,  -299,   550,   314,   315,   378,  -299,
     331,  -299,   596,   333,  -299,   642,  -299,   343,  -299,  -299,
     -16,  -299,  -299,   345,   688,   367,  -299,  -299,   346,   734,
    -299,   780,   372,  -299,   379,   826,  -299,   872,   240,   240,
     918,  -299,   398,   351,   734,  -299,  -299,   352,   354,   356,
    -299,  -299,  -299,  -299,  -299,  -299,  -299,  -299,  -299,   357,
    -299,  -299,   360,   361,   362,   365,   377,   381,   389,   391,
    -299,   399,   392,   395,   387,   408,  -299,  -299,   460,   397,
    -299,   734,  -299,  -299,  -299,  -299,  -299,  -299,  -299,  -299,
    -299,  -299,  -299,  -299,   400,  -299,  -299,   402,   403,   404,
    -299,  -299,  -299,  -299,  -299
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       3,     0,     2,     1,    34,     0,     0,    35,     0,     0,
       0,     0,     0,   107,   108,     0,   102,     0,     0,     0,
       0,     0,    36,     0,     0,     0,     0,     0,     0,     0,
       4,     5,     0,     0,    31,     0,     0,     9,    10,    11,
      12,    13,    14,    15,    16,     0,    18,    19,     0,     0,
       0,     0,     0,     0,     0,     0,    28,   166,   166,   144,
      34,   146,     0,   150,   151,   152,   153,     0,   149,     0,
       0,   180,   180,   159,     0,   114,   115,   117,   119,   122,
     125,   128,   131,   145,    44,     0,     0,     0,     0,     0,
     103,   105,   106,     0,     0,     0,     0,   101,    40,    42,
      41,    43,    93,    38,     0,     0,     0,    88,    90,    87,
      89,     0,     6,     0,     0,     0,     0,     0,   104,     7,
       8,    17,    20,    21,    22,    23,    24,    25,    26,    27,
     168,     0,   167,     0,   164,   166,   129,   130,     0,     0,
       0,   166,     0,   147,     0,     0,     0,   134,   135,   136,
     137,   138,   139,   140,   141,   142,   143,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     3,     0,
     172,   174,     0,     3,     0,     0,   100,     0,   172,    39,
       0,   172,     3,     0,     0,     0,    29,    37,     0,    33,
       0,    45,     0,    46,     0,   154,   155,   181,   170,   180,
       0,   157,   180,     0,   162,     3,   112,   113,     0,   116,
     118,   123,   124,     0,   120,   126,   127,     0,   133,   165,
       0,     0,     0,     0,    54,     0,   173,     0,     0,     0,
      98,    99,   166,     0,    95,     0,     0,     0,    92,    91,
       0,    32,    30,   169,   148,   180,     0,     0,     0,   180,
       0,   160,   166,   161,     0,   111,   121,   132,     0,     3,
      35,     0,     0,     0,     0,     0,     3,   175,    35,     0,
      94,     3,     0,    35,     0,     0,   156,   176,   177,     0,
     158,     0,     0,    35,     3,     0,     0,     0,     0,    56,
       0,     3,     0,     0,    47,     0,     3,     0,     3,   171,
       0,   163,     3,     0,     0,    35,    50,    56,     0,    55,
      51,     0,    35,    97,    35,     0,    86,     0,     0,     0,
       0,   109,    35,     0,    52,    56,    57,     0,     0,     0,
      62,    63,    64,    65,    58,    66,    67,    68,    69,     0,
      71,    72,     0,     0,     0,     0,     0,     0,     0,     0,
      81,    35,     0,     0,    35,    35,   178,   179,    35,     0,
      49,    53,    59,    60,    61,    70,    73,    74,    75,    76,
      77,    78,    79,    80,     0,    96,    83,     0,     0,     0,
      48,    82,    85,    84,   110
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -299,  -299,  -154,  -299,  -298,  -299,    -2,   401,  -299,   368,
    -279,  -271,  -262,  -259,  -250,  -237,  -299,  -299,  -264,  -299,
    -233,  -206,  -190,  -171,  -164,   369,   237,  -163,  -162,  -159,
    -139,  -130,  -129,  -143,  -140,  -101,   -96,   -95,  -299,     1,
    -299,   338,   332,  -157,    93,   -61,  -299,   348,  -299,  -299,
    -299,  -299,   -56,  -299,  -299,   -29,   406,  -299,   -67
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,     2,    31,    32,    33,    73,   117,   102,   103,
      35,    36,    37,    38,    39,    40,   224,   264,   309,   334,
      41,    42,    43,    44,    45,   104,   235,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,   208,   130,
      75,    76,    77,    78,    79,    80,    81,   160,    82,   143,
     253,    83,   131,   132,   199,   225,   226,   202,   139
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      34,   206,   133,   214,   207,   140,   136,    74,   137,    84,
     247,   327,    87,    88,   222,   200,    57,    90,    98,   229,
     300,    59,    60,    61,    97,    62,   327,    99,   237,     3,
     328,     7,   248,   318,    63,    64,    65,    66,   329,   100,
      67,   141,    58,   324,    85,   328,    86,   330,    89,   163,
     331,   254,   142,   329,   101,   319,   256,   164,    22,   332,
      68,   361,   330,   327,   227,   331,    91,    17,    18,   228,
     276,   138,   333,    92,   332,   197,   335,   178,    69,   194,
     201,    70,   328,   197,    71,   203,    72,   333,   197,    93,
     329,   335,    96,    94,    59,    60,    61,    95,    62,   330,
     215,   216,   331,   336,     7,   285,   205,    63,    64,    65,
      66,   332,   292,    67,   186,   174,   188,   295,   336,   337,
      98,   175,   262,   176,   333,   263,   107,   108,   335,    99,
     304,    22,   246,    68,   337,   250,   280,   311,   338,   197,
     198,   100,   315,   105,   317,   339,   340,   341,   320,   233,
     342,    69,   236,   338,    70,   336,   101,    71,   196,    72,
     339,   340,   341,   197,   217,   342,   346,   220,   221,   347,
     343,   337,   106,    59,    60,    61,   269,    62,   275,   344,
     345,   346,   279,     7,   347,   343,    63,    64,    65,    66,
     338,   242,    67,   243,   344,   345,   281,   339,   340,   341,
     113,   111,   342,   109,   110,   161,   162,   112,   348,   118,
      22,   134,    68,   349,   350,   114,   135,   115,   346,   119,
      34,   347,   343,   348,   261,   116,   120,    34,   349,   350,
      69,   344,   345,    70,   121,    34,    71,   122,    72,   157,
     158,   123,   197,    59,    60,    61,   124,    62,   277,   278,
     211,   212,    34,     7,   125,   126,    63,    64,    65,    66,
     348,   127,    67,   288,   128,   349,   350,   129,   144,   145,
     146,   165,   166,   167,   168,   169,   299,   170,   171,   177,
      22,   180,    68,    34,   173,   181,   183,   182,   184,   187,
      34,   189,   190,    34,   191,   192,   193,   195,   204,   218,
      69,   219,    34,    70,   230,   223,    71,    34,    72,    34,
     231,   234,   232,    34,   238,    34,   239,   240,    34,   356,
     357,   241,    34,   245,     4,   244,   249,     5,     6,   251,
     252,   282,   257,   283,   255,     8,   258,   265,   259,   228,
     267,   270,   272,     9,    10,   266,   286,    11,    12,    13,
      14,    15,    16,    17,    18,    19,    20,   271,    21,    34,
      22,   284,    23,    24,    25,    26,    27,    28,    29,   287,
       4,   289,   290,     5,     6,   291,   293,   294,   296,     7,
     297,     8,   298,   301,   303,   302,   306,   307,   308,     9,
      10,   323,    30,    11,    12,    13,    14,    15,    16,    17,
      18,    19,    20,   310,    21,   313,    22,   352,    23,    24,
      25,    26,    27,    28,    29,   316,     4,   321,   325,     5,
       6,   359,   353,   360,   362,   260,   363,     8,   364,   365,
     374,   377,   366,   367,   368,     9,    10,   369,    30,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,   370,
      21,   378,    22,   371,    23,    24,    25,    26,    27,    28,
      29,   372,     4,   373,   375,     5,     6,   376,   379,   380,
     179,   268,   381,     8,   382,   383,   384,   274,   210,   159,
     185,     9,    10,   209,    30,    11,    12,    13,    14,    15,
      16,    17,    18,    19,    20,     0,    21,     0,    22,   172,
      23,    24,    25,    26,    27,    28,    29,   213,     4,     0,
       0,     5,     6,     0,     0,     0,     0,   273,     0,     8,
       0,     0,     0,     0,     0,     0,     0,     9,    10,     0,
      30,    11,    12,    13,    14,    15,    16,    17,    18,    19,
      20,     0,    21,     0,    22,     0,    23,    24,    25,    26,
      27,    28,    29,     0,     4,     0,     0,     5,     6,     0,
       0,     0,     0,   305,     0,     8,     0,     0,     0,     0,
       0,     0,     0,     9,    10,     0,    30,    11,    12,    13,
      14,    15,    16,    17,    18,    19,    20,     0,    21,     0,
      22,     0,    23,    24,    25,    26,    27,    28,    29,     0,
       4,     0,     0,     5,     6,     0,     0,     0,     0,   312,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
      10,     0,    30,    11,    12,    13,    14,    15,    16,    17,
      18,    19,    20,     0,    21,     0,    22,     0,    23,    24,
      25,    26,    27,    28,    29,     0,     4,     0,     0,     5,
       6,     0,     0,     0,     0,   314,     0,     8,     0,     0,
       0,     0,     0,     0,     0,     9,    10,     0,    30,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,     0,
      21,     0,    22,     0,    23,    24,    25,    26,    27,    28,
      29,     0,     4,     0,     0,     5,     6,     0,     0,     0,
       0,   322,     0,     8,     0,     0,     0,     0,     0,     0,
       0,     9,    10,     0,    30,    11,    12,    13,    14,    15,
      16,    17,    18,    19,    20,     0,    21,     0,    22,     0,
      23,    24,    25,    26,    27,    28,    29,     0,     4,     0,
       0,     5,     6,     0,     0,     0,     0,     7,     0,     8,
       0,     0,     0,     0,     0,     0,     0,     9,    10,     0,
      30,    11,    12,    13,    14,    15,    16,    17,    18,    19,
      20,     0,    21,     0,    22,     0,    23,    24,    25,    26,
      27,    28,    29,     0,     4,     0,     0,     5,     6,     0,
       0,     0,     0,   351,     0,     8,     0,     0,     0,     0,
       0,     0,     0,     9,    10,     0,   326,    11,    12,    13,
      14,    15,    16,    17,    18,    19,    20,     0,    21,     0,
      22,     0,    23,    24,    25,    26,    27,    28,    29,     0,
       4,     0,     0,     5,     6,     0,     0,     0,     0,   354,
       0,     8,     0,     0,     0,     0,     0,     0,     0,     9,
      10,     0,    30,    11,    12,    13,    14,    15,    16,    17,
      18,    19,    20,     0,    21,     0,    22,     0,    23,    24,
      25,    26,    27,    28,    29,     0,     4,     0,     0,     5,
       6,     0,     0,     0,     0,   355,     0,     8,     0,     0,
       0,     0,     0,     0,     0,     9,    10,     0,    30,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,     0,
      21,     0,    22,     0,    23,    24,    25,    26,    27,    28,
      29,     0,     4,     0,     0,     5,     6,     0,     0,     0,
       0,   358,     0,     8,     0,     0,     0,     0,     0,     0,
       0,     9,    10,     0,    30,    11,    12,    13,    14,    15,
      16,    17,    18,    19,    20,     0,    21,     0,    22,     0,
      23,    24,    25,    26,    27,    28,    29,     0,   147,   148,
     149,   150,   151,   152,   153,   154,   155,   156,   157,   158,
       0,     0,     0,   114,     0,     0,     0,     0,     0,     0,
      30,   147,   148,   149,   150,   151,   152,   153,   154,   155,
     156
};

static const yytype_int16 yycheck[] =
{
       2,   144,    58,   160,   144,    72,    67,     6,    69,     8,
      49,   309,    11,    12,   168,     4,    63,    16,     4,   173,
       4,     3,     4,     5,    23,     7,   324,    13,   182,     0,
     309,    13,    71,    49,    16,    17,    18,    19,   309,    25,
      22,    63,    63,   307,     4,   324,     4,   309,     4,    66,
     309,   205,    74,   324,    40,    71,   213,    74,    40,   309,
      42,   325,   324,   361,    65,   324,     4,    33,    34,    70,
      67,    70,   309,     4,   324,    72,   309,    63,    60,   135,
      69,    63,   361,    72,    66,   141,    68,   324,    72,    63,
     361,   324,    74,    37,     3,     4,     5,    42,     7,   361,
     161,   162,   361,   309,    13,   259,    72,    16,    17,    18,
      19,   361,   266,    22,   113,    33,   115,   271,   324,   309,
       4,    39,     9,    41,   361,    12,     4,     5,   361,    13,
     284,    40,   199,    42,   324,   202,    69,   291,   309,    72,
     139,    25,   296,     4,   298,   309,   309,   309,   302,   178,
     309,    60,   181,   324,    63,   361,    40,    66,    67,    68,
     324,   324,   324,    72,   163,   324,   309,   166,   167,   309,
     309,   361,     4,     3,     4,     5,   232,     7,   245,   309,
     309,   324,   249,    13,   324,   324,    16,    17,    18,    19,
     361,   190,    22,   192,   324,   324,   252,   361,   361,   361,
      49,    43,   361,     4,     5,    61,    62,    72,   309,    71,
      40,     4,    42,   309,   309,    64,    63,    66,   361,    72,
     222,   361,   361,   324,   223,    74,    72,   229,   324,   324,
      60,   361,   361,    63,    72,   237,    66,    72,    68,    59,
      60,    72,    72,     3,     4,     5,    72,     7,   247,   248,
     157,   158,   254,    13,    72,    72,    16,    17,    18,    19,
     361,    72,    22,   262,    72,   361,   361,    72,    10,    21,
      20,     3,    63,    26,    72,    72,   275,    63,     4,     4,
      40,    24,    42,   285,    72,    63,     4,    72,     4,     6,
     292,     4,    49,   295,    65,    70,    65,    65,     4,     4,
      60,     4,   304,    63,     4,     9,    66,   309,    68,   311,
      40,     4,    63,   315,     5,   317,     5,    24,   320,   318,
     319,    67,   324,    70,     4,    65,    70,     7,     8,    65,
      63,    11,    67,    13,    72,    15,    65,    65,    72,    70,
       4,    65,    65,    23,    24,    72,    27,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    72,    38,   361,
      40,    72,    42,    43,    44,    45,    46,    47,    48,    10,
       4,    72,    14,     7,     8,    72,    36,    65,    72,    13,
      45,    15,    72,    65,     8,    72,    72,    72,    10,    23,
      24,    24,    72,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    72,    38,    72,    40,    35,    42,    43,
      44,    45,    46,    47,    48,    72,     4,    72,    72,     7,
       8,    23,    43,    72,    72,    13,    72,    15,    72,    72,
      31,    44,    72,    72,    72,    23,    24,    72,    72,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    72,
      38,    43,    40,    72,    42,    43,    44,    45,    46,    47,
      48,    72,     4,    72,    72,     7,     8,    72,     8,    72,
     102,    13,    72,    15,    72,    72,    72,   240,   146,    78,
     111,    23,    24,   145,    72,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    -1,    38,    -1,    40,    93,
      42,    43,    44,    45,    46,    47,    48,   159,     4,    -1,
      -1,     7,     8,    -1,    -1,    -1,    -1,    13,    -1,    15,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    23,    24,    -1,
      72,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    -1,    38,    -1,    40,    -1,    42,    43,    44,    45,
      46,    47,    48,    -1,     4,    -1,    -1,     7,     8,    -1,
      -1,    -1,    -1,    13,    -1,    15,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    23,    24,    -1,    72,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    -1,    38,    -1,
      40,    -1,    42,    43,    44,    45,    46,    47,    48,    -1,
       4,    -1,    -1,     7,     8,    -1,    -1,    -1,    -1,    13,
      -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    23,
      24,    -1,    72,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    -1,    38,    -1,    40,    -1,    42,    43,
      44,    45,    46,    47,    48,    -1,     4,    -1,    -1,     7,
       8,    -1,    -1,    -1,    -1,    13,    -1,    15,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    23,    24,    -1,    72,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    -1,
      38,    -1,    40,    -1,    42,    43,    44,    45,    46,    47,
      48,    -1,     4,    -1,    -1,     7,     8,    -1,    -1,    -1,
      -1,    13,    -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    23,    24,    -1,    72,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    -1,    38,    -1,    40,    -1,
      42,    43,    44,    45,    46,    47,    48,    -1,     4,    -1,
      -1,     7,     8,    -1,    -1,    -1,    -1,    13,    -1,    15,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    23,    24,    -1,
      72,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    -1,    38,    -1,    40,    -1,    42,    43,    44,    45,
      46,    47,    48,    -1,     4,    -1,    -1,     7,     8,    -1,
      -1,    -1,    -1,    13,    -1,    15,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    23,    24,    -1,    72,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    -1,    38,    -1,
      40,    -1,    42,    43,    44,    45,    46,    47,    48,    -1,
       4,    -1,    -1,     7,     8,    -1,    -1,    -1,    -1,    13,
      -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    23,
      24,    -1,    72,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    -1,    38,    -1,    40,    -1,    42,    43,
      44,    45,    46,    47,    48,    -1,     4,    -1,    -1,     7,
       8,    -1,    -1,    -1,    -1,    13,    -1,    15,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    23,    24,    -1,    72,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    -1,
      38,    -1,    40,    -1,    42,    43,    44,    45,    46,    47,
      48,    -1,     4,    -1,    -1,     7,     8,    -1,    -1,    -1,
      -1,    13,    -1,    15,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    23,    24,    -1,    72,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    -1,    38,    -1,    40,    -1,
      42,    43,    44,    45,    46,    47,    48,    -1,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      -1,    -1,    -1,    64,    -1,    -1,    -1,    -1,    -1,    -1,
      72,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    76,    77,     0,     4,     7,     8,    13,    15,    23,
      24,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    38,    40,    42,    43,    44,    45,    46,    47,    48,
      72,    78,    79,    80,    81,    85,    86,    87,    88,    89,
      90,    95,    96,    97,    98,    99,   102,   103,   104,   105,
     106,   107,   108,   109,   110,   111,   112,    63,    63,     3,
       4,     5,     7,    16,    17,    18,    19,    22,    42,    60,
      63,    66,    68,    81,   114,   115,   116,   117,   118,   119,
     120,   121,   123,   126,   114,     4,     4,   114,   114,     4,
     114,     4,     4,    63,    37,    42,    74,   114,     4,    13,
      25,    40,    83,    84,   100,     4,     4,     4,     5,     4,
       5,    43,    72,    49,    64,    66,    74,    82,    71,    72,
      72,    72,    72,    72,    72,    72,    72,    72,    72,    72,
     114,   127,   128,   127,     4,    63,   120,   120,   114,   133,
     133,    63,    74,   124,    10,    21,    20,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    82,
     122,    61,    62,    66,    74,     3,    63,    26,    72,    72,
      63,     4,   131,    72,    33,    39,    41,     4,    63,    84,
      24,    63,    72,     4,     4,   100,   114,     6,   114,     4,
      49,    65,    70,    65,   127,    65,    67,    72,   114,   129,
       4,    69,   132,   127,     4,    72,   108,   109,   113,   116,
     117,   119,   119,   122,   118,   120,   120,   114,     4,     4,
     114,   114,    77,     9,    91,   130,   131,    65,    70,    77,
       4,    40,    63,   130,     4,   101,   130,    77,     5,     5,
      24,    67,   114,   114,    65,    70,   133,    49,    71,    70,
     133,    65,    63,   125,    77,    72,   118,    67,    65,    72,
      13,   114,     9,    12,    92,    65,    72,     4,    13,   127,
      65,    72,    65,    13,   101,   133,    67,   114,   114,   133,
      69,   127,    11,    13,    72,    77,    27,    10,   114,    72,
      14,    72,    77,    36,    65,    77,    72,    45,    72,   114,
       4,    65,    72,     8,    77,    13,    72,    72,    10,    93,
      72,    77,    13,    72,    13,    77,    72,    77,    49,    71,
      77,    72,    13,    24,    93,    72,    72,    79,    85,    86,
      87,    88,    89,    90,    94,    95,    96,    97,    98,    99,
     102,   103,   104,   105,   106,   107,   108,   109,   110,   111,
     112,    13,    35,    43,    13,    13,   114,   114,    13,    23,
      72,    93,    72,    72,    72,    72,    72,    72,    72,    72,
      72,    72,    72,    72,    31,    72,    72,    44,    43,     8,
      72,    72,    72,    72,    72
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    75,    76,    77,    77,    77,    78,    78,    78,    78,
      78,    78,    78,    78,    78,    78,    78,    78,    78,    78,
      78,    78,    78,    78,    78,    78,    78,    78,    78,    79,
      79,    80,    80,    80,    81,    81,    81,    82,    83,    83,
      84,    84,    84,    84,    85,    86,    86,    86,    87,    88,
      89,    90,    91,    91,    92,    92,    93,    93,    93,    94,
      94,    94,    94,    94,    94,    94,    94,    94,    94,    94,
      94,    94,    94,    94,    94,    94,    94,    94,    94,    94,
      94,    94,    95,    96,    96,    97,    98,    99,    99,    99,
      99,    99,    99,   100,   100,   101,   102,   103,   104,   104,
     104,   105,   106,   106,   107,   108,   109,   110,   111,   112,
     112,   112,   113,   113,   114,   115,   115,   116,   116,   117,
     117,   117,   118,   118,   118,   119,   119,   119,   120,   120,
     120,   121,   121,   121,   122,   122,   122,   122,   122,   122,
     122,   122,   122,   122,   123,   123,   123,   123,   123,   123,
     123,   123,   123,   123,   123,   123,   123,   123,   123,   124,
     124,   124,   125,   125,   126,   126,   127,   127,   128,   128,
     129,   129,   130,   130,   131,   131,   132,   132,   132,   132,
     133,   133
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     2,     2,     2,     2,     1,
       1,     1,     1,     1,     1,     1,     1,     2,     1,     1,
       2,     2,     2,     2,     2,     2,     2,     2,     1,     3,
       4,     1,     4,     3,     1,     1,     1,     2,     1,     2,
       1,     1,     1,     1,     2,     4,     4,     6,    10,     9,
       7,     7,     5,     6,     0,     3,     0,     2,     2,     2,
       2,     2,     1,     1,     1,     1,     1,     1,     1,     1,
       2,     1,     1,     2,     2,     2,     2,     2,     2,     2,
       2,     1,    10,     9,    10,    10,     7,     2,     2,     2,
       2,     4,     4,     1,     4,     1,     9,     7,     4,     4,
       3,     2,     1,     2,     2,     2,     2,     1,     1,     8,
      11,     5,     1,     1,     1,     1,     3,     1,     3,     1,
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
#line 409 "src/parser.y"
                     { parsed_program = (yyvsp[0].stmt_list); (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 2324 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 413 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 2330 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 414 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 2336 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 415 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 2342 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 419 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2348 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 420 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2354 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 421 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2360 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 422 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2366 "src/parser.tab.c"
    break;

  case 10: /* statement: for_each_statement  */
#line 423 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2372 "src/parser.tab.c"
    break;

  case 11: /* statement: while_statement  */
#line 424 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2378 "src/parser.tab.c"
    break;

  case 12: /* statement: consider_statement  */
#line 425 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2384 "src/parser.tab.c"
    break;

  case 13: /* statement: function_statement  */
#line 426 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2390 "src/parser.tab.c"
    break;

  case 14: /* statement: modifier_statement  */
#line 427 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2396 "src/parser.tab.c"
    break;

  case 15: /* statement: program_statement  */
#line 428 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2402 "src/parser.tab.c"
    break;

  case 16: /* statement: library_statement  */
#line 429 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2408 "src/parser.tab.c"
    break;

  case 17: /* statement: use_statement NEWLINE  */
#line 430 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2414 "src/parser.tab.c"
    break;

  case 18: /* statement: watch_statement  */
#line 431 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2420 "src/parser.tab.c"
    break;

  case 19: /* statement: without_watchers_statement  */
#line 432 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2426 "src/parser.tab.c"
    break;

  case 20: /* statement: on_error_statement NEWLINE  */
#line 433 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2432 "src/parser.tab.c"
    break;

  case 21: /* statement: error_statement NEWLINE  */
#line 434 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2438 "src/parser.tab.c"
    break;

  case 22: /* statement: return_statement NEWLINE  */
#line 435 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2444 "src/parser.tab.c"
    break;

  case 23: /* statement: label_statement NEWLINE  */
#line 436 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2450 "src/parser.tab.c"
    break;

  case 24: /* statement: goto_statement NEWLINE  */
#line 437 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2456 "src/parser.tab.c"
    break;

  case 25: /* statement: gosub_statement NEWLINE  */
#line 438 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2462 "src/parser.tab.c"
    break;

  case 26: /* statement: break_statement NEWLINE  */
#line 439 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2468 "src/parser.tab.c"
    break;

  case 27: /* statement: continue_statement NEWLINE  */
#line 440 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2474 "src/parser.tab.c"
    break;

  case 28: /* statement: if_statement  */
#line 441 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2480 "src/parser.tab.c"
    break;

  case 29: /* assignment: lvalue OP_EQ expression  */
#line 445 "src/parser.y"
                              { (yyval.stmt) = ast_assign((yyvsp[-2].expr), ast_modifier_none(), (yyvsp[0].expr)); }
#line 2486 "src/parser.tab.c"
    break;

  case 30: /* assignment: lvalue modifier OP_EQ expression  */
#line 446 "src/parser.y"
                                       {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            yyerror("modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.stmt) = ast_assign((yyvsp[-3].expr), (yyvsp[-2].modifier), (yyvsp[0].expr));
      }
#line 2498 "src/parser.tab.c"
    break;

  case 31: /* lvalue: variable_name  */
#line 456 "src/parser.y"
                                 { (yyval.expr) = ast_ident((yyvsp[0].text)); }
#line 2504 "src/parser.tab.c"
    break;

  case 32: /* lvalue: lvalue LBRACKET expression RBRACKET  */
#line 457 "src/parser.y"
                                                       { (yyval.expr) = ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)); }
#line 2510 "src/parser.tab.c"
    break;

  case 33: /* lvalue: lvalue DOT IDENT  */
#line 458 "src/parser.y"
                                    { (yyval.expr) = ast_field((yyvsp[-2].expr), (yyvsp[0].text)); }
#line 2516 "src/parser.tab.c"
    break;

  case 34: /* variable_name: IDENT  */
#line 462 "src/parser.y"
                         { (yyval.text) = (yyvsp[0].text); }
#line 2522 "src/parser.tab.c"
    break;

  case 35: /* variable_name: END  */
#line 463 "src/parser.y"
                       { (yyval.text) = copy_const("end"); }
#line 2528 "src/parser.tab.c"
    break;

  case 36: /* variable_name: NEXT  */
#line 464 "src/parser.y"
                        { (yyval.text) = copy_const("next"); }
#line 2534 "src/parser.tab.c"
    break;

  case 37: /* modifier: MOD_LPAREN MOD_CONTENT  */
#line 468 "src/parser.y"
                             { (yyval.modifier) = parse_modifier_use((yyvsp[0].text)); }
#line 2540 "src/parser.tab.c"
    break;

  case 38: /* modifier_name: modifier_word  */
#line 472 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 2546 "src/parser.tab.c"
    break;

  case 39: /* modifier_name: modifier_name modifier_word  */
#line 473 "src/parser.y"
                                  { (yyval.text) = join_words((yyvsp[-1].text), (yyvsp[0].text)); }
#line 2552 "src/parser.tab.c"
    break;

  case 40: /* modifier_word: IDENT  */
#line 477 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 2558 "src/parser.tab.c"
    break;

  case 41: /* modifier_word: TO  */
#line 478 "src/parser.y"
         { (yyval.text) = copy_const("to"); }
#line 2564 "src/parser.tab.c"
    break;

  case 42: /* modifier_word: END  */
#line 479 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 2570 "src/parser.tab.c"
    break;

  case 43: /* modifier_word: NEXT  */
#line 480 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 2576 "src/parser.tab.c"
    break;

  case 44: /* print_statement: PRINT expression  */
#line 484 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 2582 "src/parser.tab.c"
    break;

  case 45: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 488 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 2588 "src/parser.tab.c"
    break;

  case 46: /* call_statement: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 489 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.stmt) = ast_expr_stmt(ast_qualified_call(library, name, (yyvsp[-1].expr_list)));
      }
#line 2599 "src/parser.tab.c"
    break;

  case 47: /* call_statement: ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 495 "src/parser.y"
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
#line 2614 "src/parser.tab.c"
    break;

  case 48: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 508 "src/parser.y"
                                                                                  {
        if (strcmp((yyvsp[-8].text), "lock") != 0) {
            yyerror("expected lock in with lock block");
            free((yyvsp[-8].text));
            YYERROR;
        }
        free((yyvsp[-8].text));
        (yyval.stmt) = ast_with_lock((yyvsp[-6].expr), (yyvsp[-3].stmt_list));
      }
#line 2628 "src/parser.tab.c"
    break;

  case 49: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list END FOR NEWLINE  */
#line 520 "src/parser.y"
                                                                     {
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 2636 "src/parser.tab.c"
    break;

  case 50: /* while_statement: WHILE expression NEWLINE statement_list END WHILE NEWLINE  */
#line 526 "src/parser.y"
                                                                {
        (yyval.stmt) = ast_while((yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 2644 "src/parser.tab.c"
    break;

  case 51: /* consider_statement: CONSIDER expression NEWLINE consider_branch_list consider_else_opt END_CONSIDER NEWLINE  */
#line 532 "src/parser.y"
                                                                                              {
        (yyval.stmt) = ast_consider((yyvsp[-5].expr), (yyvsp[-3].consider_branch_list), (yyvsp[-2].stmt_list));
      }
#line 2652 "src/parser.tab.c"
    break;

  case 52: /* consider_branch_list: CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 538 "src/parser.y"
                                                                  {
        (yyval.consider_branch_list) = ast_consider_branch_list_append(ast_consider_branch_list_empty(), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 2660 "src/parser.tab.c"
    break;

  case 53: /* consider_branch_list: consider_branch_list CONSIDER_IF expression THEN NEWLINE consider_statement_list  */
#line 541 "src/parser.y"
                                                                                       {
        (yyval.consider_branch_list) = ast_consider_branch_list_append((yyvsp[-5].consider_branch_list), (yyvsp[-3].expr), (yyvsp[0].stmt_list));
      }
#line 2668 "src/parser.tab.c"
    break;

  case 54: /* consider_else_opt: %empty  */
#line 547 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 2674 "src/parser.tab.c"
    break;

  case 55: /* consider_else_opt: CONSIDER_ELSE NEWLINE consider_statement_list  */
#line 548 "src/parser.y"
                                                    { (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 2680 "src/parser.tab.c"
    break;

  case 56: /* consider_statement_list: %empty  */
#line 552 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 2686 "src/parser.tab.c"
    break;

  case 57: /* consider_statement_list: consider_statement_list NEWLINE  */
#line 553 "src/parser.y"
                                      { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 2692 "src/parser.tab.c"
    break;

  case 58: /* consider_statement_list: consider_statement_list consider_body_statement  */
#line 554 "src/parser.y"
                                                      { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 2698 "src/parser.tab.c"
    break;

  case 59: /* consider_body_statement: assignment NEWLINE  */
#line 558 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2704 "src/parser.tab.c"
    break;

  case 60: /* consider_body_statement: print_statement NEWLINE  */
#line 559 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2710 "src/parser.tab.c"
    break;

  case 61: /* consider_body_statement: call_statement NEWLINE  */
#line 560 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2716 "src/parser.tab.c"
    break;

  case 62: /* consider_body_statement: with_lock_statement  */
#line 561 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2722 "src/parser.tab.c"
    break;

  case 63: /* consider_body_statement: for_each_statement  */
#line 562 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2728 "src/parser.tab.c"
    break;

  case 64: /* consider_body_statement: while_statement  */
#line 563 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2734 "src/parser.tab.c"
    break;

  case 65: /* consider_body_statement: consider_statement  */
#line 564 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2740 "src/parser.tab.c"
    break;

  case 66: /* consider_body_statement: function_statement  */
#line 565 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2746 "src/parser.tab.c"
    break;

  case 67: /* consider_body_statement: modifier_statement  */
#line 566 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2752 "src/parser.tab.c"
    break;

  case 68: /* consider_body_statement: program_statement  */
#line 567 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2758 "src/parser.tab.c"
    break;

  case 69: /* consider_body_statement: library_statement  */
#line 568 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2764 "src/parser.tab.c"
    break;

  case 70: /* consider_body_statement: use_statement NEWLINE  */
#line 569 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2770 "src/parser.tab.c"
    break;

  case 71: /* consider_body_statement: watch_statement  */
#line 570 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2776 "src/parser.tab.c"
    break;

  case 72: /* consider_body_statement: without_watchers_statement  */
#line 571 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2782 "src/parser.tab.c"
    break;

  case 73: /* consider_body_statement: on_error_statement NEWLINE  */
#line 572 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2788 "src/parser.tab.c"
    break;

  case 74: /* consider_body_statement: error_statement NEWLINE  */
#line 573 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2794 "src/parser.tab.c"
    break;

  case 75: /* consider_body_statement: return_statement NEWLINE  */
#line 574 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2800 "src/parser.tab.c"
    break;

  case 76: /* consider_body_statement: label_statement NEWLINE  */
#line 575 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2806 "src/parser.tab.c"
    break;

  case 77: /* consider_body_statement: goto_statement NEWLINE  */
#line 576 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2812 "src/parser.tab.c"
    break;

  case 78: /* consider_body_statement: gosub_statement NEWLINE  */
#line 577 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2818 "src/parser.tab.c"
    break;

  case 79: /* consider_body_statement: break_statement NEWLINE  */
#line 578 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2824 "src/parser.tab.c"
    break;

  case 80: /* consider_body_statement: continue_statement NEWLINE  */
#line 579 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2830 "src/parser.tab.c"
    break;

  case 81: /* consider_body_statement: if_statement  */
#line 580 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2836 "src/parser.tab.c"
    break;

  case 82: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 584 "src/parser.y"
                                                                                                  {
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 2844 "src/parser.tab.c"
    break;

  case 83: /* modifier_statement: MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 590 "src/parser.y"
                                                                                                   {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 0, (yyvsp[-3].stmt_list));
      }
#line 2852 "src/parser.tab.c"
    break;

  case 84: /* modifier_statement: EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 593 "src/parser.y"
                                                                                                          {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 1, (yyvsp[-3].stmt_list));
      }
#line 2860 "src/parser.tab.c"
    break;

  case 85: /* program_statement: PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE  */
#line 599 "src/parser.y"
                                                                                                {
        (yyval.stmt) = ast_program((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 2868 "src/parser.tab.c"
    break;

  case 86: /* library_statement: LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE  */
#line 605 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_library((yyvsp[-5].text), (yyvsp[-3].stmt_list));
      }
#line 2876 "src/parser.tab.c"
    break;

  case 87: /* use_statement: USE IDENT  */
#line 611 "src/parser.y"
                { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 2882 "src/parser.tab.c"
    break;

  case 88: /* use_statement: LOAD IDENT  */
#line 612 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 2888 "src/parser.tab.c"
    break;

  case 89: /* use_statement: USE STRING  */
#line 613 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 2894 "src/parser.tab.c"
    break;

  case 90: /* use_statement: LOAD STRING  */
#line 614 "src/parser.y"
                  { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 2900 "src/parser.tab.c"
    break;

  case 91: /* use_statement: USE IDENT IDENT STRING  */
#line 615 "src/parser.y"
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
#line 2916 "src/parser.tab.c"
    break;

  case 92: /* use_statement: LOAD IDENT IDENT STRING  */
#line 626 "src/parser.y"
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
#line 2932 "src/parser.tab.c"
    break;

  case 93: /* modifier_signature: modifier_name  */
#line 640 "src/parser.y"
                    { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[0].text), ast_name_list_empty()); }
#line 2938 "src/parser.tab.c"
    break;

  case 94: /* modifier_signature: modifier_name LPAREN parameter_list_opt RPAREN  */
#line 641 "src/parser.y"
                                                     { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[-3].text), (yyvsp[-1].name_list)); }
#line 2944 "src/parser.tab.c"
    break;

  case 95: /* modifier_context: IDENT  */
#line 645 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 2950 "src/parser.tab.c"
    break;

  case 96: /* watch_statement: WATCH LPAREN parameter_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 649 "src/parser.y"
                                                                                  {
        (yyval.stmt) = ast_watch((yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 2958 "src/parser.tab.c"
    break;

  case 97: /* without_watchers_statement: WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE  */
#line 655 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_without_watchers((yyvsp[-3].stmt_list));
      }
#line 2966 "src/parser.tab.c"
    break;

  case 98: /* on_error_statement: ON ERROR_VALUE GOTO IDENT  */
#line 661 "src/parser.y"
                                { (yyval.stmt) = ast_on_error_goto((yyvsp[0].text)); }
#line 2972 "src/parser.tab.c"
    break;

  case 99: /* on_error_statement: ON ERROR_VALUE RESUME NEXT  */
#line 662 "src/parser.y"
                                 { (yyval.stmt) = ast_on_error_resume_next(); }
#line 2978 "src/parser.tab.c"
    break;

  case 100: /* on_error_statement: ON ERROR_VALUE STOP  */
#line 663 "src/parser.y"
                          { (yyval.stmt) = ast_on_error_stop(); }
#line 2984 "src/parser.tab.c"
    break;

  case 101: /* error_statement: ERROR_VALUE expression  */
#line 667 "src/parser.y"
                             { (yyval.stmt) = ast_error((yyvsp[0].expr)); }
#line 2990 "src/parser.tab.c"
    break;

  case 102: /* return_statement: RETURN  */
#line 671 "src/parser.y"
             { (yyval.stmt) = ast_return(NULL); }
#line 2996 "src/parser.tab.c"
    break;

  case 103: /* return_statement: RETURN expression  */
#line 672 "src/parser.y"
                        { (yyval.stmt) = ast_return((yyvsp[0].expr)); }
#line 3002 "src/parser.tab.c"
    break;

  case 104: /* label_statement: variable_name COLON  */
#line 676 "src/parser.y"
                          { (yyval.stmt) = ast_label((yyvsp[-1].text)); }
#line 3008 "src/parser.tab.c"
    break;

  case 105: /* goto_statement: GOTO IDENT  */
#line 680 "src/parser.y"
                 { (yyval.stmt) = ast_goto((yyvsp[0].text)); }
#line 3014 "src/parser.tab.c"
    break;

  case 106: /* gosub_statement: GOSUB IDENT  */
#line 684 "src/parser.y"
                  { (yyval.stmt) = ast_gosub((yyvsp[0].text)); }
#line 3020 "src/parser.tab.c"
    break;

  case 107: /* break_statement: BREAK  */
#line 688 "src/parser.y"
            { (yyval.stmt) = ast_break(); }
#line 3026 "src/parser.tab.c"
    break;

  case 108: /* continue_statement: CONTINUE  */
#line 692 "src/parser.y"
               { (yyval.stmt) = ast_continue(); }
#line 3032 "src/parser.tab.c"
    break;

  case 109: /* if_statement: IF expression THEN NEWLINE statement_list END IF NEWLINE  */
#line 696 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_if((yyvsp[-6].expr), (yyvsp[-3].stmt_list));
      }
#line 3040 "src/parser.tab.c"
    break;

  case 110: /* if_statement: IF expression THEN NEWLINE statement_list ELSE NEWLINE statement_list END IF NEWLINE  */
#line 699 "src/parser.y"
                                                                                           {
        (yyval.stmt) = ast_if((yyvsp[-9].expr), (yyvsp[-6].stmt_list));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[-3].stmt_list);
      }
#line 3049 "src/parser.tab.c"
    break;

  case 111: /* if_statement: IF expression THEN inline_statement NEWLINE  */
#line 703 "src/parser.y"
                                                  {
        (yyval.stmt) = ast_if((yyvsp[-3].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt)));
      }
#line 3057 "src/parser.tab.c"
    break;

  case 112: /* inline_statement: goto_statement  */
#line 709 "src/parser.y"
                     { (yyval.stmt) = (yyvsp[0].stmt); }
#line 3063 "src/parser.tab.c"
    break;

  case 113: /* inline_statement: gosub_statement  */
#line 710 "src/parser.y"
                      { (yyval.stmt) = (yyvsp[0].stmt); }
#line 3069 "src/parser.tab.c"
    break;

  case 114: /* expression: or_expression  */
#line 714 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 3075 "src/parser.tab.c"
    break;

  case 115: /* or_expression: and_expression  */
#line 718 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 3081 "src/parser.tab.c"
    break;

  case 116: /* or_expression: or_expression OR and_expression  */
#line 719 "src/parser.y"
                                      { (yyval.expr) = ast_binary(copy_const("or"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 3087 "src/parser.tab.c"
    break;

  case 117: /* and_expression: comparison_expression  */
#line 723 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 3093 "src/parser.tab.c"
    break;

  case 118: /* and_expression: and_expression AND comparison_expression  */
#line 724 "src/parser.y"
                                               { (yyval.expr) = ast_binary(copy_const("and"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 3099 "src/parser.tab.c"
    break;

  case 119: /* comparison_expression: additive_expression  */
#line 728 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 3105 "src/parser.tab.c"
    break;

  case 120: /* comparison_expression: additive_expression comparison_operator additive_expression  */
#line 729 "src/parser.y"
                                                                  { (yyval.expr) = ast_binary((yyvsp[-1].text), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 3111 "src/parser.tab.c"
    break;

  case 121: /* comparison_expression: additive_expression modifier comparison_operator additive_expression  */
#line 730 "src/parser.y"
                                                                           {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            yyerror("modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.expr) = ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr));
      }
#line 3123 "src/parser.tab.c"
    break;

  case 122: /* additive_expression: multiplicative_expression  */
#line 740 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 3129 "src/parser.tab.c"
    break;

  case 123: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 741 "src/parser.y"
                                                         { (yyval.expr) = ast_binary(copy_const("+"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 3135 "src/parser.tab.c"
    break;

  case 124: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 742 "src/parser.y"
                                                          { (yyval.expr) = ast_binary(copy_const("-"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 3141 "src/parser.tab.c"
    break;

  case 125: /* multiplicative_expression: unary_expression  */
#line 746 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 3147 "src/parser.tab.c"
    break;

  case 126: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 747 "src/parser.y"
                                                      { (yyval.expr) = ast_binary(copy_const("*"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 3153 "src/parser.tab.c"
    break;

  case 127: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 748 "src/parser.y"
                                                       { (yyval.expr) = ast_binary(copy_const("/"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 3159 "src/parser.tab.c"
    break;

  case 128: /* unary_expression: postfix_expression  */
#line 752 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 3165 "src/parser.tab.c"
    break;

  case 129: /* unary_expression: NOT unary_expression  */
#line 753 "src/parser.y"
                           { (yyval.expr) = ast_unary(copy_const("not"), (yyvsp[0].expr)); }
#line 3171 "src/parser.tab.c"
    break;

  case 130: /* unary_expression: MINUS unary_expression  */
#line 754 "src/parser.y"
                             { (yyval.expr) = ast_unary(copy_const("-"), (yyvsp[0].expr)); }
#line 3177 "src/parser.tab.c"
    break;

  case 131: /* postfix_expression: primary  */
#line 758 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 3183 "src/parser.tab.c"
    break;

  case 132: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 759 "src/parser.y"
                                                      { (yyval.expr) = ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)); }
#line 3189 "src/parser.tab.c"
    break;

  case 133: /* postfix_expression: postfix_expression DOT IDENT  */
#line 760 "src/parser.y"
                                   { (yyval.expr) = ast_field((yyvsp[-2].expr), (yyvsp[0].text)); }
#line 3195 "src/parser.tab.c"
    break;

  case 134: /* comparison_operator: OP_EQ  */
#line 764 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 3201 "src/parser.tab.c"
    break;

  case 135: /* comparison_operator: OP_NE  */
#line 765 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 3207 "src/parser.tab.c"
    break;

  case 136: /* comparison_operator: OP_GT  */
#line 766 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 3213 "src/parser.tab.c"
    break;

  case 137: /* comparison_operator: OP_LT  */
#line 767 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 3219 "src/parser.tab.c"
    break;

  case 138: /* comparison_operator: OP_GE  */
#line 768 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 3225 "src/parser.tab.c"
    break;

  case 139: /* comparison_operator: OP_LE  */
#line 769 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 3231 "src/parser.tab.c"
    break;

  case 140: /* comparison_operator: OP_NGT  */
#line 770 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 3237 "src/parser.tab.c"
    break;

  case 141: /* comparison_operator: OP_NLT  */
#line 771 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 3243 "src/parser.tab.c"
    break;

  case 142: /* comparison_operator: OP_NGE  */
#line 772 "src/parser.y"
             { (yyval.text) = copy_const("!>="); }
#line 3249 "src/parser.tab.c"
    break;

  case 143: /* comparison_operator: OP_NLE  */
#line 773 "src/parser.y"
             { (yyval.text) = copy_const("!<="); }
#line 3255 "src/parser.tab.c"
    break;

  case 144: /* primary: NUMBER  */
#line 777 "src/parser.y"
             { (yyval.expr) = ast_number((yyvsp[0].number)); }
#line 3261 "src/parser.tab.c"
    break;

  case 145: /* primary: duration_terms  */
#line 778 "src/parser.y"
                     { (yyval.expr) = ast_duration((yyvsp[0].duration)); }
#line 3267 "src/parser.tab.c"
    break;

  case 146: /* primary: STRING  */
#line 779 "src/parser.y"
             { (yyval.expr) = ast_string((yyvsp[0].text)); }
#line 3273 "src/parser.tab.c"
    break;

  case 147: /* primary: variable_name ident_suffix  */
#line 780 "src/parser.y"
                                 {
        if ((yyvsp[0].ident_suffix).kind == IDENT_SUFFIX_CALL) {
            (yyval.expr) = ast_call((yyvsp[-1].text), (yyvsp[0].ident_suffix).args);
        } else if ((yyvsp[0].ident_suffix).kind == IDENT_SUFFIX_FIELD) {
            (yyval.expr) = ast_field(ast_ident((yyvsp[-1].text)), (yyvsp[0].ident_suffix).name);
        } else if ((yyvsp[0].ident_suffix).kind == IDENT_SUFFIX_QUALIFIED_CALL) {
            (yyval.expr) = ast_qualified_call((yyvsp[-1].text), (yyvsp[0].ident_suffix).name, (yyvsp[0].ident_suffix).args);
        } else {
            (yyval.expr) = ast_ident((yyvsp[-1].text));
        }
      }
#line 3289 "src/parser.tab.c"
    break;

  case 148: /* primary: QUALIFIED_IDENT LPAREN argument_list_opt RPAREN  */
#line 791 "src/parser.y"
                                                      {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident((yyvsp[-3].text), &library, &name);
        (yyval.expr) = ast_qualified_call(library, name, (yyvsp[-1].expr_list));
      }
#line 3300 "src/parser.tab.c"
    break;

  case 149: /* primary: ERROR_VALUE  */
#line 797 "src/parser.y"
                  { (yyval.expr) = ast_ident(copy_const("error")); }
#line 3306 "src/parser.tab.c"
    break;

  case 150: /* primary: TRUE  */
#line 798 "src/parser.y"
           { (yyval.expr) = ast_bool(1); }
#line 3312 "src/parser.tab.c"
    break;

  case 151: /* primary: FALSE  */
#line 799 "src/parser.y"
            { (yyval.expr) = ast_bool(0); }
#line 3318 "src/parser.tab.c"
    break;

  case 152: /* primary: NOTHING  */
#line 800 "src/parser.y"
              { (yyval.expr) = ast_null(); }
#line 3324 "src/parser.tab.c"
    break;

  case 153: /* primary: UNKNOWN_VALUE  */
#line 801 "src/parser.y"
                    { (yyval.expr) = ast_unknown(); }
#line 3330 "src/parser.tab.c"
    break;

  case 154: /* primary: LPAREN expression RPAREN  */
#line 802 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); }
#line 3336 "src/parser.tab.c"
    break;

  case 155: /* primary: LBRACKET optional_newlines RBRACKET  */
#line 803 "src/parser.y"
                                          { (yyval.expr) = ast_array(ast_expr_list_empty()); }
#line 3342 "src/parser.tab.c"
    break;

  case 156: /* primary: LBRACKET optional_newlines array_argument_list optional_newlines RBRACKET  */
#line 804 "src/parser.y"
                                                                                { (yyval.expr) = ast_array((yyvsp[-2].expr_list)); }
#line 3348 "src/parser.tab.c"
    break;

  case 157: /* primary: LBRACE optional_newlines RBRACE  */
#line 805 "src/parser.y"
                                      { (yyval.expr) = ast_record(ast_record_field_list_empty()); }
#line 3354 "src/parser.tab.c"
    break;

  case 158: /* primary: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 806 "src/parser.y"
                                                                          { (yyval.expr) = ast_record((yyvsp[-2].record_field_list)); }
#line 3360 "src/parser.tab.c"
    break;

  case 159: /* ident_suffix: %empty  */
#line 810 "src/parser.y"
                          {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_NONE;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 3370 "src/parser.tab.c"
    break;

  case 160: /* ident_suffix: LPAREN argument_list_opt RPAREN  */
#line 815 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 3380 "src/parser.tab.c"
    break;

  case 161: /* ident_suffix: DOT IDENT ident_dot_suffix  */
#line 820 "src/parser.y"
                                 {
        (yyval.ident_suffix) = (yyvsp[0].ident_suffix);
        (yyval.ident_suffix).name = (yyvsp[-1].text);
      }
#line 3389 "src/parser.tab.c"
    break;

  case 162: /* ident_dot_suffix: %empty  */
#line 827 "src/parser.y"
             {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_FIELD;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 3399 "src/parser.tab.c"
    break;

  case 163: /* ident_dot_suffix: LPAREN argument_list_opt RPAREN  */
#line 832 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_QUALIFIED_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 3409 "src/parser.tab.c"
    break;

  case 164: /* duration_terms: NUMBER IDENT  */
#line 840 "src/parser.y"
                   {
        AstDuration duration = {0};
        (yyval.duration) = duration_add_unit(duration, (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 3418 "src/parser.tab.c"
    break;

  case 165: /* duration_terms: duration_terms NUMBER IDENT  */
#line 844 "src/parser.y"
                                  {
        (yyval.duration) = duration_add_unit((yyvsp[-2].duration), (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 3426 "src/parser.tab.c"
    break;

  case 166: /* argument_list_opt: %empty  */
#line 850 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 3432 "src/parser.tab.c"
    break;

  case 167: /* argument_list_opt: argument_list  */
#line 851 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 3438 "src/parser.tab.c"
    break;

  case 168: /* argument_list: expression  */
#line 855 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 3444 "src/parser.tab.c"
    break;

  case 169: /* argument_list: argument_list COMMA expression  */
#line 856 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 3450 "src/parser.tab.c"
    break;

  case 170: /* array_argument_list: expression  */
#line 860 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 3456 "src/parser.tab.c"
    break;

  case 171: /* array_argument_list: array_argument_list COMMA optional_newlines expression  */
#line 861 "src/parser.y"
                                                             { (yyval.expr_list) = ast_expr_list_append((yyvsp[-3].expr_list), (yyvsp[0].expr)); }
#line 3462 "src/parser.tab.c"
    break;

  case 172: /* parameter_list_opt: %empty  */
#line 865 "src/parser.y"
             { (yyval.name_list) = ast_name_list_empty(); }
#line 3468 "src/parser.tab.c"
    break;

  case 173: /* parameter_list_opt: parameter_list  */
#line 866 "src/parser.y"
                     { (yyval.name_list) = (yyvsp[0].name_list); }
#line 3474 "src/parser.tab.c"
    break;

  case 174: /* parameter_list: IDENT  */
#line 870 "src/parser.y"
            { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 3480 "src/parser.tab.c"
    break;

  case 175: /* parameter_list: parameter_list COMMA IDENT  */
#line 871 "src/parser.y"
                                 { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 3486 "src/parser.tab.c"
    break;

  case 176: /* record_field_list: IDENT OP_EQ expression  */
#line 875 "src/parser.y"
                             { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 3492 "src/parser.tab.c"
    break;

  case 177: /* record_field_list: IDENT COLON expression  */
#line 876 "src/parser.y"
                             { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 3498 "src/parser.tab.c"
    break;

  case 178: /* record_field_list: record_field_list COMMA optional_newlines IDENT OP_EQ expression  */
#line 877 "src/parser.y"
                                                                       { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 3504 "src/parser.tab.c"
    break;

  case 179: /* record_field_list: record_field_list COMMA optional_newlines IDENT COLON expression  */
#line 878 "src/parser.y"
                                                                       { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 3510 "src/parser.tab.c"
    break;


#line 3514 "src/parser.tab.c"

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

#line 886 "src/parser.y"


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
        yylval.text = copy_string_literal(token.start, token.length, &ok);
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
            fprintf(stderr, "runtime error at %d:%d: %s\n",
                    token.line,
                    token.column,
                    active_lexer->error_message);
        } else {
            fprintf(stderr, "lexer error at %d:%d\n", token.line, token.column);
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
    fprintf(stderr, "parse error: %s\n", message);
}
