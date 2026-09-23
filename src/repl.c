/* The gBASIC prompt.
 *
 * A prompt is not a smaller interpreter -- it is the same one with its lifetime
 * turned inside out. `eval_program` opens an environment, runs one root and
 * releases it; a session opens once, runs a root per line, and releases at the
 * end (src/eval.c, gb_session_open / gb_session_run / gb_session_close). This
 * file is the text half: deciding when a line is finished, when it is a
 * question rather than a statement, and what the resident program is.
 *
 * FOUR DECISIONS WORTH THE COMMENT, plus Ctrl-C.
 *
 * WHEN IS A LINE FINISHED? Asked of the PARSER, not of a keyword table: a
 * chunk that fails to parse because it ran out of input wants another line.
 * gBASIC reports exactly two shapes for that -- "unexpected end of file" for an
 * unfinished block and PLAT-CONT's "unclosed '(' opened on line N" for an
 * unfinished expression -- and nothing else means incomplete. A keyword table
 * would have to know `for`/`while`/`if`/`function`/`modifier`/`library`/
 * `server`/`program`/`consider`/`do`/`select`, their `end` spellings and their
 * single-line forms, and would go wrong the first time the grammar grew one.
 * The cost is that the test suite must pin those two messages, and it does --
 * a message reworded without the tripwire noticing would silently cost the
 * prompt its continuation.
 *
 * WHEN IS A LINE A QUESTION? `1 + 2` is not a gBASIC statement, so the parse
 * fails and the prompt retries it wrapped in `print (...)`. The wrap is on its
 * OWN LINES so a trailing comment on the typed text is still a comment
 * (PLAT-CONT again: a newline inside brackets continues the statement). If the
 * wrapped form also fails, the ORIGINAL diagnostic is what gets reported --
 * a typo must be described as a typo, not as a malformed print. A chunk that
 * DOES parse, as a single expression statement, is echoed by the evaluator
 * instead (gb_session_run), which is how a call returning `nothing` stays
 * quiet.
 *
 * WHAT IS THE RESIDENT PROGRAM? Every chunk that ACTED, in the order typed. A
 * chunk that merely ANSWERED is not program text -- `sq(3)` at a prompt is an
 * enquiry, and recording it would put a discarded result into `run` and a line
 * into `save` that the author never meant as source. The two are told apart by
 * whether an answer came back, which is only known after the chunk has run, so
 * the recording happens afterwards.
 *
 * RE-ENTERING A DECLARATION REPLACES the earlier one of the same kind and name,
 * IN PLACE: that is the Tandy 1000 feel -- fix the function, type it again,
 * `run` -- without line numbers to manage, and replacing in place means fixing
 * a typo does not move the function to the bottom of a listing you are reading.
 * `run` starts a FRESH session, so a program that only works because of
 * something typed earlier fails at the prompt exactly as it would from a file.
 *
 * AND CTRL-C ends the running chunk, not the session -- see repl_on_interrupt
 * below. Without it a beginner's first `while true` costs them the prompt and
 * an unsaved program, which is not a state a first chapter can be built on. */

/* sigaction/SA_RESTART (and strdup). Declared here rather than in CFLAGS: the
 * build is strict -std=c11, and this is the only file that needs POSIX. */
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "ast.h"
#include "diagnostics.h"
#include "eval.h"
#include "gbasic.h"
#include "lineedit.h"

#define REPL_SOURCE_NAME "<prompt>"

/* strdup that cannot return NULL, since every caller here would only abort. */
static char *repl_dup(const char *s) {
    char *out = strdup(s);
    if (!out) {
        abort();
    }
    return out;
}

/* ---- the resident program ------------------------------------------------ */

typedef struct {
    char *text;   /* the source the user typed, newline-terminated */
    int   kind;   /* AstStmtKind of the declaration, or -1 when not one */
    char *name;   /* the declaration's name, or NULL */
} ReplEntry;

typedef struct {
    ReplEntry *items;
    size_t     count;
    size_t     cap;
} ReplBuffer;

static void buffer_free(ReplBuffer *b) {
    for (size_t i = 0; i < b->count; i++) {
        free(b->items[i].text);
        free(b->items[i].name);
    }
    free(b->items);
    b->items = NULL;
    b->count = 0;
    b->cap = 0;
}

static void buffer_add(ReplBuffer *b, const char *text, int kind, const char *name) {
    /* Re-entering a declaration replaces the earlier one: the point of a
     * resident program is that you can fix a function and run again, and two
     * copies in the listing would be a listing nobody can trust. A statement
     * (kind -1) always appends -- `x = x + 1` typed twice means twice. */
    if (kind >= 0 && name) {
        for (size_t i = 0; i < b->count; i++) {
            if (b->items[i].kind == kind && b->items[i].name &&
                strcmp(b->items[i].name, name) == 0) {
                /* IN PLACE. Dropping and appending would move the rewritten
                 * function to the bottom of the listing, so fixing a typo
                 * would reorder a program the author is reading. */
                free(b->items[i].text);
                b->items[i].text = repl_dup(text);
                return;
            }
        }
    }
    if (b->count == b->cap) {
        b->cap = b->cap ? b->cap * 2 : 16;
        b->items = realloc(b->items, sizeof(ReplEntry) * b->cap);
        if (!b->items) {
            abort();
        }
    }
    b->items[b->count].text = repl_dup(text);
    b->items[b->count].kind = kind;
    b->items[b->count].name = name ? repl_dup(name) : NULL;
    b->count++;
}

/* The listing. Numbered, because a resident program with no line numbers still
 * needs a way to say WHICH line to remove, and `[n]` is the question `delete`
 * answers -- the Tandy loop with the numbering moved out of the language and
 * into the display. The gutter shifts every line by five columns, so a listing
 * is for READING: `save` is what writes the program verbatim, and `consider`,
 * which recognises its branches by column, would not survive a copy-paste from
 * here. Said in `help` rather than left to be discovered. */
