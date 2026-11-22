/* The `term` module (docs/tui_design.md §2.3): the terminal as a screen of
 * cells, keys and the mouse, over src/term.c.
 *
 * Included into eval.c after the stdin source, whose descriptor it shares.
 *
 * ONE TERMINAL, SO NO HANDLE. A process has one controlling terminal, and
 * the alternate screen, raw mode and the restoring handlers are all
 * process-wide, so the verbs are the module's (`term.put`, `term.show`) and
 * `term.open` answers nothing to keep. The note's sketch had `s = term.open()`
 * and methods on `s`; a handle would have offered a second `s` that could
 * not exist.
 *
 * WAITED OR WATCHED, NEVER BOTH (docs/networking_design.md P2), as `http`
 * is: `term.wait(limit)` blocks for the next event, `watch(term.events)`
 * delivers each one on the event loop. While the second is live the first is
 * refused, and `watch(stdin.lines)` is refused while the terminal is open,
 * since both would read descriptor 0.
 *
 * STDERR IS HELD WHILE THE SCREEN IS UP. A warning or a runtime error written
 * to the alternate screen would vanish with it, so while it is open and
 * stderr is the terminal, descriptor 2 is a temporary file, replayed onto the
 * real stderr when the screen closes: at term.close, at the end of the
 * program, and from the signal handlers (src/term.c). */

#define TERM_ERROR_CODE 5301

static Value term_raise(const char *message) {
    runtime_error_raise(message, TERM_ERROR_CODE, "term");
    return value_null();
}

static void term_bind_events_global(void) {
    if (env_find_in_frame(&global_env, "term")) {
        return;     /* a program that bound it first keeps what it wrote */
    }
    Value record = value_record(NULL, 0);
    record_set(&record, "events", value_array(NULL, 0));
    Symbol *items = realloc(global_env.items, sizeof(Symbol) * (global_env.count + 1));
    if (!items) {
        abort();
    }
    global_env.items = items;
    global_env.items[global_env.count].name = name_intern("term");
    memset(&global_env.items[global_env.count].lock, 0, sizeof(TypeLock));
    global_env.items[global_env.count].value = record;
    global_env.count++;
}

#if !GBASIC_TERM
/* Windows, until increment 2 (the console's modes): the module answers that
 * it cannot, rather than being absent, so a program asking term.can() runs
 * the same everywhere. */
static int term_holds_stdin(void) {
    return 0;
}

static int term_loop_active(void) {
    return 0;
}

static long term_poll_timeout_ms(long tick_ms) {
    return tick_ms;
}

static int term_loop_service(int readable) {
    (void)readable;
    return 1;
}

static Value term_eval_call(AstExpr *expr) {
    if (!term_library_loaded) {
        return term_raise("library not loaded: term (write `load term` first)");
    }
    if (CALL_IS(expr, can) || CALL_IS(expr, is_open)) {
        return value_bool(0);
    }
    if (CALL_IS(expr, close)) {
        return value_null();
    }
    return term_raise("term is not available in this build: the Windows console is the next increment "
                      "(docs/tui_design.md §7)");
}
#else

#include "term.h"
/* After a lone ESC, how long to wait for the rest of a sequence before it is
 * the escape key. Terminals send a sequence in one write, so anything that
 * follows later than this was typed. */
#define TERM_ESC_WAIT_MS 30

static int term_open_now = 0;
/* Who has the screen: 0 nobody, 2 the `ui` module's tui backend, whose
 * windows draw on the same terminal (src/modules/ui_tui.c). term.open and a
 * tui window refuse each other rather than draw over one another. */
static int term_screen_owner = 0;
static TermScreen term_screen;
static TermDecoder term_decoder;
static double term_pending_since = 0;   /* when the decoder last took a byte, while it holds some */


static int term_holds_stdin(void) {
    return term_open_now;
}

