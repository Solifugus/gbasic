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
#line 337 "src/parser.y"

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
    NOTHING = 268,                 /* NOTHING  */
    UNKNOWN_VALUE = 269,           /* UNKNOWN_VALUE  */
    AND = 270,                     /* AND  */
    OR = 271,                      /* OR  */
    NOT = 272,                     /* NOT  */
    WITH = 273,                    /* WITH  */
    FOR = 274,                     /* FOR  */
    TO = 275,                      /* TO  */
    IN = 276,                      /* IN  */
    WHILE = 277,                   /* WHILE  */
    FUNCTION = 278,                /* FUNCTION  */
    RETURN = 279,                  /* RETURN  */
    GOTO = 280,                    /* GOTO  */
    GOSUB = 281,                   /* GOSUB  */
    WATCH = 282,                   /* WATCH  */
    WITHOUT = 283,                 /* WITHOUT  */
    WATCHERS = 284,                /* WATCHERS  */
    ON = 285,                      /* ON  */
    RESUME = 286,                  /* RESUME  */
    NEXT = 287,                    /* NEXT  */
    STOP = 288,                    /* STOP  */
    ERROR_VALUE = 289,             /* ERROR_VALUE  */
    MODIFIER = 290,                /* MODIFIER  */
    PROGRAM = 291,                 /* PROGRAM  */
    LIBRARY = 292,                 /* LIBRARY  */
    LOAD = 293,                    /* LOAD  */
    USE = 294,                     /* USE  */
    EXPORT = 295,                  /* EXPORT  */
    OP_EQ = 296,                   /* OP_EQ  */
    OP_NE = 297,                   /* OP_NE  */
    OP_GT = 298,                   /* OP_GT  */
    OP_LT = 299,                   /* OP_LT  */
    OP_GE = 300,                   /* OP_GE  */
    OP_LE = 301,                   /* OP_LE  */
    OP_NGT = 302,                  /* OP_NGT  */
    OP_NLT = 303,                  /* OP_NLT  */
    OP_NGE = 304,                  /* OP_NGE  */
    OP_NLE = 305,                  /* OP_NLE  */
    PLUS = 306,                    /* PLUS  */
    MINUS = 307,                   /* MINUS  */
    STAR = 308,                    /* STAR  */
    SLASH = 309,                   /* SLASH  */
    LPAREN = 310,                  /* LPAREN  */
    MOD_LPAREN = 311,              /* MOD_LPAREN  */
    RPAREN = 312,                  /* RPAREN  */
    LBRACKET = 313,                /* LBRACKET  */
    RBRACKET = 314,                /* RBRACKET  */
    LBRACE = 315,                  /* LBRACE  */
    RBRACE = 316,                  /* RBRACE  */
    COMMA = 317,                   /* COMMA  */
    COLON = 318,                   /* COLON  */
    NEWLINE = 319,                 /* NEWLINE  */
    NO_DOT = 320,                  /* NO_DOT  */
    DOT = 321                      /* DOT  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 354 "src/parser.y"

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

#line 164 "src/parser.tab.h"

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
