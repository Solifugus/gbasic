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
#line 466 "src/parser.y"

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

/* Parsed PBI per-field policy clause: `( copy | link | exclude | reset <expr> )` */
typedef struct {
    AstFieldPolicy policy;
    AstExpr *reset_expr;
} FieldPolicySpec;

#line 72 "src/parser.tab.h"

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
    NEW = 280,                     /* NEW  */
    SPAWN = 281,                   /* SPAWN  */
    FOR = 282,                     /* FOR  */
    TO = 283,                      /* TO  */
    IN = 284,                      /* IN  */
    EACH = 285,                    /* EACH  */
    WHILE = 286,                   /* WHILE  */
    CONSIDER = 287,                /* CONSIDER  */
    BREAK = 288,                   /* BREAK  */
    CONTINUE = 289,                /* CONTINUE  */
    FUNCTION = 290,                /* FUNCTION  */
    RETURN = 291,                  /* RETURN  */
    GOTO = 292,                    /* GOTO  */
    GOSUB = 293,                   /* GOSUB  */
    WATCH = 294,                   /* WATCH  */
    WITHOUT = 295,                 /* WITHOUT  */
    WATCHERS = 296,                /* WATCHERS  */
    ON = 297,                      /* ON  */
    RESUME = 298,                  /* RESUME  */
    NEXT = 299,                    /* NEXT  */
    STOP = 300,                    /* STOP  */
    ERROR_VALUE = 301,             /* ERROR_VALUE  */
    MODIFIER = 302,                /* MODIFIER  */
    PROGRAM = 303,                 /* PROGRAM  */
    LIBRARY = 304,                 /* LIBRARY  */
    LOAD = 305,                    /* LOAD  */
    USE = 306,                     /* USE  */
    EXPORT = 307,                  /* EXPORT  */
    OP_EQ = 308,                   /* OP_EQ  */
    OP_NE = 309,                   /* OP_NE  */
    OP_GT = 310,                   /* OP_GT  */
    OP_LT = 311,                   /* OP_LT  */
    OP_GE = 312,                   /* OP_GE  */
    OP_LE = 313,                   /* OP_LE  */
    OP_NGT = 314,                  /* OP_NGT  */
    OP_NLT = 315,                  /* OP_NLT  */
    OP_NGE = 316,                  /* OP_NGE  */
    OP_NLE = 317,                  /* OP_NLE  */
    PLUS = 318,                    /* PLUS  */
    MINUS = 319,                   /* MINUS  */
    STAR = 320,                    /* STAR  */
    SLASH = 321,                   /* SLASH  */
    LPAREN = 322,                  /* LPAREN  */
    MOD_LPAREN = 323,              /* MOD_LPAREN  */
    RPAREN = 324,                  /* RPAREN  */
    LBRACKET = 325,                /* LBRACKET  */
    RBRACKET = 326,                /* RBRACKET  */
    LBRACE = 327,                  /* LBRACE  */
    RBRACE = 328,                  /* RBRACE  */
    COMMA = 329,                   /* COMMA  */
    COLON = 330,                   /* COLON  */
    NEWLINE = 331,                 /* NEWLINE  */
    IF_WITHOUT_ELSE = 332,         /* IF_WITHOUT_ELSE  */
    NO_DOT = 333,                  /* NO_DOT  */
    DOT = 334                      /* DOT  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 489 "src/parser.y"

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

#line 185 "src/parser.tab.h"

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
