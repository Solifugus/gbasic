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

#line 407 "src/parser.tab.c"

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
  YYSYMBOL_IF = 7,                         /* IF  */
  YYSYMBOL_THEN = 8,                       /* THEN  */
  YYSYMBOL_ELSE = 9,                       /* ELSE  */
  YYSYMBOL_END = 10,                       /* END  */
  YYSYMBOL_PRINT = 11,                     /* PRINT  */
  YYSYMBOL_TRUE = 12,                      /* TRUE  */
  YYSYMBOL_FALSE = 13,                     /* FALSE  */
  YYSYMBOL_NOTHING = 14,                   /* NOTHING  */
  YYSYMBOL_UNKNOWN_VALUE = 15,             /* UNKNOWN_VALUE  */
  YYSYMBOL_AND = 16,                       /* AND  */
  YYSYMBOL_OR = 17,                        /* OR  */
  YYSYMBOL_NOT = 18,                       /* NOT  */
  YYSYMBOL_WITH = 19,                      /* WITH  */
  YYSYMBOL_FOR = 20,                       /* FOR  */
  YYSYMBOL_TO = 21,                        /* TO  */
  YYSYMBOL_IN = 22,                        /* IN  */
  YYSYMBOL_WHILE = 23,                     /* WHILE  */
  YYSYMBOL_BREAK = 24,                     /* BREAK  */
  YYSYMBOL_CONTINUE = 25,                  /* CONTINUE  */
  YYSYMBOL_FUNCTION = 26,                  /* FUNCTION  */
  YYSYMBOL_RETURN = 27,                    /* RETURN  */
  YYSYMBOL_GOTO = 28,                      /* GOTO  */
  YYSYMBOL_GOSUB = 29,                     /* GOSUB  */
  YYSYMBOL_WATCH = 30,                     /* WATCH  */
  YYSYMBOL_WITHOUT = 31,                   /* WITHOUT  */
  YYSYMBOL_WATCHERS = 32,                  /* WATCHERS  */
  YYSYMBOL_ON = 33,                        /* ON  */
  YYSYMBOL_RESUME = 34,                    /* RESUME  */
  YYSYMBOL_NEXT = 35,                      /* NEXT  */
  YYSYMBOL_STOP = 36,                      /* STOP  */
  YYSYMBOL_ERROR_VALUE = 37,               /* ERROR_VALUE  */
  YYSYMBOL_MODIFIER = 38,                  /* MODIFIER  */
  YYSYMBOL_PROGRAM = 39,                   /* PROGRAM  */
  YYSYMBOL_LIBRARY = 40,                   /* LIBRARY  */
  YYSYMBOL_LOAD = 41,                      /* LOAD  */
  YYSYMBOL_USE = 42,                       /* USE  */
  YYSYMBOL_EXPORT = 43,                    /* EXPORT  */
  YYSYMBOL_OP_EQ = 44,                     /* OP_EQ  */
  YYSYMBOL_OP_NE = 45,                     /* OP_NE  */
  YYSYMBOL_OP_GT = 46,                     /* OP_GT  */
  YYSYMBOL_OP_LT = 47,                     /* OP_LT  */
  YYSYMBOL_OP_GE = 48,                     /* OP_GE  */
  YYSYMBOL_OP_LE = 49,                     /* OP_LE  */
  YYSYMBOL_OP_NGT = 50,                    /* OP_NGT  */
  YYSYMBOL_OP_NLT = 51,                    /* OP_NLT  */
  YYSYMBOL_OP_NGE = 52,                    /* OP_NGE  */
  YYSYMBOL_OP_NLE = 53,                    /* OP_NLE  */
  YYSYMBOL_PLUS = 54,                      /* PLUS  */
  YYSYMBOL_MINUS = 55,                     /* MINUS  */
  YYSYMBOL_STAR = 56,                      /* STAR  */
  YYSYMBOL_SLASH = 57,                     /* SLASH  */
  YYSYMBOL_LPAREN = 58,                    /* LPAREN  */
  YYSYMBOL_MOD_LPAREN = 59,                /* MOD_LPAREN  */
  YYSYMBOL_RPAREN = 60,                    /* RPAREN  */
  YYSYMBOL_LBRACKET = 61,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 62,                  /* RBRACKET  */
  YYSYMBOL_LBRACE = 63,                    /* LBRACE  */
  YYSYMBOL_RBRACE = 64,                    /* RBRACE  */
  YYSYMBOL_COMMA = 65,                     /* COMMA  */
  YYSYMBOL_COLON = 66,                     /* COLON  */
  YYSYMBOL_NEWLINE = 67,                   /* NEWLINE  */
  YYSYMBOL_NO_DOT = 68,                    /* NO_DOT  */
  YYSYMBOL_DOT = 69,                       /* DOT  */
  YYSYMBOL_YYACCEPT = 70,                  /* $accept  */
  YYSYMBOL_program = 71,                   /* program  */
  YYSYMBOL_statement_list = 72,            /* statement_list  */
  YYSYMBOL_statement = 73,                 /* statement  */
  YYSYMBOL_assignment = 74,                /* assignment  */
  YYSYMBOL_variable_name = 75,             /* variable_name  */
  YYSYMBOL_modifier = 76,                  /* modifier  */
  YYSYMBOL_modifier_name = 77,             /* modifier_name  */
  YYSYMBOL_modifier_word = 78,             /* modifier_word  */
  YYSYMBOL_print_statement = 79,           /* print_statement  */
  YYSYMBOL_call_statement = 80,            /* call_statement  */
  YYSYMBOL_with_lock_statement = 81,       /* with_lock_statement  */
  YYSYMBOL_for_each_statement = 82,        /* for_each_statement  */
  YYSYMBOL_while_statement = 83,           /* while_statement  */
  YYSYMBOL_function_statement = 84,        /* function_statement  */
  YYSYMBOL_modifier_statement = 85,        /* modifier_statement  */
  YYSYMBOL_program_statement = 86,         /* program_statement  */
  YYSYMBOL_library_statement = 87,         /* library_statement  */
  YYSYMBOL_use_statement = 88,             /* use_statement  */
  YYSYMBOL_modifier_signature = 89,        /* modifier_signature  */
  YYSYMBOL_modifier_context = 90,          /* modifier_context  */
  YYSYMBOL_watch_statement = 91,           /* watch_statement  */
  YYSYMBOL_without_watchers_statement = 92, /* without_watchers_statement  */
  YYSYMBOL_on_error_statement = 93,        /* on_error_statement  */
  YYSYMBOL_error_statement = 94,           /* error_statement  */
  YYSYMBOL_return_statement = 95,          /* return_statement  */
  YYSYMBOL_label_statement = 96,           /* label_statement  */
  YYSYMBOL_goto_statement = 97,            /* goto_statement  */
  YYSYMBOL_gosub_statement = 98,           /* gosub_statement  */
  YYSYMBOL_break_statement = 99,           /* break_statement  */
  YYSYMBOL_continue_statement = 100,       /* continue_statement  */
  YYSYMBOL_if_statement = 101,             /* if_statement  */
  YYSYMBOL_inline_statement = 102,         /* inline_statement  */
  YYSYMBOL_expression = 103,               /* expression  */
  YYSYMBOL_or_expression = 104,            /* or_expression  */
  YYSYMBOL_and_expression = 105,           /* and_expression  */
  YYSYMBOL_comparison_expression = 106,    /* comparison_expression  */
  YYSYMBOL_additive_expression = 107,      /* additive_expression  */
  YYSYMBOL_multiplicative_expression = 108, /* multiplicative_expression  */
  YYSYMBOL_unary_expression = 109,         /* unary_expression  */
  YYSYMBOL_postfix_expression = 110,       /* postfix_expression  */
  YYSYMBOL_comparison_operator = 111,      /* comparison_operator  */
  YYSYMBOL_primary = 112,                  /* primary  */
  YYSYMBOL_ident_suffix = 113,             /* ident_suffix  */
  YYSYMBOL_ident_dot_suffix = 114,         /* ident_dot_suffix  */
  YYSYMBOL_duration_terms = 115,           /* duration_terms  */
  YYSYMBOL_argument_list_opt = 116,        /* argument_list_opt  */
  YYSYMBOL_argument_list = 117,            /* argument_list  */
  YYSYMBOL_parameter_list_opt = 118,       /* parameter_list_opt  */
  YYSYMBOL_parameter_list = 119,           /* parameter_list  */
  YYSYMBOL_record_field_list = 120,        /* record_field_list  */
  YYSYMBOL_optional_newlines = 121         /* optional_newlines  */
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
#define YYLAST   818

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  70
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  52
/* YYNRULES -- Number of rules.  */
#define YYNRULES  139
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  307

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   324


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
      65,    66,    67,    68,    69
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   395,   395,   399,   400,   401,   405,   406,   407,   408,
     409,   410,   411,   412,   413,   414,   415,   416,   417,   418,
     419,   420,   421,   422,   423,   424,   425,   426,   430,   431,
     432,   436,   437,   438,   442,   446,   447,   451,   452,   453,
     454,   458,   462,   463,   464,   477,   489,   495,   501,   507,
     510,   516,   522,   528,   529,   530,   541,   555,   556,   560,
     564,   570,   576,   577,   578,   582,   586,   587,   591,   595,
     599,   603,   607,   611,   614,   618,   624,   625,   629,   633,
     634,   638,   639,   643,   644,   645,   655,   656,   657,   661,
     662,   663,   667,   668,   669,   673,   674,   675,   679,   680,
     681,   682,   683,   684,   685,   686,   687,   688,   692,   693,
     694,   695,   706,   707,   708,   709,   710,   711,   712,   713,
     714,   718,   723,   728,   735,   740,   748,   752,   758,   759,
     763,   764,   768,   769,   773,   774,   778,   779,   783,   784
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
  "STRING", "MOD_CONTENT", "IF", "THEN", "ELSE", "END", "PRINT", "TRUE",
  "FALSE", "NOTHING", "UNKNOWN_VALUE", "AND", "OR", "NOT", "WITH", "FOR",
  "TO", "IN", "WHILE", "BREAK", "CONTINUE", "FUNCTION", "RETURN", "GOTO",
  "GOSUB", "WATCH", "WITHOUT", "WATCHERS", "ON", "RESUME", "NEXT", "STOP",
  "ERROR_VALUE", "MODIFIER", "PROGRAM", "LIBRARY", "LOAD", "USE", "EXPORT",
  "OP_EQ", "OP_NE", "OP_GT", "OP_LT", "OP_GE", "OP_LE", "OP_NGT", "OP_NLT",
  "OP_NGE", "OP_NLE", "PLUS", "MINUS", "STAR", "SLASH", "LPAREN",
  "MOD_LPAREN", "RPAREN", "LBRACKET", "RBRACKET", "LBRACE", "RBRACE",
  "COMMA", "COLON", "NEWLINE", "NO_DOT", "DOT", "$accept", "program",
  "statement_list", "statement", "assignment", "variable_name", "modifier",
  "modifier_name", "modifier_word", "print_statement", "call_statement",
  "with_lock_statement", "for_each_statement", "while_statement",
  "function_statement", "modifier_statement", "program_statement",
  "library_statement", "use_statement", "modifier_signature",
  "modifier_context", "watch_statement", "without_watchers_statement",
  "on_error_statement", "error_statement", "return_statement",
  "label_statement", "goto_statement", "gosub_statement",
  "break_statement", "continue_statement", "if_statement",
  "inline_statement", "expression", "or_expression", "and_expression",
  "comparison_expression", "additive_expression",
  "multiplicative_expression", "unary_expression", "postfix_expression",
  "comparison_operator", "primary", "ident_suffix", "ident_dot_suffix",
  "duration_terms", "argument_list_opt", "argument_list",
  "parameter_list_opt", "parameter_list", "record_field_list",
  "optional_newlines", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-172)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -172,    40,   209,  -172,   -34,   755,  -172,   755,    23,    44,
     755,  -172,  -172,    51,   755,    55,    59,     7,    46,    50,
    -172,    16,    12,    84,    86,    87,    88,    57,  -172,  -172,
      26,   -21,    29,    30,  -172,  -172,  -172,  -172,  -172,  -172,
    -172,    31,  -172,  -172,    35,    36,    37,    39,    42,    43,
      45,    47,  -172,   755,   103,   107,  -172,  -172,  -172,  -172,
    -172,  -172,   755,  -172,   755,   755,   755,  -172,   -15,   109,
     114,   116,  -172,    75,    19,  -172,   -25,  -172,   130,  -172,
      89,   123,    79,    95,  -172,  -172,  -172,   145,    90,    22,
     150,  -172,  -172,  -172,  -172,  -172,     4,  -172,   135,    98,
      92,   154,   156,    12,  -172,   755,   155,  -172,   118,  -172,
    -172,  -172,  -172,  -172,  -172,  -172,  -172,  -172,  -172,  -172,
    -172,   104,   100,   -12,  -172,  -172,  -172,   111,   115,     0,
     755,   169,  -172,    13,   755,   755,  -172,  -172,  -172,  -172,
    -172,  -172,  -172,  -172,  -172,  -172,   755,   755,    91,   755,
     755,   755,   755,   172,   176,   755,   755,  -172,   145,  -172,
       8,  -172,   177,   158,  -172,   133,   145,  -172,   191,   145,
    -172,   198,   200,   186,  -172,  -172,   755,  -172,   755,   755,
     755,  -172,  -172,   163,  -172,  -172,   143,   151,   152,  -172,
    -172,  -172,   147,   116,  -172,    19,    19,   755,    27,  -172,
    -172,   153,  -172,  -172,   161,   157,   264,   162,   160,   174,
     208,   309,  -172,  -172,   755,   167,  -172,   178,   170,   354,
    -172,  -172,   191,  -172,  -172,  -172,   183,   755,  -172,     5,
    -172,   755,  -172,   159,  -172,    27,  -172,   187,  -172,   195,
     188,  -172,  -172,   222,   197,  -172,  -172,   193,   218,   194,
    -172,  -172,     3,  -172,   203,   199,   257,  -172,   399,   202,
    -172,   444,   205,  -172,   489,  -172,   213,  -172,   221,  -172,
    -172,   215,   534,   247,  -172,   579,   255,  -172,   248,   624,
    -172,   669,   755,   714,  -172,   277,   231,   274,   241,   242,
     271,   273,  -172,   305,   250,  -172,   251,  -172,  -172,   254,
     256,   258,  -172,  -172,  -172,  -172,  -172
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       3,     0,     2,     1,    31,     0,    32,     0,     0,     0,
       0,    71,    72,     0,    66,     0,     0,     0,     0,     0,
      33,     0,     0,     0,     0,     0,     0,     0,     4,     5,
       0,     0,     0,     0,     9,    10,    11,    12,    13,    14,
      15,     0,    17,    18,     0,     0,     0,     0,     0,     0,
       0,     0,    27,   128,     0,   108,    31,   110,   113,   114,
     115,   116,     0,   112,     0,     0,   128,   138,   121,     0,
      78,    79,    81,    83,    86,    89,    92,    95,   109,    41,
       0,     0,     0,     0,    67,    69,    70,     0,     0,     0,
       0,    65,    37,    39,    38,    40,    57,    35,     0,     0,
       0,    54,    53,     0,     6,     0,     0,    68,     0,     7,
       8,    16,    19,    20,    21,    22,    23,    24,    25,    26,
     130,     0,   129,     0,   126,    93,    94,     0,     0,     0,
     128,     0,   111,     0,     0,     0,    98,    99,   100,   101,
     102,   103,   104,   105,   106,   107,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     3,   132,   134,
       0,     3,     0,     0,    64,     0,   132,    36,     0,   132,
       3,     0,     0,     0,    28,    34,     0,    42,     0,     0,
     128,   117,   118,     0,   119,   139,   138,     0,   124,     3,
      76,    77,     0,    80,    82,    87,    88,     0,    84,    90,
      91,     0,    97,   127,     0,     0,     0,     0,   133,     0,
       0,     0,    62,    63,   128,     0,    59,     0,     0,     0,
      56,    55,     0,    29,   131,    30,     0,     0,   138,     0,
     122,   128,   123,     0,    75,    85,    96,     0,     3,    32,
       0,     3,   135,    32,     0,    58,     3,     0,    32,     0,
      43,   136,     0,   120,     0,     0,    32,     3,     0,     0,
       3,     0,     0,    44,     0,     3,     0,     3,     0,   125,
       3,     0,     0,    32,    47,     0,    32,    61,    32,     0,
      52,     0,     0,     0,    73,    32,     0,    32,     0,     0,
      32,    32,   137,    32,     0,    46,     0,    60,    49,     0,
       0,     0,    45,    48,    51,    50,    74
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -172,  -172,  -152,  -172,  -172,    -2,   249,  -172,   219,  -172,
    -172,  -172,  -172,  -172,  -172,  -172,  -172,  -172,  -172,   211,
     102,  -172,  -172,  -172,  -172,  -172,  -172,   210,   212,  -172,
    -172,  -172,  -172,    -4,  -172,   192,   206,  -137,   -63,   -51,
    -172,   179,  -172,  -172,  -172,  -172,   -64,  -172,  -117,   243,
    -172,  -171
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_uint8 yydefgoto[] =
{
       0,     1,     2,    29,    30,    68,   108,    96,    97,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    98,
     217,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,   192,   120,    70,    71,    72,    73,    74,    75,
      76,   149,    77,   132,   232,    78,   121,   122,   207,   208,
     186,   129
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      31,    69,   128,    79,   183,   206,    82,   268,    92,   211,
      84,   125,   198,   126,    93,   229,    92,    91,   219,    55,
      56,    57,    93,   105,    53,    94,     6,    80,    58,    59,
      60,    61,   179,    94,    62,    54,   152,   233,   106,    95,
       3,    15,    16,   130,   153,   107,   180,    95,    81,   215,
     162,    20,   218,    63,   131,    83,   163,   252,   164,    85,
     235,   127,   166,    86,   184,    87,   187,   185,   209,   253,
     185,    64,   185,   210,    65,   150,   151,    66,    88,    67,
     189,   146,   147,   195,   196,    90,   258,    89,    99,   261,
     100,   101,   102,   104,   264,   103,   109,   110,   111,   199,
     200,   174,   112,   113,   114,   272,   115,   123,   275,   116,
     117,   124,   118,   279,   119,   281,   226,   133,   283,   136,
     137,   138,   139,   140,   141,   142,   143,   144,   145,   146,
     147,   134,   135,   154,   106,   136,   137,   138,   139,   140,
     141,   142,   143,   144,   145,   156,   157,   155,   201,   159,
     244,   204,   205,   158,   165,   168,   169,   161,   171,   170,
     172,   175,   176,     4,   177,   178,     5,   254,   255,   256,
       7,   181,   223,   188,   224,   225,   202,   182,     8,     9,
     203,   212,    10,    11,    12,    13,    14,    15,    16,    17,
      18,   214,    19,   213,    20,   216,    21,    22,    23,    24,
      25,    26,    27,   220,    31,   221,   222,   227,   228,    31,
     231,   230,   242,     4,   234,   236,     5,    31,   259,     6,
       7,   237,   240,   251,   238,   210,    28,   245,     8,     9,
     247,    31,    10,    11,    12,    13,    14,    15,    16,    17,
      18,   241,    19,   250,    20,   246,    21,    22,    23,    24,
      25,    26,    27,   262,   257,   260,    31,   263,   266,    31,
     265,   267,    31,   269,   271,   282,   270,   286,     4,   274,
      31,     5,   277,    31,   239,     7,    28,    31,   292,    31,
     280,    31,   284,     8,     9,   288,   289,    10,    11,    12,
      13,    14,    15,    16,    17,    18,   294,    19,   295,    20,
     296,    21,    22,    23,    24,    25,    26,    27,   297,   298,
     299,   300,   301,     4,   173,   167,     5,   302,   303,   243,
       7,   304,   148,   305,   249,   306,   193,   197,     8,     9,
     160,    28,    10,    11,    12,    13,    14,    15,    16,    17,
      18,   194,    19,   190,    20,   191,    21,    22,    23,    24,
      25,    26,    27,     0,     0,     0,     0,     0,     4,     0,
       0,     5,     0,     0,   248,     7,     0,     0,     0,     0,
       0,     0,     0,     8,     9,     0,    28,    10,    11,    12,
      13,    14,    15,    16,    17,    18,     0,    19,     0,    20,
       0,    21,    22,    23,    24,    25,    26,    27,     0,     0,
       0,     0,     0,     4,     0,     0,     5,     0,     0,   273,
       7,     0,     0,     0,     0,     0,     0,     0,     8,     9,
       0,    28,    10,    11,    12,    13,    14,    15,    16,    17,
      18,     0,    19,     0,    20,     0,    21,    22,    23,    24,
      25,    26,    27,     0,     0,     0,     0,     0,     4,     0,
       0,     5,     0,     0,   276,     7,     0,     0,     0,     0,
       0,     0,     0,     8,     9,     0,    28,    10,    11,    12,
      13,    14,    15,    16,    17,    18,     0,    19,     0,    20,
       0,    21,    22,    23,    24,    25,    26,    27,     0,     0,
       0,     0,     0,     4,     0,     0,     5,     0,     0,   278,
       7,     0,     0,     0,     0,     0,     0,     0,     8,     9,
       0,    28,    10,    11,    12,    13,    14,    15,    16,    17,
      18,     0,    19,     0,    20,     0,    21,    22,    23,    24,
      25,    26,    27,     0,     0,     0,     0,     0,     4,     0,
       0,     5,     0,     0,   285,     7,     0,     0,     0,     0,
       0,     0,     0,     8,     9,     0,    28,    10,    11,    12,
      13,    14,    15,    16,    17,    18,     0,    19,     0,    20,
       0,    21,    22,    23,    24,    25,    26,    27,     0,     0,
       0,     0,     0,     4,     0,     0,     5,     0,     0,   287,
       7,     0,     0,     0,     0,     0,     0,     0,     8,     9,
       0,    28,    10,    11,    12,    13,    14,    15,    16,    17,
      18,     0,    19,     0,    20,     0,    21,    22,    23,    24,
      25,    26,    27,     0,     0,     0,     0,     0,     4,     0,
       0,     5,     0,     0,   290,     7,     0,     0,     0,     0,
       0,     0,     0,     8,     9,     0,    28,    10,    11,    12,
      13,    14,    15,    16,    17,    18,     0,    19,     0,    20,
       0,    21,    22,    23,    24,    25,    26,    27,     0,     0,
       0,     0,     0,     4,     0,     0,     5,     0,     0,   291,
       7,     0,     0,     0,     0,     0,     0,     0,     8,     9,
       0,    28,    10,    11,    12,    13,    14,    15,    16,    17,
      18,     0,    19,     0,    20,     0,    21,    22,    23,    24,
      25,    26,    27,     0,     0,     0,     0,     0,     4,     0,
       0,     5,     0,     0,   293,     7,     0,     0,     0,     0,
       0,     0,     0,     8,     9,     0,    28,    10,    11,    12,
      13,    14,    15,    16,    17,    18,     0,    19,     0,    20,
       0,    21,    22,    23,    24,    25,    26,    27,    55,    56,
      57,     0,     0,     0,     0,     6,     0,    58,    59,    60,
      61,     0,     0,    62,     0,     0,     0,     0,     0,     0,
       0,    28,     0,     0,     0,     0,     0,     0,     0,     0,
      20,     0,    63,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      64,     0,     0,    65,     0,     0,    66,     0,    67
};

