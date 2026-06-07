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
    EACH = 282,                    /* EACH  */
    WHILE = 283,                   /* WHILE  */
    CONSIDER = 284,                /* CONSIDER  */
    BREAK = 285,                   /* BREAK  */
    CONTINUE = 286,                /* CONTINUE  */
    FUNCTION = 287,                /* FUNCTION  */
    RETURN = 288,                  /* RETURN  */
    GOTO = 289,                    /* GOTO  */
    GOSUB = 290,                   /* GOSUB  */
    WATCH = 291,                   /* WATCH  */
    WITHOUT = 292,                 /* WITHOUT  */
    WATCHERS = 293,                /* WATCHERS  */
    ON = 294,                      /* ON  */
    RESUME = 295,                  /* RESUME  */
    NEXT = 296,                    /* NEXT  */
    STOP = 297,                    /* STOP  */
    ERROR_VALUE = 298,             /* ERROR_VALUE  */
    MODIFIER = 299,                /* MODIFIER  */
    PROGRAM = 300,                 /* PROGRAM  */
    LIBRARY = 301,                 /* LIBRARY  */
    LOAD = 302,                    /* LOAD  */
    USE = 303,                     /* USE  */
    EXPORT = 304,                  /* EXPORT  */
    OP_EQ = 305,                   /* OP_EQ  */
    OP_NE = 306,                   /* OP_NE  */
    OP_GT = 307,                   /* OP_GT  */
    OP_LT = 308,                   /* OP_LT  */
    OP_GE = 309,                   /* OP_GE  */
    OP_LE = 310,                   /* OP_LE  */
    OP_NGT = 311,                  /* OP_NGT  */
    OP_NLT = 312,                  /* OP_NLT  */
    OP_NGE = 313,                  /* OP_NGE  */
    OP_NLE = 314,                  /* OP_NLE  */
    PLUS = 315,                    /* PLUS  */
    MINUS = 316,                   /* MINUS  */
    STAR = 317,                    /* STAR  */
    SLASH = 318,                   /* SLASH  */
    LPAREN = 319,                  /* LPAREN  */
    MOD_LPAREN = 320,              /* MOD_LPAREN  */
    RPAREN = 321,                  /* RPAREN  */
    LBRACKET = 322,                /* LBRACKET  */
    RBRACKET = 323,                /* RBRACKET  */
    LBRACE = 324,                  /* LBRACE  */
    RBRACE = 325,                  /* RBRACE  */
    COMMA = 326,                   /* COMMA  */
    COLON = 327,                   /* COLON  */
    NEWLINE = 328,                 /* NEWLINE  */
    IF_WITHOUT_ELSE = 329,         /* IF_WITHOUT_ELSE  */
    NO_DOT = 330,                  /* NO_DOT  */
    DOT = 331                      /* DOT  */
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

#line 175 "src/parser.tab.h"

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
