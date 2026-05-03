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
#line 117 "src/parser.y"

#include "ast.h"

#line 53 "src/parser.tab.h"

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
    IF = 261,                      /* IF  */
    THEN = 262,                    /* THEN  */
    END = 263,                     /* END  */
    PRINT = 264,                   /* PRINT  */
    TRUE = 265,                    /* TRUE  */
    FALSE = 266,                   /* FALSE  */
    AND = 267,                     /* AND  */
    OR = 268,                      /* OR  */
    NOT = 269,                     /* NOT  */
    WITH = 270,                    /* WITH  */
    FOR = 271,                     /* FOR  */
    IN = 272,                      /* IN  */
    FUNCTION = 273,                /* FUNCTION  */
    RETURN = 274,                  /* RETURN  */
    GOTO = 275,                    /* GOTO  */
    OP_EQ = 276,                   /* OP_EQ  */
    OP_NE = 277,                   /* OP_NE  */
    OP_GT = 278,                   /* OP_GT  */
    OP_LT = 279,                   /* OP_LT  */
    OP_GE = 280,                   /* OP_GE  */
    OP_LE = 281,                   /* OP_LE  */
    OP_NGT = 282,                  /* OP_NGT  */
    OP_NLT = 283,                  /* OP_NLT  */
    PLUS = 284,                    /* PLUS  */
    MINUS = 285,                   /* MINUS  */
    STAR = 286,                    /* STAR  */
    SLASH = 287,                   /* SLASH  */
    LPAREN = 288,                  /* LPAREN  */
    MOD_LPAREN = 289,              /* MOD_LPAREN  */
    RPAREN = 290,                  /* RPAREN  */
    LBRACKET = 291,                /* LBRACKET  */
    RBRACKET = 292,                /* RBRACKET  */
    LBRACE = 293,                  /* LBRACE  */
    RBRACE = 294,                  /* RBRACE  */
    COMMA = 295,                   /* COMMA  */
    DOT = 296,                     /* DOT  */
    COLON = 297,                   /* COLON  */
    NEWLINE = 298                  /* NEWLINE  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 121 "src/parser.y"

    double number;
    char *text;
    AstExpr *expr;
    AstStmt *stmt;
    AstStmtList stmt_list;
    AstExprList expr_list;
    AstRecordFieldList record_field_list;
    AstNameList name_list;
    AstDuration duration;

#line 125 "src/parser.tab.h"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;


int yyparse (void);


#endif /* !YY_YY_SRC_PARSER_TAB_H_INCLUDED  */
