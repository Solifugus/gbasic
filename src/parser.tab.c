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
        } else if (*p == '+' || *p == '-' || *p == '*' || *p == '/' ||
                   *p == '.' || *p == '[' || *p == ']' || *p == ',') {
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
    return *p == '=' || *p == '>' || *p == '<' ||
        (*p == '!' && (p[1] == '=' || p[1] == '>' || p[1] == '<'));
}

static int yylex(void);
static void yyerror(const char *message);

#line 278 "src/parser.tab.c"

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
  YYSYMBOL_END = 9,                        /* END  */
  YYSYMBOL_PRINT = 10,                     /* PRINT  */
  YYSYMBOL_TRUE = 11,                      /* TRUE  */
  YYSYMBOL_FALSE = 12,                     /* FALSE  */
  YYSYMBOL_AND = 13,                       /* AND  */
  YYSYMBOL_OR = 14,                        /* OR  */
  YYSYMBOL_NOT = 15,                       /* NOT  */
  YYSYMBOL_WITH = 16,                      /* WITH  */
  YYSYMBOL_FOR = 17,                       /* FOR  */
  YYSYMBOL_TO = 18,                        /* TO  */
  YYSYMBOL_IN = 19,                        /* IN  */
  YYSYMBOL_FUNCTION = 20,                  /* FUNCTION  */
  YYSYMBOL_RETURN = 21,                    /* RETURN  */
  YYSYMBOL_GOTO = 22,                      /* GOTO  */
  YYSYMBOL_GOSUB = 23,                     /* GOSUB  */
  YYSYMBOL_WATCH = 24,                     /* WATCH  */
  YYSYMBOL_WITHOUT = 25,                   /* WITHOUT  */
  YYSYMBOL_WATCHERS = 26,                  /* WATCHERS  */
  YYSYMBOL_ON = 27,                        /* ON  */
  YYSYMBOL_RESUME = 28,                    /* RESUME  */
  YYSYMBOL_NEXT = 29,                      /* NEXT  */
  YYSYMBOL_STOP = 30,                      /* STOP  */
  YYSYMBOL_ERROR_VALUE = 31,               /* ERROR_VALUE  */
  YYSYMBOL_MODIFIER = 32,                  /* MODIFIER  */
  YYSYMBOL_PROGRAM = 33,                   /* PROGRAM  */
  YYSYMBOL_LIBRARY = 34,                   /* LIBRARY  */
  YYSYMBOL_LOAD = 35,                      /* LOAD  */
  YYSYMBOL_USE = 36,                       /* USE  */
  YYSYMBOL_EXPORT = 37,                    /* EXPORT  */
  YYSYMBOL_OP_EQ = 38,                     /* OP_EQ  */
  YYSYMBOL_OP_NE = 39,                     /* OP_NE  */
  YYSYMBOL_OP_GT = 40,                     /* OP_GT  */
  YYSYMBOL_OP_LT = 41,                     /* OP_LT  */
  YYSYMBOL_OP_GE = 42,                     /* OP_GE  */
  YYSYMBOL_OP_LE = 43,                     /* OP_LE  */
  YYSYMBOL_OP_NGT = 44,                    /* OP_NGT  */
  YYSYMBOL_OP_NLT = 45,                    /* OP_NLT  */
  YYSYMBOL_OP_NGE = 46,                    /* OP_NGE  */
  YYSYMBOL_OP_NLE = 47,                    /* OP_NLE  */
  YYSYMBOL_PLUS = 48,                      /* PLUS  */
  YYSYMBOL_MINUS = 49,                     /* MINUS  */
  YYSYMBOL_STAR = 50,                      /* STAR  */
  YYSYMBOL_SLASH = 51,                     /* SLASH  */
  YYSYMBOL_LPAREN = 52,                    /* LPAREN  */
  YYSYMBOL_MOD_LPAREN = 53,                /* MOD_LPAREN  */
  YYSYMBOL_RPAREN = 54,                    /* RPAREN  */
  YYSYMBOL_LBRACKET = 55,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 56,                  /* RBRACKET  */
  YYSYMBOL_LBRACE = 57,                    /* LBRACE  */
  YYSYMBOL_RBRACE = 58,                    /* RBRACE  */
  YYSYMBOL_COMMA = 59,                     /* COMMA  */
  YYSYMBOL_COLON = 60,                     /* COLON  */
  YYSYMBOL_NEWLINE = 61,                   /* NEWLINE  */
  YYSYMBOL_NO_DOT = 62,                    /* NO_DOT  */
  YYSYMBOL_DOT = 63,                       /* DOT  */
  YYSYMBOL_YYACCEPT = 64,                  /* $accept  */
  YYSYMBOL_program = 65,                   /* program  */
  YYSYMBOL_statement_list = 66,            /* statement_list  */
  YYSYMBOL_statement = 67,                 /* statement  */
  YYSYMBOL_assignment = 68,                /* assignment  */
  YYSYMBOL_variable_name = 69,             /* variable_name  */
  YYSYMBOL_modifier = 70,                  /* modifier  */
  YYSYMBOL_modifier_name = 71,             /* modifier_name  */
  YYSYMBOL_modifier_word = 72,             /* modifier_word  */
  YYSYMBOL_print_statement = 73,           /* print_statement  */
  YYSYMBOL_call_statement = 74,            /* call_statement  */
  YYSYMBOL_with_lock_statement = 75,       /* with_lock_statement  */
  YYSYMBOL_for_each_statement = 76,        /* for_each_statement  */
  YYSYMBOL_function_statement = 77,        /* function_statement  */
  YYSYMBOL_modifier_statement = 78,        /* modifier_statement  */
  YYSYMBOL_program_statement = 79,         /* program_statement  */
  YYSYMBOL_library_statement = 80,         /* library_statement  */
  YYSYMBOL_use_statement = 81,             /* use_statement  */
  YYSYMBOL_modifier_signature = 82,        /* modifier_signature  */
  YYSYMBOL_modifier_context = 83,          /* modifier_context  */
  YYSYMBOL_watch_statement = 84,           /* watch_statement  */
  YYSYMBOL_without_watchers_statement = 85, /* without_watchers_statement  */
  YYSYMBOL_on_error_statement = 86,        /* on_error_statement  */
  YYSYMBOL_error_statement = 87,           /* error_statement  */
  YYSYMBOL_return_statement = 88,          /* return_statement  */
  YYSYMBOL_label_statement = 89,           /* label_statement  */
  YYSYMBOL_goto_statement = 90,            /* goto_statement  */
  YYSYMBOL_gosub_statement = 91,           /* gosub_statement  */
  YYSYMBOL_if_statement = 92,              /* if_statement  */
  YYSYMBOL_inline_statement = 93,          /* inline_statement  */
  YYSYMBOL_expression = 94,                /* expression  */
  YYSYMBOL_or_expression = 95,             /* or_expression  */
  YYSYMBOL_and_expression = 96,            /* and_expression  */
  YYSYMBOL_comparison_expression = 97,     /* comparison_expression  */
  YYSYMBOL_additive_expression = 98,       /* additive_expression  */
  YYSYMBOL_multiplicative_expression = 99, /* multiplicative_expression  */
  YYSYMBOL_unary_expression = 100,         /* unary_expression  */
  YYSYMBOL_postfix_expression = 101,       /* postfix_expression  */
  YYSYMBOL_comparison_operator = 102,      /* comparison_operator  */
  YYSYMBOL_primary = 103,                  /* primary  */
  YYSYMBOL_ident_suffix = 104,             /* ident_suffix  */
  YYSYMBOL_ident_dot_suffix = 105,         /* ident_dot_suffix  */
  YYSYMBOL_duration_terms = 106,           /* duration_terms  */
  YYSYMBOL_argument_list_opt = 107,        /* argument_list_opt  */
  YYSYMBOL_argument_list = 108,            /* argument_list  */
  YYSYMBOL_parameter_list_opt = 109,       /* parameter_list_opt  */
  YYSYMBOL_parameter_list = 110,           /* parameter_list  */
  YYSYMBOL_record_field_list = 111,        /* record_field_list  */
  YYSYMBOL_optional_newlines = 112         /* optional_newlines  */
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
#define YYLAST   619

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  64
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  49
/* YYNRULES -- Number of rules.  */
#define YYNRULES  130
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  285

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   318


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
      55,    56,    57,    58,    59,    60,    61,    62,    63
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   266,   266,   270,   271,   272,   276,   277,   278,   279,
     280,   281,   282,   283,   284,   285,   286,   287,   288,   289,
     290,   291,   292,   293,   294,   298,   299,   300,   304,   305,
     306,   310,   314,   315,   319,   320,   321,   322,   326,   330,
     331,   332,   345,   357,   363,   369,   372,   378,   384,   390,
     391,   392,   403,   417,   418,   422,   426,   432,   438,   439,
     440,   444,   448,   449,   453,   457,   461,   465,   468,   474,
     475,   479,   483,   484,   488,   489,   493,   494,   495,   499,
     500,   501,   505,   506,   507,   511,   512,   513,   517,   518,
     519,   523,   524,   525,   526,   527,   528,   529,   530,   531,
     532,   536,   537,   538,   539,   550,   551,   552,   553,   554,
     555,   556,   560,   565,   570,   577,   582,   590,   594,   600,
     601,   605,   606,   610,   611,   615,   616,   620,   621,   625,
     626
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
  "STRING", "MOD_CONTENT", "IF", "THEN", "END", "PRINT", "TRUE", "FALSE",
  "AND", "OR", "NOT", "WITH", "FOR", "TO", "IN", "FUNCTION", "RETURN",
  "GOTO", "GOSUB", "WATCH", "WITHOUT", "WATCHERS", "ON", "RESUME", "NEXT",
  "STOP", "ERROR_VALUE", "MODIFIER", "PROGRAM", "LIBRARY", "LOAD", "USE",
  "EXPORT", "OP_EQ", "OP_NE", "OP_GT", "OP_LT", "OP_GE", "OP_LE", "OP_NGT",
  "OP_NLT", "OP_NGE", "OP_NLE", "PLUS", "MINUS", "STAR", "SLASH", "LPAREN",
  "MOD_LPAREN", "RPAREN", "LBRACKET", "RBRACKET", "LBRACE", "RBRACE",
  "COMMA", "COLON", "NEWLINE", "NO_DOT", "DOT", "$accept", "program",
  "statement_list", "statement", "assignment", "variable_name", "modifier",
  "modifier_name", "modifier_word", "print_statement", "call_statement",
  "with_lock_statement", "for_each_statement", "function_statement",
  "modifier_statement", "program_statement", "library_statement",
  "use_statement", "modifier_signature", "modifier_context",
  "watch_statement", "without_watchers_statement", "on_error_statement",
  "error_statement", "return_statement", "label_statement",
  "goto_statement", "gosub_statement", "if_statement", "inline_statement",
  "expression", "or_expression", "and_expression", "comparison_expression",
  "additive_expression", "multiplicative_expression", "unary_expression",
  "postfix_expression", "comparison_operator", "primary", "ident_suffix",
  "ident_dot_suffix", "duration_terms", "argument_list_opt",
  "argument_list", "parameter_list_opt", "parameter_list",
  "record_field_list", "optional_newlines", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-162)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -162,    19,    96,  -162,   -34,   562,  -162,   562,    47,    51,
      62,   562,    70,    73,    27,    55,    53,  -162,    12,    29,
      82,    83,    84,    85,    58,  -162,  -162,    30,   -16,    35,
      37,  -162,  -162,  -162,  -162,  -162,  -162,    38,  -162,  -162,
      40,    46,    48,    49,    50,    54,  -162,   562,   104,   118,
    -162,  -162,  -162,  -162,   562,  -162,   562,   562,   562,  -162,
     -24,   116,   112,   121,  -162,   191,   -15,  -162,   -13,  -162,
     132,  -162,    86,   117,    87,  -162,  -162,  -162,   138,    88,
      18,   139,  -162,  -162,  -162,  -162,  -162,    16,  -162,   127,
      93,    89,   142,   144,    29,  -162,   562,   145,  -162,   114,
    -162,  -162,  -162,  -162,  -162,  -162,  -162,  -162,  -162,  -162,
      99,    95,   -26,  -162,  -162,  -162,   101,   100,     1,   562,
     154,  -162,     9,   562,   562,  -162,  -162,  -162,  -162,  -162,
    -162,  -162,  -162,  -162,  -162,   562,   562,   540,   562,   562,
     562,   562,   155,   157,   562,   562,   138,  -162,    17,  -162,
     161,   137,  -162,   115,   138,  -162,   164,   138,  -162,   167,
     169,   152,  -162,  -162,   562,  -162,   562,   562,   562,  -162,
    -162,   133,  -162,  -162,   119,   123,   128,  -162,  -162,  -162,
     120,   121,  -162,   -15,   -15,   562,    24,  -162,  -162,   129,
    -162,  -162,   125,   131,   130,   135,   146,   201,   166,  -162,
    -162,   562,   158,  -162,   147,   159,   247,  -162,  -162,   164,
    -162,  -162,  -162,   160,   562,  -162,    -9,  -162,   562,  -162,
     282,  -162,    24,  -162,   148,  -162,   150,  -162,  -162,   181,
     162,  -162,  -162,   156,   185,   163,  -162,  -162,     2,  -162,
     168,   208,  -162,   317,  -162,   352,   165,  -162,   387,  -162,
     184,  -162,   182,  -162,   186,   422,   204,   457,   199,  -162,
     193,   492,  -162,   527,   562,  -162,   212,   187,   222,   188,
     189,   219,   226,  -162,   205,  -162,   214,  -162,  -162,   216,
     224,  -162,  -162,  -162,  -162
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       3,     0,     2,     1,    28,     0,    29,     0,     0,     0,
       0,    62,     0,     0,     0,     0,     0,    30,     0,     0,
       0,     0,     0,     0,     0,     4,     5,     0,     0,     0,
       0,     9,    10,    11,    12,    13,    14,     0,    16,    17,
       0,     0,     0,     0,     0,     0,    24,   119,     0,   101,
      28,   103,   106,   107,     0,   105,     0,     0,   119,   129,
     112,     0,    71,    72,    74,    76,    79,    82,    85,    88,
     102,    38,     0,     0,     0,    63,    65,    66,     0,     0,
       0,     0,    61,    34,    36,    35,    37,    53,    32,     0,
       0,     0,    50,    49,     0,     6,     0,     0,    64,     0,
       7,     8,    15,    18,    19,    20,    21,    22,    23,   121,
       0,   120,     0,   117,    86,    87,     0,     0,     0,   119,
       0,   104,     0,     0,     0,    91,    92,    93,    94,    95,
      96,    97,    98,    99,   100,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   123,   125,     0,     3,
       0,     0,    60,     0,   123,    33,     0,   123,     3,     0,
       0,     0,    25,    31,     0,    39,     0,     0,   119,   108,
     109,     0,   110,   130,   129,     0,   115,     3,    69,    70,
       0,    73,    75,    80,    81,     0,    77,    83,    84,     0,
      90,   118,     0,     0,     0,   124,     0,     0,     0,    58,
      59,   119,     0,    55,     0,     0,     0,    52,    51,     0,
      26,   122,    27,     0,     0,   129,     0,   113,   119,   114,
       0,    68,    78,    89,     0,     3,     0,     3,   126,    29,
       0,    54,     3,     0,    29,     0,    40,   127,     0,   111,
       0,    29,     3,     0,     3,     0,     0,    41,     0,     3,
       0,     3,     0,   116,     0,     0,    29,     0,    29,    57,
      29,     0,    48,     0,     0,    67,    29,     0,    29,     0,
       0,    29,    29,   128,     0,    43,     0,    56,    45,     0,
       0,    42,    44,    47,    46
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -162,  -162,  -147,  -162,  -162,    -2,   197,  -162,   178,  -162,
    -162,  -162,  -162,  -162,  -162,  -162,  -162,  -162,   179,    78,
    -162,  -162,  -162,  -162,  -162,  -162,   171,   172,  -162,  -162,
      -4,  -162,   173,   176,  -129,   -53,   -46,  -162,   151,  -162,
    -162,  -162,  -162,   -54,  -162,   -97,   217,  -162,  -161
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_uint8 yydefgoto[] =
{
       0,     1,     2,    26,    27,    60,    99,    87,    88,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    89,   204,
      38,    39,    40,    41,    42,    43,    44,    45,    46,   180,
     109,    62,    63,    64,    65,    66,    67,    68,   138,    69,
     121,   219,    70,   110,   111,   194,   195,   174,   118
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      28,    61,   198,    71,   117,   171,   252,    75,   114,   186,
     115,   206,   167,   216,    82,    49,    50,    51,    47,     3,
      83,     6,    96,    52,    53,    84,   168,    54,   119,    48,
     220,    12,    13,    83,    85,   139,   140,    97,    84,   120,
     150,    17,   141,    55,    98,    86,   151,    85,   152,   239,
     142,    72,   173,   116,   238,    73,   222,   202,    86,   172,
     205,    56,   173,   173,    57,   175,    74,    58,   154,    59,
     177,   196,   135,   136,    76,    81,   197,    77,   243,    78,
     245,    79,   183,   184,    80,   248,    90,    91,    92,    93,
      94,    95,   162,   187,   188,   255,   100,   257,   101,   102,
       4,   103,   261,     5,   263,     6,     7,   104,   112,   105,
     106,   107,     8,     9,   213,   108,    10,    11,    12,    13,
      14,    15,   113,    16,   122,    17,   123,    18,    19,    20,
      21,    22,    23,    24,   124,   143,   145,   189,   144,   146,
     192,   193,   147,   153,   156,   157,   159,   230,   160,   149,
     158,   163,   164,   165,   166,   169,   170,    25,   176,   190,
     210,   191,   211,   212,   240,   199,   200,   201,   203,   209,
       4,   214,   207,     5,   208,   229,     7,   217,   215,   224,
     218,   221,     8,     9,   226,   223,    10,    11,    12,    13,
      14,    15,   225,    16,   197,    17,    28,    18,    19,    20,
      21,    22,    23,    24,    28,   228,   246,   227,   232,   242,
     237,   244,   231,   233,   236,   254,   247,   249,    28,   250,
     264,   267,   253,   269,   251,   270,   259,    25,   274,   125,
     126,   127,   128,   129,   130,   131,   132,   133,   134,   135,
     136,    28,   276,    28,    97,   262,    28,   265,   275,   277,
     278,     4,   279,    28,     5,    28,   234,     7,   280,    28,
     273,    28,   137,     8,     9,   155,   281,    10,    11,    12,
      13,    14,    15,   161,    16,   282,    17,   283,    18,    19,
      20,    21,    22,    23,    24,   284,     4,   235,   185,     5,
       0,   241,     7,   178,   179,   148,   181,     0,     8,     9,
     182,     0,    10,    11,    12,    13,    14,    15,    25,    16,
       0,    17,     0,    18,    19,    20,    21,    22,    23,    24,
       0,     4,     0,     0,     5,     0,   256,     7,     0,     0,
       0,     0,     0,     8,     9,     0,     0,    10,    11,    12,
      13,    14,    15,    25,    16,     0,    17,     0,    18,    19,
      20,    21,    22,    23,    24,     0,     4,     0,     0,     5,
       0,   258,     7,     0,     0,     0,     0,     0,     8,     9,
       0,     0,    10,    11,    12,    13,    14,    15,    25,    16,
       0,    17,     0,    18,    19,    20,    21,    22,    23,    24,
       0,     4,     0,     0,     5,     0,   260,     7,     0,     0,
       0,     0,     0,     8,     9,     0,     0,    10,    11,    12,
      13,    14,    15,    25,    16,     0,    17,     0,    18,    19,
      20,    21,    22,    23,    24,     0,     4,     0,     0,     5,
       0,   266,     7,     0,     0,     0,     0,     0,     8,     9,
       0,     0,    10,    11,    12,    13,    14,    15,    25,    16,
       0,    17,     0,    18,    19,    20,    21,    22,    23,    24,
       0,     4,     0,     0,     5,     0,   268,     7,     0,     0,
       0,     0,     0,     8,     9,     0,     0,    10,    11,    12,
      13,    14,    15,    25,    16,     0,    17,     0,    18,    19,
      20,    21,    22,    23,    24,     0,     4,     0,     0,     5,
       0,   271,     7,     0,     0,     0,     0,     0,     8,     9,
       0,     0,    10,    11,    12,    13,    14,    15,    25,    16,
       0,    17,     0,    18,    19,    20,    21,    22,    23,    24,
       0,     4,     0,     0,     5,     0,   272,     7,     0,     0,
       0,     0,     0,     8,     9,     0,     0,    10,    11,    12,
      13,    14,    15,    25,    16,     0,    17,     0,    18,    19,
      20,    21,    22,    23,    24,    49,    50,    51,     0,     0,
       0,     6,     0,    52,    53,     0,     0,    54,   125,   126,
     127,   128,   129,   130,   131,   132,   133,   134,    25,     0,
       0,    17,     0,    55,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    56,     0,     0,    57,     0,     0,    58,     0,    59
};

