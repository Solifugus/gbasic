#ifndef GBASIC_AST_H
#define GBASIC_AST_H

#include <stddef.h>

/* PLAT-WARN warning-channel modes (docs/warning_model_design.md). */
typedef enum {
    WARN_MODE_UNSET = 0,   /* no explicit setting: look further out */
    WARN_MODE_PRINT,
    WARN_MODE_IGNORE,
    WARN_MODE_NEXT,
    WARN_MODE_STOP
} WarnMode;

typedef enum {
    AST_STMT_ASSIGN,
    AST_STMT_PRINT,
    AST_STMT_EXPR,
    AST_STMT_WITH_LOCK,
    AST_STMT_WITH_PRINCIPAL,
    AST_STMT_FOR_EACH,
    AST_STMT_FOR_RANGE,
    AST_STMT_FUNCTION,
    AST_STMT_RETURN,
    AST_STMT_LABEL,
    AST_STMT_GOTO,
    AST_STMT_GOSUB,
    AST_STMT_WATCH,
    AST_STMT_UNWATCH,
    AST_STMT_WITHOUT_WATCHERS,
    AST_STMT_ON_ERROR_GOTO,
    AST_STMT_ON_ERROR_GOTO_NEXT,
    AST_STMT_ON_WARNING,        /* PLAT-WARN: mode in as.warn_mode */
    AST_STMT_WARNING,           /* PLAT-WARN: raise one; as.error_message */
    AST_STMT_ON_ERROR_STOP,
    AST_STMT_ERROR,
    AST_STMT_MODIFIER,
    AST_STMT_PROGRAM,
    AST_STMT_LIBRARY,
    AST_STMT_USE,
    AST_STMT_IF,
    AST_STMT_WHILE,
    AST_STMT_DO_LOOP,
    AST_STMT_CONSIDER,
    AST_STMT_BREAK,
    AST_STMT_CONTINUE,
    AST_STMT_SERVER,
    AST_STMT_WINDOW,
    AST_STMT_DATABASE,
    AST_STMT_SQL,
    AST_STMT_WAIT,    /* wait until <condition> [for <limit>] (docs/watch_scope_design.md §4) */
    AST_STMT_SHAPE    /* shape NAME ... end shape (docs/shapes_design.md) */
} AstStmtKind;

typedef enum {
    AST_EXPR_NUMBER,
    AST_EXPR_STRING,
    AST_EXPR_IDENT,
    AST_EXPR_BOOL,
    AST_EXPR_NULL,
    AST_EXPR_UNKNOWN,
    AST_EXPR_ARRAY,
    AST_EXPR_RECORD,
    AST_EXPR_DURATION,
    AST_EXPR_INDEX,
    AST_EXPR_FIELD,
    AST_EXPR_CALL,
    AST_EXPR_BINARY,
    AST_EXPR_UNARY,
    AST_EXPR_NEW,
    AST_EXPR_SPAWN,
    /* An inline type modifier: `{file}path`, `{date}s`, `{USD}amount`. The
     * SAME AstModifierUse an assignment clause carries and the same
     * value->value semantics -- what differs is only that there is no target,
     * so it may appear wherever an expression may. */
    AST_EXPR_MODIFIER_APPLY,
    /* A value already evaluated, standing in an argument list. Never parsed:
     * the interpreter builds one for `h.verb(args)`, which hands the receiver
     * to `module.verb` as its first argument without evaluating it twice
     * (docs/handle_methods_design.md). Points at a Value it does not own. */
    AST_EXPR_HELD,
    /* `.city` inside a G-SQL path's brackets: a column of the table to the
     * left of the bracket (docs/gsql_paths_design.md §3.1). Its name is in
     * `ident`. Anywhere else it is refused. */
    AST_EXPR_COLUMN,
    /* `path{ name, email }`: the columns a G-SQL path reads of the table
     * before the braces (docs/gsql_paths_design.md §3.5). */
    AST_EXPR_PROJECT,
    /* `a if c else b` (Matthew, 2026-11-20, Python's spelling): only the
     * chosen value is evaluated, and the condition is held to `if`'s rule. */
    AST_EXPR_CONDITIONAL
} AstExprKind;

typedef struct AstExpr AstExpr;
typedef struct AstStmt AstStmt;

/* `--profile`'s counts for one function or builtin (docs/profiler_design.md
 * §3): its calls (the main thread's), and the samples it was on top of the
 * stack (self) or anywhere on it (total, once a sample: stamp is the last
 * sample that counted it), which only the sampler thread writes. */
typedef struct {
    unsigned long calls;
    unsigned long self;
    unsigned long total;
    unsigned long stamp;
} AstProfCounts;

