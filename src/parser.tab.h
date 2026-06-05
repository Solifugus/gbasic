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
#line 364 "src/parser.y"

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
    QUALIFIED_IDENT = 262,         /* QUALIFIED_IDENT  */
    IF = 263,                      /* IF  */
    CONSIDER_IF = 264,             /* CONSIDER_IF  */
    THEN = 265,                    /* THEN  */
    ELSE = 266,                    /* ELSE  */
    CONSIDER_ELSE = 267,           /* CONSIDER_ELSE  */
    END = 268,                     /* END  */
    END_CONSIDER = 269,            /* END_CONSIDER  */
    PRINT = 270,                   /* PRINT  */
    TRUE = 271,                    /* TRUE  */
    FALSE = 272,                   /* FALSE  */
    NOTHING = 273,                 /* NOTHING  */
    UNKNOWN_VALUE = 274,           /* UNKNOWN_VALUE  */
    AND = 275,                     /* AND  */
    OR = 276,                      /* OR  */
    NOT = 277,                     /* NOT  */
    WITH = 278,                    /* WITH  */
    FOR = 279,                     /* FOR  */
    TO = 280,                      /* TO  */
    IN = 281,                      /* IN  */
    WHILE = 282,                   /* WHILE  */
    CONSIDER = 283,                /* CONSIDER  */
    BREAK = 284,                   /* BREAK  */
    CONTINUE = 285,                /* CONTINUE  */
    FUNCTION = 286,                /* FUNCTION  */
    RETURN = 287,                  /* RETURN  */
    GOTO = 288,                    /* GOTO  */
    GOSUB = 289,                   /* GOSUB  */
    WATCH = 290,                   /* WATCH  */
    WITHOUT = 291,                 /* WITHOUT  */
    WATCHERS = 292,                /* WATCHERS  */
    ON = 293,                      /* ON  */
    RESUME = 294,                  /* RESUME  */
    NEXT = 295,                    /* NEXT  */
    STOP = 296,                    /* STOP  */
    ERROR_VALUE = 297,             /* ERROR_VALUE  */
    MODIFIER = 298,                /* MODIFIER  */
    PROGRAM = 299,                 /* PROGRAM  */
    LIBRARY = 300,                 /* LIBRARY  */
    LOAD = 301,                    /* LOAD  */
    USE = 302,                     /* USE  */
    EXPORT = 303,                  /* EXPORT  */
    OP_EQ = 304,                   /* OP_EQ  */
    OP_NE = 305,                   /* OP_NE  */
    OP_GT = 306,                   /* OP_GT  */
    OP_LT = 307,                   /* OP_LT  */
    OP_GE = 308,                   /* OP_GE  */
    OP_LE = 309,                   /* OP_LE  */
    OP_NGT = 310,                  /* OP_NGT  */
    OP_NLT = 311,                  /* OP_NLT  */
    OP_NGE = 312,                  /* OP_NGE  */
    OP_NLE = 313,                  /* OP_NLE  */
    PLUS = 314,                    /* PLUS  */
    MINUS = 315,                   /* MINUS  */
    STAR = 316,                    /* STAR  */
    SLASH = 317,                   /* SLASH  */
    LPAREN = 318,                  /* LPAREN  */
    MOD_LPAREN = 319,              /* MOD_LPAREN  */
    RPAREN = 320,                  /* RPAREN  */
    LBRACKET = 321,                /* LBRACKET  */
    RBRACKET = 322,                /* RBRACKET  */
    LBRACE = 323,                  /* LBRACE  */
    RBRACE = 324,                  /* RBRACE  */
    COMMA = 325,                   /* COMMA  */
    COLON = 326,                   /* COLON  */
    NEWLINE = 327,                 /* NEWLINE  */
    NO_DOT = 328,                  /* NO_DOT  */
    DOT = 329                      /* DOT  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 381 "src/parser.y"

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

#line 173 "src/parser.tab.h"

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
