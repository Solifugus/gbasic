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
    LENS_CONTENT = 261,            /* LENS_CONTENT  */
    QUALIFIED_IDENT = 262,         /* QUALIFIED_IDENT  */
    AS = 263,                      /* AS  */
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
    STEP = 284,                    /* STEP  */
    DO = 285,                      /* DO  */
    LOOP = 286,                    /* LOOP  */
    UNTIL = 287,                   /* UNTIL  */
    IN = 288,                      /* IN  */
    EACH = 289,                    /* EACH  */
    WHILE = 290,                   /* WHILE  */
    CONSIDER = 291,                /* CONSIDER  */
    BREAK = 292,                   /* BREAK  */
    CONTINUE = 293,                /* CONTINUE  */
    FUNCTION = 294,                /* FUNCTION  */
    RETURN = 295,                  /* RETURN  */
    GOTO = 296,                    /* GOTO  */
    GOSUB = 297,                   /* GOSUB  */
    WATCH = 298,                   /* WATCH  */
    UNWATCH = 299,                 /* UNWATCH  */
    WITHOUT = 300,                 /* WITHOUT  */
    WATCHERS = 301,                /* WATCHERS  */
    ON = 302,                      /* ON  */
    NEXT = 303,                    /* NEXT  */
    STOP = 304,                    /* STOP  */
    ERROR_VALUE = 305,             /* ERROR_VALUE  */
    MODIFIER = 306,                /* MODIFIER  */
    PROGRAM = 307,                 /* PROGRAM  */
    LIBRARY = 308,                 /* LIBRARY  */
    LOAD = 309,                    /* LOAD  */
    USE = 310,                     /* USE  */
    EXPORT = 311,                  /* EXPORT  */
    OP_EQ = 312,                   /* OP_EQ  */
    OP_NE = 313,                   /* OP_NE  */
    OP_GT = 314,                   /* OP_GT  */
    OP_LT = 315,                   /* OP_LT  */
    OP_GE = 316,                   /* OP_GE  */
    OP_LE = 317,                   /* OP_LE  */
    OP_NGT = 318,                  /* OP_NGT  */
    OP_NLT = 319,                  /* OP_NLT  */
    OP_NGE = 320,                  /* OP_NGE  */
    OP_NLE = 321,                  /* OP_NLE  */
    PLUS = 322,                    /* PLUS  */
    MINUS = 323,                   /* MINUS  */
    STAR = 324,                    /* STAR  */
    SLASH = 325,                   /* SLASH  */
    LPAREN = 326,                  /* LPAREN  */
    RPAREN = 327,                  /* RPAREN  */
    LBRACKET = 328,                /* LBRACKET  */
    RBRACKET = 329,                /* RBRACKET  */
    LBRACE = 330,                  /* LBRACE  */
    RBRACE = 331,                  /* RBRACE  */
    COMMA = 332,                   /* COMMA  */
    COLON = 333,                   /* COLON  */
    NEWLINE = 334,                 /* NEWLINE  */
    IF_WITHOUT_ELSE = 335,         /* IF_WITHOUT_ELSE  */
    NO_DOT = 336,                  /* NO_DOT  */
    DOT = 337                      /* DOT  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 362 "src/parser.y"

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
    AstServerItem *server_item;
    AstServerItemList server_item_list;

#line 192 "src/parser.tab.h"

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