/* PBI per-property derivation policy (docs/pbi_design.md §2). COPY is the
 * default so an un-annotated field behaves exactly as before. */
typedef enum {
    AST_FIELD_POLICY_COPY = 0,
    AST_FIELD_POLICY_LINK,
    AST_FIELD_POLICY_RESET,
    AST_FIELD_POLICY_EXCLUDE
} AstFieldPolicy;

typedef struct {
    char *name;
    AstExpr *value;
    AstFieldPolicy policy;   /* declared derivation policy; default COPY */
    AstExpr *reset_expr;     /* evaluated per-instance; non-NULL only for RESET */
} AstRecordField;

typedef struct {
    AstStmt **items;
    size_t count;
} AstStmtList;

typedef struct {
    AstExpr **items;
    size_t count;
} AstExprList;

typedef struct {
    AstRecordField *items;
    size_t count;
} AstRecordFieldList;

typedef struct {
    AstExpr *match;
    AstStmtList body;
} AstConsiderBranch;

typedef struct {
    AstConsiderBranch *items;
    size_t count;
} AstConsiderBranchList;

/* A TYPE LOCK: the `number` in `x:number = e` (docs/typed_variables_design.md).
 * `name` is the type as written (`number`, `text`, `money`), `arg` the one
 * argument `money(USD)` takes, NULL otherwise. The parser has already refused
 * a name that is not a data type, so every lock that reaches the evaluator
 * names one. `name` NULL means no lock. */
typedef struct {
    char *name;
    char *arg;
    int line;
    int column;
    /* the name is a variable of the body the lock is written in, holding a
       shape (docs/shapes_design.md §6.4): `r:s = row` in a function handed s.
       Set by the parser once the file is parsed; resolved when the lock runs */
    int by_value;
} AstTypeLock;

/* ONE FIELD OF A SHAPE (docs/shapes_design.md §2): `name:type = default`,
 * `name = value` (untyped: lock.name NULL), or a PBI policy before the colon.
 * A lock naming something that is not a data type names another shape,
 * resolved when the shape is declared. `value` NULL: no default. */
typedef struct {
    char *name;
    AstTypeLock lock;
    AstExpr *value;
    AstFieldPolicy policy;
    AstExpr *reset_expr;
    int line;
    int column;
} AstShapeField;

typedef struct {
    AstShapeField *items;
    size_t count;
} AstShapeFieldList;

/* a shape's body while it is parsed: its fields and its methods */
typedef struct {
    AstShapeFieldList fields;
    AstStmt **methods;
    size_t method_count;
} AstShapeBody;

/* A parameter list. `defaults[i]` is NULL when the parameter is required, and
 * otherwise a LITERAL expression -- number, string, boolean, `nothing` or
 * `unknown`, optionally signed. Literals only, deliberately: an arbitrary
 * default expression would have to be evaluated in some scope, and gBASIC has
 * no closures, so "what can a default see" is a question with no comfortable
 * answer. A literal has nothing to see. Defaults must be TRAILING, checked
 * where the function is defined. */
typedef struct {
    char **items;
    AstExpr **defaults;      /* NULL entry = required parameter */
    size_t count;
    size_t required;         /* parameters before the first default */
    /* `function f(a:number)`: a type lock per parameter, or NULL when no
     * parameter in the list has one (docs/typed_variables_design.md). Only a
     * function honours them; the other users of a parameter list refuse one. */
    AstTypeLock *locks;
    /* The names interned (src/eval.c param_key), filled the first time a
     * function's parameters are bound; NULL until then. */
    const char **keys;
} AstNameList;

/* PLAT-WEB-5: one entry in a `server` declarative block. The grammar is
 * generic (IDENT-led productions, zero reserved words); which words are legal
 * -- verbs, directives, sub-block kinds, hook names -- is decided by the
 * load-time validation pass, exactly as the design draft's §3 asks. */
typedef enum {
    AST_SERVER_DIRECTIVE,   /* root "public" / trust_proxy "ip", ... */
    AST_SERVER_HANDLER,     /* get "/path"( req ) ... end get  (stream too) */
    AST_SERVER_SITE,        /* web name( host: ... ) ... end web */
    AST_SERVER_HOOK         /* on drain ... end on */
} AstServerItemKind;

typedef struct AstServerItem AstServerItem;

typedef struct {
    AstServerItem **items;
    size_t count;
} AstServerItemList;