static int term_events_watched(void) {
    for (size_t i = 0; i < watcher_count; i++) {
        if (watchers[i].active && watcher_matches_change(watchers[i].stmt, "term.events")) {
            return 1;
        }
    }
    return 0;
}

static double term_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* ---- events as values ---------------------------------------------------- */

static void term_set_mods(Value *e, unsigned mods) {
    record_set(e, "ctrl", value_bool((mods & TERM_MOD_CTRL) != 0));
    record_set(e, "alt", value_bool((mods & TERM_MOD_ALT) != 0));
    record_set(e, "shift", value_bool((mods & TERM_MOD_SHIFT) != 0));
}

static Value term_event_value(const TermEvent *ev) {
    Value e = value_record(NULL, 0);
    if (ev->kind == TERM_EV_KEY) {
        record_set(&e, "kind", value_string("key"));
        record_set(&e, "key", value_string(ev->key));
        record_set(&e, "text", value_string(ev->text));
        term_set_mods(&e, ev->mods);
    } else if (ev->kind == TERM_EV_MOUSE) {
        record_set(&e, "kind", value_string("mouse"));
        record_set(&e, "action", value_string(ev->action));
        record_set(&e, "button", value_number_exact(ev->button));
        record_set(&e, "column", value_number_exact(ev->column));
        record_set(&e, "row", value_number_exact(ev->row));
        term_set_mods(&e, ev->mods);
    } else {
        record_set(&e, "kind", value_string("paste"));
        record_set(&e, "text", value_string_n(ev->data, ev->data_len));
    }
    return e;
}

static Value term_resize_value(void) {
    Value e = value_record(NULL, 0);
    record_set(&e, "kind", value_string("resize"));
    record_set(&e, "columns", value_number_exact(term_screen.columns));
    record_set(&e, "rows", value_number_exact(term_screen.rows));
    return e;
}

/* A resize the signal reported: the screen takes the new size (keeping what
 * fits; the terminal's contents become unknown, so the next show redraws
 * all). 1 when there was one. */
static int term_check_resized(void) {
    if (!term_take_resized()) {
        return 0;
    }
    int columns, rows;
    if (term_size(&columns, &rows) == 0) {
        term_screen_resize(&term_screen, columns, rows);
    } else {
        term_screen.have_valid = 0;
    }
    return 1;
}

/* Reads what descriptor 0 has (without blocking: the caller polled it). 0
 * at end of input. */
static int term_read_input(void) {
    unsigned char buf[4096];
    ssize_t got = read(0, buf, sizeof buf);
    if (got < 0) {
        return errno == EINTR || errno == EAGAIN ? 1 : 0;
    }
    if (got == 0) {
        return 0;
    }
    term_decoder_feed(&term_decoder, buf, (size_t)got);
    term_pending_since = term_now_ms();
    return 1;
}

/* The next decoded event, if there is one now; an unfinished sequence is
 * given up on once TERM_ESC_WAIT_MS has passed with nothing after it. */
static int term_next_decoded(TermEvent *ev) {
    int flush = term_decoder_pending(&term_decoder) &&
                term_now_ms() - term_pending_since >= TERM_ESC_WAIT_MS;
    return term_decoder_next(&term_decoder, flush, ev);
}

/* ---- the screen's hold on stderr --------------------------------------- */

static int term_saved_stderr = -1;
static int term_held_stderr = -1;

static void term_hold_stderr(void) {
    if (!isatty(2)) {
        return;     /* a log file or a pipe: what goes there does not touch the screen */
    }
    char path[] = "/tmp/gbasic-term-stderr-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) {
        return;
    }
    unlink(path);
    fflush(stderr);
    term_saved_stderr = dup(2);
    if (term_saved_stderr < 0) {
        close(fd);
        return;
    }
    dup2(fd, 2);
    term_held_stderr = fd;
    term_stderr_hold(term_saved_stderr, term_held_stderr);
}

