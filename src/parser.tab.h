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
    CONSIDER_IF = 263,             /* CONSIDER_IF  */
    THEN = 264,                    /* THEN  */
    ELSE = 265,                    /* ELSE  */
    CONSIDER_ELSE = 266,           /* CONSIDER_ELSE  */
    END = 267,                     /* END  */
    END_CONSIDER = 268,            /* END_CONSIDER  */
    PRINT = 269,                   /* PRINT  */
    TRUE = 270,                    /* TRUE  */
    FALSE = 271,                   /* FALSE  */
    NOTHING = 272,                 /* NOTHING  */
    UNKNOWN_VALUE = 273,           /* UNKNOWN_VALUE  */
    AND = 274,                     /* AND  */
    OR = 275,                      /* OR  */
    NOT = 276,                     /* NOT  */
    WITH = 277,                    /* WITH  */
    FOR = 278,                     /* FOR  */
    TO = 279,                      /* TO  */
    IN = 280,                      /* IN  */
    WHILE = 281,                   /* WHILE  */
    CONSIDER = 282,                /* CONSIDER  */
    BREAK = 283,                   /* BREAK  */
    CONTINUE = 284,                /* CONTINUE  */
    FUNCTION = 285,                /* FUNCTION  */
    RETURN = 286,                  /* RETURN  */
    GOTO = 287,                    /* GOTO  */
    GOSUB = 288,                   /* GOSUB  */
    WATCH = 289,                   /* WATCH  */
    WITHOUT = 290,                 /* WITHOUT  */
    WATCHERS = 291,                /* WATCHERS  */
    ON = 292,                      /* ON  */
    RESUME = 293,                  /* RESUME  */
    NEXT = 294,                    /* NEXT  */
    STOP = 295,                    /* STOP  */
    ERROR_VALUE = 296,             /* ERROR_VALUE  */
    MODIFIER = 297,                /* MODIFIER  */
    PROGRAM = 298,                 /* PROGRAM  */
    LIBRARY = 299,                 /* LIBRARY  */
    LOAD = 300,                    /* LOAD  */
    USE = 301,                     /* USE  */
    EXPORT = 302,                  /* EXPORT  */
    OP_EQ = 303,                   /* OP_EQ  */
    OP_NE = 304,                   /* OP_NE  */
    OP_GT = 305,                   /* OP_GT  */
    OP_LT = 306,                   /* OP_LT  */
    OP_GE = 307,                   /* OP_GE  */
    OP_LE = 308,                   /* OP_LE  */
    OP_NGT = 309,                  /* OP_NGT  */
    OP_NLT = 310,                  /* OP_NLT  */
    OP_NGE = 311,                  /* OP_NGE  */
    OP_NLE = 312,                  /* OP_NLE  */
    PLUS = 313,                    /* PLUS  */
    MINUS = 314,                   /* MINUS  */
    STAR = 315,                    /* STAR  */
    SLASH = 316,                   /* SLASH  */
    LPAREN = 317,                  /* LPAREN  */
    MOD_LPAREN = 318,              /* MOD_LPAREN  */
    RPAREN = 319,                  /* RPAREN  */
    LBRACKET = 320,                /* LBRACKET  */
    RBRACKET = 321,                /* RBRACKET  */
    LBRACE = 322,                  /* LBRACE  */
    RBRACE = 323,                  /* RBRACE  */
    COMMA = 324,                   /* COMMA  */
    COLON = 325,                   /* COLON  */
    NEWLINE = 326,                 /* NEWLINE  */
    NO_DOT = 327,                  /* NO_DOT  */
    DOT = 328                      /* DOT  */
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
    AstConsiderBranchList consider_branch_list;
    AstNameList name_list;
    AstModifierUse modifier;
    AstModifierSignature modifier_signature;
    AstDuration duration;
    AstIdentSuffix ident_suffix;

#line 172 "src/parser.tab.h"

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
