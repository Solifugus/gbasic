/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
 *
 * THE `qt` MODULE -- the gBASIC-facing half of the Qt backend. The C++ half is
 * `src/modules/qt_backend.cpp` behind the C seam in `include/qt_backend.h`,
 * and the split is forced rather than chosen: this file is `#include`d into
 * `src/eval.c` the way every other module is, and eval.c is C11.
 *
 * WHAT THIS FILE OWNS: walking the `window` record. A window block binds an
 * introspectable record (docs/qt_gui_design.md §0.2) and a `Value` can only be
 * read here, so the tree walk is on this side and the C++ side takes one
 * widget at a time. The alternative -- handing C++ a `Value` -- would put the
 * record's representation in two languages, and the record is the thing most
 * likely to grow.
 *
 * NO `load` IS REQUIRED, which matches the declaration's own rule: a `window`
 * block implies no import, so neither does rendering one. `timer` and
 * `process` set the precedent for a module with no `load`; and `load qt` on a
 * build without Qt would raise at the IMPORT, taking the whole program with
 * it, which is the shrink run_discovery.sh's note argues against.
 *
 * THE ADDRESS OF A WINDOW IS ITS DECLARATION, not a handle. `qt.open(customer)`
 * takes the record the block bound and `qt.read(customer, "name")` takes the
 * same thing -- exactly as `web.routes(app)` takes a server record. So there is
 * no 28th value kind, which would owe answers for rendering, `encode`,
 * comparison and copying (PLAT-EQ's lesson: a new kind silently opted into the
 * numeric coercion, and was found by ADDING one).
 *
 * WHAT IS DECLARED AND NOT YET ACTED ON, said out loud rather than dropped in
 * silence:
 *
 *   - `type: {USD}` is carried by the declaration and this backend does not
 *     use it. A field is a plain text editor; nothing is damaged, and parsing
 *     through the modifier arrives with the binding increment.
 *   - `show:` / `edit:` CANNOT be acted on from the record at all, and that is
 *     a property of the declaration rather than an omission here: a non-literal
 *     option is recorded as `unknown` by name and not by value (§0.2), because
 *     `show: cust.id` names a place to read in the frame where the subject
 *     lives. So population is the program's: `qt.write(customer, "name",
 *     cust.name)`. The watcher pair that makes `form to edit` two-way is the
 *     next increment.
 */

#define QT_ERROR_CODE 6201

#if HAVE_QT
#include "qt_backend.h"
#endif

static void qt_raise(const char *message) {
    runtime_error_raise(message, QT_ERROR_CODE, "qt");
}

#if HAVE_QT

/* ONE OPEN WINDOW PER DECLARED NAME. A declaration's name is unique in its
 * scope by construction, so it is a key that cannot collide -- which is what
 * lets the record be the address. */
typedef struct {
    char *name;
    void *node;      /* the backend's window node */
} QtOpenWindow;

static QtOpenWindow *qt_windows = NULL;
static size_t qt_window_count = 0;
static int qt_begun = 0;
static int qt_callbacks_set = 0;

static void *qt_window_find(const char *name) {
    for (size_t i = 0; i < qt_window_count; i++) {
        if (strcmp(qt_windows[i].name, name) == 0) {
            return qt_windows[i].node;
        }
    }
    return NULL;
}

static void qt_window_remember(const char *name, void *node) {
    QtOpenWindow *grown = realloc(qt_windows,
                                  sizeof(*qt_windows) * (qt_window_count + 1));
    if (!grown) {
        abort();
    }
    qt_windows = grown;
    qt_windows[qt_window_count].name = copy_string(name);
    qt_windows[qt_window_count].node = node;
    qt_window_count++;
}

/* RE-ENTRY INTO THE EVALUATOR FROM A NATIVE SIGNAL. This is the pattern
 * `gi_signal_marshal` has shipped for GTK, moved to a Qt relay: resolve the
 * handler the declaration registered, call it, and -- the part that matters --
 * CONTAIN A RAISE, because the frame that pumped this handler is Qt's and
 * there is no armed gBASIC frame on the path. Reported rather than swallowed
 * (the old error model reported at raise time, so silence here would be a
 * regression), then the flight is cleared and Qt's loop is asked to stop,
 * which is what `gi_main_loop_quit` does one toolkit over. */
static void qt_invoke_handler(const char *handler) {
    FunctionDef *def = function_resolve(NULL, handler);
    if (!def || !def->stmt) {
        return;   /* the handler function disappeared; nothing to call */
    }
    int saved_stopped = runtime_stopped;
    int saved_line = current_line;
    int saved_column = current_column;
    int before_gen = error_generation;

    Value result = invoke_function(def->stmt, NULL, 0, NULL);

    if (runtime_stopped || error_generation != before_gen) {
        if (raise_in_flight) {
            raise_report_contained();
        }
        runtime_stopped = saved_stopped;
        error_clear_state();
        gb_qt_stop();
    }
    value_free(result);
    current_line = saved_line;
    current_column = saved_column;
}

/* ONE NON-BLOCKING ITERATION OF gBASIC'S OWN LOOP, driven by a Qt timer.
 *
 * Qt owns the wait (§5.1 direction A, measured), so gBASIC's sources cannot
 * have a `poll()` of their own. A Qt timer is serviced inside a NESTED Qt
 * event loop exactly as a QSocketNotifier is, which is the property that makes
 * direction A survive a modal dialog -- and it DRAINS BY CONSTRUCTION, running
 * the real loop body, which is the contract §5.1 had to state after a probe
 * that did not read its pipe fired 545,066 times for one byte.
 *
 * Returns 0 to ask Qt's loop to stop, which is what a raise inside a gBASIC
 * source means. */
static int qt_pump_gbasic(void) {
    if (!webserver_any_active() && !aux_loop_sources_active()) {
        return 1;   /* nothing of ours to service; Qt keeps the window up */
    }
    if (!webserver_run_event_loop_once()) {
        return 0;
    }
    return 1;
}

/* --- walking the declaration -------------------------------------------- */

#define QT_ERRBUF 256

static const char *qt_record_text(Value *record, const char *field) {
    RecordField *f = record_find(record, field);
    if (!f || !f->value || f->value->kind != VALUE_STRING) {
        return NULL;
    }
    return f->value->as.string;
}

/* The options this backend owns because they are NOT widget properties.
 * Everything else goes to the widget's own QMetaObject by name, which is the
 * whole argument for the by-name route (§1): QPushButton alone exposes 75
 * properties, and a table of them would be bindings forever. */
static int qt_structural_option(const char *name) {
    static const char *structural[] = {
        "label", "margin", "spacing", "grow", "show", "edit", "type", NULL
    };
    for (size_t i = 0; structural[i]; i++) {
        if (strcmp(name, structural[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int qt_apply_options(void *node, Value *options, const char *word,
                            int is_container) {
    char err[QT_ERRBUF];
    char message[512];
    if (!options || options->kind != VALUE_RECORD) {
        return 1;
    }
    for (size_t i = 0; i < options->as.record.count; i++) {
        RecordField *f = &options->as.record.fields[i];
        /* A RecordField holds a Value POINTER, not a Value. */
        Value *v = f->value;
        err[0] = '\0';
        if (strcmp(f->name, "label") == 0) {
            continue;   /* consumed at construction: where it goes is the parent's decision */
        }
        if (strcmp(f->name, "show") == 0 || strcmp(f->name, "edit") == 0 ||
            strcmp(f->name, "type") == 0) {
            continue;   /* declared, not acted on -- see the file header */
        }
        if (strcmp(f->name, "margin") == 0 || strcmp(f->name, "spacing") == 0) {
            if (v->kind != VALUE_NUMBER) {
                snprintf(message, sizeof(message),
                         "window: `%s:` on `%s` must be a number", f->name, word);
                qt_raise(message);
                return 0;
            }
            if (!gb_qt_layout_number(node, f->name, (long)v->as.num.value, err,
                                     sizeof(err))) {
                snprintf(message, sizeof(message), "window: %s", err);
                qt_raise(message);
                return 0;
            }
            continue;
        }
        if (strcmp(f->name, "grow") == 0) {
            long weight = 1;
            if (v->kind == VALUE_NUMBER) {
                weight = (long)v->as.num.value;
            } else if (v->kind != VALUE_BOOL) {
                snprintf(message, sizeof(message),
                         "window: `grow:` must be a number or true");
                qt_raise(message);
                return 0;
            }
            if (!gb_qt_grow(node, weight, err, sizeof(err))) {
                snprintf(message, sizeof(message), "window: %s", err);
                qt_raise(message);
                return 0;
            }
            continue;
        }
        if (qt_structural_option(f->name)) {
            continue;
        }
        /* BY NAME, THROUGH QMetaObject. A property the widget does not carry
         * is refused rather than stored as a dynamic one nothing reads --
         * which is an option silently doing nothing, the defect the window
         * block's closed vocabulary exists to prevent one level up. */
        int ok = 0;
        switch (v->kind) {
        case VALUE_STRING:
            ok = gb_qt_set_text(node, f->name, v->as.string, err, sizeof(err));
            break;
        case VALUE_NUMBER:
            ok = gb_qt_set_number(node, f->name, v->as.num.value, err, sizeof(err));
            break;
        case VALUE_BOOL:
            ok = gb_qt_set_bool(node, f->name, v->as.boolean, err, sizeof(err));
            break;
        default:
            snprintf(err, sizeof(err),
                     "`%s:` must be text, a number or true/false", f->name);
            break;
        }
        if (!ok) {
            snprintf(message, sizeof(message), "window: on `%s`, %s",
                     word, err[0] ? err : "that option was refused");
            qt_raise(message);
            return 0;
        }
    }
    (void)is_container;
    return 1;
}

static int qt_build_items(void *parent, Value *items, void *root);

static int qt_build_item(void *parent, Value *item, void *root) {
    char err[QT_ERRBUF];
    char message[512];
    const char *kind = qt_record_text(item, "kind");
    if (!kind) {
        qt_raise("window: this declaration is not a window record");
        return 0;
    }
    if (strcmp(kind, "handler") == 0) {
        return 1;   /* connected in a second pass, once every name exists */
    }
    const char *word = qt_record_text(item, "word");
    const char *name = qt_record_text(item, "name");
    const char *label = qt_record_text(item, "label");
    RecordField *opts = record_find(item, "options");
    err[0] = '\0';

    if (strcmp(kind, "container") == 0) {
        void *node = gb_qt_container(parent, word ? word : "", label, err,
                                     sizeof(err));
        if (!node) {
            snprintf(message, sizeof(message), "window: %s",
                     err[0] ? err : "that container could not be built");
            qt_raise(message);
            return 0;
        }
        if (!qt_apply_options(node, opts ? opts->value : NULL,
                              word ? word : "", 1)) {
            return 0;
        }
        RecordField *entries = record_find(item, "items");
        if (entries && entries->value && entries->value->kind == VALUE_ARRAY) {
            return qt_build_items(node, entries->value, root);
        }
        return 1;
    }

    void *node = gb_qt_part(parent, word ? word : "", name, label, err,
                            sizeof(err));
    if (!node) {
        snprintf(message, sizeof(message), "window: %s",
                 err[0] ? err : "that part could not be built");
        qt_raise(message);
        return 0;
    }
    return qt_apply_options(node, opts ? opts->value : NULL, word ? word : "", 0);
}

static int qt_build_items(void *parent, Value *items, void *root) {
    size_t count = items->as.array.store ? items->as.array.store->count : 0;
    for (size_t i = 0; i < count; i++) {
        Value *entry = &items->as.array.store->items[i];
        if (entry->kind != VALUE_RECORD) {
            qt_raise("window: this declaration is not a window record");
            return 0;
        }
        if (!qt_build_item(parent, entry, root)) {
            return 0;
        }
    }
    return 1;
}

/* HANDLERS IN A SECOND PASS, because `on save.clicked` may be written above
 * the `save` it names -- the declaration is a declaration, so source order is
 * not build order. The load-time pass already refused a target that does not
 * exist anywhere in the window (§0.2), so a failure here means the record and
 * the build disagree rather than that the author made a mistake. */
static int qt_connect_items(void *root, Value *items) {
    char err[QT_ERRBUF];
    char message[512];
    size_t count = items->as.array.store ? items->as.array.store->count : 0;
    for (size_t i = 0; i < count; i++) {
        Value *entry = &items->as.array.store->items[i];
        if (entry->kind != VALUE_RECORD) {
            continue;
        }
        const char *kind = qt_record_text(entry, "kind");
        if (kind && strcmp(kind, "handler") == 0) {
            const char *target = qt_record_text(entry, "target");
            const char *signal = qt_record_text(entry, "signal");
            RecordField *hf = record_find(entry, "handler");
            if (!target || !signal || !hf || !hf->value || hf->value->kind != VALUE_FUNCTION) {
                qt_raise("window: this handler carries no function to call");
                return 0;
            }
            err[0] = '\0';
            if (!gb_qt_connect(root, target, signal,
                               hf->value->as.function.name, err, sizeof(err))) {
                snprintf(message, sizeof(message), "window: %s",
                         err[0] ? err : "that handler could not be connected");
                qt_raise(message);
                return 0;
            }
            continue;
        }
        RecordField *entries = record_find(entry, "items");
        if (entries && entries->value && entries->value->kind == VALUE_ARRAY) {
            if (!qt_connect_items(root, entries->value)) {
                return 0;
            }
        }
    }
    return 1;
}

#endif /* HAVE_QT */

/* TEARDOWN, which is a requirement rather than tidiness. run_repl.sh's note
 * says why in as many words: the session teardown is EXTRACTED so a module
 * cannot be forgotten, and a forgotten one does not leak -- it leaves
 * something BOUND after the session ended. Here that something is a live
 * QApplication and an open window, which would outlive the program that asked
 * for it and meet the next `qt.open` in the same process as a second
 * QApplication, which Qt does not allow. Called from runtime_teardown beside
 * http_shutdown and timer_shutdown. */
static void qt_shutdown(void) {
#if HAVE_QT
    for (size_t i = 0; i < qt_window_count; i++) {
        free(qt_windows[i].name);
    }
    free(qt_windows);
    qt_windows = NULL;
    qt_window_count = 0;
    if (qt_begun) {
        gb_qt_end();
        qt_begun = 0;
    }
    qt_callbacks_set = 0;
#endif
}

#if HAVE_QT

static int qt_window_record(Value *v, const char *verb) {
    char message[256];
    if (v->kind != VALUE_RECORD) {
        snprintf(message, sizeof(message),
                 "qt.%s expects a window declaration", verb);
        qt_raise(message);
        return 0;
    }
    const char *kind = qt_record_text(v, "kind");
    if (!kind || strcmp(kind, "window") != 0) {
        snprintf(message, sizeof(message),
                 "qt.%s expects a window declaration", verb);
        qt_raise(message);
        return 0;
    }
    return 1;
}

#endif /* HAVE_QT */

static Value qt_eval_call(AstExpr *expr) {
    const char *verb = expr->as.call.name;
    size_t argc = expr->as.call.args.count;
    char message[512];

#if !HAVE_QT
    (void)argc;
    /* `qt.can_display` answers rather than raising, deliberately and for the
     * same reason `has_builtin` exists: a program asking whether it may open a
     * window is DEGRADING GRACEFULLY, and a build with no Qt is one of the
     * answers to that question. Every other verb refuses in the convention's
     * own words. */
    if (strcmp(verb, "can_display") == 0 && argc == 0) {
        return value_bool(0);
    }
    snprintf(message, sizeof(message),
             "Qt support is not available in this build");
    qt_raise(message);
    return value_null();
#else
    char err[QT_ERRBUF];

    if (strcmp(verb, "can_display") == 0) {
        if (argc != 0) {
            qt_raise("qt.can_display takes no arguments");
            return value_null();
        }
        return value_bool(gb_qt_can_display() ? 1 : 0);
    }

    if (strcmp(verb, "open") == 0) {
        if (argc != 1) {
            qt_raise("qt.open expects one window declaration");
            return value_null();
        }
        Value win = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(win);
            return value_null();
        }
        if (!qt_window_record(&win, "open")) {
            value_free(win);
            return value_null();
        }
        const char *name = qt_record_text(&win, "name");
        if (name && qt_window_find(name)) {
            snprintf(message, sizeof(message),
                     "window `%s` is already open", name);
            qt_raise(message);
            value_free(win);
            return value_null();
        }
        /* THE REFUSAL, BEFORE ANYTHING IS CONSTRUCTED (§0.3). With no display
         * Qt calls `qFatal`, which aborts uncatchably and takes
         * block-buffered stdout with it, so a program printing its progress
         * prints nothing and the failure reads as line 1. The sentence names
         * what is MISSING -- a display -- rather than reporting a Qt plugin
         * failure, which names the one thing the author cannot fix. */
        if (!gb_qt_can_display()) {
            snprintf(message, sizeof(message),
                     "no display is available, so window `%s` cannot be "
                     "opened. Set QT_QPA_PLATFORM=offscreen to render without "
                     "one", name ? name : "");
            qt_raise(message);
            value_free(win);
            return value_null();
        }
        if (!qt_begun) {
            err[0] = '\0';
            if (!gb_qt_begin(err, sizeof(err))) {
                snprintf(message, sizeof(message), "qt.open: %s",
                         err[0] ? err : "Qt could not start");
                qt_raise(message);
                value_free(win);
                return value_null();
            }
            qt_begun = 1;
        }
        if (!qt_callbacks_set) {
            gb_qt_set_callbacks(qt_invoke_handler, qt_pump_gbasic);
            qt_callbacks_set = 1;
        }

        RecordField *of = record_find(&win, "options");
        const char *title = NULL;
        long width = 0, height = 0;
        if (of && of->value && of->value->kind == VALUE_RECORD) {
            title = qt_record_text(of->value, "title");
            RecordField *wf = record_find(of->value, "width");
            RecordField *hf = record_find(of->value, "height");
            if (wf && wf->value && wf->value->kind == VALUE_NUMBER) {
                width = (long)wf->value->as.num.value;
            }
            if (hf && hf->value && hf->value->kind == VALUE_NUMBER) {
                height = (long)hf->value->as.num.value;
            }
        }
        void *root = gb_qt_window(title ? title : (name ? name : ""),
                                  (int)width, (int)height);
        RecordField *items = record_find(&win, "items");
        if (items && items->value && items->value->kind == VALUE_ARRAY) {
            if (!qt_build_items(root, items->value, root) ||
                !qt_connect_items(root, items->value)) {
                value_free(win);
                return value_null();
            }
        }
        gb_qt_show(root);
        if (name) {
            qt_window_remember(name, root);
        }
        value_free(win);
        return value_bool(1);
    }

    if (strcmp(verb, "read") == 0 || strcmp(verb, "write") == 0) {
        int writing = strcmp(verb, "write") == 0;
        if (argc != (writing ? 3u : 2u)) {
            snprintf(message, sizeof(message),
                     writing ? "qt.write expects a window, a name and a value"
                             : "qt.read expects a window and a name");
            qt_raise(message);
            return value_null();
        }
        Value win = eval_expr(expr->as.call.args.items[0]);
        Value which = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending()) {
            value_free(win);
            value_free(which);
            return value_null();
        }
        if (!qt_window_record(&win, verb) || which.kind != VALUE_STRING) {
            if (which.kind != VALUE_STRING && !error_action_pending()) {
                snprintf(message, sizeof(message),
                         "qt.%s expects the part's name as text", verb);
                qt_raise(message);
            }
            value_free(win);
            value_free(which);
            return value_null();
        }
        const char *wname = qt_record_text(&win, "name");
        void *root = wname ? qt_window_find(wname) : NULL;
        if (!root) {
            snprintf(message, sizeof(message),
                     "window `%s` is not open", wname ? wname : "");
            qt_raise(message);
            value_free(win);
            value_free(which);
            return value_null();
        }
        if (!writing) {
            char *text = gb_qt_read(root, which.as.string);
            value_free(win);
            value_free(which);
            if (!text) {
                snprintf(message, sizeof(message),
                         "this window has no part named `%s`", which.as.string);
                qt_raise(message);
                return value_null();
            }
            Value out = value_string(text);
            free(text);
            return out;
        }
        Value v = eval_expr(expr->as.call.args.items[2]);
        if (error_action_pending()) {
            value_free(win);
            value_free(which);
            value_free(v);
            return value_null();
        }
        /* RENDERED THROUGH THE ONE RENDERER (PLAT-RENDER), so a date, money or
         * a duration reaches a widget in the words the language itself uses --
         * rather than this module growing a second opinion about how a value
         * looks. */
        /* builtin_string_value CONSUMES ITS ARGUMENT -- it returns the value
         * itself for a string and frees it on every other branch -- so `v`
         * must not be freed here. Found the direct way: freeing both gave a
         * double free that valgrind named and an ordinary run reported as
         * `malloc(): unaligned tcache chunk detected`, which says nothing
         * about where it happened. */
        Value text = builtin_string_value(v);
        int ok = 0;
        err[0] = '\0';
        if (text.kind == VALUE_STRING) {
            ok = gb_qt_write(root, which.as.string, text.as.string, err,
                             sizeof(err));
        }
        value_free(text);
        if (!ok) {
            snprintf(message, sizeof(message), "qt.write: %s",
                     err[0] ? err : "that value could not be shown");
            qt_raise(message);
            value_free(win);
            value_free(which);
            return value_null();
        }
        value_free(win);
        value_free(which);
        return value_bool(1);
    }

    if (strcmp(verb, "invoke") == 0) {
        if (argc != 3) {
            qt_raise("qt.invoke expects a window, a part's name and a method");
            return value_null();
        }
        Value win = eval_expr(expr->as.call.args.items[0]);
        Value which = eval_expr(expr->as.call.args.items[1]);
        Value method = eval_expr(expr->as.call.args.items[2]);
        if (error_action_pending()) {
            value_free(win);
            value_free(which);
            value_free(method);
            return value_null();
        }
        if (!qt_window_record(&win, "invoke") ||
            which.kind != VALUE_STRING || method.kind != VALUE_STRING) {
            if (!error_action_pending()) {
                qt_raise("qt.invoke expects the part's name and the method as text");
            }
            value_free(win);
            value_free(which);
            value_free(method);
            return value_null();
        }
        const char *wname = qt_record_text(&win, "name");
        void *root = wname ? qt_window_find(wname) : NULL;
        if (!root) {
            snprintf(message, sizeof(message), "window `%s` is not open",
                     wname ? wname : "");
            qt_raise(message);
            value_free(win);
            value_free(which);
            value_free(method);
            return value_null();
        }
        err[0] = '\0';
        int ok = gb_qt_invoke(root, which.as.string, method.as.string, err,
                              sizeof(err));
        value_free(win);
        value_free(which);
        value_free(method);
        if (!ok) {
            snprintf(message, sizeof(message), "qt.invoke: %s",
                     err[0] ? err : "that method could not be invoked");
            qt_raise(message);
            return value_null();
        }
        return value_bool(1);
    }

    if (strcmp(verb, "run") == 0) {
        if (argc != 0) {
            qt_raise("qt.run takes no arguments");
            return value_null();
        }
        if (!qt_begun) {
            qt_raise("qt.run has no window to run: call qt.open first");
            return value_null();
        }
        /* 50 ms is gBASIC'S OWN TICK, not a number chosen here:
         * webserver_run_event_loop polls with a 50 ms timeout, so a source
         * serviced on this timer is serviced exactly as often as it would have
         * been without Qt in the picture. */
        int rc = gb_qt_run(50);
        if (error_action_pending()) {
            return value_null();
        }
        return value_number((double)rc);
    }

    if (strcmp(verb, "stop") == 0) {
        if (argc != 0) {
            qt_raise("qt.stop takes no arguments");
            return value_null();
        }
        gb_qt_stop();
        return value_bool(1);
    }

    module_unknown_verb(message, sizeof(message), "qt", verb);
    qt_raise(message);
    return value_null();
#endif
}