struct AstServerItem {
    AstServerItemKind kind;
    char *word;              /* directive name / verb / site kind word / hook name */
    char *path;              /* handler: the path string literal */
    char *name;              /* site: the declared name */
    char *close_word;        /* handler/site: the word after `end`, matched in validation */
    AstNameList strings;     /* directive: string-literal arguments */
    AstRecordFieldList options; /* site: head options */
    AstNameList params;      /* handler: parameter names */
    AstStmtList body;        /* handler/hook body */
    AstServerItemList entries;  /* site: nested items */
    char *fn_name;           /* handler/hook: minted internal function name (NULL until registration) */
    AstStmt *fn_stmt;        /* handler/hook: the synthesized function node the registry
                              * holds. A SHELL over this item's params/body (shared, not
                              * copied), so the item frees those and nothing frees this. */
    int line;
    int column;
};

/* A `window` BLOCK'S ITEMS. The same treatment the server block gets -- parsed
 * into data, validated in one load-time pass, introspectable with no display --
 * and the three kinds are the three HEAD SHAPES bison measures at zero
 * conflicts (docs/qt_gui_design.md §5.2), not a taxonomy chosen for tidiness:
 *
 *   LEAF       `stretch()`, `field id( label: "Account" )`   -- no body
 *   CONTAINER  `row`, `column( margin: 8 )`, `group "Credit"`,
 *              `form to edit cust`                          -- a body
 *   HANDLER    `on save.clicked ... end on`
 *
 * WHAT MAKES THAT FREE is the discipline the server block already follows:
 * every head shape decides on its own whether a body follows, so no two share
 * a prefix and differ in body-ness. It is why `stretch()` carries parentheses
 * (a body-less bare word would collide with `row`, a body-bearing one) and why
 * options are ALWAYS parenthesised (an unparenthesised field list has no
 * closing token, so the parser cannot tell where a value ends -- every
 * conflict in the near-miss shape was inside one). */
typedef enum {
    AST_WINDOW_LEAF,
    AST_WINDOW_CONTAINER,
    AST_WINDOW_HANDLER
} AstWindowItemKind;

typedef struct AstWindowItem AstWindowItem;

typedef struct {
    AstWindowItem **items;
    size_t count;
} AstWindowItemList;

struct AstWindowItem {
    AstWindowItemKind kind;
    char *word;              /* field / button / row / column / form / group / stretch */
    char *name;              /* leaf: the declared name (`field id` -> "id") */
    char *label;             /* container: a string literal (`group "Credit"`) */
    char *subject_verb;      /* form: the word after the directive, which must be `to` */
    char *subject_direction; /* form: `edit` or `show` -- the INFINITIVE carries it */
    char *subject;           /* form: the record being edited or shown */
    char *close_word;        /* container/handler: the word after `end` */
    char *event_target;      /* handler: `save` in `on save.clicked` */
    char *event_signal;      /* handler: `clicked` */
    AstRecordFieldList options;
    AstWindowItemList entries;
    AstStmtList body;        /* handler */
    char *fn_name;           /* handler: minted internal function name (NULL until registration) */
    AstStmt *fn_stmt;        /* handler: the synthesized function node the registry holds.
                              * A SHELL over this item's body (shared, not copied), so the
                              * item frees the body and nothing frees this. The server
                              * block's own handlers work exactly this way -- which is what
                              * lets a backend CALL `on save.clicked` while reading only the
                              * record, since the record carries a function VALUE. */
    int line;
    int column;
};

/* A MODIFIER CLAUSE IS A CHAIN OF STAGES, and `next` is additive on purpose:
 * every existing reader of library/name/args sees STAGE ONE unchanged, and only
 * the apply path and the free walk the rest. The struct is passed BY VALUE
 * throughout (~100 sites), which is why a list field would have been the
 * expensive shape and a pointer to the tail is not.
 *
 * `next` is NULL for the one-stage clause every program already writes. Stages
 * are separated by a top-level `;` inside the braces -- see §9 of
 * docs/brace_modifier_design.md for why the comma could not be used. */
typedef struct AstModifierUse {
    char *library;
    char *name;
    AstExprList args;
    struct AstModifierUse *next;
} AstModifierUse;

typedef struct {
    char *name;
    AstNameList params;
} AstModifierSignature;



typedef struct {
    int years;
    int months;
    int weeks;
    int days;
    int hours;
    int minutes;
    int seconds;
    /* A SIZE IN CHARACTERS, not a duration (docs/tui_design.md §8.1, Q3):
     * `40 columns`, `12 rows`. Read by the same NUMBER IDENT rule, so it cost
     * no grammar, and the parser turns it into the record `{ columns: 40 }`
     * rather than a duration; these fields only carry it that far. */
    int cells;              /* 0 none, 1 columns, 2 rows */
    double cells_amount;
    int cells_mixed;        /* more than one term with a size among them */
} AstDuration;

/* a call's `reached` */
enum { CALL_REACHED_LIBRARY = 1, CALL_REACHED_VALUE = 2 };