static void term_release_stderr(void) {
    if (term_saved_stderr < 0) {
        return;
    }
    fflush(stderr);
    dup2(term_saved_stderr, 2);
    term_stderr_replay();
    close(term_saved_stderr);
    close(term_held_stderr);
    term_saved_stderr = term_held_stderr = -1;
    term_stderr_hold(-1, -1);
}

static void term_close_now(void) {
    if (!term_open_now) {
        return;
    }
    term_leave();
    term_release_stderr();
    term_screen_free(&term_screen);
    term_decoder_free(&term_decoder);
    term_open_now = 0;
}

static void term_at_exit(void) {
    term_close_now();
}

/* ---- arguments ------------------------------------------------------------ */

static int term_whole(Value v, const char *what, long min, long max, long *out) {
    if (v.kind != VALUE_NUMBER || v.as.num.value != floor(v.as.num.value) ||
        v.as.num.value < (double)min || v.as.num.value > (double)max) {
        char msg[200];
        snprintf(msg, sizeof msg, "%s must be a whole number from %ld to %ld", what, min, max);
        term_raise(msg);
        return 0;
    }
    *out = (long)v.as.num.value;
    return 1;
}

static const struct { const char *name; uint32_t rgb; } term_colour_names[] = {
    { "black", 0x000000 }, { "red", 0xCD0000 }, { "green", 0x00CD00 }, { "yellow", 0xCDCD00 },
    { "blue", 0x0000EE }, { "magenta", 0xCD00CD }, { "cyan", 0x00CDCD }, { "white", 0xE5E5E5 },
    { "gray", 0x7F7F7F }, { "bright_red", 0xFF0000 }, { "bright_green", 0x00FF00 },
    { "bright_yellow", 0xFFFF00 }, { "bright_blue", 0x5C5CFF }, { "bright_magenta", 0xFF00FF },
    { "bright_cyan", 0x00FFFF }, { "bright_white", 0xFFFFFF },
};

/* "default", one of the sixteen names, or "#rrggbb". */
static int term_colour(Value *v, const char *what, uint32_t *out) {
    if (v->kind == VALUE_STRING) {
        const char *s = v->as.string;
        if (strcmp(s, "default") == 0) {
            *out = TERM_DEFAULT;
            return 1;
        }
        for (size_t i = 0; i < sizeof term_colour_names / sizeof term_colour_names[0]; i++) {
            if (strcmp(s, term_colour_names[i].name) == 0) {
                *out = term_colour_names[i].rgb;
                return 1;
            }
        }
        if (s[0] == '#' && strlen(s) == 7 && strspn(s + 1, "0123456789abcdefABCDEF") == 6) {
            *out = (uint32_t)strtoul(s + 1, NULL, 16);
            return 1;
        }
    }
    char msg[300];
    snprintf(msg, sizeof msg, "%s must be \"default\", a colour name (black, red, green, yellow, blue, "
             "magenta, cyan, white, gray, or bright_ and one of them) or \"#rrggbb\"", what);
    term_raise(msg);
    return 0;
}

/* A style record: { fg, bg, bold, dim, italic, underline, reverse }. */
static int term_style(Value *v, const char *verb, TermStyle *out) {
    static const char *const known[] = { "fg", "bg", "bold", "dim", "italic", "underline", "reverse", NULL };
    static const unsigned bits[] = { 0, 0, TERM_BOLD, TERM_DIM, TERM_ITALIC, TERM_UNDERLINE, TERM_REVERSE };
    out->fg = out->bg = TERM_DEFAULT;
    out->attrs = 0;
    if (!v || v->kind == VALUE_NULL) {
        return 1;
    }
    char msg[300];
    if (v->kind != VALUE_RECORD) {
        snprintf(msg, sizeof msg, "%s: a style is a record such as { fg: \"red\", bold: true }", verb);
        term_raise(msg);
        return 0;
    }
    for (size_t i = 0; i < v->as.record.count; i++) {
        const char *name = v->as.record.fields[i].name;
        Value *f = v->as.record.fields[i].value;
        size_t k = 0;
        while (known[k] && strcmp(known[k], name) != 0) {
            k++;
        }
        if (!known[k]) {
            snprintf(msg, sizeof msg, "%s: a style has no '%s' (it has fg, bg, bold, dim, italic, underline, reverse)", verb, name);
            term_raise(msg);
            return 0;
        }
        if (f->kind == VALUE_NULL) {
            continue;
        }
        char what[64];
        snprintf(what, sizeof what, "%s: %s", verb, name);
        if (k < 2) {
            if (!term_colour(f, what, k == 0 ? &out->fg : &out->bg)) {
                return 0;
            }
        } else if (f->kind != VALUE_BOOL) {
            snprintf(msg, sizeof msg, "%s must be true or false", what);
            term_raise(msg);
            return 0;
        } else if (f->as.boolean) {
            out->attrs |= bits[k];
        }
    }
    return 1;
}

