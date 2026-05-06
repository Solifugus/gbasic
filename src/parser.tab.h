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
#line 208 "src/parser.y"

#include "ast.h"

typedef enum {
    IDENT_SUFFIX_NONE,
    IDENT_SUFFIX_CALL,
    IDENT_SUFFIX_FIELD,
    IDENT_SUFFIX_QUALIFIED_CALL
} AstIdentSuffixKind;

typedef struct {
    AstIdentSuffixKind kind;
    char *name;
    AstExprList args;
} AstIdentSuffix;

#line 66 "src/parser.tab.h"

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
    IF = 262,                      /* IF  */
    THEN = 263,                    /* THEN  */
    END = 264,                     /* END  */
    PRINT = 265,                   /* PRINT  */
    TRUE = 266,                    /* TRUE  */
    FALSE = 267,                   /* FALSE  */
    AND = 268,                     /* AND  */
    OR = 269,                      /* OR  */
    NOT = 270,                     /* NOT  */
    WITH = 271,                    /* WITH  */
    FOR = 272,                     /* FOR  */
    TO = 273,                      /* TO  */
    IN = 274,                      /* IN  */
    FUNCTION = 275,                /* FUNCTION  */
    RETURN = 276,                  /* RETURN  */
    GOTO = 277,                    /* GOTO  */
    GOSUB = 278,                   /* GOSUB  */
    WATCH = 279,                   /* WATCH  */
    WITHOUT = 280,                 /* WITHOUT  */
    WATCHERS = 281,                /* WATCHERS  */
    ON = 282,                      /* ON  */
    RESUME = 283,                  /* RESUME  */
    NEXT = 284,                    /* NEXT  */
    STOP = 285,                    /* STOP  */
    ERROR_VALUE = 286,             /* ERROR_VALUE  */
    MODIFIER = 287,                /* MODIFIER  */
    PROGRAM = 288,                 /* PROGRAM  */
    LIBRARY = 289,                 /* LIBRARY  */
    LOAD = 290,                    /* LOAD  */
    USE = 291,                     /* USE  */
    EXPORT = 292,                  /* EXPORT  */
    OP_EQ = 293,                   /* OP_EQ  */
    OP_NE = 294,                   /* OP_NE  */
    OP_GT = 295,                   /* OP_GT  */
    OP_LT = 296,                   /* OP_LT  */
    OP_GE = 297,                   /* OP_GE  */
    OP_LE = 298,                   /* OP_LE  */
    OP_NGT = 299,                  /* OP_NGT  */
    OP_NLT = 300,                  /* OP_NLT  */
    OP_NGE = 301,                  /* OP_NGE  */
    OP_NLE = 302,                  /* OP_NLE  */
    PLUS = 303,                    /* PLUS  */
    MINUS = 304,                   /* MINUS  */
    STAR = 305,                    /* STAR  */
    SLASH = 306,                   /* SLASH  */
    LPAREN = 307,                  /* LPAREN  */
    MOD_LPAREN = 308,              /* MOD_LPAREN  */
    RPAREN = 309,                  /* RPAREN  */
    LBRACKET = 310,                /* LBRACKET  */
    RBRACKET = 311,                /* RBRACKET  */
    LBRACE = 312,                  /* LBRACE  */
    RBRACE = 313,                  /* RBRACE  */
    COMMA = 314,                   /* COMMA  */
    COLON = 315,                   /* COLON  */
    NEWLINE = 316,                 /* NEWLINE  */
    NO_DOT = 317,                  /* NO_DOT  */
    DOT = 318                      /* DOT  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 225 "src/parser.y"

    double number;
    char *text;
    AstExpr *expr;
    AstStmt *stmt;
    AstStmtList stmt_list;
    AstExprList expr_list;
    AstRecordFieldList record_field_list;
    AstNameList name_list;
    AstModifierUse modifier;
    AstModifierSignature modifier_signature;
    AstDuration duration;
    AstIdentSuffix ident_suffix;

#line 161 "src/parser.tab.h"

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


extern YYSTYPE yylval;
extern YYLTYPE yylloc;

int yyparse (void);


#endif /* !YY_YY_SRC_PARSER_TAB_H_INCLUDED  */
