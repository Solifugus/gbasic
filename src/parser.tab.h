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
    PLUS_EQ = 264,                 /* PLUS_EQ  */
    MINUS_EQ = 265,                /* MINUS_EQ  */
    STAR_EQ = 266,                 /* STAR_EQ  */
    SLASH_EQ = 267,                /* SLASH_EQ  */
    IF = 268,                      /* IF  */
    CONSIDER_IF = 269,             /* CONSIDER_IF  */
    THEN = 270,                    /* THEN  */
    ELSE = 271,                    /* ELSE  */
    CONSIDER_ELSE = 272,           /* CONSIDER_ELSE  */
    END = 273,                     /* END  */
    END_CONSIDER = 274,            /* END_CONSIDER  */
    PRINT = 275,                   /* PRINT  */
    TRUE = 276,                    /* TRUE  */
    FALSE = 277,                   /* FALSE  */
    NOTHING = 278,                 /* NOTHING  */
    UNKNOWN_VALUE = 279,           /* UNKNOWN_VALUE  */
    AND = 280,                     /* AND  */
    OR = 281,                      /* OR  */
    NOT = 282,                     /* NOT  */
    WITH = 283,                    /* WITH  */
    NEW = 284,                     /* NEW  */
    SPAWN = 285,                   /* SPAWN  */
    FOR = 286,                     /* FOR  */
    TO = 287,                      /* TO  */
    STEP = 288,                    /* STEP  */
    DO = 289,                      /* DO  */
    LOOP = 290,                    /* LOOP  */
    UNTIL = 291,                   /* UNTIL  */
    IN = 292,                      /* IN  */
    EACH = 293,                    /* EACH  */
    WHILE = 294,                   /* WHILE  */
    CONSIDER = 295,                /* CONSIDER  */
    BREAK = 296,                   /* BREAK  */
    CONTINUE = 297,                /* CONTINUE  */
    FUNCTION = 298,                /* FUNCTION  */
    RETURN = 299,                  /* RETURN  */
    GOTO = 300,                    /* GOTO  */
    GOSUB = 301,                   /* GOSUB  */
    WATCH = 302,                   /* WATCH  */
    UNWATCH = 303,                 /* UNWATCH  */
    WITHOUT = 304,                 /* WITHOUT  */
    WATCHERS = 305,                /* WATCHERS  */
    ON = 306,                      /* ON  */
    NEXT = 307,                    /* NEXT  */
    STOP = 308,                    /* STOP  */
    ERROR_VALUE = 309,             /* ERROR_VALUE  */
    MODIFIER = 310,                /* MODIFIER  */
    PROGRAM = 311,                 /* PROGRAM  */
    LIBRARY = 312,                 /* LIBRARY  */
    LOAD = 313,                    /* LOAD  */
    USE = 314,                     /* USE  */
    EXPORT = 315,                  /* EXPORT  */
    OP_EQ = 316,                   /* OP_EQ  */
    OP_NE = 317,                   /* OP_NE  */
    OP_GT = 318,                   /* OP_GT  */
    OP_LT = 319,                   /* OP_LT  */
    OP_GE = 320,                   /* OP_GE  */
    OP_LE = 321,                   /* OP_LE  */
    OP_NGT = 322,                  /* OP_NGT  */
    OP_NLT = 323,                  /* OP_NLT  */
    OP_NGE = 324,                  /* OP_NGE  */
    OP_NLE = 325,                  /* OP_NLE  */
    PLUS = 326,                    /* PLUS  */
    MINUS = 327,                   /* MINUS  */
    STAR = 328,                    /* STAR  */
    SLASH = 329,                   /* SLASH  */
    LPAREN = 330,                  /* LPAREN  */
    RPAREN = 331,                  /* RPAREN  */
    LBRACKET = 332,                /* LBRACKET  */
    RBRACKET = 333,                /* RBRACKET  */
    LBRACE = 334,                  /* LBRACE  */
    RBRACE = 335,                  /* RBRACE  */
    COMMA = 336,                   /* COMMA  */
    COLON = 337,                   /* COLON  */
    NEWLINE = 338,                 /* NEWLINE  */
    IF_WITHOUT_ELSE = 339,         /* IF_WITHOUT_ELSE  */
    NO_DOT = 340,                  /* NO_DOT  */
    DOT = 341                      /* DOT  */
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

#line 197 "src/parser.tab.h"

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