static void buffer_list(const ReplBuffer *b, const char *only, FILE *out) {
    int found = 0;
    for (size_t i = 0; i < b->count; i++) {
        if (only && !(b->items[i].name && strcmp(b->items[i].name, only) == 0)) {
            continue;
        }
        found = 1;
        const char *t = b->items[i].text;
        int first = 1;
        while (*t) {
            const char *nl = strchr(t, '\n');
            size_t len = nl ? (size_t)(nl - t) : strlen(t);
            if (first) {
                fprintf(out, "%3zu  ", i + 1);
                first = 0;
            } else {
                fputs("     ", out);
            }
            fwrite(t, 1, len, out);
            fputc('\n', out);
            if (!nl) {
                break;
            }
            t = nl + 1;
        }
    }
    if (only && !found) {
        fprintf(out, "nothing named '%s' in the program\n", only);
    }
}

/* Remove one entry: `delete 2` by its listed number, `delete sq` by the name it
 * declares. Returns 0 when there is nothing of that description, so the prompt
 * can say so rather than silently doing nothing -- the commonest way a delete
 * command lies. */
static int buffer_delete(ReplBuffer *b, const char *what) {
    size_t index = b->count;
    int all_digits = *what != '\0';
    for (const char *c = what; *c; c++) {
        if (!isdigit((unsigned char)*c)) {
            all_digits = 0;
            break;
        }
    }
    if (all_digits) {
        long n = strtol(what, NULL, 10);
        if (n < 1 || (size_t)n > b->count) {
            return 0;
        }
        index = (size_t)n - 1;
    } else {
        for (size_t i = 0; i < b->count; i++) {
            if (b->items[i].name && strcmp(b->items[i].name, what) == 0) {
                index = i;
                break;
            }
        }
        if (index == b->count) {
            return 0;
        }
    }
    free(b->items[index].text);
    free(b->items[index].name);
    memmove(&b->items[index], &b->items[index + 1],
            sizeof(ReplEntry) * (b->count - index - 1));
    b->count--;
    return 1;
}

static char *buffer_source(const ReplBuffer *b) {
    size_t total = 1;
    for (size_t i = 0; i < b->count; i++) {
        total += strlen(b->items[i].text);
    }
    char *out = malloc(total);
    if (!out) {
        abort();
    }
    out[0] = '\0';
    for (size_t i = 0; i < b->count; i++) {
        strcat(out, b->items[i].text);
    }
    return out;
}

/* The one declaration a chunk is, when it is exactly one. Only a whole-chunk
 * declaration replaces by name: a line that declares something AND does
 * something else is kept as typed, because splitting it would change what
 * `list` shows from what the user wrote. */
static void chunk_declaration(AstStmtList program, int *kind, const char **name) {
    *kind = -1;
    *name = NULL;
    if (program.count != 1) {
        return;
    }
    AstStmt *st = program.items[0];
    switch (st->kind) {
    case AST_STMT_FUNCTION:
        /* A dotted `function obj.method()` has no single declared name until
         * evaluation, so it appends rather than replacing. */
        if (st->as.function.name) {
            *kind = (int)st->kind;
            *name = st->as.function.name;
        }
        break;
    case AST_STMT_MODIFIER:
        *kind = (int)st->kind; *name = st->as.modifier.name; break;
    case AST_STMT_LIBRARY:
        *kind = (int)st->kind; *name = st->as.library.name; break;
    case AST_STMT_PROGRAM:
        *kind = (int)st->kind; *name = st->as.program.name; break;
    case AST_STMT_SERVER:
        *kind = (int)st->kind; *name = st->as.server.name; break;
    default:
        break;
    }
}

/* ---- the session cache ---------------------------------------------------
 *
 * WHAT YOU HAVE TYPED IS ALWAYS IN A FILE, whether or not you have saved it.
 * Two things fall out, and the second is the one that motivated it.
 *
 * `spawn` WORKS AT THE PROMPT. A spawned actor is fork+exec and the child
 * re-parses the SOURCE FILE; a session had none, so `spawn` could only refuse.
 * The cache IS that file, so the refusal is now a fallback for the case where
 * no cache could be written at all rather than the ordinary answer.
 *
 * AND A KILLED SESSION LOSES NOTHING. The file is removed on a CLEAN exit, so
 * one left behind is by definition a session that did not get to finish -- no
 * flag to keep in sync, and the absence of the file is the proof.
 *
 * IT HOLDS THE PROGRAM, NOT THE TRANSCRIPT, which is the same rule `list` and
 * `save` follow: a line that ACTED is program text, a line that merely ANSWERED
 * was a question. The literal transcript is what HISTORY is for, and that
 * persists too -- so between them nothing typed is lost, filed under what it
 * actually was.
 *
 * RECOVERY NEVER RUNS ANYTHING. Coming back to a prompt and having yesterday's
 * half-finished program execute itself is a surprise, and it can be a
 * destructive one. `recover` restores the PROGRAM; `run` is still what makes it
 * live, exactly as for a program you typed. */

static char session_cache[5120];   /* empty when there is nowhere to write one */

/* Whether the resident program has been written somewhere the user chose. Not
 * a crash flag: leaving deliberately with work you never saved loses it just as
 * completely as being killed, so the cache is kept for BOTH and the notice says
 * "ended without saving" rather than "crashed", which is true of each. */
static int program_saved = 1;

