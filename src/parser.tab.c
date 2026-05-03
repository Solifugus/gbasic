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

static char *copy_string_literal(const char *start, int length) {
    if (length < 2) {
        return copy_text("", 0);
    }

    char *text = malloc((size_t)length - 1);
    if (!text) {
        abort();
    }
    int out = 0;
    for (int i = 1; i < length - 1; i++) {
        if (start[i] == '\\' && i + 1 < length - 1) {
            i++;
            if (start[i] == 'n') {
                text[out++] = '\n';
            } else if (start[i] == 't') {
                text[out++] = '\t';
            } else if (start[i] == '"' || start[i] == '\\') {
                text[out++] = start[i];
            } else {
                text[out++] = start[i];
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
    if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || *p == '_')) {
        return 0;
    }
    saw_term = 1;
    while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
           (*p >= '0' && *p <= '9') || *p == '_') {
        p++;
    }
    while (*p == ' ' || *p == '\t' || *p == '\r') {
        p++;
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

#line 187 "src/parser.tab.c"

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
  YYSYMBOL_IF = 6,                         /* IF  */
  YYSYMBOL_THEN = 7,                       /* THEN  */
  YYSYMBOL_END = 8,                        /* END  */
  YYSYMBOL_PRINT = 9,                      /* PRINT  */
  YYSYMBOL_TRUE = 10,                      /* TRUE  */
  YYSYMBOL_FALSE = 11,                     /* FALSE  */
  YYSYMBOL_AND = 12,                       /* AND  */
  YYSYMBOL_OR = 13,                        /* OR  */
  YYSYMBOL_NOT = 14,                       /* NOT  */
  YYSYMBOL_WITH = 15,                      /* WITH  */
  YYSYMBOL_FOR = 16,                       /* FOR  */
  YYSYMBOL_IN = 17,                        /* IN  */
  YYSYMBOL_OP_EQ = 18,                     /* OP_EQ  */
  YYSYMBOL_OP_NE = 19,                     /* OP_NE  */
  YYSYMBOL_OP_GT = 20,                     /* OP_GT  */
  YYSYMBOL_OP_LT = 21,                     /* OP_LT  */
  YYSYMBOL_OP_GE = 22,                     /* OP_GE  */
  YYSYMBOL_OP_LE = 23,                     /* OP_LE  */
  YYSYMBOL_OP_NGT = 24,                    /* OP_NGT  */
  YYSYMBOL_OP_NLT = 25,                    /* OP_NLT  */
  YYSYMBOL_PLUS = 26,                      /* PLUS  */
  YYSYMBOL_MINUS = 27,                     /* MINUS  */
  YYSYMBOL_STAR = 28,                      /* STAR  */
  YYSYMBOL_SLASH = 29,                     /* SLASH  */
  YYSYMBOL_LPAREN = 30,                    /* LPAREN  */
  YYSYMBOL_MOD_LPAREN = 31,                /* MOD_LPAREN  */
  YYSYMBOL_RPAREN = 32,                    /* RPAREN  */
  YYSYMBOL_LBRACKET = 33,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 34,                  /* RBRACKET  */
  YYSYMBOL_LBRACE = 35,                    /* LBRACE  */
  YYSYMBOL_RBRACE = 36,                    /* RBRACE  */
  YYSYMBOL_COMMA = 37,                     /* COMMA  */
  YYSYMBOL_DOT = 38,                       /* DOT  */
  YYSYMBOL_NEWLINE = 39,                   /* NEWLINE  */
  YYSYMBOL_YYACCEPT = 40,                  /* $accept  */
  YYSYMBOL_program = 41,                   /* program  */
  YYSYMBOL_statement_list = 42,            /* statement_list  */
  YYSYMBOL_statement = 43,                 /* statement  */
  YYSYMBOL_assignment = 44,                /* assignment  */
  YYSYMBOL_modifier = 45,                  /* modifier  */
  YYSYMBOL_print_statement = 46,           /* print_statement  */
  YYSYMBOL_call_statement = 47,            /* call_statement  */
  YYSYMBOL_with_lock_statement = 48,       /* with_lock_statement  */
  YYSYMBOL_for_each_statement = 49,        /* for_each_statement  */
  YYSYMBOL_if_statement = 50,              /* if_statement  */
  YYSYMBOL_expression = 51,                /* expression  */
  YYSYMBOL_or_expression = 52,             /* or_expression  */
  YYSYMBOL_and_expression = 53,            /* and_expression  */
  YYSYMBOL_comparison_expression = 54,     /* comparison_expression  */
  YYSYMBOL_additive_expression = 55,       /* additive_expression  */
  YYSYMBOL_multiplicative_expression = 56, /* multiplicative_expression  */
  YYSYMBOL_unary_expression = 57,          /* unary_expression  */
  YYSYMBOL_postfix_expression = 58,        /* postfix_expression  */
  YYSYMBOL_comparison_operator = 59,       /* comparison_operator  */
  YYSYMBOL_primary = 60,                   /* primary  */
  YYSYMBOL_duration_terms = 61,            /* duration_terms  */
  YYSYMBOL_argument_list_opt = 62,         /* argument_list_opt  */
  YYSYMBOL_argument_list = 63,             /* argument_list  */
  YYSYMBOL_record_field_list = 64,         /* record_field_list  */
  YYSYMBOL_optional_newlines = 65          /* optional_newlines  */
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
typedef yytype_uint8 yy_state_t;

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
         || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

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
#define YYLAST   148

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  40
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  26
/* YYNRULES -- Number of rules.  */
#define YYNRULES  69
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  139

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   294


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
      35,    36,    37,    38,    39
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   151,   151,   155,   156,   157,   161,   162,   163,   164,
     165,   166,   170,   171,   172,   176,   180,   184,   188,   200,
     206,   212,   216,   217,   221,   222,   226,   227,   228,   232,
     233,   234,   238,   239,   240,   244,   245,   246,   250,   251,
     252,   256,   257,   258,   259,   260,   261,   262,   263,   267,
     268,   269,   270,   271,   272,   273,   274,   275,   276,   277,
     281,   285,   291,   292,   296,   297,   301,   302,   306,   307
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
  "STRING", "IF", "THEN", "END", "PRINT", "TRUE", "FALSE", "AND", "OR",
  "NOT", "WITH", "FOR", "IN", "OP_EQ", "OP_NE", "OP_GT", "OP_LT", "OP_GE",
  "OP_LE", "OP_NGT", "OP_NLT", "PLUS", "MINUS", "STAR", "SLASH", "LPAREN",
  "MOD_LPAREN", "RPAREN", "LBRACKET", "RBRACKET", "LBRACE", "RBRACE",
  "COMMA", "DOT", "NEWLINE", "$accept", "program", "statement_list",
  "statement", "assignment", "modifier", "print_statement",
  "call_statement", "with_lock_statement", "for_each_statement",
  "if_statement", "expression", "or_expression", "and_expression",
  "comparison_expression", "additive_expression",
  "multiplicative_expression", "unary_expression", "postfix_expression",
  "comparison_operator", "primary", "duration_terms", "argument_list_opt",
  "argument_list", "record_field_list", "optional_newlines", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-92)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int8 yypact[] =
{
     -92,    20,    51,   -92,    65,    58,    58,     3,    26,   -92,
     -92,    -4,    15,    19,   -92,   -92,   -92,    58,    58,    55,
      66,    57,    69,    46,   -92,   -92,   -92,    58,    58,    58,
      58,   -92,    72,    68,    74,   -92,    91,   -15,   -92,   -11,
     -92,    81,   -92,    59,    70,   -92,   -92,   -92,   -92,   -92,
      60,    61,    62,    79,    58,   -92,    58,   -92,   -92,    67,
      71,     6,    63,    58,    58,   -92,   -92,   -92,   -92,   -92,
     -92,   -92,   -92,    58,    58,   109,    58,    58,    58,    58,
      96,    97,    58,    58,   -92,    58,   -92,    58,   -92,    75,
     -92,   -92,    86,   -92,   -92,    82,   -92,    74,   -92,   -15,
     -15,    58,    24,   -92,   -92,    87,   -92,   -92,    88,    84,
     -92,   -92,   -92,    58,   -92,    -8,     0,    24,   -92,    85,
     -92,   -92,     7,   -92,   100,   -92,    17,   107,    98,    32,
     119,    58,   -92,   121,    99,   -92,   101,   -92,   -92
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       3,     0,     2,     1,     0,     0,     0,     0,     0,     4,
       5,     0,     0,     0,     9,    10,    11,     0,    62,     0,
       0,     0,    49,    52,    51,    54,    55,     0,     0,     0,
      62,    68,     0,    21,    22,    24,    26,    29,    32,    35,
      38,    50,    16,     0,     0,     6,     7,     8,    12,    64,
       0,    63,     0,     0,     0,    60,    62,    36,    37,     0,
       0,     0,     0,     0,     0,    41,    42,    43,    44,    45,
      46,    47,    48,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    17,     0,    15,     0,    13,     0,
      56,    57,     0,    58,    69,    68,     3,    23,    25,    30,
      31,     0,    27,    33,    34,     0,    40,    61,     0,     0,
      65,    14,    53,     0,    68,     0,     0,    28,    39,     0,
       3,    66,     0,    59,     0,     3,     0,     0,     0,     0,
       0,     0,    20,     0,     0,    67,     0,    19,    18
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int8 yypgoto[] =
{
     -92,   -92,   -91,   -92,   -92,   103,   -92,   -92,   -92,   -92,
     -92,    -5,   -92,    78,    80,   -57,    -9,   -25,   -92,    73,
     -92,   -92,   -13,   -92,   -92,   -77
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int8 yydefgoto[] =
{
       0,     1,     2,    10,    11,    21,    12,    13,    14,    15,
      16,    49,    33,    34,    35,    36,    37,    38,    39,    76,
      40,    41,    50,    51,    95,    61
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_uint8 yytable[] =
{
      32,    42,    57,    58,     4,   116,     5,    43,   124,     6,
      92,   127,    48,    77,    78,     7,     8,    60,   115,   102,
       3,     4,    79,     5,    59,   130,     6,    80,   123,   126,
      44,    94,     7,     8,   129,    45,     4,   122,     5,     9,
     133,     6,    93,    89,   117,    94,    94,     7,     8,    88,
      73,    74,   103,   104,    46,     4,     9,     5,    47,    52,
       6,    22,    23,    24,    99,   100,     7,     8,    25,    26,
      53,     9,    27,    55,   105,    54,    56,   108,   109,    62,
     110,    63,   111,    17,    81,    28,    64,    83,    29,    82,
       9,    30,    84,    31,    86,    18,    19,    87,    85,    90,
     106,   107,    96,    20,   113,    91,   128,   112,   121,    65,
      66,    67,    68,    69,    70,    71,    72,    73,    74,   114,
     119,   118,    19,   120,   125,   131,   135,    65,    66,    67,
      68,    69,    70,    71,    72,   134,   136,   132,   137,    75,
     138,    97,     0,     0,    98,     0,     0,     0,   101
};

static const yytype_int16 yycheck[] =
{
       5,     6,    27,    28,     4,    96,     6,     4,     8,     9,
       4,     4,    17,    28,    29,    15,    16,    30,    95,    76,
       0,     4,    33,     6,    29,     8,     9,    38,    36,   120,
       4,    39,    15,    16,   125,    39,     4,   114,     6,    39,
       8,     9,    36,    56,   101,    39,    39,    15,    16,    54,
      26,    27,    77,    78,    39,     4,    39,     6,    39,     4,
       9,     3,     4,     5,    73,    74,    15,    16,    10,    11,
       4,    39,    14,     4,    79,    18,    30,    82,    83,     7,
      85,    13,    87,    18,     3,    27,    12,    17,    30,    30,
      39,    33,    32,    35,    32,    30,    31,    18,    37,    32,
       4,     4,    39,    38,    18,    34,     6,    32,   113,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    37,
      32,    34,    31,    39,    39,    18,   131,    18,    19,    20,
      21,    22,    23,    24,    25,    16,    15,    39,    39,    36,
      39,    63,    -1,    -1,    64,    -1,    -1,    -1,    75
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,    41,    42,     0,     4,     6,     9,    15,    16,    39,
      43,    44,    46,    47,    48,    49,    50,    18,    30,    31,
      38,    45,     3,     4,     5,    10,    11,    14,    27,    30,
      33,    35,    51,    52,    53,    54,    55,    56,    57,    58,
      60,    61,    51,     4,     4,    39,    39,    39,    51,    51,
      62,    63,     4,     4,    18,     4,    30,    57,    57,    51,
      62,    65,     7,    13,    12,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    45,    59,    28,    29,    33,
      38,     3,    30,    17,    32,    37,    32,    18,    51,    62,
      32,    34,     4,    36,    39,    64,    39,    53,    54,    56,
      56,    59,    55,    57,    57,    51,     4,     4,    51,    51,
      51,    51,    32,    18,    37,    65,    42,    55,    34,    32,
      39,    51,    65,    36,     8,    39,    42,     4,     6,    42,
       8,    18,    39,     8,    16,    51,    15,    39,    39
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    40,    41,    42,    42,    42,    43,    43,    43,    43,
      43,    43,    44,    44,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    52,    53,    53,    54,    54,    54,    55,
      55,    55,    56,    56,    56,    57,    57,    57,    58,    58,
      58,    59,    59,    59,    59,    59,    59,    59,    59,    60,
      60,    60,    60,    60,    60,    60,    60,    60,    60,    60,
      61,    61,    62,    62,    63,    63,    64,    64,    65,    65
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     2,     2,     2,     2,     1,
       1,     1,     3,     4,     5,     3,     2,     4,    10,     9,
       8,     1,     1,     3,     1,     3,     1,     3,     4,     1,
       3,     3,     1,     3,     3,     1,     2,     2,     1,     4,
       3,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     4,     1,     1,     3,     3,     3,     5,
       2,     3,     0,     1,     1,     3,     3,     6,     0,     2
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




# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
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
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  yy_symbol_value_print (yyo, yykind, yyvaluep);
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
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp,
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
                       &yyvsp[(yyi + 1) - (yynrhs)]);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
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
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep)
{
  YY_USE (yyvaluep);
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

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;

  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYPTRDIFF_T yymsg_alloc = sizeof yymsgbuf;

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */

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

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
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
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

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


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 2: /* program: statement_list  */
#line 151 "src/parser.y"
                     { parsed_program = (yyvsp[0].stmt_list); (yyval.stmt_list) = (yyvsp[0].stmt_list); }
#line 1599 "src/parser.tab.c"
    break;

  case 3: /* statement_list: %empty  */
#line 155 "src/parser.y"
             { (yyval.stmt_list) = ast_stmt_list_empty(); }
#line 1605 "src/parser.tab.c"
    break;

  case 4: /* statement_list: statement_list NEWLINE  */
#line 156 "src/parser.y"
                             { (yyval.stmt_list) = (yyvsp[-1].stmt_list); }
#line 1611 "src/parser.tab.c"
    break;

  case 5: /* statement_list: statement_list statement  */
#line 157 "src/parser.y"
                               { (yyval.stmt_list) = ast_stmt_list_append((yyvsp[-1].stmt_list), (yyvsp[0].stmt)); }
#line 1617 "src/parser.tab.c"
    break;

  case 6: /* statement: assignment NEWLINE  */
#line 161 "src/parser.y"
                         { (yyval.stmt) = (yyvsp[-1].stmt); }
#line 1623 "src/parser.tab.c"
    break;

  case 7: /* statement: print_statement NEWLINE  */
#line 162 "src/parser.y"
                              { (yyval.stmt) = (yyvsp[-1].stmt); }
#line 1629 "src/parser.tab.c"
    break;

  case 8: /* statement: call_statement NEWLINE  */
#line 163 "src/parser.y"
                             { (yyval.stmt) = (yyvsp[-1].stmt); }
#line 1635 "src/parser.tab.c"
    break;

  case 9: /* statement: with_lock_statement  */
#line 164 "src/parser.y"
                          { (yyval.stmt) = (yyvsp[0].stmt); }
#line 1641 "src/parser.tab.c"
    break;

  case 10: /* statement: for_each_statement  */
#line 165 "src/parser.y"
                         { (yyval.stmt) = (yyvsp[0].stmt); }
#line 1647 "src/parser.tab.c"
    break;

  case 11: /* statement: if_statement  */
#line 166 "src/parser.y"
                   { (yyval.stmt) = (yyvsp[0].stmt); }
#line 1653 "src/parser.tab.c"
    break;

  case 12: /* assignment: IDENT OP_EQ expression  */
#line 170 "src/parser.y"
                             { (yyval.stmt) = ast_assign((yyvsp[-2].text), NULL, (yyvsp[0].expr)); }
#line 1659 "src/parser.tab.c"
    break;

  case 13: /* assignment: IDENT modifier OP_EQ expression  */
#line 171 "src/parser.y"
                                      { (yyval.stmt) = ast_assign((yyvsp[-3].text), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 1665 "src/parser.tab.c"
    break;

  case 14: /* assignment: IDENT DOT IDENT OP_EQ expression  */
#line 172 "src/parser.y"
                                       { (yyval.stmt) = ast_field_assign((yyvsp[-4].text), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 1671 "src/parser.tab.c"
    break;

  case 15: /* modifier: MOD_LPAREN IDENT RPAREN  */
#line 176 "src/parser.y"
                              { (yyval.text) = (yyvsp[-1].text); }
#line 1677 "src/parser.tab.c"
    break;

  case 16: /* print_statement: PRINT expression  */
#line 180 "src/parser.y"
                       { (yyval.stmt) = ast_print((yyvsp[0].expr)); }
#line 1683 "src/parser.tab.c"
    break;

  case 17: /* call_statement: IDENT LPAREN argument_list_opt RPAREN  */
#line 184 "src/parser.y"
                                            { (yyval.stmt) = ast_expr_stmt(ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list))); }
#line 1689 "src/parser.tab.c"
    break;

  case 18: /* with_lock_statement: WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE  */
#line 188 "src/parser.y"
                                                                                  {
        if (strcmp((yyvsp[-8].text), "lock") != 0) {
            yyerror("expected lock in with lock block");
            free((yyvsp[-8].text));
            YYERROR;
        }
        free((yyvsp[-8].text));
        (yyval.stmt) = ast_with_lock((yyvsp[-6].expr), (yyvsp[-3].stmt_list));
      }
#line 1703 "src/parser.tab.c"
    break;

  case 19: /* for_each_statement: FOR IDENT IN expression NEWLINE statement_list END FOR NEWLINE  */
#line 200 "src/parser.y"
                                                                     {
        (yyval.stmt) = ast_for_each((yyvsp[-7].text), (yyvsp[-5].expr), (yyvsp[-3].stmt_list));
      }
#line 1711 "src/parser.tab.c"
    break;

  case 20: /* if_statement: IF expression THEN NEWLINE statement_list END IF NEWLINE  */
#line 206 "src/parser.y"
                                                               {
        (yyval.stmt) = ast_if((yyvsp[-6].expr), (yyvsp[-3].stmt_list));
      }
#line 1719 "src/parser.tab.c"
    break;

  case 21: /* expression: or_expression  */
#line 212 "src/parser.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 1725 "src/parser.tab.c"
    break;

  case 22: /* or_expression: and_expression  */
#line 216 "src/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 1731 "src/parser.tab.c"
    break;

  case 23: /* or_expression: or_expression OR and_expression  */
#line 217 "src/parser.y"
                                      { (yyval.expr) = ast_binary(copy_const("or"), NULL, (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 1737 "src/parser.tab.c"
    break;

  case 24: /* and_expression: comparison_expression  */
#line 221 "src/parser.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 1743 "src/parser.tab.c"
    break;

  case 25: /* and_expression: and_expression AND comparison_expression  */
#line 222 "src/parser.y"
                                               { (yyval.expr) = ast_binary(copy_const("and"), NULL, (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 1749 "src/parser.tab.c"
    break;

  case 26: /* comparison_expression: additive_expression  */
#line 226 "src/parser.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 1755 "src/parser.tab.c"
    break;

  case 27: /* comparison_expression: additive_expression comparison_operator additive_expression  */
#line 227 "src/parser.y"
                                                                  { (yyval.expr) = ast_binary((yyvsp[-1].text), NULL, (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 1761 "src/parser.tab.c"
    break;

  case 28: /* comparison_expression: additive_expression modifier comparison_operator additive_expression  */
#line 228 "src/parser.y"
                                                                           { (yyval.expr) = ast_binary((yyvsp[-1].text), (yyvsp[-2].text), (yyvsp[-3].expr), (yyvsp[0].expr)); }
#line 1767 "src/parser.tab.c"
    break;

  case 29: /* additive_expression: multiplicative_expression  */
#line 232 "src/parser.y"
                                { (yyval.expr) = (yyvsp[0].expr); }
#line 1773 "src/parser.tab.c"
    break;

  case 30: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 233 "src/parser.y"
                                                         { (yyval.expr) = ast_binary(copy_const("+"), NULL, (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 1779 "src/parser.tab.c"
    break;

  case 31: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 234 "src/parser.y"
                                                          { (yyval.expr) = ast_binary(copy_const("-"), NULL, (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 1785 "src/parser.tab.c"
    break;

  case 32: /* multiplicative_expression: unary_expression  */
#line 238 "src/parser.y"
                       { (yyval.expr) = (yyvsp[0].expr); }
#line 1791 "src/parser.tab.c"
    break;

  case 33: /* multiplicative_expression: multiplicative_expression STAR unary_expression  */
#line 239 "src/parser.y"
                                                      { (yyval.expr) = ast_binary(copy_const("*"), NULL, (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 1797 "src/parser.tab.c"
    break;

  case 34: /* multiplicative_expression: multiplicative_expression SLASH unary_expression  */
#line 240 "src/parser.y"
                                                       { (yyval.expr) = ast_binary(copy_const("/"), NULL, (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 1803 "src/parser.tab.c"
    break;

  case 35: /* unary_expression: postfix_expression  */
#line 244 "src/parser.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 1809 "src/parser.tab.c"
    break;

  case 36: /* unary_expression: NOT unary_expression  */
#line 245 "src/parser.y"
                           { (yyval.expr) = ast_unary(copy_const("not"), (yyvsp[0].expr)); }
#line 1815 "src/parser.tab.c"
    break;

  case 37: /* unary_expression: MINUS unary_expression  */
#line 246 "src/parser.y"
                             { (yyval.expr) = ast_unary(copy_const("-"), (yyvsp[0].expr)); }
#line 1821 "src/parser.tab.c"
    break;

  case 38: /* postfix_expression: primary  */
#line 250 "src/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 1827 "src/parser.tab.c"
    break;

  case 39: /* postfix_expression: postfix_expression LBRACKET expression RBRACKET  */
#line 251 "src/parser.y"
                                                      { (yyval.expr) = ast_index((yyvsp[-3].expr), (yyvsp[-1].expr)); }
#line 1833 "src/parser.tab.c"
    break;

  case 40: /* postfix_expression: postfix_expression DOT IDENT  */
#line 252 "src/parser.y"
                                   { (yyval.expr) = ast_field((yyvsp[-2].expr), (yyvsp[0].text)); }
#line 1839 "src/parser.tab.c"
    break;

  case 41: /* comparison_operator: OP_EQ  */
#line 256 "src/parser.y"
            { (yyval.text) = copy_const("="); }
#line 1845 "src/parser.tab.c"
    break;

  case 42: /* comparison_operator: OP_NE  */
#line 257 "src/parser.y"
            { (yyval.text) = copy_const("!="); }
#line 1851 "src/parser.tab.c"
    break;

  case 43: /* comparison_operator: OP_GT  */
#line 258 "src/parser.y"
            { (yyval.text) = copy_const(">"); }
#line 1857 "src/parser.tab.c"
    break;

  case 44: /* comparison_operator: OP_LT  */
#line 259 "src/parser.y"
            { (yyval.text) = copy_const("<"); }
#line 1863 "src/parser.tab.c"
    break;

  case 45: /* comparison_operator: OP_GE  */
#line 260 "src/parser.y"
            { (yyval.text) = copy_const(">="); }
#line 1869 "src/parser.tab.c"
    break;

  case 46: /* comparison_operator: OP_LE  */
#line 261 "src/parser.y"
            { (yyval.text) = copy_const("<="); }
#line 1875 "src/parser.tab.c"
    break;

  case 47: /* comparison_operator: OP_NGT  */
#line 262 "src/parser.y"
             { (yyval.text) = copy_const("!>"); }
#line 1881 "src/parser.tab.c"
    break;

  case 48: /* comparison_operator: OP_NLT  */
#line 263 "src/parser.y"
             { (yyval.text) = copy_const("!<"); }
#line 1887 "src/parser.tab.c"
    break;

  case 49: /* primary: NUMBER  */
#line 267 "src/parser.y"
             { (yyval.expr) = ast_number((yyvsp[0].number)); }
#line 1893 "src/parser.tab.c"
    break;

  case 50: /* primary: duration_terms  */
#line 268 "src/parser.y"
                     { (yyval.expr) = ast_duration((yyvsp[0].duration)); }
#line 1899 "src/parser.tab.c"
    break;

  case 51: /* primary: STRING  */
#line 269 "src/parser.y"
             { (yyval.expr) = ast_string((yyvsp[0].text)); }
#line 1905 "src/parser.tab.c"
    break;

  case 52: /* primary: IDENT  */
#line 270 "src/parser.y"
            { (yyval.expr) = ast_ident((yyvsp[0].text)); }
#line 1911 "src/parser.tab.c"
    break;

  case 53: /* primary: IDENT LPAREN argument_list_opt RPAREN  */
#line 271 "src/parser.y"
                                            { (yyval.expr) = ast_call((yyvsp[-3].text), (yyvsp[-1].expr_list)); }
#line 1917 "src/parser.tab.c"
    break;

  case 54: /* primary: TRUE  */
#line 272 "src/parser.y"
           { (yyval.expr) = ast_bool(1); }
#line 1923 "src/parser.tab.c"
    break;

  case 55: /* primary: FALSE  */
#line 273 "src/parser.y"
            { (yyval.expr) = ast_bool(0); }
#line 1929 "src/parser.tab.c"
    break;

  case 56: /* primary: LPAREN expression RPAREN  */
#line 274 "src/parser.y"
                               { (yyval.expr) = (yyvsp[-1].expr); }
#line 1935 "src/parser.tab.c"
    break;

  case 57: /* primary: LBRACKET argument_list_opt RBRACKET  */
#line 275 "src/parser.y"
                                          { (yyval.expr) = ast_array((yyvsp[-1].expr_list)); }
#line 1941 "src/parser.tab.c"
    break;

  case 58: /* primary: LBRACE optional_newlines RBRACE  */
#line 276 "src/parser.y"
                                      { (yyval.expr) = ast_record(ast_record_field_list_empty()); }
#line 1947 "src/parser.tab.c"
    break;

  case 59: /* primary: LBRACE optional_newlines record_field_list optional_newlines RBRACE  */
#line 277 "src/parser.y"
                                                                          { (yyval.expr) = ast_record((yyvsp[-2].record_field_list)); }
#line 1953 "src/parser.tab.c"
    break;

  case 60: /* duration_terms: NUMBER IDENT  */
#line 281 "src/parser.y"
                   {
        AstDuration duration = {0};
        (yyval.duration) = duration_add_unit(duration, (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 1962 "src/parser.tab.c"
    break;

  case 61: /* duration_terms: duration_terms NUMBER IDENT  */
#line 285 "src/parser.y"
                                  {
        (yyval.duration) = duration_add_unit((yyvsp[-2].duration), (yyvsp[-1].number), (yyvsp[0].text));
      }
#line 1970 "src/parser.tab.c"
    break;

  case 62: /* argument_list_opt: %empty  */
#line 291 "src/parser.y"
             { (yyval.expr_list) = ast_expr_list_empty(); }
#line 1976 "src/parser.tab.c"
    break;

  case 63: /* argument_list_opt: argument_list  */
#line 292 "src/parser.y"
                    { (yyval.expr_list) = (yyvsp[0].expr_list); }
#line 1982 "src/parser.tab.c"
    break;

  case 64: /* argument_list: expression  */
#line 296 "src/parser.y"
                 { (yyval.expr_list) = ast_expr_list_append(ast_expr_list_empty(), (yyvsp[0].expr)); }
#line 1988 "src/parser.tab.c"
    break;

  case 65: /* argument_list: argument_list COMMA expression  */
#line 297 "src/parser.y"
                                     { (yyval.expr_list) = ast_expr_list_append((yyvsp[-2].expr_list), (yyvsp[0].expr)); }
#line 1994 "src/parser.tab.c"
    break;

  case 66: /* record_field_list: IDENT OP_EQ expression  */
#line 301 "src/parser.y"
                             { (yyval.record_field_list) = ast_record_field_list_append(ast_record_field_list_empty(), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 2000 "src/parser.tab.c"
    break;

  case 67: /* record_field_list: record_field_list COMMA optional_newlines IDENT OP_EQ expression  */
#line 302 "src/parser.y"
                                                                       { (yyval.record_field_list) = ast_record_field_list_append((yyvsp[-5].record_field_list), (yyvsp[-2].text), (yyvsp[0].expr)); }
#line 2006 "src/parser.tab.c"
    break;


#line 2010 "src/parser.tab.c"

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
          = {yyssp, yytoken};
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
                      yytoken, &yylval);
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


      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


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
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp);
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

#line 310 "src/parser.y"


int parse_source(const char *source, AstStmtList *out_program) {
    Lexer lexer;
    lexer_init(&lexer, source);
    active_lexer = &lexer;
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

    switch (token.type) {
    case TOKEN_EOF: return 0;
    case TOKEN_IDENT:
        yylval.text = copy_text(token.start, token.length);
        return IDENT;
    case TOKEN_NUMBER:
        yylval.number = strtod(token.start, NULL);
        return NUMBER;
    case TOKEN_STRING:
        yylval.text = copy_string_literal(token.start, token.length);
        return STRING;
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
    case TOKEN_IN: return IN;
    case TOKEN_OP_EQ: return OP_EQ;
    case TOKEN_OP_NE: return OP_NE;
    case TOKEN_OP_GT: return OP_GT;
    case TOKEN_OP_LT: return OP_LT;
    case TOKEN_OP_GE: return OP_GE;
    case TOKEN_OP_LE: return OP_LE;
    case TOKEN_OP_NGT: return OP_NGT;
    case TOKEN_OP_NLT: return OP_NLT;
    case TOKEN_PLUS: return PLUS;
    case TOKEN_MINUS: return MINUS;
    case TOKEN_STAR: return STAR;
    case TOKEN_SLASH: return SLASH;
    case TOKEN_LPAREN:
        if (modifier_lparen_ahead(token.start)) {
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
    case TOKEN_NEWLINE: return NEWLINE;
    case TOKEN_ERROR:
        fprintf(stderr, "lexer error at %d:%d\n", token.line, token.column);
        return 0;
    default:
        fprintf(stderr, "unexpected token %s at %d:%d\n",
                token_type_name(token.type), token.line, token.column);
        return 0;
    }
}

static void yyerror(const char *message) {
    fprintf(stderr, "parse error: %s\n", message);
}
