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
#line 638 "src/parser.y"

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
    SQL_TEXT = 268,                /* SQL_TEXT  */
    AS = 269,                      /* AS  */
    DIM = 270,                     /* DIM  */
    PLUS_EQ = 271,                 /* PLUS_EQ  */
    MINUS_EQ = 272,                /* MINUS_EQ  */
    STAR_EQ = 273,                 /* STAR_EQ  */
    SLASH_EQ = 274,                /* SLASH_EQ  */
    EXCLUDING = 275,               /* EXCLUDING  */
    INTERSECTING = 276,            /* INTERSECTING  */
    IF = 277,                      /* IF  */
    CONSIDER_IF = 278,             /* CONSIDER_IF  */
    THEN = 279,                    /* THEN  */
    ELSE = 280,                    /* ELSE  */
    CONSIDER_ELSE = 281,           /* CONSIDER_ELSE  */
    END = 282,                     /* END  */
    END_CONSIDER = 283,            /* END_CONSIDER  */
    PRINT = 284,                   /* PRINT  */
    TRUE = 285,                    /* TRUE  */
    FALSE = 286,                   /* FALSE  */
    NOTHING = 287,                 /* NOTHING  */
    UNKNOWN_VALUE = 288,           /* UNKNOWN_VALUE  */
    AND = 289,                     /* AND  */
    OR = 290,                      /* OR  */
    NOT = 291,                     /* NOT  */
    TRY = 292,                     /* TRY  */
    WITH = 293,                    /* WITH  */
    NEW = 294,                     /* NEW  */
    SPAWN = 295,                   /* SPAWN  */
    FOR = 296,                     /* FOR  */
    TO = 297,                      /* TO  */
    STEP = 298,                    /* STEP  */
    DO = 299,                      /* DO  */
    UNTIL = 300,                   /* UNTIL  */
    IN = 301,                      /* IN  */
    EACH = 302,                    /* EACH  */
    WHILE = 303,                   /* WHILE  */
    CONSIDER = 304,                /* CONSIDER  */
    BREAK = 305,                   /* BREAK  */
    CONTINUE = 306,                /* CONTINUE  */
    FUNCTION = 307,                /* FUNCTION  */
    RETURN = 308,                  /* RETURN  */
    GOTO = 309,                    /* GOTO  */
    GOSUB = 310,                   /* GOSUB  */
    WATCH = 311,                   /* WATCH  */
    UNWATCH = 312,                 /* UNWATCH  */
    WAIT = 313,                    /* WAIT  */
    WITHOUT = 314,                 /* WITHOUT  */
    WATCHERS = 315,                /* WATCHERS  */
    ON = 316,                      /* ON  */
    NEXT = 317,                    /* NEXT  */
    STOP = 318,                    /* STOP  */
    ERROR_VALUE = 319,             /* ERROR_VALUE  */
    MODIFIER = 320,                /* MODIFIER  */
    PROGRAM = 321,                 /* PROGRAM  */
    LIBRARY = 322,                 /* LIBRARY  */
    LOAD = 323,                    /* LOAD  */
    USE = 324,                     /* USE  */
    EXPORT = 325,                  /* EXPORT  */
    OP_EQ = 326,                   /* OP_EQ  */
    OP_NE = 327,                   /* OP_NE  */
    OP_GT = 328,                   /* OP_GT  */
    OP_LT = 329,                   /* OP_LT  */
    OP_GE = 330,                   /* OP_GE  */
    OP_LE = 331,                   /* OP_LE  */
    OP_NGT = 332,                  /* OP_NGT  */
    OP_NLT = 333,                  /* OP_NLT  */
    OP_NGE = 334,                  /* OP_NGE  */
    OP_NLE = 335,                  /* OP_NLE  */
    PLUS = 336,                    /* PLUS  */
    MINUS = 337,                   /* MINUS  */
    STAR = 338,                    /* STAR  */
    SLASH = 339,                   /* SLASH  */
    LPAREN = 340,                  /* LPAREN  */
    RPAREN = 341,                  /* RPAREN  */
    LBRACKET = 342,                /* LBRACKET  */
    RBRACKET = 343,                /* RBRACKET  */
    LBRACE = 344,                  /* LBRACE  */
    RBRACE = 345,                  /* RBRACE  */
    COMMA = 346,                   /* COMMA  */
    COLON = 347,                   /* COLON  */
    NEWLINE = 348,                 /* NEWLINE  */
    IF_WITHOUT_ELSE = 349,         /* IF_WITHOUT_ELSE  */
    NO_DOT = 350,                  /* NO_DOT  */
    DOT = 351                      /* DOT  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 663 "src/parser.y"

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
    AstWindowItem *window_item;
    AstWindowItemList window_item_list;
    AstTypeLock type_lock;

#line 213 "src/parser.tab.h"

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
