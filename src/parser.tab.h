/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison interface for Yacc-like parsers in C

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

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

#ifndef YY_YY_SRC_PARSER_TAB_H_INCLUDED
# define YY_YY_SRC_PARSER_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif
/* "%code requires" blocks.  */
#line 599 "src/parser.y"

#include "ast.h"
#include "parse_ctx.h"

typedef enum {
    IDENT_SUFFIX_NONE,
    IDENT_SUFFIX_CALL,
    IDENT_SUFFIX_FIELD,
    IDENT_SUFFIX_QUALIFIED_CALL,
    IDENT_SUFFIX_METHOD          /* var.field.method(args): a chained method call */
} AstIdentSuffixKind;

typedef struct {
    AstIdentSuffixKind kind;
    char *name;
    AstExprList args;
} AstIdentSuffix;

/* Parsed PBI per-field policy clause: `( copy | link | exclude | reset <expr> )` */
typedef struct {
    AstFieldPolicy policy;
    AstExpr *reset_expr;
} FieldPolicySpec;

#line 74 "src/parser.tab.h"

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    NUMBER = 258,                  /* NUMBER  */
    IDENT = 259,                   /* IDENT  */
    STRING = 260,                  /* STRING  */
    MOD_CONTENT = 261,             /* MOD_CONTENT  */
    LENS_CONTENT = 262,            /* LENS_CONTENT  */
    QUALIFIED_IDENT = 263,         /* QUALIFIED_IDENT  */
    AS = 264,                      /* AS  */
    IF = 265,                      /* IF  */
    CONSIDER_IF = 266,             /* CONSIDER_IF  */
    THEN = 267,                    /* THEN  */
    ELSE = 268,                    /* ELSE  */
    CONSIDER_ELSE = 269,           /* CONSIDER_ELSE  */
    END = 270,                     /* END  */
    END_CONSIDER = 271,            /* END_CONSIDER  */
    PRINT = 272,                   /* PRINT  */
    TRUE = 273,                    /* TRUE  */
    FALSE = 274,                   /* FALSE  */
    NOTHING = 275,                 /* NOTHING  */
    UNKNOWN_VALUE = 276,           /* UNKNOWN_VALUE  */
    AND = 277,                     /* AND  */
    OR = 278,                      /* OR  */
    NOT = 279,                     /* NOT  */
    WITH = 280,                    /* WITH  */
    NEW = 281,                     /* NEW  */
    SPAWN = 282,                   /* SPAWN  */
    FOR = 283,                     /* FOR  */
    TO = 284,                      /* TO  */
    STEP = 285,                    /* STEP  */
    IN = 286,                      /* IN  */
    EACH = 287,                    /* EACH  */
    WHILE = 288,                   /* WHILE  */
    CONSIDER = 289,                /* CONSIDER  */
    BREAK = 290,                   /* BREAK  */
    CONTINUE = 291,                /* CONTINUE  */
    FUNCTION = 292,                /* FUNCTION  */
    RETURN = 293,                  /* RETURN  */
    GOTO = 294,                    /* GOTO  */
    GOSUB = 295,                   /* GOSUB  */
    WATCH = 296,                   /* WATCH  */
    WITHOUT = 297,                 /* WITHOUT  */
    WATCHERS = 298,                /* WATCHERS  */
    ON = 299,                      /* ON  */
    RESUME = 300,                  /* RESUME  */
    NEXT = 301,                    /* NEXT  */
    STOP = 302,                    /* STOP  */
    ERROR_VALUE = 303,             /* ERROR_VALUE  */
    MODIFIER = 304,                /* MODIFIER  */
    PROGRAM = 305,                 /* PROGRAM  */
    LIBRARY = 306,                 /* LIBRARY  */
    LOAD = 307,                    /* LOAD  */
    USE = 308,                     /* USE  */
    EXPORT = 309,                  /* EXPORT  */
    OP_EQ = 310,                   /* OP_EQ  */
    OP_NE = 311,                   /* OP_NE  */
    OP_GT = 312,                   /* OP_GT  */
    OP_LT = 313,                   /* OP_LT  */
    OP_GE = 314,                   /* OP_GE  */
    OP_LE = 315,                   /* OP_LE  */
    OP_NGT = 316,                  /* OP_NGT  */
    OP_NLT = 317,                  /* OP_NLT  */
    OP_NGE = 318,                  /* OP_NGE  */
    OP_NLE = 319,                  /* OP_NLE  */
    PLUS = 320,                    /* PLUS  */
    MINUS = 321,                   /* MINUS  */
    STAR = 322,                    /* STAR  */
    SLASH = 323,                   /* SLASH  */
    LPAREN = 324,                  /* LPAREN  */
    MOD_LPAREN = 325,              /* MOD_LPAREN  */
    RPAREN = 326,                  /* RPAREN  */
    LBRACKET = 327,                /* LBRACKET  */
    RBRACKET = 328,                /* RBRACKET  */
    LBRACE = 329,                  /* LBRACE  */
    RBRACE = 330,                  /* RBRACE  */
    COMMA = 331,                   /* COMMA  */
    COLON = 332,                   /* COLON  */
    NEWLINE = 333,                 /* NEWLINE  */
    IF_WITHOUT_ELSE = 334,         /* IF_WITHOUT_ELSE  */
    NO_DOT = 335,                  /* NO_DOT  */
    DOT = 336                      /* DOT  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 624 "src/parser.y"

    double number;
    char *text;
    AstExpr *expr;
    AstStmt *stmt;
    AstStmtList stmt_list;
    AstExprList expr_list;
    AstRecordFieldList record_field_list;
    AstConsiderBranchList consider_branch_list;
    AstNameList name_list;
    AstModifierUse modifier;
    AstModifierSignature modifier_signature;
    AstDuration duration;
    AstIdentSuffix ident_suffix;
    FieldPolicySpec field_policy;

#line 189 "src/parser.tab.h"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif

/* Location type.  */
#if ! defined YYLTYPE && ! defined YYLTYPE_IS_DECLARED
typedef struct YYLTYPE YYLTYPE;
struct YYLTYPE
{
  int first_line;
  int first_column;
  int last_line;
  int last_column;
};
# define YYLTYPE_IS_DECLARED 1
# define YYLTYPE_IS_TRIVIAL 1
#endif




int yyparse (gb_parse_ctx *ctx);


#endif /* !YY_YY_SRC_PARSER_TAB_H_INCLUDED  */
