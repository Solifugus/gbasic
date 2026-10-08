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
#line 628 "src/parser.y"

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
    MODIFIER_PREFIX = 263,         /* MODIFIER_PREFIX  */
    AS = 264,                      /* AS  */
    DIM = 265,                     /* DIM  */
    PLUS_EQ = 266,                 /* PLUS_EQ  */
    MINUS_EQ = 267,                /* MINUS_EQ  */
    STAR_EQ = 268,                 /* STAR_EQ  */
    SLASH_EQ = 269,                /* SLASH_EQ  */
    EXCLUDING = 270,               /* EXCLUDING  */
    INTERSECTING = 271,            /* INTERSECTING  */
    IF = 272,                      /* IF  */
    CONSIDER_IF = 273,             /* CONSIDER_IF  */
    THEN = 274,                    /* THEN  */
    ELSE = 275,                    /* ELSE  */
    CONSIDER_ELSE = 276,           /* CONSIDER_ELSE  */
    END = 277,                     /* END  */
    END_CONSIDER = 278,            /* END_CONSIDER  */
    PRINT = 279,                   /* PRINT  */
    TRUE = 280,                    /* TRUE  */
    FALSE = 281,                   /* FALSE  */
    NOTHING = 282,                 /* NOTHING  */
    UNKNOWN_VALUE = 283,           /* UNKNOWN_VALUE  */
    AND = 284,                     /* AND  */
    OR = 285,                      /* OR  */
    NOT = 286,                     /* NOT  */
    WITH = 287,                    /* WITH  */
    NEW = 288,                     /* NEW  */
    SPAWN = 289,                   /* SPAWN  */
    FOR = 290,                     /* FOR  */
    TO = 291,                      /* TO  */
    STEP = 292,                    /* STEP  */
    DO = 293,                      /* DO  */
    UNTIL = 294,                   /* UNTIL  */
    IN = 295,                      /* IN  */
    EACH = 296,                    /* EACH  */
    WHILE = 297,                   /* WHILE  */
    CONSIDER = 298,                /* CONSIDER  */
    BREAK = 299,                   /* BREAK  */
    CONTINUE = 300,                /* CONTINUE  */
    FUNCTION = 301,                /* FUNCTION  */
    RETURN = 302,                  /* RETURN  */
    GOTO = 303,                    /* GOTO  */
    GOSUB = 304,                   /* GOSUB  */
    WATCH = 305,                   /* WATCH  */
    UNWATCH = 306,                 /* UNWATCH  */
    WITHOUT = 307,                 /* WITHOUT  */
    WATCHERS = 308,                /* WATCHERS  */
    ON = 309,                      /* ON  */
    NEXT = 310,                    /* NEXT  */
    STOP = 311,                    /* STOP  */
    ERROR_VALUE = 312,             /* ERROR_VALUE  */
    MODIFIER = 313,                /* MODIFIER  */
    PROGRAM = 314,                 /* PROGRAM  */
    LIBRARY = 315,                 /* LIBRARY  */
    LOAD = 316,                    /* LOAD  */
    USE = 317,                     /* USE  */
    EXPORT = 318,                  /* EXPORT  */
    OP_EQ = 319,                   /* OP_EQ  */
    OP_NE = 320,                   /* OP_NE  */
    OP_GT = 321,                   /* OP_GT  */
    OP_LT = 322,                   /* OP_LT  */
    OP_GE = 323,                   /* OP_GE  */
    OP_LE = 324,                   /* OP_LE  */
    OP_NGT = 325,                  /* OP_NGT  */
    OP_NLT = 326,                  /* OP_NLT  */
    OP_NGE = 327,                  /* OP_NGE  */
    OP_NLE = 328,                  /* OP_NLE  */
    PLUS = 329,                    /* PLUS  */
    MINUS = 330,                   /* MINUS  */
    STAR = 331,                    /* STAR  */
    SLASH = 332,                   /* SLASH  */
    LPAREN = 333,                  /* LPAREN  */
    RPAREN = 334,                  /* RPAREN  */
    LBRACKET = 335,                /* LBRACKET  */
    RBRACKET = 336,                /* RBRACKET  */
    LBRACE = 337,                  /* LBRACE  */
    RBRACE = 338,                  /* RBRACE  */
    COMMA = 339,                   /* COMMA  */
    COLON = 340,                   /* COLON  */
    NEWLINE = 341,                 /* NEWLINE  */
    IF_WITHOUT_ELSE = 342,         /* IF_WITHOUT_ELSE  */
    NO_DOT = 343,                  /* NO_DOT  */
    DOT = 344                      /* DOT  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 653 "src/parser.y"

    double number;
    /* A NUMBER LITERAL plus the exactness `yylex` determined from its digits.
     * `number` stays for anything that only needs the double. */
    struct { double value; long long exact; int is_exact; } numlit;
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

#line 203 "src/parser.tab.h"

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