static int term_need_open(const char *verb) {
    if (!term_open_now) {
        char msg[160];
        snprintf(msg, sizeof msg, "%s: the terminal is not open; call term.open() first", verb);
        term_raise(msg);
        return 0;
    }
    return 1;
}

/* Evaluates every argument; 0 having raised or with an evaluation pending. */
static int term_args(AstExpr *expr, Value *out, size_t min, size_t max, const char *usage) {
    size_t argc = expr->as.call.args.count;
    if (argc < min || argc > max) {
        term_raise(usage);
        return 0;
    }
    for (size_t i = 0; i < max; i++) {
        out[i] = value_null();
    }
    for (size_t i = 0; i < argc; i++) {
        out[i] = eval_expr(expr->as.call.args.items[i]);
        if (error_action_pending()) {
            for (size_t j = 0; j <= i; j++) {
                value_free(out[j]);
            }
            return 0;
        }
    }
    return 1;
}

static void term_free_args(Value *v, size_t n) {
    for (size_t i = 0; i < n; i++) {
        value_free(v[i]);
    }
}

/* ---- the verbs ------------------------------------------------------------- */

static Value term_open(AstExpr *expr) {
    Value a[1];
    if (!term_args(expr, a, 0, 1, "term.open expects an optional options record { mouse }")) {
        return value_null();
    }
    int mouse = 0;
    if (a[0].kind == VALUE_RECORD) {
        for (size_t i = 0; i < a[0].as.record.count; i++) {
            const char *name = a[0].as.record.fields[i].name;
            Value *f = a[0].as.record.fields[i].value;
            if (strcmp(name, "mouse") != 0) {
                char msg[160];
                snprintf(msg, sizeof msg, "term.open: unknown option '%s' (known: mouse)", name);
                term_free_args(a, 1);
                return term_raise(msg);
            }
            if (f->kind != VALUE_BOOL) {
                term_free_args(a, 1);
                return term_raise("term.open: mouse must be true or false");
            }
            mouse = f->as.boolean;
        }
    } else if (a[0].kind != VALUE_NULL) {
        term_free_args(a, 1);
        return term_raise("term.open expects an optional options record { mouse }");
    }
    term_free_args(a, 1);
    if (term_open_now) {
        return term_raise("term.open: the terminal is already open");
    }
    if (term_screen_owner == 2) {
        return term_raise("term.open: a window is open on this terminal (the ui module's tui "
                          "backend); close it first");
    }
    if (stdin_events_watched()) {
        return term_raise("term.open: standard input is watched as lines (watch(stdin.lines)); "
                          "a terminal reads keys from the same descriptor, so stop that watch first");
    }
    const char *why = term_unusable();
    if (why) {
        char msg[300];
        snprintf(msg, sizeof msg, "term.open: %s", why);
        return term_raise(msg);
    }
    int columns, rows;
    if (term_size(&columns, &rows) != 0) {
        return term_raise("term.open: the terminal does not report its size");
    }
    fflush(stdout);
    int rc = term_enter(mouse);
    if (rc != 0) {
        char msg[200];
        snprintf(msg, sizeof msg, "term.open: raw mode could not be set: %s", strerror(rc));
        return term_raise(msg);
    }
    term_screen_init(&term_screen, columns, rows, term_depth_from_env(getenv("COLORTERM"), getenv("TERM")));
    term_screen.have_valid = 1;     /* term_enter cleared it */
    term_decoder_init(&term_decoder);
    term_hold_stderr();
    static int registered = 0;
    if (!registered) {
        atexit(term_at_exit);
        registered = 1;
    }
    term_open_now = 1;
    return value_null();
}

