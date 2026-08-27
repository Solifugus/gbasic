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
#line 366 "src/parser.y"

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
    DIM = 264,                     /* DIM  */
    PLUS_EQ = 265,                 /* PLUS_EQ  */
    MINUS_EQ = 266,                /* MINUS_EQ  */
    STAR_EQ = 267,                 /* STAR_EQ  */
    SLASH_EQ = 268,                /* SLASH_EQ  */
    IF = 269,                      /* IF  */
    CONSIDER_IF = 270,             /* CONSIDER_IF  */
    THEN = 271,                    /* THEN  */
    ELSE = 272,                    /* ELSE  */
    CONSIDER_ELSE = 273,           /* CONSIDER_ELSE  */
    END = 274,                     /* END  */
    END_CONSIDER = 275,            /* END_CONSIDER  */
    PRINT = 276,                   /* PRINT  */
    TRUE = 277,                    /* TRUE  */
    FALSE = 278,                   /* FALSE  */
    NOTHING = 279,                 /* NOTHING  */
    UNKNOWN_VALUE = 280,           /* UNKNOWN_VALUE  */
    AND = 281,                     /* AND  */
    OR = 282,                      /* OR  */
    NOT = 283,                     /* NOT  */
    WITH = 284,                    /* WITH  */
    NEW = 285,                     /* NEW  */
    SPAWN = 286,                   /* SPAWN  */
    FOR = 287,                     /* FOR  */
    TO = 288,                      /* TO  */
    STEP = 289,                    /* STEP  */
    DO = 290,                      /* DO  */
    LOOP = 291,                    /* LOOP  */
    UNTIL = 292,                   /* UNTIL  */
    IN = 293,                      /* IN  */
    EACH = 294,                    /* EACH  */
    WHILE = 295,                   /* WHILE  */
    CONSIDER = 296,                /* CONSIDER  */
    BREAK = 297,                   /* BREAK  */
    CONTINUE = 298,                /* CONTINUE  */
    FUNCTION = 299,                /* FUNCTION  */
    RETURN = 300,                  /* RETURN  */
    GOTO = 301,                    /* GOTO  */
    GOSUB = 302,                   /* GOSUB  */
    WATCH = 303,                   /* WATCH  */
    UNWATCH = 304,                 /* UNWATCH  */
    WITHOUT = 305,                 /* WITHOUT  */
    WATCHERS = 306,                /* WATCHERS  */
    ON = 307,                      /* ON  */
    NEXT = 308,                    /* NEXT  */
    STOP = 309,                    /* STOP  */
    ERROR_VALUE = 310,             /* ERROR_VALUE  */
    MODIFIER = 311,                /* MODIFIER  */
    PROGRAM = 312,                 /* PROGRAM  */
    LIBRARY = 313,                 /* LIBRARY  */
    LOAD = 314,                    /* LOAD  */
    USE = 315,                     /* USE  */
    EXPORT = 316,                  /* EXPORT  */
    OP_EQ = 317,                   /* OP_EQ  */
    OP_NE = 318,                   /* OP_NE  */
    OP_GT = 319,                   /* OP_GT  */
    OP_LT = 320,                   /* OP_LT  */
    OP_GE = 321,                   /* OP_GE  */
    OP_LE = 322,                   /* OP_LE  */
    OP_NGT = 323,                  /* OP_NGT  */
    OP_NLT = 324,                  /* OP_NLT  */
    OP_NGE = 325,                  /* OP_NGE  */
    OP_NLE = 326,                  /* OP_NLE  */
    PLUS = 327,                    /* PLUS  */
    MINUS = 328,                   /* MINUS  */
    STAR = 329,                    /* STAR  */
    SLASH = 330,                   /* SLASH  */
    LPAREN = 331,                  /* LPAREN  */
    RPAREN = 332,                  /* RPAREN  */
    LBRACKET = 333,                /* LBRACKET  */
    RBRACKET = 334,                /* RBRACKET  */
    LBRACE = 335,                  /* LBRACE  */
    RBRACE = 336,                  /* RBRACE  */
    COMMA = 337,                   /* COMMA  */
    COLON = 338,                   /* COLON  */
    NEWLINE = 339,                 /* NEWLINE  */
    IF_WITHOUT_ELSE = 340,         /* IF_WITHOUT_ELSE  */
    NO_DOT = 341,                  /* NO_DOT  */
    DOT = 342                      /* DOT  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 391 "src/parser.y"

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
    char op_char;   /* compound-assignment operator: + - * / */
    AstServerItem *server_item;
    AstServerItemList server_item_list;

#line 198 "src/parser.tab.h"

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