static void cache_dir(char *out, size_t n) {
    const char *dir = getenv("GBASIC_SESSION_DIR");
    if (dir && *dir) {
        snprintf(out, n, "%s", dir);
        return;
    }
    const char *xdg = getenv("XDG_STATE_HOME");
    if (xdg && *xdg) {
        snprintf(out, n, "%s/gbasic", xdg);
        return;
    }
    const char *home = getenv("HOME");
    if (home && *home) {
        snprintf(out, n, "%s/.local/state/gbasic", home);
        return;
    }
    out[0] = '\0';
}

/* mkdir -p, for the two or three levels a state directory needs. */
static int make_dirs(const char *path) {
    char buf[4096];
    snprintf(buf, sizeof(buf), "%s", path);
    for (char *p = buf + 1; *p; p++) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        mkdir(buf, 0700);
        *p = '/';
    }
    return mkdir(buf, 0700) == 0 || errno == EEXIST ? 0 : -1;
}

static void cache_open(void) {
    char dir[4096];
    cache_dir(dir, sizeof(dir));
    if (!*dir || make_dirs(dir) != 0) {
        session_cache[0] = '\0';
        return;
    }
    snprintf(session_cache, sizeof(session_cache), "%s/session-%ld.bas",
             dir, (long)getpid());
}

/* Written through a temporary and renamed, so a crash during the write cannot
 * leave a half-file where a whole one was -- the point of the cache is that
 * what is there is always something that ran. 0600 because a session can hold
 * a connection string, the same reason the history file has it. */
static void cache_write(const ReplBuffer *b) {
    if (!*session_cache) {
        return;
    }
    char tmp[5200];
    snprintf(tmp, sizeof(tmp), "%s.tmp", session_cache);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        return;
    }
    FILE *f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        unlink(tmp);
        return;
    }
    char *src = buffer_source(b);
    fputs(src, f);
    free(src);
    if (fflush(f) != 0 || fsync(fileno(f)) != 0) {
        fclose(f);
        unlink(tmp);
        return;
    }
    fclose(f);
    if (rename(tmp, session_cache) != 0) {
        unlink(tmp);
    }
}

static void cache_remove(void) {
    if (*session_cache) {
        unlink(session_cache);
    }
}

/* A cache left by a session that is no longer running. The pid is in the name,
 * so "did it finish" is a question the kernel answers -- and a recycled pid can
 * only make us MISS one, never invent one, which is the safe direction. */
static char *orphan_find(size_t *count) {
    char dir[4096];
    cache_dir(dir, sizeof(dir));
    *count = 0;
    if (!*dir) {
        return NULL;
    }
    DIR *d = opendir(dir);
    if (!d) {
        return NULL;
    }
    char *newest = NULL;
    time_t newest_at = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        long pid;
        if (sscanf(e->d_name, "session-%ld.bas", &pid) != 1) {
            continue;
        }
        char path[4600];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if (*session_cache && strcmp(path, session_cache) == 0) {
            continue;
        }
        if (kill((pid_t)pid, 0) == 0 || errno == EPERM) {
            continue;                      /* still running: not an orphan */
        }
        struct stat st;
        if (stat(path, &st) != 0 || st.st_size == 0) {
            unlink(path);                  /* empty: nothing was lost */
            continue;
        }
        (*count)++;
        if (!newest || st.st_mtime >= newest_at) {
            free(newest);
            newest = repl_dup(path);
            newest_at = st.st_mtime;
        }
    }
    closedir(d);
    return newest;
}

static void orphan_discard(void) {
    for (;;) {
        size_t n = 0;
        char *p = orphan_find(&n);
        if (!p) {
            return;
        }
        unlink(p);
        free(p);
        if (n <= 1) {
            return;
        }
    }
}

static void buffer_add_source(ReplBuffer *b, const char *src);

/* ---- input --------------------------------------------------------------- */

static char *read_line(FILE *in) {
    size_t cap = 128, len = 0;
    char *line = malloc(cap);
    if (!line) {
        abort();
    }
    int c;
    while ((c = fgetc(in)) != EOF) {
        if (len + 2 > cap) {
            cap *= 2;
            line = realloc(line, cap);
            if (!line) {
                abort();
            }
        }
        if (c == '\n') {
            break;
        }
        line[len++] = (char)c;
    }
    if (c == EOF && len == 0) {
        free(line);
        return NULL;
    }
    line[len] = '\0';
    return line;
}

static int blank_line(const char *s) {
    for (; *s; s++) {
        if (!isspace((unsigned char)*s)) {
            return 0;
        }
    }
    return 1;
}

/* ---- diagnostics --------------------------------------------------------- */

/* The parser ran out of input rather than meeting something it refuses. Three
 * shapes, and ONLY three: the grammar's own end-of-file message for an
 * unfinished block, PLAT-CONT's unclosed-bracket message for an unfinished
 * expression, and the LEXER's unterminated-string message for an unfinished
 * literal. Pinned by tests/run_repl.sh (tier CONTINUE_MESSAGES) -- if any
 * wording moves, the prompt stops asking for the rest of a `for` loop and
 * starts reporting it as an error, which is a silent loss of the feature.
 *
 * THE THIRD ONE COMES FROM A DIFFERENT STAGE AND THAT IS THE WHOLE STORY OF
 * WHY IT WAS MISSING. A string literal may run across several lines -- a file
 * has always allowed it and `tutorial.md` teaches it -- but the LEXER reaches
 * the end of its input inside the quotes and refuses there, one stage before
 * the parser would have asked for more. So the prompt was line-oriented for
 * one token and not the other: `print("hello"` put up `...` and waited, and
 * `m = "hello` answered `unterminated string` and threw the line away. It is
 * sound to read that message as "give me the rest": the lexer emits it at
 * exactly one place, having run off the end of the buffer with a string still
 * open, so it cannot mean anything else.
 *
 * THE ESCAPE IS THE BLANK LINE, the same one every other continuation has: a
 * quote the author forgot rather than meant would otherwise hold the prompt
 * open with nothing to type that ends it. The cost is stated rather than
 * hidden -- a blank line INSIDE a multi-line string submits the chunk instead
 * of becoming a paragraph break, which is the price of having a way out at
 * all, and it is the rule brackets already follow. */
