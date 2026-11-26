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
#line 658 "src/parser.y"

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
    MODIFIER_VALUE = 264,          /* MODIFIER_VALUE  */
    WINDOW_OPEN = 265,             /* WINDOW_OPEN  */
    DATABASE_OPEN = 266,           /* DATABASE_OPEN  */
    SQL_OPEN = 267,                /* SQL_OPEN  */
    SHAPE_OPEN = 268,              /* SHAPE_OPEN  */
    SQL_TEXT = 269,                /* SQL_TEXT  */
    AS = 270,                      /* AS  */
    DIM = 271,                     /* DIM  */
    PLUS_EQ = 272,                 /* PLUS_EQ  */
    MINUS_EQ = 273,                /* MINUS_EQ  */
    STAR_EQ = 274,                 /* STAR_EQ  */
    SLASH_EQ = 275,                /* SLASH_EQ  */
    EXCLUDING = 276,               /* EXCLUDING  */
    INTERSECTING = 277,            /* INTERSECTING  */
    IF = 278,                      /* IF  */
    CONSIDER_IF = 279,             /* CONSIDER_IF  */
    THEN = 280,                    /* THEN  */
    ELSE = 281,                    /* ELSE  */
    CONSIDER_ELSE = 282,           /* CONSIDER_ELSE  */
    END = 283,                     /* END  */
    END_CONSIDER = 284,            /* END_CONSIDER  */
    PRINT = 285,                   /* PRINT  */
    TRUE = 286,                    /* TRUE  */
    FALSE = 287,                   /* FALSE  */
    NOTHING = 288,                 /* NOTHING  */
    UNKNOWN_VALUE = 289,           /* UNKNOWN_VALUE  */
    AND = 290,                     /* AND  */
    OR = 291,                      /* OR  */
    NOT = 292,                     /* NOT  */
    TRY = 293,                     /* TRY  */
    WITH = 294,                    /* WITH  */
    NEW = 295,                     /* NEW  */
    SPAWN = 296,                   /* SPAWN  */
    FOR = 297,                     /* FOR  */
    TO = 298,                      /* TO  */
    STEP = 299,                    /* STEP  */
    DO = 300,                      /* DO  */
    UNTIL = 301,                   /* UNTIL  */
    IN = 302,                      /* IN  */
    EACH = 303,                    /* EACH  */
    WHILE = 304,                   /* WHILE  */
    CONSIDER = 305,                /* CONSIDER  */
    BREAK = 306,                   /* BREAK  */
    CONTINUE = 307,                /* CONTINUE  */
    FUNCTION = 308,                /* FUNCTION  */
    RETURN = 309,                  /* RETURN  */
    GOTO = 310,                    /* GOTO  */
    GOSUB = 311,                   /* GOSUB  */
    WATCH = 312,                   /* WATCH  */
    UNWATCH = 313,                 /* UNWATCH  */
    WAIT = 314,                    /* WAIT  */
    WITHOUT = 315,                 /* WITHOUT  */
    WATCHERS = 316,                /* WATCHERS  */
    ON = 317,                      /* ON  */
    NEXT = 318,                    /* NEXT  */
    STOP = 319,                    /* STOP  */
    ERROR_VALUE = 320,             /* ERROR_VALUE  */
    MODIFIER = 321,                /* MODIFIER  */
    PROGRAM = 322,                 /* PROGRAM  */
    LIBRARY = 323,                 /* LIBRARY  */
    LOAD = 324,                    /* LOAD  */
    USE = 325,                     /* USE  */
    EXPORT = 326,                  /* EXPORT  */
    OP_EQ = 327,                   /* OP_EQ  */
    OP_NE = 328,                   /* OP_NE  */
    OP_GT = 329,                   /* OP_GT  */
    OP_LT = 330,                   /* OP_LT  */
    OP_GE = 331,                   /* OP_GE  */
    OP_LE = 332,                   /* OP_LE  */
    OP_NGT = 333,                  /* OP_NGT  */
    OP_NLT = 334,                  /* OP_NLT  */
    OP_NGE = 335,                  /* OP_NGE  */
    OP_NLE = 336,                  /* OP_NLE  */
    PLUS = 337,                    /* PLUS  */
    MINUS = 338,                   /* MINUS  */
    STAR = 339,                    /* STAR  */
    SLASH = 340,                   /* SLASH  */
    LPAREN = 341,                  /* LPAREN  */
    RPAREN = 342,                  /* RPAREN  */
    LBRACKET = 343,                /* LBRACKET  */
    RBRACKET = 344,                /* RBRACKET  */
    LBRACE = 345,                  /* LBRACE  */
    RBRACE = 346,                  /* RBRACE  */
    COMMA = 347,                   /* COMMA  */
    COLON = 348,                   /* COLON  */
    NEWLINE = 349,                 /* NEWLINE  */
    PATH_LBRACE = 350,             /* PATH_LBRACE  */
    IF_WITHOUT_ELSE = 351,         /* IF_WITHOUT_ELSE  */
    NO_DOT = 352,                  /* NO_DOT  */
    DOT = 353                      /* DOT  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 683 "src/parser.y"

    double number;
    /* A NUMBER LITERAL plus the exactness `yylex` determined from its digits.
     * `number` stays for anything that only needs the double. */
    struct { double value; long long exact; int is_exact; int scale; } numlit;
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
    AstWindowItem *window_item;
    AstWindowItemList window_item_list;
    AstTypeLock type_lock;
    AstShapeField shape_field;
    AstShapeBody shape_body;

#line 217 "src/parser.tab.h"

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