struct AstExpr {
    AstExprKind kind;
    int line;
    int column;
    /* `--profile`: for a call, the builtin's counts once this site has been
     * classified, or a marker that it calls no builtin. NULL until then.
     * prof_handle is the same for the module verb a handle method reaches
     * (`db.query()` is `gsql.query(db)`), which the site itself is not. */
    void *prof_builtin;
    void *prof_handle;
    /* An identifier's name interned (src/eval.c name_intern), found the first
     * time it is evaluated: a variable is looked up by comparing pointers
     * (docs/llvm_backend_design.md, increment 1). NULL until then. */
    const char *key;
    /* For a call: the intern table's generation when its name was last found
     * NOT to be interned (src/eval.c name_known). */
    unsigned long key_generation;
    /* written in parentheses: `(shop.customer[3]).name` ends a G-SQL path
       there, so `.name` is the row's field (docs/gsql_paths_design.md §3.2) */
    int parenthesized;
    union {
        /* A NUMBER LITERAL, with the exactness the lexer determined. The literal
         * is where exactness ORIGINATES, so it has to survive the parse: an
         * `AstExpr` holding only the double would have thrown it away before
         * evaluation ever saw it. Same shape as `Value`'s numeric member. */
        struct {
            double value;
            long long exact;
            int is_exact;
            int scale;      /* an exact decimal: exact x 10^-scale (docs/exact_decimal_design.md) */
        } num;
        char *string;
        char *ident;
        int boolean;
        AstExprList array;
        AstRecordFieldList record;
        AstDuration duration;
        struct {
            AstExpr *array;
            AstExpr *index;
            /* `a[x, y]`: `index` is an AST_EXPR_ARRAY of every expression the
               brackets held -- a G-SQL path's key or filter list
               (docs/gsql_paths_design.md §3.1); refused on anything else */
            int list;
        } index;
        struct {
            AstExpr *object;
            char *field;
            /* A CACHE, never the answer (docs/shapes_design.md §7): the
               slot this site last found its field at, tried first and
               checked by name, and the shape field its last write to a
               shaped record converted by. `length` is strlen(field), 0
               until first used. */
            size_t length;
            size_t slot;
            const void *shape;
            size_t shape_index;
        } field;
        struct {
            AstExpr *object;
            AstNameList names;
        } project;
        struct {
            char *library;    /* library/receiver-variable name, or NULL */
            char *name;       /* function / method name */
            AstExprList args;
            AstExpr *receiver; /* NULL for a plain/qualified call; set for a
                                * method call whose receiver is an arbitrary
                                * expression (a.b.method(), make().method()) */
            int name_id;       /* src/call_names.h's number for `name`, found
                                * the first time the call runs; 0 until then */
            int registered;    /* in the builtin registry: 2 yes, 1 no, 0 not
                                * yet asked */
            int library_id;    /* the same number for `library`; 0 until then */
            const char *library_key;   /* `library` interned, once it is */
            unsigned long library_key_generation;
            /* The function an unqualified call reaches (src/eval.c
             * call_function): its index in the function table plus one, or 0
             * for none, good while the table's generation and the calling
             * library are the ones recorded. */
            size_t fn_index;
            unsigned long fn_generation;
            const char *fn_library;
            /* how far eval_call_rest has gone: CALL_REACHED_LIBRARY past
             * every native module to a qualified call, CALL_REACHED_VALUE
             * past every builtin to a function value; 0 not yet. What it
             * passed depends only on the names, so the compiled code
             * (gbrt_call_enter) skips it from then on. */
            int reached;
        } call;
        struct {
            char *op;
            AstModifierUse modifier;
            AstExpr *left;
            AstExpr *right;
        } binary;
        struct {
            char *op;
            AstExpr *expr;
        } unary;
        struct {
            AstExpr *condition;
            AstExpr *then_value;   /* the value before `if` */
            AstExpr *else_value;
        } conditional;
        struct {
            AstExpr *proto;   /* prototype to derive from */
            AstExpr *with;    /* optional override record literal, or NULL */
        } derive;
        struct {
            AstModifierUse modifier;
            AstExpr *value;
        } modifier_apply;
        struct {
            void *value;   /* a Value *, borrowed */
        } held;
    } as;
};