static int diagnostics_say_incomplete(const gb_diagnostics *diags) {
    for (size_t i = 0; i < gb_diagnostics_count(diags); i++) {
        const gb_diag *d = gb_diagnostics_at(diags, i);
        if (d->severity != GB_SEVERITY_ERROR || !d->message) {
            continue;
        }
        if (strstr(d->message, "unexpected end of file") ||
            strstr(d->message, "unclosed '") ||
            strstr(d->message, "unterminated string")) {
            return 1;
        }
    }
    return 0;
}

static void drain(gb_diagnostics *diags, int json) {
    for (size_t i = 0; i < gb_diagnostics_count(diags); i++) {
        const gb_diag *d = gb_diagnostics_at(diags, i);
        if (json) {
            gb_diag_write_json(stderr, d);
        } else {
            gb_diag_format(stderr, d);
        }
    }
    fflush(stderr);
}

/* ---- running ------------------------------------------------------------- */

/* Wrap typed text as an expression to print. The parentheses sit on their own
 * lines so a trailing comment on the text stays a comment -- a newline inside
 * brackets continues the statement, which is the whole of PLAT-CONT.
 *
 * THE WRAPPER MUST BE INVISIBLE IN A DIAGNOSTIC, and putting the opener on its
 * own line is exactly what made it visible: the author's one typed line became
 * line 2 of a buffer they typed one line of, so `age > 12` reported
 * `<prompt>:2:5` -- a line that does not exist, which is the one part of an
 * error message a reader cannot learn to use. No wrapper can fix that: `print (`
 * is seven characters against the author's one, so keeping the text on line 1
 * merely trades the wrong line for a column six to the right (measured: 13
 * where the author typed 7). The buffer is parsed with WRAP_FIRST_LINE instead,
 * which numbers the opener 0 and hands the text back its own line number. */
#define WRAP_FIRST_LINE 0

/* The wrapper's `print` is a statement the author never typed, and MOST RUNTIME
 * ERRORS REPORT THE ENCLOSING STATEMENT'S POSITION rather than the failing
 * sub-expression's -- so numbering the opener line 0 moved the defect instead of
 * removing it: `? age` reported `<prompt>:0:1`, which is the same
 * line-that-does-not-exist shape 27dd275 took out of the echo path. Measured
 * across eight error shapes, seven of eight named line 0; only `1/0` reported a
 * real position, because a binary operator carries its own.
 *
 * So the synthetic statement is LOCATED AT ITS OWN EXPRESSION, which the parser
 * already placed correctly inside the author's text. */
static void locate_wrapper(AstStmtList chunk) {
    for (size_t i = 0; i < chunk.count; i++) {
        AstStmt *stmt = chunk.items[i];
        if (stmt->kind == AST_STMT_PRINT && stmt->as.print.expr) {
            stmt->line = stmt->as.print.expr->line;
            stmt->column = stmt->as.print.expr->column;
        }
    }
}
static char *wrap_as_print(const char *text) {
    size_t n = strlen(text);
    char *out = malloc(n + 16);
    if (!out) {
        abort();
    }
    sprintf(out, "print (\n%s\n)\n", text);
    return out;
}

/* Ctrl-C.
 *
 * WHILE A CHUNK RUNS it must end the chunk and NOT the session: a beginner's
 * first `while true` would otherwise take the whole prompt with it, along with
 * a resident program they have not saved. The handler sets one flag and
 * returns; the evaluator unwinds through its ordinary raise path, so locks are
 * released and frames unwound exactly as a real failure unwinds them.
 *
 * WHILE THE PROMPT WAITS FOR INPUT there is nothing to interrupt, and the flag
 * would otherwise be waiting to kill whatever the author typed next -- so
 * `repl_running` gates it, and gb_session_run clears the flag at the start of
 * every chunk as a second line of defence. A Ctrl-C at an idle prompt does
 * nothing, which is what every prompt does.
 *
 * Installed only here. A script's Ctrl-C still ends the process, which is what
 * it has always meant and what anything running gbasic in a pipeline expects. */
static volatile sig_atomic_t repl_running = 0;

static void repl_on_interrupt(int signal_number) {
    (void)signal_number;
    if (repl_running) {
        gb_session_interrupt();
    }
}

/* Open a session and tell it what to call itself. The name is not sticky: the
 * teardown clears the root source path with everything else, so a session
 * started by `run` or `new` would otherwise report its diagnostics against `?`. */
static void session_start(void) {
    gb_session_open();
    eval_set_source_path(REPL_SOURCE_NAME);
}

typedef struct {
    ReplBuffer *buffer;
    int         json;
    int         failed;   /* ANY chunk in this session raised */
} ReplState;

/* Parse and run one finished chunk. Returns 0 always -- a chunk that fails is
 * reported and the prompt continues; only `quit` and EOF end a session.
 *
 * `asked` says the author wrote `?`, which DECLARES the line a question, so the
 * question reading is tried FIRST rather than as a fallback. The two readings
 * are not always the same text: `x = 5` is a perfectly good ASSIGNMENT, so the
 * ordinary path took it, acted, and answered nothing -- and `? x = 5` therefore
 * SET x TO 5 while the reader believed they had asked whether it was. Worse
 * than the silence reported, because the session and the resident program then
 * disagree: the assignment is not recorded, so `run` rebuilds a different x. */