static Value term_size_value(void) {
    Value r = value_record(NULL, 0);
    record_set(&r, "columns", value_number_exact(term_screen.columns));
    record_set(&r, "rows", value_number_exact(term_screen.rows));
    return r;
}

static Value term_put(AstExpr *expr) {
    Value a[4];
    if (!term_args(expr, a, 3, 4, "term.put expects a column, a row, the text and an optional style")) {
        return value_null();
    }
    long column, row;
    TermStyle st;
    Value out = value_null();
    if (term_need_open("term.put") &&
        term_whole(a[0], "term.put: the column", -1000000, 1000000, &column) &&
        term_whole(a[1], "term.put: the row", -1000000, 1000000, &row) &&
        term_style(&a[3], "term.put", &st)) {
        if (a[2].kind != VALUE_STRING) {
            term_raise("term.put: the text must be text");
        } else {
            term_check_resized();
            size_t bad = 0;
            int next = term_screen_put(&term_screen, (int)column, (int)row, a[2].as.string,
                                       string_length(a[2].as.string), st, &bad);
            if (next < 0) {
                char msg[200];
                snprintf(msg, sizeof msg, "term.put: the text holds a control character or invalid UTF-8 at byte %zu; "
                         "a terminal would act on it rather than show it", bad);
                term_raise(msg);
            } else {
                out = value_number_exact(next);
            }
        }
    }
    term_free_args(a, 4);
    return out;
}

/* term.fill(column, row, columns, rows [, style]): spaces in the style. */
static Value term_fill(AstExpr *expr) {
    Value a[5];
    if (!term_args(expr, a, 4, 5, "term.fill expects a column, a row, a width, a height and an optional style")) {
        return value_null();
    }
    long c, r, w, h;
    TermStyle st;
    if (term_need_open("term.fill") &&
        term_whole(a[0], "term.fill: the column", -1000000, 1000000, &c) &&
        term_whole(a[1], "term.fill: the row", -1000000, 1000000, &r) &&
        term_whole(a[2], "term.fill: the width", 0, 10000, &w) &&
        term_whole(a[3], "term.fill: the height", 0, 10000, &h) &&
        term_style(&a[4], "term.fill", &st)) {
        term_check_resized();
        char *spaces = malloc((size_t)w + 1);
        if (!spaces) {
            abort();
        }
        memset(spaces, ' ', (size_t)w);
        for (long y = r; y < r + h; y++) {
            term_screen_put(&term_screen, (int)c, (int)y, spaces, (size_t)w, st, NULL);
        }
        free(spaces);
    }
    term_free_args(a, 5);
    return value_null();
}

static Value term_clear(AstExpr *expr) {
    Value a[1];
    if (!term_args(expr, a, 0, 1, "term.clear expects an optional style")) {
        return value_null();
    }
    TermStyle st;
    if (term_need_open("term.clear") && term_style(&a[0], "term.clear", &st)) {
        term_check_resized();
        term_screen_clear(&term_screen, st);
    }
    term_free_args(a, 1);
    return value_null();
}

/* term.cursor(column, row) shows the cursor there after the next show;
 * term.cursor() hides it. */