static const yytype_int16 yycheck[] =
{
       2,     5,   149,     7,    58,     4,     4,    11,    54,   138,
      56,   158,    38,   174,    18,     3,     4,     5,    52,     0,
       4,     9,    38,    11,    12,     9,    52,    15,    52,    63,
     177,    22,    23,     4,    18,    50,    51,    53,     9,    63,
      22,    29,    55,    31,    60,    29,    28,    18,    30,    58,
      63,     4,    61,    57,   215,     4,   185,   154,    29,    58,
     157,    49,    61,    61,    52,   119,     4,    55,    52,    57,
      61,    54,    48,    49,     4,    63,    59,     4,   225,    52,
     227,    26,   135,   136,    31,   232,     4,     4,     4,     4,
      32,    61,    96,   139,   140,   242,    61,   244,    61,    61,
       4,    61,   249,     7,   251,     9,    10,    61,     4,    61,
      61,    61,    16,    17,   168,    61,    20,    21,    22,    23,
      24,    25,     4,    27,     8,    29,    14,    31,    32,    33,
      34,    35,    36,    37,    13,     3,    19,   141,    52,    52,
     144,   145,     4,     4,    17,    52,     4,   201,     4,    61,
      61,     6,    38,    54,    59,    54,    56,    61,     4,     4,
     164,     4,   166,   167,   218,     4,    29,    52,     4,    17,
       4,    38,     5,     7,     5,     9,    10,    54,    59,    54,
      52,    61,    16,    17,    54,    56,    20,    21,    22,    23,
      24,    25,    61,    27,    59,    29,   198,    31,    32,    33,
      34,    35,    36,    37,   206,     4,    25,    61,    61,    61,
     214,    61,    54,    54,    54,     7,    54,    61,   220,    34,
      38,    17,    54,    24,    61,    32,    61,    61,    16,    38,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,   243,    20,   245,    53,    61,   248,    61,    61,    61,
      61,     4,    33,   255,     7,   257,     9,    10,    32,   261,
     264,   263,    65,    16,    17,    87,    61,    20,    21,    22,
      23,    24,    25,    94,    27,    61,    29,    61,    31,    32,
      33,    34,    35,    36,    37,    61,     4,   209,   137,     7,
      -1,     9,    10,   122,   122,    78,   123,    -1,    16,    17,
     124,    -1,    20,    21,    22,    23,    24,    25,    61,    27,
      -1,    29,    -1,    31,    32,    33,    34,    35,    36,    37,
      -1,     4,    -1,    -1,     7,    -1,     9,    10,    -1,    -1,
      -1,    -1,    -1,    16,    17,    -1,    -1,    20,    21,    22,
      23,    24,    25,    61,    27,    -1,    29,    -1,    31,    32,
      33,    34,    35,    36,    37,    -1,     4,    -1,    -1,     7,
      -1,     9,    10,    -1,    -1,    -1,    -1,    -1,    16,    17,
      -1,    -1,    20,    21,    22,    23,    24,    25,    61,    27,
      -1,    29,    -1,    31,    32,    33,    34,    35,    36,    37,
      -1,     4,    -1,    -1,     7,    -1,     9,    10,    -1,    -1,
      -1,    -1,    -1,    16,    17,    -1,    -1,    20,    21,    22,
      23,    24,    25,    61,    27,    -1,    29,    -1,    31,    32,
      33,    34,    35,    36,    37,    -1,     4,    -1,    -1,     7,
      -1,     9,    10,    -1,    -1,    -1,    -1,    -1,    16,    17,
      -1,    -1,    20,    21,    22,    23,    24,    25,    61,    27,
      -1,    29,    -1,    31,    32,    33,    34,    35,    36,    37,
      -1,     4,    -1,    -1,     7,    -1,     9,    10,    -1,    -1,
      -1,    -1,    -1,    16,    17,    -1,    -1,    20,    21,    22,
      23,    24,    25,    61,    27,    -1,    29,    -1,    31,    32,
      33,    34,    35,    36,    37,    -1,     4,    -1,    -1,     7,
      -1,     9,    10,    -1,    -1,    -1,    -1,    -1,    16,    17,
      -1,    -1,    20,    21,    22,    23,    24,    25,    61,    27,
      -1,    29,    -1,    31,    32,    33,    34,    35,    36,    37,
      -1,     4,    -1,    -1,     7,    -1,     9,    10,    -1,    -1,
      -1,    -1,    -1,    16,    17,    -1,    -1,    20,    21,    22,
      23,    24,    25,    61,    27,    -1,    29,    -1,    31,    32,
      33,    34,    35,    36,    37,     3,     4,     5,    -1,    -1,
      -1,     9,    -1,    11,    12,    -1,    -1,    15,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    61,    -1,
      -1,    29,    -1,    31,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    49,    -1,    -1,    52,    -1,    -1,    55,    -1,    57
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,    65,    66,     0,     4,     7,     9,    10,    16,    17,
      20,    21,    22,    23,    24,    25,    27,    29,    31,    32,
      33,    34,    35,    36,    37,    61,    67,    68,    69,    73,
      74,    75,    76,    77,    78,    79,    80,    81,    84,    85,
      86,    87,    88,    89,    90,    91,    92,    52,    63,     3,
       4,     5,    11,    12,    15,    31,    49,    52,    55,    57,
      69,    94,    95,    96,    97,    98,    99,   100,   101,   103,
     106,    94,     4,     4,     4,    94,     4,     4,    52,    26,
      31,    63,    94,     4,     9,    18,    29,    71,    72,    82,
       4,     4,     4,     4,    32,    61,    38,    53,    60,    70,
      61,    61,    61,    61,    61,    61,    61,    61,    61,    94,
     107,   108,     4,     4,   100,   100,    94,   107,   112,    52,
      63,   104,     8,    14,    13,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    70,   102,    50,
      51,    55,    63,     3,    52,    19,    52,     4,   110,    61,
      22,    28,    30,     4,    52,    72,    17,    52,    61,     4,
       4,    82,    94,     6,    38,    54,    59,    38,    52,    54,
      56,     4,    58,    61,   111,   107,     4,    61,    90,    91,
      93,    96,    97,    99,    99,   102,    98,   100,   100,    94,
       4,     4,    94,    94,   109,   110,    54,    59,    66,     4,
      29,    52,   109,     4,    83,   109,    66,     5,     5,    17,
      94,    94,    94,   107,    38,    59,   112,    54,    52,   105,
      66,    61,    98,    56,    54,    61,    54,    61,     4,     9,
     107,    54,    61,    54,     9,    83,    54,    94,   112,    58,
     107,     9,    61,    66,    61,    66,    25,    54,    66,    61,
      34,    61,     4,    54,     7,    66,     9,    66,     9,    61,
       9,    66,    61,    66,    38,    61,     9,    17,     9,    24,
      32,     9,     9,    94,    16,    61,    20,    61,    61,    33,
      32,    61,    61,    61,    61
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    64,    65,    66,    66,    66,    67,    67,    67,    67,
      67,    67,    67,    67,    67,    67,    67,    67,    67,    67,
      67,    67,    67,    67,    67,    68,    68,    68,    69,    69,
      69,    70,    71,    71,    72,    72,    72,    72,    73,    74,
      74,    74,    75,    76,    77,    78,    78,    79,    80,    81,
      81,    81,    81,    82,    82,    83,    84,    85,    86,    86,
      86,    87,    88,    88,    89,    90,    91,    92,    92,    93,
      93,    94,    95,    95,    96,    96,    97,    97,    97,    98,
      98,    98,    99,    99,    99,   100,   100,   100,   101,   101,
     101,   102,   102,   102,   102,   102,   102,   102,   102,   102,
     102,   103,   103,   103,   103,   103,   103,   103,   103,   103,
     103,   103,   104,   104,   104,   105,   105,   106,   106,   107,
     107,   108,   108,   109,   109,   110,   110,   111,   111,   112,
     112
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     2,     2,     2,     2,     1,
       1,     1,     1,     1,     1,     2,     1,     1,     2,     2,
       2,     2,     2,     2,     1,     3,     4,     5,     1,     1,
       1,     2,     1,     2,     1,     1,     1,     1,     2,     4,
       6,     6,    10,     9,    10,     9,    10,    10,     7,     2,
       2,     4,     4,     1,     4,     1,     9,     7,     4,     4,
       3,     2,     1,     2,     2,     2,     2,     8,     5,     1,
       1,     1,     1,     3,     1,     3,     1,     3,     4,     1,
       3,     3,     1,     3,     3,     1,     2,     2,     1,     4,
       3,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     2,     1,     1,     1,     3,     3,
       3,     5,     0,     3,     3,     0,     3,     2,     3,     0,
       1,     1,     3,     0,     1,     1,     3,     3,     6,     0,
       2
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
#line 266 "src/parser.y"
                     { parsed_program = (yyvsp[0].stmt_list); (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 2030 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 270 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 2036 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 271 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 2042 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 272 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 2048 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 276 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2054 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 277 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2060 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 278 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2066 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 279 "src/parser.y"
                          { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2072 "src/parser.tab.c"
    break;

  case 10: /* statement: for_each_statement  */
#line 280 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2078 "src/parser.tab.c"
    break;

  case 11: /* statement: function_statement  */
#line 281 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2084 "src/parser.tab.c"
    break;

  case 12: /* statement: modifier_statement  */
#line 282 "src/parser.y"
                         { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2090 "src/parser.tab.c"
    break;

  case 13: /* statement: program_statement  */
#line 283 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2096 "src/parser.tab.c"
    break;

  case 14: /* statement: library_statement  */
#line 284 "src/parser.y"
                        { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2102 "src/parser.tab.c"
    break;

  case 15: /* statement: use_statement NEWLINE  */
#line 285 "src/parser.y"
                            { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2108 "src/parser.tab.c"
    break;

  case 16: /* statement: watch_statement  */
#line 286 "src/parser.y"
                      { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2114 "src/parser.tab.c"
    break;

  case 17: /* statement: without_watchers_statement  */
#line 287 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2120 "src/parser.tab.c"
    break;

  case 18: /* statement: on_error_statement NEWLINE  */
#line 288 "src/parser.y"
                                 { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2126 "src/parser.tab.c"
    break;

  case 19: /* statement: error_statement NEWLINE  */
#line 289 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2132 "src/parser.tab.c"
    break;

  case 20: /* statement: return_statement NEWLINE  */
#line 290 "src/parser.y"
                               { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2138 "src/parser.tab.c"
    break;

  case 21: /* statement: label_statement NEWLINE  */
#line 291 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2144 "src/parser.tab.c"
    break;

  case 22: /* statement: goto_statement NEWLINE  */
#line 292 "src/parser.y"
                             { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2150 "src/parser.tab.c"
    break;

  case 23: /* statement: gosub_statement NEWLINE  */
#line 293 "src/parser.y"
                              { (yyval.stmt) = ast_stmt_position((yyvsp[-1].stmt), (yylsp[-1]).first_line, (yylsp[-1]).first_column); }
#line 2156 "src/parser.tab.c"
    break;

  case 24: /* statement: if_statement  */
#line 294 "src/parser.y"
                   { (yyval.stmt) = ast_stmt_position((yyvsp[0].stmt), (yylsp[0]).first_line, (yylsp[0]).first_column); }
#line 2162 "src/parser.tab.c"
    break;

  case 25: /* assignment: variable_name OP_EQ expression  */
#line 298 "src/parser.y"
                                     { (yyval.stmt) = ast_assign((yyvsp[-2].text), ast_modifier_none(), (yyvsp[0].expr)); }
#line 2168 "src/parser.tab.c"
    break;

  case 26: /* assignment: variable_name modifier OP_EQ expression  */
#line 299 "src/parser.y"
                                              { (yyval.stmt) = ast_assign((yyvsp[-3].text), (yyvsp[-2].modifier), (yyvsp[0].expr)); }
#line 2174 "src/parser.tab.c"
    break;

  case 27: /* assignment: IDENT DOT IDENT OP_EQ expression  */
#line 300 "src/parser.y"
                                       { (yyval.stmt) = ast_field_assign((yyvsp[-4].text), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 2180 "src/parser.tab.c"
    break;

  case 28: /* variable_name: IDENT  */
#line 304 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 2186 "src/parser.tab.c"
    break;

  case 29: /* variable_name: END  */
#line 305 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 2192 "src/parser.tab.c"
    break;

  case 30: /* variable_name: NEXT  */
#line 306 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 2198 "src/parser.tab.c"
    break;

  case 31: /* modifier: MOD_LPAREN MOD_CONTENT  */
#line 310 "src/parser.y"
                             { (yyval.modifier) = parse_modifier_use((yyvsp[0].text)); }
#line 2204 "src/parser.tab.c"
    break;

  case 32: /* modifier_name: modifier_word  */
#line 314 "src/parser.y"
                    { (yyval.text) = (yyvsp[0].text); }
#line 2210 "src/parser.tab.c"
    break;

  case 33: /* modifier_name: modifier_name modifier_word  */
#line 315 "src/parser.y"
                                  { (yyval.text) = join_words((yyvsp[-1].text), (yyvsp[0].text)); }
#line 2216 "src/parser.tab.c"
    break;

  case 34: /* modifier_word: IDENT  */
#line 319 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 2222 "src/parser.tab.c"
    break;

  case 35: /* modifier_word: TO  */
#line 320 "src/parser.y"
         { (yyval.text) = copy_const("to"); }
#line 2228 "src/parser.tab.c"
    break;

  case 36: /* modifier_word: END  */
#line 321 "src/parser.y"
          { (yyval.text) = copy_const("end"); }
#line 2234 "src/parser.tab.c"
    break;

  case 37: /* modifier_word: NEXT  */
#line 322 "src/parser.y"
           { (yyval.text) = copy_const("next"); }
#line 2240 "src/parser.tab.c"
    break;

  case 38: /* print_statement: PRINT expression  */
#line 326 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 2246 "src/parser.tab.c"
    break;

  case 39: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 330 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 2252 "src/parser.tab.c"
    break;

  case 40: /* call_statement: IDENT DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 331 "src/parser.y"
                                                      { (yyval.stmt) = ast_expr_stmt(ast_qualified_call((yyvsp[-5].text), (yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 2258 "src/parser.tab.c"
    break;

  case 41: /* call_statement: ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN  */
#line 332 "src/parser.y"
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
#line 2273 "src/parser.tab.c"
    break;

  case 42: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 345 "src/parser.y"
                                                                                  {
        if (strcmp((yyvsp[-8].text), "lock") != 0) {
            yyerror("expected lock in with lock block");
            free((yyvsp[-8].text));
            YYERROR;
        }
        free((yyvsp[-8].text));
        (yyval.stmt) = ast_with_lock((yyvsp[-6].expr), (yyvsp[-3].stmt_list));
      }
#line 2287 "src/parser.tab.c"
    break;

  case 43: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list END FOR NEWLINE  */
#line 357 "src/parser.y"
                                                                     {
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 2295 "src/parser.tab.c"
    break;

  case 44: /* function_statement: FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE  */
#line 363 "src/parser.y"
                                                                                                  {
        (yyval.stmt) = ast_function((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 2303 "src/parser.tab.c"
    break;

  case 45: /* modifier_statement: MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 369 "src/parser.y"
                                                                                                   {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 0, (yyvsp[-3].stmt_list));
      }
#line 2311 "src/parser.tab.c"
    break;

  case 46: /* modifier_statement: EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE  */
#line 372 "src/parser.y"
                                                                                                          {
        (yyval.stmt) = ast_modifier((yyvsp[-7].modifier_signature).name, (yyvsp[-7].modifier_signature).params, (yyvsp[-5].text), 1, (yyvsp[-3].stmt_list));
      }
#line 2319 "src/parser.tab.c"
    break;

  case 47: /* program_statement: PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE  */
#line 378 "src/parser.y"
                                                                                                {
        (yyval.stmt) = ast_program((yyvsp[-8].text), (yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 2327 "src/parser.tab.c"
    break;

  case 48: /* library_statement: LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE  */
#line 384 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_library((yyvsp[-5].text), (yyvsp[-3].stmt_list));
      }
#line 2335 "src/parser.tab.c"
    break;

  case 49: /* use_statement: USE IDENT  */
#line 390 "src/parser.y"
                { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 2341 "src/parser.tab.c"
    break;

  case 50: /* use_statement: LOAD IDENT  */
#line 391 "src/parser.y"
                 { (yyval.stmt) = ast_use((yyvsp[0].text), NULL); }
#line 2347 "src/parser.tab.c"
    break;

  case 51: /* use_statement: USE IDENT IDENT STRING  */
#line 392 "src/parser.y"
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
#line 2363 "src/parser.tab.c"
    break;

  case 52: /* use_statement: LOAD IDENT IDENT STRING  */
#line 403 "src/parser.y"
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
#line 2379 "src/parser.tab.c"
    break;

  case 53: /* modifier_signature: modifier_name  */
#line 417 "src/parser.y"
                    { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[0].text), ast_name_list_empty()); }
#line 2385 "src/parser.tab.c"
    break;

  case 54: /* modifier_signature: modifier_name LPAREN parameter_list_opt RPAREN  */
#line 418 "src/parser.y"
                                                     { (yyval.modifier_signature) = ast_modifier_signature((yyvsp[-3].text), (yyvsp[-1].name_list)); }
#line 2391 "src/parser.tab.c"
    break;

  case 55: /* modifier_context: IDENT  */
#line 422 "src/parser.y"
            { (yyval.text) = (yyvsp[0].text); }
#line 2397 "src/parser.tab.c"
    break;

  case 56: /* watch_statement: WATCH LPAREN parameter_list RPAREN NEWLINE statement_list END WATCH NEWLINE  */
#line 426 "src/parser.y"
                                                                                  {
        (yyval.stmt) = ast_watch((yyvsp[-6].name_list), (yyvsp[-3].stmt_list));
      }
#line 2405 "src/parser.tab.c"
    break;

  case 57: /* without_watchers_statement: WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE  */
#line 432 "src/parser.y"
                                                                  {
        (yyval.stmt) = ast_without_watchers((yyvsp[-3].stmt_list));
      }
#line 2413 "src/parser.tab.c"
    break;

  case 58: /* on_error_statement: ON ERROR_VALUE GOTO IDENT  */
#line 438 "src/parser.y"
                                { (yyval.stmt) = ast_on_error_goto((yyvsp[0].text)); }
#line 2419 "src/parser.tab.c"
    break;

  case 59: /* on_error_statement: ON ERROR_VALUE RESUME NEXT  */
#line 439 "src/parser.y"
                                 { (yyval.stmt) = ast_on_error_resume_next(); }
#line 2425 "src/parser.tab.c"
    break;

  case 60: /* on_error_statement: ON ERROR_VALUE STOP  */
#line 440 "src/parser.y"
                          { (yyval.stmt) = ast_on_error_stop(); }
#line 2431 "src/parser.tab.c"
    break;

  case 61: /* error_statement: ERROR_VALUE expression  */
#line 444 "src/parser.y"
                             { (yyval.stmt) = ast_error((yyvsp[0].expr)); }
#line 2437 "src/parser.tab.c"
    break;

  case 62: /* return_statement: RETURN  */
#line 448 "src/parser.y"
             { (yyval.stmt) = ast_return(NULL); }
#line 2443 "src/parser.tab.c"
    break;

  case 63: /* return_statement: RETURN expression  */
#line 449 "src/parser.y"
                        { (yyval.stmt) = ast_return((yyvsp[0].expr)); }
#line 2449 "src/parser.tab.c"
    break;

  case 64: /* label_statement: variable_name COLON  */
#line 453 "src/parser.y"
                          { (yyval.stmt) = ast_label((yyvsp[-1].text)); }
#line 2455 "src/parser.tab.c"
    break;

  case 65: /* goto_statement: GOTO IDENT  */
#line 457 "src/parser.y"
                 { (yyval.stmt) = ast_goto((yyvsp[0].text)); }
#line 2461 "src/parser.tab.c"
    break;

  case 66: /* gosub_statement: GOSUB IDENT  */
#line 461 "src/parser.y"
                  { (yyval.stmt) = ast_gosub((yyvsp[0].text)); }
#line 2467 "src/parser.tab.c"
    break;

  case 67: /* if_statement: IF expression THEN NEWLINE statement_list END IF NEWLINE  */
#line 465 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_if((yyvsp[-6].expr), (yyvsp[-3].stmt_list));
      }
#line 2475 "src/parser.tab.c"
    break;

  case 68: /* if_statement: IF expression THEN inline_statement NEWLINE  */
#line 468 "src/parser.y"
                                                  {
        (yyval.stmt) = ast_if((yyvsp[-3].expr), ast_stmt_list_append(ast_stmt_list_empty(), (yyvsp[-1].stmt)));
      }
#line 2483 "src/parser.tab.c"
    break;

  case 69: /* inline_statement: goto_statement  */
#line 474 "src/parser.y"
                     { (yyval.stmt) = (yyvsp[0].stmt); }
#line 2489 "src/parser.tab.c"
    break;

  case 70: /* inline_statement: gosub_statement  */
#line 475 "src/parser.y"
                      { (yyval.stmt) = (yyvsp[0].stmt); }
#line 2495 "src/parser.tab.c"
    break;

  case 71: /* expression: or_expression  */
#line 479 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 2501 "src/parser.tab.c"
    break;

  case 72: /* or_expression: and_expression  */
#line 483 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 2507 "src/parser.tab.c"
    break;

  case 73: /* or_expression: or_expression OR and_expression  */
#line 484 "src/parser.y"
                                      { (yyval.expr) = ast_binary(copy_const("or"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 2513 "src/parser.tab.c"
    break;

  case 74: /* and_expression: comparison_expression  */
#line 488 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 2519 "src/parser.tab.c"
    break;

  case 75: /* and_expression: and_expression AND comparison_expression  */
#line 489 "src/parser.y"
                                               { (yyval.expr) = ast_binary(copy_const("and"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 2525 "src/parser.tab.c"
    break;

  case 76: /* comparison_expression: additive_expression  */
#line 493 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 2531 "src/parser.tab.c"
    break;

  case 77: /* comparison_expression: additive_expression comparison_operator additive_expression  */
#line 494 "src/parser.y"
                                                                  { (yyval.expr) = ast_binary((yyvsp[-1].text), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 2537 "src/parser.tab.c"
    break;

  case 78: /* comparison_expression: additive_expression modifier comparison_operator additive_expression  */
#line 495 "src/parser.y"
                                                                           { (yyval.expr) = ast_binary((yyvsp[-1].text), (yyvsp[-2].modifier), (yyvsp[-3].expr), (yyvsp[0].expr)); }
#line 2543 "src/parser.tab.c"
    break;

  case 79: /* additive_expression: multiplicative_expression  */
#line 499 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 2549 "src/parser.tab.c"
    break;

  case 80: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 500 "src/parser.y"
                                                         { (yyval.expr) = ast_binary(copy_const("+"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 2555 "src/parser.tab.c"
    break;

  case 81: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 501 "src/parser.y"
                                                          { (yyval.expr) = ast_binary(copy_const("-"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 2561 "src/parser.tab.c"
    break;

  case 82: /* multiplicative_expression: unary_expression  */
#line 505 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 2567 "src/parser.tab.c"
    break;

  case 83: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 506 "src/parser.y"
                                                      { (yyval.expr) = ast_binary(copy_const("*"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 2573 "src/parser.tab.c"
    break;

  case 84: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 507 "src/parser.y"
                                                       { (yyval.expr) = ast_binary(copy_const("/"), ast_modifier_none(), (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 2579 "src/parser.tab.c"
    break;

  case 85: /* unary_expression: postfix_expression  */
#line 511 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 2585 "src/parser.tab.c"
    break;

  case 86: /* unary_expression: NOT unary_expression  */
#line 512 "src/parser.y"
                           { (yyval.expr) = ast_unary(copy_const("not"), (yyvsp[0].expr)); }
#line 2591 "src/parser.tab.c"
    break;

  case 87: /* unary_expression: MINUS unary_expression  */
#line 513 "src/parser.y"
                             { (yyval.expr) = ast_unary(copy_const("-"), (yyvsp[0].expr)); }
#line 2597 "src/parser.tab.c"
    break;

  case 88: /* postfix_expression: primary  */
#line 517 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 2603 "src/parser.tab.c"
    break;

  case 89: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 518 "src/parser.y"
                                                      { (yyval.expr) = ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)); }
#line 2609 "src/parser.tab.c"
    break;

  case 90: /* postfix_expression: postfix_expression DOT IDENT  */
#line 519 "src/parser.y"
                                   { (yyval.expr) = ast_field((yyvsp[-2].expr), (yyvsp[0].text)); }
#line 2615 "src/parser.tab.c"
    break;

  case 91: /* comparison_operator: OP_EQ  */
#line 523 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 2621 "src/parser.tab.c"
    break;

  case 92: /* comparison_operator: OP_NE  */
#line 524 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 2627 "src/parser.tab.c"
    break;

  case 93: /* comparison_operator: OP_GT  */
#line 525 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 2633 "src/parser.tab.c"
    break;

  case 94: /* comparison_operator: OP_LT  */
#line 526 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 2639 "src/parser.tab.c"
    break;

  case 95: /* comparison_operator: OP_GE  */
#line 527 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 2645 "src/parser.tab.c"
    break;

  case 96: /* comparison_operator: OP_LE  */
#line 528 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 2651 "src/parser.tab.c"
    break;

  case 97: /* comparison_operator: OP_NGT  */
#line 529 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 2657 "src/parser.tab.c"
    break;

  case 98: /* comparison_operator: OP_NLT  */
#line 530 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 2663 "src/parser.tab.c"
    break;

  case 99: /* comparison_operator: OP_NGE  */
#line 531 "src/parser.y"
             { (yyval.text) = copy_const("!>="); }
#line 2669 "src/parser.tab.c"
    break;

  case 100: /* comparison_operator: OP_NLE  */
#line 532 "src/parser.y"
             { (yyval.text) = copy_const("!<="); }
#line 2675 "src/parser.tab.c"
    break;

  case 101: /* primary: NUMBER  */
#line 536 "src/parser.y"
             { (yyval.expr) = ast_number((yyvsp[0].number)); }
#line 2681 "src/parser.tab.c"
    break;

  case 102: /* primary: duration_terms  */
#line 537 "src/parser.y"
                     { (yyval.expr) = ast_duration((yyvsp[0].duration)); }
#line 2687 "src/parser.tab.c"
    break;

  case 103: /* primary: STRING  */
#line 538 "src/parser.y"
             { (yyval.expr) = ast_string((yyvsp[0].text)); }
#line 2693 "src/parser.tab.c"
    break;

  case 104: /* primary: variable_name ident_suffix  */
#line 539 "src/parser.y"
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
#line 2709 "src/parser.tab.c"
    break;

  case 105: /* primary: ERROR_VALUE  */
#line 550 "src/parser.y"
                  { (yyval.expr) = ast_ident(copy_const("error")); }
#line 2715 "src/parser.tab.c"
    break;

  case 106: /* primary: TRUE  */
#line 551 "src/parser.y"
           { (yyval.expr) = ast_bool(1); }
#line 2721 "src/parser.tab.c"
    break;

  case 107: /* primary: FALSE  */
#line 552 "src/parser.y"
            { (yyval.expr) = ast_bool(0); }
#line 2727 "src/parser.tab.c"
    break;

  case 108: /* primary: LPAREN expression RPAREN  */
#line 553 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); }
#line 2733 "src/parser.tab.c"
    break;

  case 109: /* primary: LBRACKET argument_list_opt RBRACKET  */
#line 554 "src/parser.y"
                                          { (yyval.expr) = ast_array((yyvsp[-1].expr_list)); }
#line 2739 "src/parser.tab.c"
    break;

  case 110: /* primary: LBRACE optional_newlines RBRACE  */
#line 555 "src/parser.y"
                                      { (yyval.expr) = ast_record(ast_record_field_list_empty()); }
#line 2745 "src/parser.tab.c"
    break;

  case 111: /* primary: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 556 "src/parser.y"
                                                                          { (yyval.expr) = ast_record((yyvsp[-2].record_field_list)); }
#line 2751 "src/parser.tab.c"
    break;

  case 112: /* ident_suffix: %empty  */
#line 560 "src/parser.y"
                          {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_NONE;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 2761 "src/parser.tab.c"
    break;

  case 113: /* ident_suffix: LPAREN argument_list_opt RPAREN  */
#line 565 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 2771 "src/parser.tab.c"
    break;

  case 114: /* ident_suffix: DOT IDENT ident_dot_suffix  */
#line 570 "src/parser.y"
                                 {
        (yyval.ident_suffix) = (yyvsp[0].ident_suffix);
        (yyval.ident_suffix).name = (yyvsp[-1].text);
      }
#line 2780 "src/parser.tab.c"
    break;

  case 115: /* ident_dot_suffix: %empty  */
#line 577 "src/parser.y"
             {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_FIELD;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = ast_expr_list_empty();
      }
#line 2790 "src/parser.tab.c"
    break;

  case 116: /* ident_dot_suffix: LPAREN argument_list_opt RPAREN  */
#line 582 "src/parser.y"
                                      {
        (yyval.ident_suffix).kind = IDENT_SUFFIX_QUALIFIED_CALL;
        (yyval.ident_suffix).name = NULL;
        (yyval.ident_suffix).args = (yyvsp[-1].expr_list);
      }
#line 2800 "src/parser.tab.c"
    break;

  case 117: /* duration_terms: NUMBER IDENT  */
#line 590 "src/parser.y"
                   {
        AstDuration duration = {0};
        (yyval.duration) = duration_add_unit(duration, (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 2809 "src/parser.tab.c"
    break;

  case 118: /* duration_terms: duration_terms NUMBER IDENT  */
#line 594 "src/parser.y"
                                  {
        (yyval.duration) = duration_add_unit((yyvsp[-2].duration), (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 2817 "src/parser.tab.c"
    break;

  case 119: /* argument_list_opt: %empty  */
#line 600 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 2823 "src/parser.tab.c"
    break;

  case 120: /* argument_list_opt: argument_list  */
#line 601 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 2829 "src/parser.tab.c"
    break;

  case 121: /* argument_list: expression  */
#line 605 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 2835 "src/parser.tab.c"
    break;

  case 122: /* argument_list: argument_list COMMA expression  */
#line 606 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 2841 "src/parser.tab.c"
    break;

  case 123: /* parameter_list_opt: %empty  */
#line 610 "src/parser.y"
             { (yyval.name_list) = ast_name_list_empty(); }
#line 2847 "src/parser.tab.c"
    break;

  case 124: /* parameter_list_opt: parameter_list  */
#line 611 "src/parser.y"
                     { (yyval.name_list) = (yyvsp[0].name_list); }
#line 2853 "src/parser.tab.c"
    break;

  case 125: /* parameter_list: IDENT  */
#line 615 "src/parser.y"
            { (yyval.name_list) = ast_name_list_append(ast_name_list_empty(), (yyvsp[0].text)); }
#line 2859 "src/parser.tab.c"
    break;

  case 126: /* parameter_list: parameter_list COMMA IDENT  */
#line 616 "src/parser.y"
                                 { (yyval.name_list) = ast_name_list_append((yyvsp[-2].name_list), (yyvsp[0].text)); }
#line 2865 "src/parser.tab.c"
    break;

  case 127: /* record_field_list: IDENT OP_EQ expression  */
#line 620 "src/parser.y"
                             { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 2871 "src/parser.tab.c"
    break;

  case 128: /* record_field_list: record_field_list COMMA optional_newlines IDENT OP_EQ expression  */
#line 621 "src/parser.y"
                                                                       { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 2877 "src/parser.tab.c"
    break;


#line 2881 "src/parser.tab.c"

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

#line 629 "src/parser.y"


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
    case TOKEN_END: return END;
    case TOKEN_PRINT: return PRINT;
    case TOKEN_TRUE: return TRUE;
    case TOKEN_FALSE: return FALSE;
    case TOKEN_AND: return AND;
    case TOKEN_OR: return OR;
    case TOKEN_NOT: return NOT;
    case TOKEN_WITH: return WITH;
    case TOKEN_FOR: return FOR;
    case TOKEN_TO: return TO;
    case TOKEN_IN: return IN;
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