struct AstStmt {
    AstStmtKind kind;
    int line;         /* 1-based BYTE start line   (set by ast_stmt_span/position) */
    int column;       /* 1-based BYTE start column */
    int end_line;     /* 1-based BYTE end line     (exclusive end; 0 if unset)     */
    int end_column;   /* 1-based BYTE end column   (exclusive: one past last byte) */
    /* `--profile`'s exact counts (docs/profiler_design.md §3): how many times
     * this statement ran, and for a function how many times it was called.
     * Zero and untouched unless the run is profiled. */
    unsigned long prof_runs;
    /* The samples taken while this statement ran (§3), written only by the
     * sampler thread and read after it is joined. */
    unsigned long prof_samples;
    /* A function's calls and its sampled time. */
    AstProfCounts prof_fn;
    /* A function's compiled body (include/gb_jit.h): NULL until its first
     * call under GBASIC_COMPILE, then the entry, or a marker of src/jit.c's
     * when it is interpreted. */
    void *jit;
    /* A `for` counter's name interned, as AstExpr.key. */
    const char *key;
    union {
        struct {
            AstExpr *target;
            AstModifierUse modifier;
            AstExpr *value;
            /* Compound assignment: '+', '-', '*', '/', or 0 for a plain `=`.
             *
             * Carried as an OPERATOR rather than desugared in the grammar
             * into `target = target op value`, which would need the target
             * subtree DUPLICATED -- two owners of the same nodes, and a double
             * free to go with them. The evaluator folds it instead, by handing
             * the real operator a synthetic binary node. */
            char op;
            /* `x:number = e`: lock `x` to a data type. Unset (name NULL) for
             * every other assignment. */
            AstTypeLock lock;
        } assign;
        /* PLAT-STDERR: `print` and `print to error` are one node kind with a
         * destination, not two kinds. They differ only in which stream the
         * rendered bytes go to, so sharing the node keeps every consumer that
         * switches on AstStmtKind -- the evaluator, the AST dump, --add-loads,
         * source_outline -- correct without any of them learning a new case. */
        struct {
            AstExpr *expr;
            int to_stderr;   /* 0 = standard output, 1 = standard error */
        } print;
        AstExpr *expr_stmt;
        struct {
            AstExpr *file;
            AstStmtList body;
        } with_lock;
        /* `with principal(p) ... end with`: the identity on whose behalf the
         * body acts. Same node shape as with_lock and the same grammar
         * production -- the opener word is checked by POSITION, so neither
         * `lock` nor `principal` is a reserved word. */
        struct {
            AstExpr *value;
            AstStmtList body;
        } with_principal;
        struct {
            char *name;
            /* The optional INDEX variable of `for each item, i in list`. NULL
             * when the form was not used. It is a plain loop-scoped number,
             * not a reference: gBASIC has no references, so the index is how
             * a loop body writes back into the array it is walking
             * (`list[i] = item`), which is an in-place path assignment. */
            char *index_name;
            /* `for each x:number in xs`: each element bound as `x:number =
             * element` binds it (docs/typed_variables_design.md). Unset (name
             * NULL) for a loop that names no type. */
            AstTypeLock lock;
            AstExpr *iterable;
            AstStmtList body;
            /* PLAT-WARN 2107: the dead-write analysis, computed ONCE on first
             * execution and cached here -- a for-each nested in a hot loop
             * must not re-walk its body per execution. 0 unchecked, 1 clean,
             * 2 a dead write was found at (dead_line, dead_column). */
            signed char dead_checked;
            int dead_line;
            int dead_column;
        } for_each;
        struct {
            AstStmtList body;
            AstExpr *condition;  /* post-test STOP condition: the body repeats
                                  * until this becomes true. There is no
                                  * continue-condition form -- see parser.y. */
        } do_loop;
        struct {
            char *name;          /* the counter variable */
            AstExpr *start;
            AstExpr *limit;      /* INCLUSIVE, as BASIC has always had it */
            AstExpr *step;       /* NULL means 1 */
            AstStmtList body;
        } for_range;
        struct {
            char *name;          /* bare: the declared name; dotted: internal name (NULL until eval) */
            AstNameList params;
            AstStmtList body;
            char *object;        /* dotted `function obj.method()`: receiver var name; else NULL */
            char *field;         /* dotted: the method field name; else NULL */
            /* The file this function was PARSED from, or NULL for the root
             * source. Owned here; stamped at registration (eval.c), not by the
             * parser, because the parser is handed text and does not know the
             * path it came from.
             *
             * It exists so a runtime error inside a loaded library names the
             * LIBRARY. Line numbers come from the library's own AST nodes, but
             * the path used to come from a global that had already been restored
             * by the time the function was CALLED -- so every stdlib diagnostic
             * reported a real line against the wrong file. */
            char *source_path;
            /* The LIBRARY this function belongs to, or NULL for the root
             * source. Stamped at registration beside `source_path`, and for the
             * same kind of reason: an unqualified call inside a library must
             * reach that library's own function, and the file path cannot say
             * so when the library is declared in the SAME FILE as the program
             * that loads it -- it has no separate path to match on. */
            char *library;
            /* `function f(...):number`: every `return` with a value converts
             * into the type or is refused. Unset (name NULL) for a function
             * with no declared return type. */
            AstTypeLock returns;
            /* Warning 2125, worked out at the first call: for each parameter,
             * the line and column of a field write to it that nothing in the
             * body reads afterwards (0 when none), two ints a parameter. */
            int *param_dead;
            signed char param_dead_checked;
        } function;
        AstExpr *return_expr;
        char *label;
        char *goto_label;
        char *gosub_label;
        char *loop_target;   /* `break x` / `continue x`: the named loop, or NULL */
        struct {
            char *name;          /* NULL for the anonymous form */
            AstNameList names;
            AstStmtList body;
        } watch;
        AstExpr *unwatch_expr;
        struct {
            AstExpr *condition;
            AstExpr *limit;      /* the `for` duration, or NULL */
        } wait;
        AstStmtList without_watchers;
        char *on_error_label;
        int warn_mode;   /* WarnMode; PLAT-WARN */
        AstExpr *error_message;
        struct {
            char *name;
            AstNameList params;
            char *context;
            int exported;
            AstStmtList body;
        } modifier;
        struct {
            char *name;
            AstNameList args;
            AstStmtList body;
        } program;
        struct {
            char *name;
            AstStmtList body;
        } library;
        struct {
            char *name;
            char *path;
            /* `load NAME as ALIAS` -- the name this FILE calls the library by,
             * or NULL. The library's own declared name is unaffected; an alias
             * is a rename at qualification time, scoped to the importer. */
            char *alias;
        } use_stmt;
        struct {
            AstExpr *condition;
            AstStmtList body;
            AstStmtList else_body;
        } if_stmt;
        struct {
            AstExpr *condition;
            AstStmtList body;
        } while_stmt;
        struct {
            AstExpr *subject;
            AstConsiderBranchList branches;
            AstStmtList else_body;
        } consider;
        struct {
            char *name;          /* the declared name bound in the enclosing scope */
            char *close_word;    /* the word after the closing `end` */
            char *subject_verb;      /* `window c( ... ) to edit cust` -- the shorthand */
            char *subject_direction;
            char *subject;
            AstRecordFieldList options;
            AstWindowItemList items;
        } window;
        /* `shape NAME` ... `end shape`: its fields in declared order, and its
         * methods, each a FUNCTION statement (docs/shapes_design.md §2.3) */
        struct {
            char *name;
            AstShapeFieldList fields;
            size_t method_count;
            AstStmt **methods;
        } shape;
        /* `database shop( file: "shop.gsql" )` (docs/gsql_module_design.md §7) */
        struct {
            char *name;
            AstRecordFieldList options;
        } database;
        /* `sql cheap on shop` ... `end sql`: the body is the raw SQL text, and
         * text_line/text_column place its first character in the source, so a
         * position G-SQL's parser reports can be located in the file */
        struct {
            char *name;
            char *database;
            char *text;
            int text_line;
            int text_column;
            /* set by the load-time pass when the name is never used, so
             * the block can never run (warning 2121, eval.c) */
            int unused;
        } sql;
        struct {
            char *word;          /* the block kind word; "server" is the only one today */
            char *name;          /* the declared name bound in the enclosing scope */
            char *close_word;    /* the word after the closing `end` */
            AstRecordFieldList options;
            AstServerItemList items;
        } server;
    } as;
};

