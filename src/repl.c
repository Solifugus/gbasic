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
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ast.h"
#include "diagnostics.h"
#include "eval.h"
#include "gbasic.h"

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

/* The parser ran out of input rather than meeting something it refuses. Two
 * shapes, and ONLY two: the grammar's own end-of-file message for an unfinished
 * block, and PLAT-CONT's unclosed-bracket message for an unfinished expression.
 * Pinned by tests/run_repl.sh (tier CONTINUE_MESSAGES) -- if either wording
 * moves, the prompt stops asking for the rest of a `for` loop and starts
 * reporting it as an error, which is a silent loss of the feature. */
static int diagnostics_say_incomplete(const gb_diagnostics *diags) {
    for (size_t i = 0; i < gb_diagnostics_count(diags); i++) {
        const gb_diag *d = gb_diagnostics_at(diags, i);
        if (d->severity != GB_SEVERITY_ERROR || !d->message) {
            continue;
        }
        if (strstr(d->message, "unexpected end of file") ||
            strstr(d->message, "unclosed '")) {
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
 * brackets continues the statement, which is the whole of PLAT-CONT. */
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
 * reported and the prompt continues; only `quit` and EOF end a session. */
static void run_chunk(ReplState *st, const char *text, int record) {
    gb_diagnostics diags;
    gb_diagnostics_init(&diags);
    AstStmtList program = ast_stmt_list_empty();

    if (gb_parse(text, REPL_SOURCE_NAME, &program, &diags) == 0) {
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
         * discarded result into `run`, which warns about exactly that. */
        if (record && !gb_session_echoed()) {
            buffer_add(st->buffer, text, kind, name);
        }
        return;
    }

    /* Not a statement. It may still be a question. */
    ast_free_program(program);
    char *wrapped = wrap_as_print(text);
    gb_diagnostics echo_diags;
    gb_diagnostics_init(&echo_diags);
    AstStmtList echo = ast_stmt_list_empty();
    if (gb_parse(wrapped, REPL_SOURCE_NAME, &echo, &echo_diags) == 0) {
        free(wrapped);
        gb_diagnostics_free(&echo_diags);
        gb_diagnostics_free(&diags);
        /* NOT recorded. This text is not a statement -- it only ran because
         * the prompt wrapped it as a question -- so putting it in the resident
         * program would make `run` and `save` produce source that does not
         * parse. A question is not part of the program. */
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
        "  list            show the resident program\n"
        "  run             start a fresh session and run the resident program\n"
        "  new             forget the resident program and start a fresh session\n"
        "  vars            show the names this session holds\n"
        "  save \"file\"     write the resident program to a file\n"
        "  load \"file\"     read a file into the prompt and run it\n"
        "                  (`load sqlite`, with no quotes, is still the gBASIC statement)\n"
        "  help            this list\n"
        "  quit            leave (so does `bye`, and end-of-input)\n"
        "\n"
        "A line that is not a statement is treated as a question: `1 + 2` answers 3.\n"
        "An unfinished block or bracket asks for the rest; a blank line submits anyway.\n");
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
    int interactive = isatty(STDIN_FILENO) || (force && strcmp(force, "1") == 0);

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

    if (interactive) {
        printf("gBASIC. `help` for prompt commands, `quit` to leave.\n");
    }

    char *pending = NULL;   /* an unfinished chunk, awaiting more lines */
    int status = 0;
    /* Tracked separately from `status`, because `exit(0)` and "nothing asked"
     * are the same number and must not be the same claim: one of them overrides
     * the failure bump below and the other does not. */
    int exit_requested = 0;

    for (;;) {
        if (interactive) {
            fputs(pending ? "... " : "> ", stdout);
            fflush(stdout);
        }
        char *line = read_line(stdin);
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
            if (word_is(line, "quit") || word_is(line, "bye")) {
                free(line);
                break;
            }
            if (word_is(line, "help") || word_is(line, "?")) {
                print_help(stdout);
                free(line);
                continue;
            }
            if (word_is(line, "list")) {
                char *src = buffer_source(&buffer);
                fputs(src, stdout);
                free(src);
                free(line);
                continue;
            }
            if (word_is(line, "vars")) {
                gb_session_list_vars(stdout);
                free(line);
                continue;
            }
            if (word_is(line, "new")) {
                buffer_free(&buffer);
                gb_session_close();
                session_start();
                free(line);
                continue;
            }
            if (word_is(line, "run")) {
                char *src = buffer_source(&buffer);
                gb_session_close();
                session_start();
                /* `record` is 0: the resident program is what produced this
                 * run, so recording it would append the whole program to
                 * itself every time `run` is typed. */
                if (*src) {
                    run_chunk(&st, src, 0);
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
                    fputs(src, f);
                    free(src);
                    fclose(f);
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
                    run_chunk(&st, src, 1);
                    free(src);
                }
                free(path);
                free(line);
                continue;
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

        run_chunk(&st, joined, 1);
        free(joined);

        int requested = gb_session_exit_code();
        if (requested >= 0) {
            status = requested;
            exit_requested = 1;
            break;
        }
    }

    free(pending);
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