static const yytype_int16 yycheck[] =
{
       2,     5,    66,     7,     4,   157,    10,     4,     4,   161,
      14,    62,   149,    64,    10,   186,     4,    21,   170,     3,
       4,     5,    10,    44,    58,    21,    10,     4,    12,    13,
      14,    15,    44,    21,    18,    69,    61,   189,    59,    35,
       0,    28,    29,    58,    69,    66,    58,    35,     4,   166,
      28,    35,   169,    37,    69,     4,    34,   228,    36,     4,
     197,    65,    58,     4,    64,    58,   130,    67,    60,    64,
      67,    55,    67,    65,    58,    56,    57,    61,    32,    63,
      67,    54,    55,   146,   147,    69,   238,    37,     4,   241,
       4,     4,     4,    67,   246,    38,    67,    67,    67,   150,
     151,   105,    67,    67,    67,   257,    67,     4,   260,    67,
      67,     4,    67,   265,    67,   267,   180,     8,   270,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    17,    16,     3,    59,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    22,    67,    58,   152,     4,
     214,   155,   156,    58,     4,    20,    58,    67,     4,    67,
       4,     6,    44,     4,    60,    65,     7,   231,     9,    10,
      11,    60,   176,     4,   178,   179,     4,    62,    19,    20,
       4,     4,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    58,    33,    35,    35,     4,    37,    38,    39,    40,
      41,    42,    43,     5,   206,     5,    20,    44,    65,   211,
      58,    60,     4,     4,    67,    62,     7,   219,    23,    10,
      11,    60,    60,   227,    67,    65,    67,    60,    19,    20,
      60,   233,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    67,    33,    60,    35,    67,    37,    38,    39,    40,
      41,    42,    43,    31,    67,    67,   258,    60,    40,   261,
      67,    67,   264,    60,     7,    44,    67,    20,     4,    67,
     272,     7,    67,   275,    10,    11,    67,   279,   282,   281,
      67,   283,    67,    19,    20,    30,    38,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    19,    33,    67,    35,
      26,    37,    38,    39,    40,    41,    42,    43,    67,    67,
      39,    38,     7,     4,   103,    96,     7,    67,    67,    10,
      11,    67,    73,    67,   222,    67,   134,   148,    19,    20,
      87,    67,    23,    24,    25,    26,    27,    28,    29,    30,
      31,   135,    33,   133,    35,   133,    37,    38,    39,    40,
      41,    42,    43,    -1,    -1,    -1,    -1,    -1,     4,    -1,
      -1,     7,    -1,    -1,    10,    11,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    19,    20,    -1,    67,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    -1,    33,    -1,    35,
      -1,    37,    38,    39,    40,    41,    42,    43,    -1,    -1,
      -1,    -1,    -1,     4,    -1,    -1,     7,    -1,    -1,    10,
      11,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    19,    20,
      -1,    67,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    -1,    33,    -1,    35,    -1,    37,    38,    39,    40,
      41,    42,    43,    -1,    -1,    -1,    -1,    -1,     4,    -1,
      -1,     7,    -1,    -1,    10,    11,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    19,    20,    -1,    67,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    -1,    33,    -1,    35,
      -1,    37,    38,    39,    40,    41,    42,    43,    -1,    -1,
      -1,    -1,    -1,     4,    -1,    -1,     7,    -1,    -1,    10,
      11,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    19,    20,
      -1,    67,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    -1,    33,    -1,    35,    -1,    37,    38,    39,    40,
      41,    42,    43,    -1,    -1,    -1,    -1,    -1,     4,    -1,
      -1,     7,    -1,    -1,    10,    11,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    19,    20,    -1,    67,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    -1,    33,    -1,    35,
      -1,    37,    38,    39,    40,    41,    42,    43,    -1,    -1,
      -1,    -1,    -1,     4,    -1,    -1,     7,    -1,    -1,    10,
      11,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    19,    20,
      -1,    67,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    -1,    33,    -1,    35,    -1,    37,    38,    39,    40,
      41,    42,    43,    -1,    -1,    -1,    -1,    -1,     4,    -1,
      -1,     7,    -1,    -1,    10,    11,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    19,    20,    -1,    67,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    -1,    33,    -1,    35,
      -1,    37,    38,    39,    40,    41,    42,    43,    -1,    -1,
      -1,    -1,    -1,     4,    -1,    -1,     7,    -1,    -1,    10,
      11,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    19,    20,
      -1,    67,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    -1,    33,    -1,    35,    -1,    37,    38,    39,    40,
      41,    42,    43,    -1,    -1,    -1,    -1,    -1,     4,    -1,
      -1,     7,    -1,    -1,    10,    11,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    19,    20,    -1,    67,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    -1,    33,    -1,    35,
      -1,    37,    38,    39,    40,    41,    42,    43,     3,     4,
       5,    -1,    -1,    -1,    -1,    10,    -1,    12,    13,    14,
      15,    -1,    -1,    18,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    67,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      35,    -1,    37,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      55,    -1,    -1,    58,    -1,    -1,    61,    -1,    63
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,    71,    72,     0,     4,     7,    10,    11,    19,    20,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    33,
      35,    37,    38,    39,    40,    41,    42,    43,    67,    73,
      74,    75,    79,    80,    81,    82,    83,    84,    85,    86,
      87,    88,    91,    92,    93,    94,    95,    96,    97,    98,
      99,   100,   101,    58,    69,     3,     4,     5,    12,    13,
      14,    15,    18,    37,    55,    58,    61,    63,    75,   103,
     104,   105,   106,   107,   108,   109,   110,   112,   115,   103,
       4,     4,   103,     4,   103,     4,     4,    58,    32,    37,
      69,   103,     4,    10,    21,    35,    77,    78,    89,     4,
       4,     4,     4,    38,    67,    44,    59,    66,    76,    67,
      67,    67,    67,    67,    67,    67,    67,    67,    67,    67,
     103,   116,   117,     4,     4,   109,   109,   103,   116,   121,
      58,    69,   113,     8,    17,    16,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    76,   111,
      56,    57,    61,    69,     3,    58,    22,    67,    58,     4,
     119,    67,    28,    34,    36,     4,    58,    78,    20,    58,
      67,     4,     4,    89,   103,     6,    44,    60,    65,    44,
      58,    60,    62,     4,    64,    67,   120,   116,     4,    67,
      97,    98,   102,   105,   106,   108,   108,   111,   107,   109,
     109,   103,     4,     4,   103,   103,    72,   118,   119,    60,
      65,    72,     4,    35,    58,   118,     4,    90,   118,    72,
       5,     5,    20,   103,   103,   103,   116,    44,    65,   121,
      60,    58,   114,    72,    67,   107,    62,    60,    67,    10,
      60,    67,     4,    10,   116,    60,    67,    60,    10,    90,
      60,   103,   121,    64,   116,     9,    10,    67,    72,    23,
      67,    72,    31,    60,    72,    67,    40,    67,     4,    60,
      67,     7,    72,    10,    67,    72,    10,    67,    10,    72,
      67,    72,    44,    72,    67,    10,    20,    10,    30,    38,
      10,    10,   103,    10,    19,    67,    26,    67,    67,    39,
      38,     7,    67,    67,    67,    67,    67
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    70,    71,    72,    72,    72,    73,    73,    73,    73,
      73,    73,    73,    73,    73,    73,    73,    73,    73,    73,
      73,    73,    73,    73,    73,    73,    73,    73,    74,    74,
      74,    75,    75,    75,    76,    77,    77,    78,    78,    78,
      78,    79,    80,    80,    80,    81,    82,    83,    84,    85,
      85,    86,    87,    88,    88,    88,    88,    89,    89,    90,
      91,    92,    93,    93,    93,    94,    95,    95,    96,    97,
      98,    99,   100,   101,   101,   101,   102,   102,   103,   104,
     104,   105,   105,   106,   106,   106,   107,   107,   107,   108,
     108,   108,   109,   109,   109,   110,   110,   110,   111,   111,
     111,   111,   111,   111,   111,   111,   111,   111,   112,   112,
     112,   112,   112,   112,   112,   112,   112,   112,   112,   112,
     112,   113,   113,   113,   114,   114,   115,   115,   116,   116,
     117,   117,   118,   118,   119,   119,   120,   120,   121,   121
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     2,     2,     2,     2,     1,
       1,     1,     1,     1,     1,     1,     2,     1,     1,     2,
       2,     2,     2,     2,     2,     2,     2,     1,     3,     4,
       5,     1,     1,     1,     2,     1,     2,     1,     1,     1,
       1,     2,     4,     6,     6,    10,     9,     7,    10,     9,
      10,    10,     7,     2,     2,     4,     4,     1,     4,     1,
       9,     7,     4,     4,     3,     2,     1,     2,     2,     2,
       2,     1,     1,     8,    11,     5,     1,     1,     1,     1,
       3,     1,     3,     1,     3,     4,     1,     3,     3,     1,
       3,     3,     1,     2,     2,     1,     4,     3,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     2,     1,     1,     1,     1,     1,     3,     3,     3,
       5,     0,     3,     3,     0,     3,     2,     3,     0,     1,
       1,     3,     0,     1,     1,     3,     3,     6,     0,     2
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
#line 395 "src/parser.y"
                     { parsed_program = (yyvsp[0].stmt_list); (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 2220 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 399 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 2226 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 400 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 2232 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 401 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 2238 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 405 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2244 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 406 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2250 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 407 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2256 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 408 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2262 "src/parser.tab.c"
    break;

  case 10: /* statement: for_each_statement  */
#line 409 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2268 "src/parser.tab.c"
    break;

  case 11: /* statement: while_statement  */
#line 410 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2274 "src/parser.tab.c"
    break;

  case 12: /* statement: function_statement  */
#line 411 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2280 "src/parser.tab.c"
    break;

  case 13: /* statement: modifier_statement  */
#line 412 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2286 "src/parser.tab.c"
    break;

  case 14: /* statement: program_statement  */
#line 413 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2292 "src/parser.tab.c"
    break;

  case 15: /* statement: library_statement  */
#line 414 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2298 "src/parser.tab.c"
    break;

  case 16: /* statement: use_statement NEWLINE  */
#line 415 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2304 "src/parser.tab.c"
    break;

  case 17: /* statement: watch_statement  */
#line 416 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2310 "src/parser.tab.c"
    break;

  case 18: /* statement: without_watchers_statement  */
#line 417 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2316 "src/parser.tab.c"
    break;

  case 19: /* statement: on_error_statement NEWLINE  */
#line 418 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2322 "src/parser.tab.c"
    break;

  case 20: /* statement: error_statement NEWLINE  */
#line 419 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2328 "src/parser.tab.c"
    break;

  case 21: /* statement: return_statement NEWLINE  */
#line 420 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2334 "src/parser.tab.c"
    break;

  case 22: /* statement: label_statement NEWLINE  */
#line 421 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2340 "src/parser.tab.c"
    break;

  case 23: /* statement: goto_statement NEWLINE  */
#line 422 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2346 "src/parser.tab.c"
    break;

  case 24: /* statement: gosub_statement NEWLINE  */
#line 423 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2352 "src/parser.tab.c"
    break;

  case 25: /* statement: break_statement NEWLINE  */
#line 424 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2358 "src/parser.tab.c"
    break;

  case 26: /* statement: continue_statement NEWLINE  */
#line 425 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2364 "src/parser.tab.c"
    break;

  case 27: /* statement: if_statement  */
#line 426 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2370 "src/parser.tab.c"
    break;

  case 28: /* assignment: variable_name OP_EQ expression  */
#line 430 "src/parser.y"
                                     { (yyval.stmt) = ast_assign((yyvsp[-2].text), ast_modifier_none(), (yyvsp[0].expr)); }
#line 2376 "src/parser.tab.c"
    break;

  case 29: /* assignment: variable_name modifier OP_EQ expression  */
#line 431 "src/parser.y"
                                              { (yyval.stmt) = ast_assign((yyvsp[-3].text), (yyvsp[-2].modifier), (yyvsp[0].expr)); }
#line 2382 "src/parser.tab.c"
    break;

  case 30: /* assignment: IDENT DOT IDENT OP_EQ expression  */
#line 432 "src/parser.y"
                                       { (yyval.stmt) = ast_field_assign((yyvsp[-4].text), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 2388 "src/parser.tab.c"
    break;

  case 31: /* variable_name: IDENT  */
#line 436 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 2394 "src/parser.tab.c"
    break;

  case 32: /* variable_name: END  */
#line 437 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 2400 "src/parser.tab.c"
    break;

  case 33: /* variable_name: NEXT  */
#line 438 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 2406 "src/parser.tab.c"
    break;

  case 34: /* modifier: MOD_LPAREN MOD_CONTENT  */
#line 442 "src/parser.y"
                             { (yyval.modifier) = parse_modifier_use((yyvsp[0].text)); }
#line 2412 "src/parser.tab.c"
    break;

  case 35: /* modifier_name: modifier_word  */
#line 446 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 2418 "src/parser.tab.c"
    break;

  case 36: /* modifier_name: modifier_name modifier_word  */
#line 447 "src/parser.y"
                                  { (yyval.text) = join_words((yyvsp[-1].text), (yyvsp[0].text)); }
#line 2424 "src/parser.tab.c"
    break;

  case 37: /* modifier_word: IDENT  */
#line 451 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 2430 "src/parser.tab.c"
    break;

  case 38: /* modifier_word: TO  */
#line 452 "src/parser.y"
         { (yyval.text) = copy_const("to"); }
#line 2436 "src/parser.tab.c"
    break;

  case 39: /* modifier_word: END  */
#line 453 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 2442 "src/parser.tab.c"
    break;

  case 40: /* modifier_word: NEXT  */
#line 454 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 2448 "src/parser.tab.c"
    break;

  case 41: /* print_statement: PRINT expression  */
#line 458 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 2454 "src/parser.tab.c"
    break;

  case 42: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 462 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 2460 "src/parser.tab.c"
    break;

  case 43: /* call_statement: IDENT DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 463 "src/parser.y"
                                                      { (yyval.stmt) = ast_expr_stmt(ast_qualified_call((yyvsp[-5].text), (yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 2466 "src/parser.tab.c"
    break;

  case 44: /* call_statement: ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 464 "src/parser.y"
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
#line 2481 "src/parser.tab.c"
    break;

  case 45: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 477 "src/parser.y"
                                                                                  {
        if (strcmp((yyvsp[-8].text), "lock") != 0) {
            yyerror("expected lock in with lock block");
            free((yyvsp[-8].text));
            YYERROR;
        }
        free((yyvsp[-8].text));
        (yyval.stmt) = ast_with_lock((yyvsp[-6].expr), (yyvsp[-3].stmt_list));
      }
#line 2495 "src/parser.tab.c"
    break;

  case 46: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list END FOR NEWLINE  */
#line 489 "src/parser.y"
                                                                     {
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 2503 "src/parser.tab.c"
    break;

  case 47: /* while_statement: WHILE expression NEWLINE statement_list END WHILE NEWLINE  */
#line 495 "src/parser.y"
                                                                {
        (yyval.stmt) = ast_while((yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 2511 "src/parser.tab.c"
    break;

  case 48: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 501 "src/parser.y"
                                                                                                  {
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 2519 "src/parser.tab.c"
    break;

  case 49: /* modifier_statement: MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 507 "src/parser.y"
                                                                                                   {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 0, (yyvsp[-3].stmt_list));
      }
#line 2527 "src/parser.tab.c"
    break;

  case 50: /* modifier_statement: EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 510 "src/parser.y"
                                                                                                          {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 1, (yyvsp[-3].stmt_list));
      }
#line 2535 "src/parser.tab.c"
    break;

  case 51: /* program_statement: PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE  */
#line 516 "src/parser.y"
                                                                                                {
        (yyval.stmt) = ast_program((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 2543 "src/parser.tab.c"
    break;

  case 52: /* library_statement: LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE  */
#line 522 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_library((yyvsp[-5].text), (yyvsp[-3].stmt_list));
      }
#line 2551 "src/parser.tab.c"
    break;

  case 53: /* use_statement: USE IDENT  */
#line 528 "src/parser.y"
                { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 2557 "src/parser.tab.c"
    break;

  case 54: /* use_statement: LOAD IDENT  */
#line 529 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 2563 "src/parser.tab.c"
    break;

  case 55: /* use_statement: USE IDENT IDENT STRING  */
#line 530 "src/parser.y"
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
#line 2579 "src/parser.tab.c"
    break;

  case 56: /* use_statement: LOAD IDENT IDENT STRING  */
#line 541 "src/parser.y"
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
#line 2595 "src/parser.tab.c"
    break;

  case 57: /* modifier_signature: modifier_name  */
#line 555 "src/parser.y"
                    { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[0].text), ast_name_list_empty()); }
#line 2601 "src/parser.tab.c"
    break;

  case 58: /* modifier_signature: modifier_name LPAREN parameter_list_opt RPAREN  */
#line 556 "src/parser.y"
                                                     { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[-3].text), (yyvsp[-1].name_list)); }
#line 2607 "src/parser.tab.c"
    break;

  case 59: /* modifier_context: IDENT  */
#line 560 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 2613 "src/parser.tab.c"
    break;

  case 60: /* watch_statement: WATCH LPAREN parameter_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 564 "src/parser.y"
                                                                                  {
        (yyval.stmt) = ast_watch((yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 2621 "src/parser.tab.c"
    break;

  case 61: /* without_watchers_statement: WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE  */
#line 570 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_without_watchers((yyvsp[-3].stmt_list));
      }
#line 2629 "src/parser.tab.c"
    break;

  case 62: /* on_error_statement: ON ERROR_VALUE GOTO IDENT  */
#line 576 "src/parser.y"
                                { (yyval.stmt) = ast_on_error_goto((yyvsp[0].text)); }
#line 2635 "src/parser.tab.c"
    break;

  case 63: /* on_error_statement: ON ERROR_VALUE RESUME NEXT  */
#line 577 "src/parser.y"
                                 { (yyval.stmt) = ast_on_error_resume_next(); }
#line 2641 "src/parser.tab.c"
    break;

  case 64: /* on_error_statement: ON ERROR_VALUE STOP  */
#line 578 "src/parser.y"
                          { (yyval.stmt) = ast_on_error_stop(); }
#line 2647 "src/parser.tab.c"
    break;

  case 65: /* error_statement: ERROR_VALUE expression  */
#line 582 "src/parser.y"
                             { (yyval.stmt) = ast_error((yyvsp[0].expr)); }
#line 2653 "src/parser.tab.c"
    break;

  case 66: /* return_statement: RETURN  */
#line 586 "src/parser.y"
             { (yyval.stmt) = ast_return(NULL); }
#line 2659 "src/parser.tab.c"
    break;

  case 67: /* return_statement: RETURN expression  */
#line 587 "src/parser.y"
                        { (yyval.stmt) = ast_return((yyvsp[0].expr)); }
#line 2665 "src/parser.tab.c"
    break;

  case 68: /* label_statement: variable_name COLON  */
#line 591 "src/parser.y"
                          { (yyval.stmt) = ast_label((yyvsp[-1].text)); }
#line 2671 "src/parser.tab.c"
    break;

  case 69: /* goto_statement: GOTO IDENT  */
#line 595 "src/parser.y"
                 { (yyval.stmt) = ast_goto((yyvsp[0].text)); }
#line 2677 "src/parser.tab.c"
    break;

  case 70: /* gosub_statement: GOSUB IDENT  */
#line 599 "src/parser.y"
                  { (yyval.stmt) = ast_gosub((yyvsp[0].text)); }
#line 2683 "src/parser.tab.c"
    break;

  case 71: /* break_statement: BREAK  */
#line 603 "src/parser.y"
            { (yyval.stmt) = ast_break(); }
#line 2689 "src/parser.tab.c"
    break;

  case 72: /* continue_statement: CONTINUE  */
#line 607 "src/parser.y"
               { (yyval.stmt) = ast_continue(); }
#line 2695 "src/parser.tab.c"
    break;

  case 73: /* if_statement: IF expression THEN NEWLINE statement_list END IF NEWLINE  */
#line 611 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_if((yyvsp[-6].expr), (yyvsp[-3].stmt_list));
      }
#line 2703 "src/parser.tab.c"
    break;

  case 74: /* if_statement: IF expression THEN NEWLINE statement_list ELSE NEWLINE statement_list END IF NEWLINE  */
#line 614 "src/parser.y"
                                                                                           {
        (yyval.stmt) = ast_if((yyvsp[-9].expr), (yyvsp[-6].stmt_list));
        (yyval.stmt)->as.if_stmt.else_body = (yyvsp[-3].stmt_list);
      }
#line 2712 "src/parser.tab.c"
    break;

  case 75: /* if_statement: IF expression THEN inline_statement NEWLINE  */
#line 618 "src/parser.y"
                                                  {
        (yyval.stmt) = ast_if((yyvsp[-3].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt)));
      }
#line 2720 "src/parser.tab.c"
    break;

  case 76: /* inline_statement: goto_statement  */
#line 624 "src/parser.y"
                     { (yyval.stmt) = (yyvsp[0].stmt); }
#line 2726 "src/parser.tab.c"
    break;

  case 77: /* inline_statement: gosub_statement  */
#line 625 "src/parser.y"
                      { (yyval.stmt) = (yyvsp[0].stmt); }
#line 2732 "src/parser.tab.c"
    break;

  case 78: /* expression: or_expression  */
#line 629 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 2738 "src/parser.tab.c"
    break;

  case 79: /* or_expression: and_expression  */
#line 633 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 2744 "src/parser.tab.c"
    break;

  case 80: /* or_expression: or_expression OR and_expression  */
#line 634 "src/parser.y"
                                      { (yyval.expr) = ast_binary(copy_const("or"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 2750 "src/parser.tab.c"
    break;

  case 81: /* and_expression: comparison_expression  */
#line 638 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 2756 "src/parser.tab.c"
    break;

  case 82: /* and_expression: and_expression AND comparison_expression  */
#line 639 "src/parser.y"
                                               { (yyval.expr) = ast_binary(copy_const("and"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 2762 "src/parser.tab.c"
    break;

  case 83: /* comparison_expression: additive_expression  */
#line 643 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 2768 "src/parser.tab.c"
    break;

  case 84: /* comparison_expression: additive_expression comparison_operator additive_expression  */
#line 644 "src/parser.y"
                                                                  { (yyval.expr) = ast_binary((yyvsp[-1].text), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 2774 "src/parser.tab.c"
    break;

  case 85: /* comparison_expression: additive_expression modifier comparison_operator additive_expression  */
#line 645 "src/parser.y"
                                                                           {
        if (!is_modifier_target_expr((yyvsp[-3].expr))) {
            yyerror("modifier target must be a variable, field, or index");
            YYERROR;
        }
        (yyval.expr) = ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr));
      }
#line 2786 "src/parser.tab.c"
    break;

  case 86: /* additive_expression: multiplicative_expression  */
#line 655 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 2792 "src/parser.tab.c"
    break;

  case 87: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 656 "src/parser.y"
                                                         { (yyval.expr) = ast_binary(copy_const("+"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 2798 "src/parser.tab.c"
    break;

  case 88: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 657 "src/parser.y"
                                                          { (yyval.expr) = ast_binary(copy_const("-"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 2804 "src/parser.tab.c"
    break;

  case 89: /* multiplicative_expression: unary_expression  */
#line 661 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 2810 "src/parser.tab.c"
    break;

  case 90: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 662 "src/parser.y"
                                                      { (yyval.expr) = ast_binary(copy_const("*"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 2816 "src/parser.tab.c"
    break;

  case 91: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 663 "src/parser.y"
                                                       { (yyval.expr) = ast_binary(copy_const("/"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 2822 "src/parser.tab.c"
    break;

  case 92: /* unary_expression: postfix_expression  */
#line 667 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 2828 "src/parser.tab.c"
    break;

  case 93: /* unary_expression: NOT unary_expression  */
#line 668 "src/parser.y"
                           { (yyval.expr) = ast_unary(copy_const("not"), (yyvsp[0].expr)); }
#line 2834 "src/parser.tab.c"
    break;

  case 94: /* unary_expression: MINUS unary_expression  */
#line 669 "src/parser.y"
                             { (yyval.expr) = ast_unary(copy_const("-"), (yyvsp[0].expr)); }
#line 2840 "src/parser.tab.c"
    break;

  case 95: /* postfix_expression: primary  */
#line 673 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 2846 "src/parser.tab.c"
    break;

  case 96: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 674 "src/parser.y"
                                                      { (yyval.expr) = ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)); }
#line 2852 "src/parser.tab.c"
    break;

  case 97: /* postfix_expression: postfix_expression DOT IDENT  */
#line 675 "src/parser.y"
                                   { (yyval.expr) = ast_field((yyvsp[-2].expr), (yyvsp[0].text)); }
#line 2858 "src/parser.tab.c"
    break;

  case 98: /* comparison_operator: OP_EQ  */
#line 679 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 2864 "src/parser.tab.c"
    break;

  case 99: /* comparison_operator: OP_NE  */
#line 680 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 2870 "src/parser.tab.c"
    break;

  case 100: /* comparison_operator: OP_GT  */
#line 681 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 2876 "src/parser.tab.c"
    break;

  case 101: /* comparison_operator: OP_LT  */
#line 682 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 2882 "src/parser.tab.c"
    break;

  case 102: /* comparison_operator: OP_GE  */
#line 683 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 2888 "src/parser.tab.c"
    break;

  case 103: /* comparison_operator: OP_LE  */
#line 684 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 2894 "src/parser.tab.c"
    break;

  case 104: /* comparison_operator: OP_NGT  */
#line 685 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 2900 "src/parser.tab.c"
    break;

  case 105: /* comparison_operator: OP_NLT  */
#line 686 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 2906 "src/parser.tab.c"
    break;

  case 106: /* comparison_operator: OP_NGE  */
#line 687 "src/parser.y"
             { (yyval.text) = copy_const("!>="); }
#line 2912 "src/parser.tab.c"
    break;

  case 107: /* comparison_operator: OP_NLE  */
#line 688 "src/parser.y"
             { (yyval.text) = copy_const("!<="); }
#line 2918 "src/parser.tab.c"
    break;

  case 108: /* primary: NUMBER  */
#line 692 "src/parser.y"
             { (yyval.expr) = ast_number((yyvsp[0].number)); }
#line 2924 "src/parser.tab.c"
    break;

  case 109: /* primary: duration_terms  */
#line 693 "src/parser.y"
                     { (yyval.expr) = ast_duration((yyvsp[0].duration)); }
#line 2930 "src/parser.tab.c"
    break;

  case 110: /* primary: STRING  */
#line 694 "src/parser.y"
             { (yyval.expr) = ast_string((yyvsp[0].text)); }
#line 2936 "src/parser.tab.c"
    break;

  case 111: /* primary: variable_name ident_suffix  */
#line 695 "src/parser.y"
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
#line 2952 "src/parser.tab.c"
    break;

  case 112: /* primary: ERROR_VALUE  */
#line 706 "src/parser.y"
                  { (yyval.expr) = ast_ident(copy_const("error")); }
#line 2958 "src/parser.tab.c"
    break;

  case 113: /* primary: TRUE  */
#line 707 "src/parser.y"
           { (yyval.expr) = ast_bool(1); }
#line 2964 "src/parser.tab.c"
    break;

  case 114: /* primary: FALSE  */
#line 708 "src/parser.y"
            { (yyval.expr) = ast_bool(0); }
#line 2970 "src/parser.tab.c"
    break;

  case 115: /* primary: NOTHING  */
#line 709 "src/parser.y"
              { (yyval.expr) = ast_null(); }
#line 2976 "src/parser.tab.c"
    break;

  case 116: /* primary: UNKNOWN_VALUE  */
#line 710 "src/parser.y"
                    { (yyval.expr) = ast_unknown(); }
#line 2982 "src/parser.tab.c"
    break;

  case 117: /* primary: LPAREN expression RPAREN  */
#line 711 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); }
#line 2988 "src/parser.tab.c"
    break;

  case 118: /* primary: LBRACKET argument_list_opt RBRACKET  */
#line 712 "src/parser.y"
                                          { (yyval.expr) = ast_array((yyvsp[-1].expr_list)); }
#line 2994 "src/parser.tab.c"
    break;

  case 119: /* primary: LBRACE optional_newlines RBRACE  */
#line 713 "src/parser.y"
                                      { (yyval.expr) = ast_record(ast_record_field_list_empty()); }
#line 3000 "src/parser.tab.c"
    break;

  case 120: /* primary: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 714 "src/parser.y"
                                                                          { (yyval.expr) = ast_record((yyvsp[-2].record_field_list)); }
#line 3006 "src/parser.tab.c"
    break;

  case 121: /* ident_suffix: %empty  */
#line 718 "src/parser.y"
                          {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_NONE;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 3016 "src/parser.tab.c"
    break;

  case 122: /* ident_suffix: LPAREN argument_list_opt RPAREN  */
#line 723 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 3026 "src/parser.tab.c"
    break;

  case 123: /* ident_suffix: DOT IDENT ident_dot_suffix  */
#line 728 "src/parser.y"
                                 {
        (yyval.ident_suffix) = (yyvsp[0].ident_suffix);
        (yyval.ident_suffix).name = (yyvsp[-1].text);
      }
#line 3035 "src/parser.tab.c"
    break;

  case 124: /* ident_dot_suffix: %empty  */
#line 735 "src/parser.y"
             {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_FIELD;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 3045 "src/parser.tab.c"
    break;

  case 125: /* ident_dot_suffix: LPAREN argument_list_opt RPAREN  */
#line 740 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_QUALIFIED_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 3055 "src/parser.tab.c"
    break;

  case 126: /* duration_terms: NUMBER IDENT  */
#line 748 "src/parser.y"
                   {
        AstDuration duration = {0};
        (yyval.duration) = duration_add_unit(duration, (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 3064 "src/parser.tab.c"
    break;

  case 127: /* duration_terms: duration_terms NUMBER IDENT  */
#line 752 "src/parser.y"
                                  {
        (yyval.duration) = duration_add_unit((yyvsp[-2].duration), (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 3072 "src/parser.tab.c"
    break;

  case 128: /* argument_list_opt: %empty  */
#line 758 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 3078 "src/parser.tab.c"
    break;

  case 129: /* argument_list_opt: argument_list  */
#line 759 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 3084 "src/parser.tab.c"
    break;

  case 130: /* argument_list: expression  */
#line 763 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 3090 "src/parser.tab.c"
    break;

  case 131: /* argument_list: argument_list COMMA expression  */
#line 764 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 3096 "src/parser.tab.c"
    break;

  case 132: /* parameter_list_opt: %empty  */
#line 768 "src/parser.y"
             { (yyval.name_list) = ast_name_list_empty(); }
#line 3102 "src/parser.tab.c"
    break;

  case 133: /* parameter_list_opt: parameter_list  */
#line 769 "src/parser.y"
                     { (yyval.name_list) = (yyvsp[0].name_list); }
#line 3108 "src/parser.tab.c"
    break;

  case 134: /* parameter_list: IDENT  */
#line 773 "src/parser.y"
            { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 3114 "src/parser.tab.c"
    break;

  case 135: /* parameter_list: parameter_list COMMA IDENT  */
#line 774 "src/parser.y"
                                 { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 3120 "src/parser.tab.c"
    break;

  case 136: /* record_field_list: IDENT OP_EQ expression  */
#line 778 "src/parser.y"
                             { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 3126 "src/parser.tab.c"
    break;

  case 137: /* record_field_list: record_field_list COMMA optional_newlines IDENT OP_EQ expression  */
#line 779 "src/parser.y"
                                                                       { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 3132 "src/parser.tab.c"
    break;


#line 3136 "src/parser.tab.c"

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

#line 787 "src/parser.y"


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
    case TOKEN_THEN: return THEN;
    case TOKEN_ELSE: return ELSE;
    case TOKEN_END: return END;
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
