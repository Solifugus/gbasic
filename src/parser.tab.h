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
#line 388 "src/parser.y"

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
    LENS_CONTENT = 262,            /* LENS_CONTENT  */
    QUALIFIED_IDENT = 263,         /* QUALIFIED_IDENT  */
    IF = 264,                      /* IF  */
    CONSIDER_IF = 265,             /* CONSIDER_IF  */
    THEN = 266,                    /* THEN  */
    ELSE = 267,                    /* ELSE  */
    CONSIDER_ELSE = 268,           /* CONSIDER_ELSE  */
    END = 269,                     /* END  */
    END_CONSIDER = 270,            /* END_CONSIDER  */
    PRINT = 271,                   /* PRINT  */
    TRUE = 272,                    /* TRUE  */
    FALSE = 273,                   /* FALSE  */
    NOTHING = 274,                 /* NOTHING  */
    UNKNOWN_VALUE = 275,           /* UNKNOWN_VALUE  */
    AND = 276,                     /* AND  */
    OR = 277,                      /* OR  */
    NOT = 278,                     /* NOT  */
    WITH = 279,                    /* WITH  */
    FOR = 280,                     /* FOR  */
    TO = 281,                      /* TO  */
    IN = 282,                      /* IN  */
    EACH = 283,                    /* EACH  */
    WHILE = 284,                   /* WHILE  */
    CONSIDER = 285,                /* CONSIDER  */
    BREAK = 286,                   /* BREAK  */
    CONTINUE = 287,                /* CONTINUE  */
    FUNCTION = 288,                /* FUNCTION  */
    RETURN = 289,                  /* RETURN  */
    GOTO = 290,                    /* GOTO  */
    GOSUB = 291,                   /* GOSUB  */
    WATCH = 292,                   /* WATCH  */
    WITHOUT = 293,                 /* WITHOUT  */
    WATCHERS = 294,                /* WATCHERS  */
    ON = 295,                      /* ON  */
    RESUME = 296,                  /* RESUME  */
    NEXT = 297,                    /* NEXT  */
    STOP = 298,                    /* STOP  */
    ERROR_VALUE = 299,             /* ERROR_VALUE  */
    MODIFIER = 300,                /* MODIFIER  */
    PROGRAM = 301,                 /* PROGRAM  */
    LIBRARY = 302,                 /* LIBRARY  */
    LOAD = 303,                    /* LOAD  */
    USE = 304,                     /* USE  */
    EXPORT = 305,                  /* EXPORT  */
    OP_EQ = 306,                   /* OP_EQ  */
    OP_NE = 307,                   /* OP_NE  */
    OP_GT = 308,                   /* OP_GT  */
    OP_LT = 309,                   /* OP_LT  */
    OP_GE = 310,                   /* OP_GE  */
    OP_LE = 311,                   /* OP_LE  */
    OP_NGT = 312,                  /* OP_NGT  */
    OP_NLT = 313,                  /* OP_NLT  */
    OP_NGE = 314,                  /* OP_NGE  */
    OP_NLE = 315,                  /* OP_NLE  */
    PLUS = 316,                    /* PLUS  */
    MINUS = 317,                   /* MINUS  */
    STAR = 318,                    /* STAR  */
    SLASH = 319,                   /* SLASH  */
    LPAREN = 320,                  /* LPAREN  */
    MOD_LPAREN = 321,              /* MOD_LPAREN  */
    RPAREN = 322,                  /* RPAREN  */
    LBRACKET = 323,                /* LBRACKET  */
    RBRACKET = 324,                /* RBRACKET  */
    LBRACE = 325,                  /* LBRACE  */
    RBRACE = 326,                  /* RBRACE  */
    COMMA = 327,                   /* COMMA  */
    COLON = 328,                   /* COLON  */
    NEWLINE = 329,                 /* NEWLINE  */
    IF_WITHOUT_ELSE = 330,         /* IF_WITHOUT_ELSE  */
    NO_DOT = 331,                  /* NO_DOT  */
    DOT = 332                      /* DOT  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 405 "src/parser.y"

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

#line 176 "src/parser.tab.h"

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