static void run_chunk(ReplState *st, const char *text, int record, int asked) {
    gb_diagnostics diags;
    gb_diagnostics_init(&diags);
    AstStmtList program = ast_stmt_list_empty();
    int parses = gb_parse(text, REPL_SOURCE_NAME, &program, &diags) == 0;

    if (parses && asked) {
        /* It parses as a statement AND the author asked for an answer. Prefer
         * the answer. If the text has no expression reading (`? print("hi")`)
         * this falls through and the statement runs, so `?` never costs the
         * author a line that would otherwise have worked. */
        char *wrapped = wrap_as_print(text);
        gb_diagnostics question_diags;
        gb_diagnostics_init(&question_diags);
        AstStmtList question = ast_stmt_list_empty();
        if (gb_parse_at(wrapped, REPL_SOURCE_NAME, WRAP_FIRST_LINE,
                        &question, &question_diags) == 0) {
            ast_free_program(program);
            free(wrapped);
            gb_diagnostics_free(&question_diags);
            gb_diagnostics_free(&diags);
            locate_wrapper(question);
            repl_running = 1;
            st->failed |= gb_session_run(question) != 0;
            repl_running = 0;
            return;
        }
        ast_free_program(question);
        gb_diagnostics_free(&question_diags);
        free(wrapped);
    }

    if (parses) {
        int kind; const char *name;
        chunk_declaration(program, &kind, &name);
        gb_diagnostics_free(&diags);
        /* The sink is NOT installed for the run: a prompt reports as it goes,
         * so a raise on line 3 of a `for` loop is seen before the loop's own
         * output scrolls past. A script defers so its single fatal line lands
         * last on stderr; there is no "last" here. */
        repl_running = 1;
        st->failed |= gb_session_run(program) != 0;
        repl_running = 0;
        /* Recorded AFTER the run, because whether this was a question is only
         * known once it has been answered: `sq(3)` at a prompt shows 9 and is
         * an enquiry, while `setup()` returning nothing did something and
         * belongs in the program. Recording the first would also put a
         * discarded result into `run`, which warns about exactly that.
         *
         * BOTH HALVES OF THE RULE ARE ASKED, and from the day the prompt shipped
         * until the book found it four days later only one was. The
         * reference says a line that ACTS is kept and one that MERELY answers
         * is not, and the implemented test read the answer alone -- so
         * `append(nodes, x)` and `write(f, text)`, which act AND answer, were
         * dropped: the array grew, the file was written, and `list`, `save`
         * and `run` had never heard of either line. `gb_session_acted` is the
         * missing half, raised by the evaluator rather than guessed from the
         * shape of the text, because nothing in `name(args)` tells `sq(3)`
         * from `append(xs, 3)`. */
        if (record && (!gb_session_echoed() || gb_session_acted())) {
            /* ONE recording path. A typed chunk is by construction the smallest
             * text that parses, so this adds exactly one entry and costs a
             * re-parse; a file read by `load` splits into the entries it is
             * made of, which is what `list` and `delete` need it to be. */
            (void)kind; (void)name;
            buffer_add_source(st->buffer, text);
            program_saved = 0;
            cache_write(st->buffer);
        }
        return;
    }

    /* Not a statement. It may still be a question. */
    ast_free_program(program);
    char *wrapped = wrap_as_print(text);
    gb_diagnostics echo_diags;
    gb_diagnostics_init(&echo_diags);
    AstStmtList echo = ast_stmt_list_empty();
    if (gb_parse_at(wrapped, REPL_SOURCE_NAME, WRAP_FIRST_LINE,
                    &echo, &echo_diags) == 0) {
        free(wrapped);
        gb_diagnostics_free(&echo_diags);
        gb_diagnostics_free(&diags);
        /* NOT recorded. This text is not a statement -- it only ran because
         * the prompt wrapped it as a question -- so putting it in the resident
         * program would make `run` and `save` produce source that does not
         * parse. A question is not part of the program. */
        locate_wrapper(echo);
        repl_running = 1;
        st->failed |= gb_session_run(echo) != 0;
        repl_running = 0;
        return;
    }
    ast_free_program(echo);
    gb_diagnostics_free(&echo_diags);
    free(wrapped);

    /* Report what the author actually wrote, never the wrapper's complaint. */
    drain(&diags, st->json);
    gb_diagnostics_free(&diags);
    st->failed = 1;
}

/* Add a whole source file to the resident program AS THE ENTRIES IT WAS, by
 * re-chunking it the way the prompt chunks typed input: accumulate lines until
 * they parse, and that is one entry. Recorded as a single blob instead, a
 * recovered ten-line program came back as ONE numbered entry, so `delete 1`
 * removed all of it and `list` numbered only the first line -- reported by the
 * book's Chapter 1 plan, which had to warn readers not to use `delete` after
 * `recover`. A book working around a defect is the defect asking to be fixed.
 *
 * The same rule that decides a typed line is finished decides this, so what
 * comes back is what was typed. A trailing fragment that never parses is kept
 * as one entry rather than dropped: it is still the author's text. */
static void buffer_add_source(ReplBuffer *b, const char *src) {
    size_t start = 0, len = strlen(src);
    size_t pos = 0;
    char *acc = NULL;
    size_t acc_len = 0;
    while (pos <= len) {
        const char *nl = strchr(src + pos, '\n');
        size_t line_end = nl ? (size_t)(nl - src) + 1 : len;
        if (pos >= len) {
            break;
        }
        acc_len = line_end - start;
        acc = realloc(acc, acc_len + 1);
        if (!acc) {
            abort();
        }
        memcpy(acc, src + start, acc_len);
        acc[acc_len] = '\0';
        pos = line_end;

        gb_diagnostics d;
        gb_diagnostics_init(&d);
        AstStmtList parsed = ast_stmt_list_empty();
        int ok = gb_parse(acc, REPL_SOURCE_NAME, &parsed, &d) == 0;
        int incomplete = !ok && diagnostics_say_incomplete(&d);
        if (ok || !incomplete) {
            int kind = -1;
            const char *name = NULL;
            if (ok) {
                chunk_declaration(parsed, &kind, &name);
            }
            buffer_add(b, acc, kind, name);
            start = pos;
        }
        ast_free_program(parsed);
        gb_diagnostics_free(&d);
    }
    if (start < len) {
        buffer_add(b, src + start, -1, NULL);
    }
    free(acc);
}