static Value term_cursor(AstExpr *expr) {
    Value a[2];
    if (!term_args(expr, a, 0, 2, "term.cursor expects a column and a row, or nothing to hide it")) {
        return value_null();
    }
    long c, r;
    if (term_need_open("term.cursor")) {
        if (expr->as.call.args.count == 0) {
            term_screen.cursor_visible = 0;
        } else if (expr->as.call.args.count == 1) {
            term_raise("term.cursor expects a column and a row, or nothing to hide it");
        } else if (term_whole(a[0], "term.cursor: the column", 1, 10000, &c) &&
                   term_whole(a[1], "term.cursor: the row", 1, 10000, &r)) {
            term_screen.cursor_column = (int)c;
            term_screen.cursor_row = (int)r;
            term_screen.cursor_visible = 1;
        }
    }
    term_free_args(a, 2);
    return value_null();
}

static Value term_show(void) {
    if (!term_need_open("term.show")) {
        return value_null();
    }
    term_check_resized();
    TermBuf b = { 0 };
    term_screen_render(&term_screen, &b);
    if (b.n && term_write(b.p, b.n) != 0) {
        term_buf_free(&b);
        return term_raise("term.show: the terminal could not be written to");
    }
    term_buf_free(&b);
    return value_null();
}

/* term.width(text): the cells text takes, by the same table term.put uses. */
static Value term_width_of(AstExpr *expr) {
    Value a[1];
    if (!term_args(expr, a, 1, 1, "term.width expects text")) {
        return value_null();
    }
    Value out = value_null();
    if (a[0].kind != VALUE_STRING) {
        term_raise("term.width expects text");
    } else {
        size_t bad = 0;
        long cells = term_text_width(a[0].as.string, string_length(a[0].as.string), &bad);
        if (cells < 0) {
            char msg[200];
            snprintf(msg, sizeof msg, "term.width: the text holds a control character or invalid UTF-8 at byte %zu", bad);
            term_raise(msg);
        } else {
            out = value_number_exact(cells);
        }
    }
    term_free_args(a, 1);
    return out;
}

/* term.wait(limit): the next event, or unknown when the limit passes first
 * (a duration or seconds; none waits as long as it takes). */
static Value term_wait(AstExpr *expr) {
    Value a[1];
    if (!term_args(expr, a, 0, 1, "term.wait expects an optional limit (a duration or seconds)")) {
        return value_null();
    }
    double limit = -1;
    if (expr->as.call.args.count == 1) {
        if (a[0].kind == VALUE_NUMBER && a[0].as.num.value >= 0) {
            limit = a[0].as.num.value;
        } else if (a[0].kind == VALUE_DURATION) {
            long long months = 0, seconds = 0;
            duration_totals(a[0].as.duration, &months, &seconds);
            if (months == 0 && seconds >= 0) {
                limit = (double)seconds;
            }
        }
        if (limit < 0) {
            term_free_args(a, 1);
            return term_raise("term.wait: the limit must be a duration (2 seconds) or a number of seconds");
        }
    }
    term_free_args(a, 1);
    if (!term_need_open("term.wait")) {
        return value_null();
    }
    if (term_events_watched()) {
        return term_raise("term.wait: term.events is watched, so the event loop delivers each event; "
                          "a wait would take them from it");
    }
    double deadline = limit < 0 ? -1 : term_now_ms() + limit * 1000.0;
    for (;;) {
        if (term_check_resized()) {
            return term_resize_value();
        }
        TermEvent ev;
        if (term_next_decoded(&ev)) {
            Value v = term_event_value(&ev);
            term_event_free(&ev);
            return v;
        }
        double now = term_now_ms();
        if (deadline >= 0 && now >= deadline) {
            return value_unknown();
        }
        long wait = deadline < 0 ? 1000 : (long)(deadline - now) + 1;
        if (term_decoder_pending(&term_decoder)) {
            long esc = (long)(term_pending_since + TERM_ESC_WAIT_MS - now) + 1;
            if (esc < wait) {
                wait = esc < 0 ? 0 : esc;
            }
        }
        struct pollfd pfd = { 0, POLLIN, 0 };
        int ready = poll(&pfd, 1, (int)wait);
        if (ready > 0 && !term_read_input()) {
            return term_raise("term.wait: the terminal closed (end of input)");
        }
    }
}