AstStmtList ast_stmt_list_empty(void);
AstStmtList ast_stmt_list_append(AstStmtList list, AstStmt *stmt);
AstExprList ast_expr_list_empty(void);
AstExprList ast_expr_list_append(AstExprList list, AstExpr *expr);
AstRecordFieldList ast_record_field_list_empty(void);
AstRecordFieldList ast_record_field_list_append(AstRecordFieldList list, char *name, AstExpr *value);
AstRecordFieldList ast_record_field_list_append_policy(AstRecordFieldList list, char *name, AstExpr *value, AstFieldPolicy policy, AstExpr *reset_expr);
AstConsiderBranchList ast_consider_branch_list_empty(void);
AstConsiderBranchList ast_consider_branch_list_append(AstConsiderBranchList list, AstExpr *match, AstStmtList body);
AstNameList ast_name_list_empty(void);
AstNameList ast_name_list_append(AstNameList list, char *name);
AstNameList ast_name_list_lock_last(AstNameList list, AstTypeLock lock);
AstNameList ast_name_list_append_default(AstNameList list, char *name,
                                         AstExpr *default_value);

AstWindowItemList ast_window_item_list_empty(void);
AstWindowItemList ast_window_item_list_append(AstWindowItemList list, AstWindowItem *item);
AstWindowItem *ast_window_leaf(char *word, char *name, AstRecordFieldList options,
                               int line, int column);