/* ---- commands ------------------------------------------------------------ */

static const char *skip_space(const char *s) {
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    return s;
}

static int word_is(const char *line, const char *word) {
    const char *s = skip_space(line);
    size_t n = strlen(word);
    if (strncmp(s, word, n) != 0) {
        return 0;
    }
    return blank_line(s + n);
}

/* The line's first WORD, when it is `word` and is followed by whitespace or by
 * nothing. THE DELIMITER IS THE WHOLE POINT: `run` is a prompt command and
 * `run()` is a call to the author's own function, and the character after the
 * word is the only thing that tells them apart. `*rest` is left pointing at the
 * argument, already stripped of leading space. */
/* The argument a `list` or `delete` takes: a bare name, or a number. Anything
 * else means this was never the command -- `list = 5` is an assignment to a
 * variable somebody called `list`, and reading it as a command would make that
 * variable unreachable while reporting a puzzle about a program it never saw.
 * (`? list` is then how you read it back, since the bare word IS the command.) */
static int command_arg_ok(const char *arg) {
    if (!*arg) {
        return 1;
    }
    if (isdigit((unsigned char)*arg)) {
        for (const char *c = arg; *c; c++) {
            if (!isdigit((unsigned char)*c)) {
                return 0;
            }
        }
        return 1;
    }
    if (!isalpha((unsigned char)*arg) && *arg != '_') {
        return 0;
    }
    for (const char *c = arg; *c; c++) {
        if (!isalnum((unsigned char)*c) && *c != '_') {
            return 0;
        }
    }
    return 1;
}

static int command_word(const char *line, const char *word, const char **rest) {
    const char *s = skip_space(line);
    size_t n = strlen(word);
    if (strncmp(s, word, n) != 0) {
        return 0;
    }
    if (s[n] != '\0' && !isspace((unsigned char)s[n])) {
        return 0;
    }
    *rest = skip_space(s + n);
    return 1;
}

/* `save "path"` / `load "path"`: the argument is a STRING literal, which is
 * what keeps `load "lib.bas"` (read this file into the prompt) apart from
 * `load sqlite` (the gBASIC statement). A quoted path is a file; a bare word is
 * a library, and the language keeps it. */
static char *command_path(const char *line, const char *word) {
    const char *s = skip_space(line);
    size_t n = strlen(word);
    if (strncmp(s, word, n) != 0 || !isspace((unsigned char)s[n])) {
        return NULL;
    }
    s = skip_space(s + n);
    if (*s != '"') {
        return NULL;
    }
    s++;
    const char *end = strchr(s, '"');
    if (!end || !blank_line(end + 1)) {
        return NULL;
    }
    size_t len = (size_t)(end - s);
    char *path = malloc(len + 1);
    if (!path) {
        abort();
    }
    memcpy(path, s, len);
    path[len] = '\0';
    return path;
}

static void print_help(FILE *out) {
    fprintf(out,
        "gBASIC prompt commands (everything else is gBASIC):\n"
        "  ? expr          show the value of expr\n"
        "  list [name]     show the resident program, or one declaration\n"
        "  delete n|name   remove entry n (the number `list` shows), or a declaration\n"
        "  run             start a fresh session and run the resident program\n"
        "  new             forget the resident program and start a fresh session\n"
        "  vars            show the names this session holds\n"
        "  cls             clear the screen\n"
        "  recover         bring back a program a previous session did not save\n"
        "  discard         forget it instead\n"
        "  save \"file\"     write the resident program to a file, exactly as typed\n"
        "  load \"file\"     read a file into the prompt and run it\n"
        "                  (`load sqlite`, with no quotes, is still the gBASIC statement)\n"
        "  help            this list\n"
        "  quit            leave (so does `bye`, and end-of-input)\n"
        "\n"
        "A line that is not a statement is treated as a question: `1 + 2` answers 3.\n"
        "An unfinished block or bracket asks for the rest; a blank line submits anyway.\n"
        "A command name is only a command as a whole word: `run()` still calls your\n"
        "own function, and `? list` shows a variable you called `list`.\n"
        "`list` numbers its lines so `delete` has something to name; those numbers\n"
        "shift the text, so use `save` when you want the program itself.\n"
        "\n"
        "What you type is kept in a file as you go, so a session that is killed --\n"
        "or left without saving -- can be brought back with `recover` next time.\n"
        "That is a safety net, not a substitute for `save`: it holds one program,\n"
        "and `new` replaces it.\n");
}

static char *read_whole_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        abort();
    }
    size_t n;
    while ((n = fread(buf + len, 1, cap - len - 1, f)) > 0) {
        len += n;
        if (len + 1 >= cap) {
            cap *= 2;
            buf = realloc(buf, cap);
            if (!buf) {
                abort();
            }
        }
    }
    buf[len] = '\0';
    fclose(f);
    return buf;
}

/* ---- the loop ------------------------------------------------------------ */