static Value term_eval_call(AstExpr *expr) {
    const char *verb = expr->as.call.name;
    if (!term_library_loaded) {
        return term_raise("library not loaded: term (write `load term` first)");
    }
    if (CALL_IS(expr, can)) {
        if (expr->as.call.args.count != 0) {
            return term_raise("term.can expects no arguments");
        }
        return value_bool(term_open_now || term_unusable() == NULL);
    }
    if (CALL_IS(expr, open)) {
        return term_open(expr);
    }
    if (CALL_IS(expr, close)) {
        term_close_now();
        return value_null();
    }
    if (CALL_IS(expr, is_open)) {
        return value_bool(term_open_now);
    }
    if (CALL_IS(expr, size)) {
        if (!term_need_open("term.size")) {
            return value_null();
        }
        term_check_resized();
        return term_size_value();
    }
    if (CALL_IS(expr, colours) || CALL_IS(expr, colors)) {
        if (!term_need_open("term.colors")) {
            return value_null();
        }
        return value_number_exact(term_screen.depth == TERM_DEPTH_24BIT ? 16777216 : term_screen.depth);
    }
    if (CALL_IS(expr, put)) {
        return term_put(expr);
    }
    if (CALL_IS(expr, fill)) {
        return term_fill(expr);
    }
    if (CALL_IS(expr, clear)) {
        return term_clear(expr);
    }
    if (CALL_IS(expr, cursor)) {
        return term_cursor(expr);
    }
    if (CALL_IS(expr, show)) {
        return term_show();
    }
    if (CALL_IS(expr, width)) {
        return term_width_of(expr);
    }
    if (CALL_IS(expr, wait)) {
        return term_wait(expr);
    }
    char message[200];
    module_unknown_verb(message, sizeof message, "term", verb);
    return term_raise(message);
}

/* ---- the event loop --------------------------------------------------------- */

static int term_loop_active(void) {
    return term_open_now && term_events_watched();
}

/* Milliseconds the loop may sleep: shortened while an ESC waits for the rest
 * of its sequence, so the escape key arrives TERM_ESC_WAIT_MS after it was
 * pressed rather than at the next tick. */
static long term_poll_timeout_ms(long tick_ms) {
    if (!term_decoder_pending(&term_decoder)) {
        return tick_ms;
    }
    long left = (long)(term_pending_since + TERM_ESC_WAIT_MS - term_now_ms()) + 1;
    if (left < 0) {
        left = 0;
    }
    return left < tick_ms ? left : tick_ms;
}

static int term_deliver(Value e) {
    Symbol *symbol = env_find_in_frame(&global_env, "term");
    if (!symbol || symbol->value.kind != VALUE_RECORD) {
        value_free(e);
        return 1;
    }
    RecordField *events = record_find(&symbol->value, "events");
    if (!events || events->value->kind != VALUE_ARRAY) {
        value_free(e);
        return 1;
    }
    Value ignored = append_to_array_ref(events->value, e, 0);
    value_free(ignored);
    return watcher_trigger_delivery("term.events", NULL);
}

/* Reads (when the poll said so), then delivers every complete event, each
 * on its own, as stdin.lines delivers lines. 0 when a watcher raised. */
static int term_loop_service(int readable) {
    if (readable && !term_read_input()) {
        /* end of input: the terminal went away (a hangup without SIGHUP) */
        term_close_now();
        return 1;
    }
    if (term_check_resized() && !term_deliver(term_resize_value())) {
        return 0;
    }
    TermEvent ev;
    while (term_open_now && term_next_decoded(&ev)) {
        Value v = term_event_value(&ev);
        term_event_free(&ev);
        if (!term_deliver(v)) {
            return 0;
        }
    }
    return 1;
}

#endif /* GBASIC_TERM */