AstWindowItem *ast_window_container(char *word, char *label, AstRecordFieldList options,
                                    AstWindowItemList entries, char *close_word,
                                    int line, int column);
AstWindowItem *ast_window_form(char *word, char *verb, char *direction, char *subject,
                               AstWindowItemList entries, char *close_word,
                               int line, int column);
AstWindowItem *ast_window_named_container(char *word, char *name, char *label,
                                          AstWindowItemList entries,
                                          char *close_word, int line,
                                          int column);
AstWindowItem *ast_window_named_form(char *word, char *name, char *verb,
                                     char *direction, char *subject,
                                     AstWindowItemList entries,
                                     char *close_word, int line, int column);
AstWindowItem *ast_window_handler(char *target, char *signal, AstStmtList body,
                                  int line, int column);
AstStmt *ast_window(char *name, AstRecordFieldList options, AstWindowItemList items,
                    char *close_word, char *verb, char *direction, char *subject,
                    int line, int column);

void ast_free_window_item_list(AstWindowItemList list);
AstStmt *ast_database(char *name, AstRecordFieldList options, int line, int column);
AstShapeFieldList ast_shape_field_list_empty(void);
AstShapeBody ast_shape_body_empty(void);
AstShapeBody ast_shape_body_add_field(AstShapeBody body, AstShapeField field);
AstShapeBody ast_shape_body_add_method(AstShapeBody body, AstStmt *method);
void ast_free_shape_field(AstShapeField field);
void ast_free_shape_body(AstShapeBody body);
AstShapeFieldList ast_shape_field_list_append(AstShapeFieldList list, AstShapeField field);
/* the shape's body as parsed: its fields, and its methods (FUNCTION
 * statements) */
AstStmt *ast_shape(char *name, AstShapeFieldList fields, AstStmt **methods,
                   size_t method_count, int line, int column);
AstStmt *ast_sql(char *name, char *database, char *text, int text_line, int text_column,
                 int line, int column);

AstServerItemList ast_server_item_list_empty(void);
AstServerItemList ast_server_item_list_append(AstServerItemList list, AstServerItem *item);
AstServerItem *ast_server_directive(char *word, AstNameList strings, int line, int column);
AstServerItem *ast_server_handler(char *word, char *path, AstNameList params,
                                  AstStmtList body, char *close_word, int line, int column);
AstServerItem *ast_server_site(char *word, char *name, AstRecordFieldList options,
                               AstServerItemList entries, char *close_word, int line, int column);
AstServerItem *ast_server_hook(char *word, AstStmtList body, int line, int column);
AstStmt *ast_server(char *word, char *name, AstRecordFieldList options,
                    AstServerItemList items, char *close_word);

AstExpr *ast_number(double value, long long exact, int is_exact, int scale);
AstExpr *ast_string(char *value);
AstExpr *ast_ident(char *name);
AstExpr *ast_bool(int value);
AstExpr *ast_null(void);
AstExpr *ast_unknown(void);
AstExpr *ast_array(AstExprList items);
AstExpr *ast_record(AstRecordFieldList fields);
AstExpr *ast_duration(AstDuration duration);
AstExpr *ast_index(AstExpr *array, AstExpr *index);
AstExpr *ast_index_list(AstExpr *array, AstExprList items);
AstExpr *ast_column(char *name);
AstExpr *ast_project(AstExpr *object, AstNameList names);
AstExpr *ast_field(AstExpr *object, char *field);
AstExpr *ast_call(char *name, AstExprList args);
AstExpr *ast_qualified_call(char *library, char *name, AstExprList args);
AstExpr *ast_method_call(AstExpr *receiver, char *name, AstExprList args);
AstModifierUse ast_modifier_none(void);
AstModifierUse ast_modifier_use(char *name, AstExprList args);
AstModifierSignature ast_modifier_signature(char *name, AstNameList params);
AstExpr *ast_binary(char *op, AstModifierUse modifier, AstExpr *left, AstExpr *right);
AstExpr *ast_modifier_apply(AstModifierUse modifier, AstExpr *value);
AstExpr *ast_unary(char *op, AstExpr *expr);
AstExpr *ast_conditional(AstExpr *condition, AstExpr *then_value, AstExpr *else_value);
AstExpr *ast_new(AstExpr *proto, AstExpr *with);
AstExpr *ast_spawn(char *name, AstExprList args);
AstExpr *ast_expr_position(AstExpr *expr, int line, int column);