int repl_main(int json_diagnostics) {
    /* Prompts only when a person is typing. Piped in, the prompt is a filter
     * and its output is exactly the program's, which is what makes a golden
     * possible; GBASIC_REPL_PROMPT=1 forces them so the prompting itself can be
     * tested without a pseudo-terminal. */
    const char *force = getenv("GBASIC_REPL_PROMPT");
    /* `editing` is the stricter of the two: prompts can be forced on for a
     * test, but the line editor needs a REAL terminal -- it puts one in raw
     * mode and reads escape sequences back. */
    int editing = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    int interactive = editing || (force && strcmp(force, "1") == 0);

    /* History lives between sessions, because the line you want back is often
     * from yesterday. GBASIC_HISTORY overrides the path; setting it to an empty
     * string turns persistence off without turning editing off. */
    const char *hist_path = getenv("GBASIC_HISTORY");
    char hist_default[4096] = {0};
    if (!hist_path) {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(hist_default, sizeof(hist_default), "%s/.gbasic_history", home);
            hist_path = hist_default;
        }
    }
    if (editing && hist_path && *hist_path) {
        line_history_load(hist_path);
    }

    /* Line-buffered unconditionally. Interleaving is the whole readability of a
     * prompt: a diagnostic on stderr must land between the lines that produced
     * it, not after a block buffer flushes at the end. Piped, this is also what
     * makes a golden possible. */
    setvbuf(stdout, NULL, _IOLBF, BUFSIZ);

    /* SA_RESTART so the blocking read of the next line is resumed rather than
     * failing with EINTR: a Ctrl-C at an idle prompt must do nothing, and
     * without this it would look like end-of-input and close the session. */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = repl_on_interrupt;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    /* Runtime errors name their source the way a script's do. `<prompt>` rather
     * than a path, and the line is the line WITHIN the chunk -- which is what a
     * multi-line block needs and a single line reports as 1. */
    ReplBuffer buffer = {0};
    ReplState st = { &buffer, json_diagnostics, 0 };

    session_start();
    cache_open();
    /* The cache IS the file a spawned actor re-execs. Without one, `spawn` at
     * the prompt still refuses -- which is now the fallback for a machine with
     * nowhere to write, rather than the ordinary answer. */
    if (*session_cache) {
        gb_set_reexec_path(session_cache);
        cache_write(&buffer);
    }

    size_t orphan_count = 0;
    char *orphan = orphan_find(&orphan_count);

    if (interactive) {
        printf("gBASIC. `help` for prompt commands, `quit` to leave.\n");
        if (orphan) {
            if (orphan_count == 1) {
                printf("A previous session ended without saving. `recover` brings it back, `discard` forgets it.\n");
            } else {
                printf("%zu previous sessions ended without saving. `recover` brings the most recent back, `discard` forgets them all.\n",
                       orphan_count);
            }
        }
    }

    char *pending = NULL;   /* an unfinished chunk, awaiting more lines */
    int status = 0;
    /* Tracked separately from `status`, because `exit(0)` and "nothing asked"
     * are the same number and must not be the same claim: one of them overrides
     * the failure bump below and the other does not. */
    int exit_requested = 0;

    for (;;) {
        /* Two readers, and the split is the same one the prompts follow: a
         * person at a terminal gets editing and history, a pipe gets a plain
         * read. Pointing the editor at a pipe would write escape sequences into
         * whatever is reading the output. */
        char *line;
        if (editing) {
            line = line_edit(pending ? "... " : "> ");
            /* Everything but the line that ends the session: pressing Up and
             * finding `quit` waiting under your finger is the one recall
             * nobody wants, and it would be the FIRST one every time. */
            if (line && !word_is(line, "quit") && !word_is(line, "bye")) {
                line_history_add(line);
            }
        } else {
            if (interactive) {
                fputs(pending ? "... " : "> ", stdout);
                fflush(stdout);
            }
            line = read_line(stdin);
        }
        if (!line) {
            if (interactive) {
                fputc('\n', stdout);
            }
            break;
        }

        if (!pending) {
            if (blank_line(line)) {
                free(line);
                continue;
            }
            const char *arg = NULL;
            if (command_word(line, "quit", &arg) || command_word(line, "bye", &arg)) {
                free(line);
                break;
            }
            if (word_is(line, "help") || word_is(line, "?")) {
                print_help(stdout);
                free(line);
                continue;
            }
            if (command_word(line, "recover", &arg) && !*arg) {
                if (!orphan) {
                    fprintf(stderr, "nothing to recover\n");
                    status = 1;
                } else {
                    char *src = read_whole_file(orphan);
                    if (!src) {
                        fprintf(stderr, "cannot read %s\n", orphan);
                        status = 1;
                    } else {
                        /* RESTORED, NOT RUN. `run` is still what makes a
                         * program live, exactly as for one you typed -- a
                         * prompt that executed yesterday's half-finished work
                         * on your behalf would be a surprise, and could be a
                         * destructive one. Recorded as ONE entry, as typed. */
                        if (*src) {
                            buffer_add_source(&buffer, src);
                            cache_write(&buffer);
                        }
                        free(src);
                        unlink(orphan);
                        free(orphan);
                        orphan = NULL;
                        orphan_count = 0;
                        printf("recovered; `list` to see it, `run` to run it\n");
                    }
                }
                free(line);
                continue;
            }
            if (command_word(line, "discard", &arg) && !*arg) {
                orphan_discard();
                free(orphan);
                orphan = NULL;
                orphan_count = 0;
                free(line);
                continue;
            }
            if (command_word(line, "cls", &arg) && !*arg) {
                /* The two escapes every terminal since the VT100 understands:
                 * home the cursor, erase the screen. Emitted whether or not a
                 * person is typing -- the line asked for it. */
                fputs("\033[H\033[2J", stdout);
                free(line);
                continue;
            }
            if (command_word(line, "list", &arg) && command_arg_ok(arg)) {
                buffer_list(&buffer, *arg ? arg : NULL, stdout);
                free(line);
                continue;
            }
            if (command_word(line, "delete", &arg) && command_arg_ok(arg)) {
                if (!*arg) {
                    fprintf(stderr, "delete needs a number from `list`, or a name\n");
                    status = 1;
                } else if (!buffer_delete(&buffer, arg)) {
                    fprintf(stderr, "nothing to delete: %s\n", arg);
                    status = 1;
                } else {
                    program_saved = 0;
                    cache_write(&buffer);
                }
                free(line);
                continue;
            }
            if (command_word(line, "vars", &arg) && !*arg) {
                gb_session_list_vars(stdout);
                free(line);
                continue;
            }
            if (command_word(line, "new", &arg) && !*arg) {
                buffer_free(&buffer);
                gb_session_close();
                session_start();
                program_saved = 1;
                cache_write(&buffer);
                free(line);
                continue;
            }
            if (command_word(line, "run", &arg) && !*arg) {
                char *src = buffer_source(&buffer);
                gb_session_close();
                session_start();
                /* `record` is 0: the resident program is what produced this
                 * run, so recording it would append the whole program to
                 * itself every time `run` is typed. */
                if (*src) {
                    run_chunk(&st, src, 0, 0);
                }
                free(src);
                free(line);
                continue;
            }
            char *path = command_path(line, "save");
            if (path) {
                FILE *f = fopen(path, "wb");
                if (!f) {
                    fprintf(stderr, "cannot write %s\n", path);
                    status = 1;
                } else {
                    char *src = buffer_source(&buffer);
                    size_t lines = 0;
                    for (const char *c = src; *c; c++) {
                        if (*c == '\n') {
                            lines++;
                        }
                    }
                    fputs(src, f);
                    free(src);
                    fclose(f);
                    program_saved = 1;
                    /* CONFIRMED, not silent. Unix convention is silence on
                     * success, and this is a beginner's prompt rather than a
                     * Unix tool -- `recover` and `delete` already talk back,
                     * and a reader who cannot tell whether `save` worked will
                     * find out by quitting and losing it. */
                    printf("saved %zu line%s to %s\n", lines,
                           lines == 1 ? "" : "s", path);
                }
                free(path);
                free(line);
                continue;
            }
            path = command_path(line, "load");
            if (path) {
                char *src = read_whole_file(path);
                if (!src) {
                    fprintf(stderr, "cannot read %s\n", path);
                    status = 1;
                } else {
                    run_chunk(&st, src, 1, 0);
                    free(src);
                }
                free(path);
                free(line);
                continue;
            }
            /* `? expr` -- BASIC's own shorthand, and the reason it is a
             * QUESTION rather than sugar for `print`: at a prompt it is how you
             * ask for something whose bare name a command would otherwise
             * claim. `? list` shows a variable called `list`. */
            {
                const char *q = skip_space(line);
                if (*q == '?' && !blank_line(q + 1)) {
                    /* THE `?` IS BLANKED, NOT STRIPPED. Removing it shifts
                     * every column left by one, so a diagnostic pointed one
                     * character to the left of what the reader is looking at
                     * -- the smaller half of the same defect as the wrapper's
                     * line, and invisible in exactly the same way, since a
                     * column that is nearly right still reads as right. The
                     * whitespace before it is blanked too, so `  ? x` lines up
                     * as well. */
                    size_t prefix = (size_t)(q - line) + 1;
                    char *asked = malloc(prefix + strlen(q + 1) + 2);
                    if (!asked) {
                        abort();
                    }
                    memset(asked, ' ', prefix);
                    sprintf(asked + prefix, "%s\n", q + 1);
                    run_chunk(&st, asked, 0, 1);
                    free(asked);
                    free(line);
                    continue;
                }
            }
        }

        /* A blank line while a chunk is unfinished SUBMITS it: otherwise a
         * typo that merely looks unfinished ("print (1 +") would hold the
         * prompt open with no way out but end-of-input, and the author would
         * never see the diagnostic that names it. */
        int forced = (pending && blank_line(line));

        size_t have = pending ? strlen(pending) : 0;
        char *joined = malloc(have + strlen(line) + 2);
        if (!joined) {
            abort();
        }
        joined[0] = '\0';
        if (pending) {
            strcat(joined, pending);
        }
        strcat(joined, line);
        strcat(joined, "\n");
        free(pending);
        pending = NULL;
        free(line);

        if (!forced) {
            gb_diagnostics probe;
            gb_diagnostics_init(&probe);
            AstStmtList parsed = ast_stmt_list_empty();
            int ok = gb_parse(joined, REPL_SOURCE_NAME, &parsed, &probe) == 0;
            ast_free_program(parsed);
            int incomplete = !ok && diagnostics_say_incomplete(&probe);
            gb_diagnostics_free(&probe);
            if (incomplete) {
                pending = joined;
                continue;
            }
        }

        run_chunk(&st, joined, 1, 0);
        free(joined);

        int requested = gb_session_exit_code();
        if (requested >= 0) {
            status = requested;
            exit_requested = 1;
            break;
        }
    }

    free(pending);
    free(orphan);
    /* KEPT when there is unsaved work, however the session ended. Removing it
     * on a clean exit would make "nothing is lost" true only of a crash, which
     * is the smaller half of the promise -- typing a program, quitting, and
     * wanting it tomorrow is the commoner one. Removed when the program is
     * empty or has been saved, so the notice on the next start is never noise. */
    if (buffer.count == 0 || program_saved) {
        cache_remove();
    } else if (interactive && *session_cache) {
        printf("Your program was not saved; `recover` will bring it back next time.\n");
    }
    if (editing && hist_path && *hist_path) {
        line_history_save(hist_path);
    }
    line_history_free();
    gb_session_close();
    buffer_free(&buffer);
    /* A SESSION THAT SAW A FAILURE EXITS NONZERO. Interactively that costs
     * nothing -- nobody reads $? after typing -- and piped it is the difference
     * between a script that worked and one that reported three errors and was
     * taken for success. An explicit `exit(n)` still wins: the program said
     * what it meant. */
    if (!exit_requested && !status && st.failed) {
        status = 1;
    }
    return status;
}
