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

#ifndef YY_YY_PARSER_TAB_H_INCLUDED
# define YY_YY_PARSER_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif
/* "%code requires" blocks.  */
#line 21 "parser.y"

#include "ast.h"

#line 53 "parser.tab.h"

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    IDENT = 258,                   /* IDENT  */
    STRING = 259,                  /* STRING  */
    NUMBER = 260,                  /* NUMBER  */
    IF = 261,                      /* IF  */
    THEN = 262,                    /* THEN  */
    ELSE = 263,                    /* ELSE  */
    END = 264,                     /* END  */
    PRINT = 265,                   /* PRINT  */
    TRUE = 266,                    /* TRUE  */
    FALSE = 267,                   /* FALSE  */
    AND = 268,                     /* AND  */
    OR = 269,                      /* OR  */
    NOT = 270,                     /* NOT  */
    OP_EQ = 271,                   /* OP_EQ  */
    OP_NE = 272,                   /* OP_NE  */
    OP_GT = 273,                   /* OP_GT  */
    OP_LT = 274,                   /* OP_LT  */
    OP_GE = 275,                   /* OP_GE  */
    OP_LE = 276,                   /* OP_LE  */
    OP_NGT = 277,                  /* OP_NGT  */
    OP_NLT = 278,                  /* OP_NLT  */
    PLUS = 279,                    /* PLUS  */
    MINUS = 280,                   /* MINUS  */
    STAR = 281,                    /* STAR  */
    SLASH = 282,                   /* SLASH  */
    LPAREN = 283,                  /* LPAREN  */
    MOD_LPAREN = 284,              /* MOD_LPAREN  */
    RPAREN = 285,                  /* RPAREN  */
    LBRACKET = 286,                /* LBRACKET  */
    RBRACKET = 287,                /* RBRACKET  */
    COMMA = 288,                   /* COMMA  */
    NEWLINE = 289,                 /* NEWLINE  */
    UMINUS = 290                   /* UMINUS  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 25 "parser.y"

    double number;
    char *string;
    Expr *expr;
    Stmt *stmt;
    ExprList expr_list;
    StmtList stmt_list;
    Modifier *modifier;

#line 115 "parser.tab.h"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;


int yyparse (void);


#endif /* !YY_YY_PARSER_TAB_H_INCLUDED  */