AstStmt *ast_assign(AstExpr *target, AstModifierUse modifier, AstExpr *value);
AstStmt *ast_assign_op(AstExpr *target, AstModifierUse modifier, AstExpr *value, char op);
AstStmt *ast_assign_locked(AstExpr *target, AstTypeLock lock, AstExpr *value);
AstTypeLock ast_type_lock_none(void);
AstTypeLock ast_type_lock(char *name, char *arg, int line, int column);
void ast_type_lock_free(AstTypeLock lock);
AstStmt *ast_print(AstExpr *expr);
AstStmt *ast_print_error(AstExpr *expr);
AstStmt *ast_expr_stmt(AstExpr *expr);
AstStmt *ast_with_lock(AstExpr *file, AstStmtList body);
AstStmt *ast_with_principal(AstExpr *value, AstStmtList body);
AstStmt *ast_for_each(char *name, char *index_name, AstExpr *iterable,
                      AstStmtList body);
AstStmt *ast_for_each_locked(char *name, AstTypeLock lock, char *index_name,
                             AstExpr *iterable, AstStmtList body);
AstStmt *ast_for_range(char *name, AstExpr *start, AstExpr *limit,
                       AstExpr *step, AstStmtList body);
AstStmt *ast_do_loop(AstStmtList body, AstExpr *condition);
AstStmt *ast_function(char *name, AstNameList params, AstStmtList body);
AstStmt *ast_function_returning(char *name, AstNameList params, AstTypeLock returns, AstStmtList body);
AstStmt *ast_return(AstExpr *expr);
AstStmt *ast_label(char *name);
AstStmt *ast_goto(char *name);
AstStmt *ast_gosub(char *name);
AstStmt *ast_watch(char *name, AstNameList names, AstStmtList body);
AstStmt *ast_unwatch(AstExpr *expr);
AstStmt *ast_wait(AstExpr *condition, AstExpr *limit);
AstStmt *ast_without_watchers(AstStmtList body);
AstStmt *ast_on_error_goto(char *label);
AstStmt *ast_on_error_goto_next(void);
AstStmt *ast_on_error_stop(void);
AstStmt *ast_on_warning(int mode);
AstStmt *ast_warning(AstExpr *message);
AstStmt *ast_error(AstExpr *message);
AstStmt *ast_modifier(char *name, AstNameList params, char *context, int exported, AstStmtList body);
AstStmt *ast_program(char *name, AstNameList args, AstStmtList body);
AstStmt *ast_library(char *name, AstStmtList body);
AstStmt *ast_use(char *name, char *path, char *alias);
AstStmt *ast_if(AstExpr *condition, AstStmtList body);
AstStmt *ast_while(AstExpr *condition, AstStmtList body);
AstStmt *ast_consider(AstExpr *subject, AstConsiderBranchList branches, AstStmtList else_body);
AstStmt *ast_break(char *target);
AstStmt *ast_continue(char *target);
AstStmt *ast_stmt_position(AstStmt *stmt, int line, int column);
/* Set both the start (line,column) and the exclusive end (end_line,end_column)
 * BYTE positions on a statement. end_* use the parser's yylloc last_* convention
 * (one past the last byte of the last token in the construct). */
AstStmt *ast_stmt_span(AstStmt *stmt, int line, int column,
                       int end_line, int end_column);

void ast_dump(AstStmtList program);
void ast_free_program(AstStmtList program);

/* Single-node + aggregate frees for parser error-recovery destructors (all accept
 * the empty/NULL case). */
void ast_free_expr(AstExpr *expr);
void ast_free_stmt(AstStmt *stmt);
void ast_free_expr_list(AstExprList list);
void ast_free_name_list(AstNameList list);
void ast_free_record_field_list(AstRecordFieldList list);
void ast_free_server_item_list(AstServerItemList list);
void ast_free_consider_branch_list(AstConsiderBranchList list);
void ast_free_modifier_use(AstModifierUse modifier);
void ast_free_modifier_signature(AstModifierSignature sig);

/* THE ONE WORDING for a declarative block whose head word is not `server`.
 *
 * The rule is checked at TWO MOMENTS and that is not a duplication: the
 * frontend's load-time pass sees only blocks that PARSED, and `sub greet()`
 * followed by `print "hi"` does not parse -- its body is not server items --
 * so the parser has to say it too, or the author is told about a line two
 * below the mistake (DOGFOOD 31). Two moments, one sentence, written here so
 * they cannot drift apart. */
void gb_format_unknown_block(char *out, size_t size, const char *word);

/* Is `name` a shape the RUNTIME already has? Set by the evaluator when it
 * declares its first shape, so a prompt line can lock to a shape an earlier
 * line declared; NULL until then, and then the parse asks only its own file. */
extern int (*gb_parse_shape_known)(const char *name);

#endif
