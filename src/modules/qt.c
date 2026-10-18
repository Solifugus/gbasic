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
/* WHAT A FORM CAPTURED WHEN THE WINDOW OPENED. The declaration names the
 * subject (`form to edit cust` records subject "cust"), and its VALUE is read
 * from the frame that called `qt.open` -- which is what §0.2 means by "a place
 * to read in the frame where the subject lives", that frame being exactly the
 * one doing the asking.
 *
 * THE RECORD IS KEPT RATHER THAN RE-READ, and that is forced rather than
 * preferred. §2.2 proposed "a watcher per field in each direction"; the first
 * direction works that way and THE SECOND CANNOT, because a widget's signal
 * fires inside QApplication::exec() beneath `qt.run()`, so the frame current
 * at signal time is whichever one called `qt.run` and not reliably the one
 * that called `qt.open`. There is no frame to write back into.
 *
 * What replaces it is smaller than a watcher, and it is the language's own
 * rule: a record is a VALUE, so `qt.subject` reads the widgets AT THE MOMENT
 * IT IS ASKED and folds them into this capture. Nothing has to be tracked
 * between the two, which also means no change signal can be missed. */
typedef struct {
    char *subject;
    char *direction;   /* "show" or "edit" */
    Value record;      /* the value read at open */
} QtBinding;

typedef struct {
    char *name;
    void *node;      /* the backend's window node */
    QtBinding *bindings;
    size_t binding_count;
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
    qt_windows[qt_window_count].bindings = NULL;
    qt_windows[qt_window_count].binding_count = 0;
    qt_window_count++;
}

/* UNREMEMBER A WINDOW whose open did not finish. Binding needs the registry
 * entry (that is where a form's capture lives), so the entry is made before
 * the last step that can fail -- and a declaration left registered after a
 * failed open would meet the author's retry with "already open", which is a
 * second message about the first mistake. */
static void qt_window_forget(const char *name) {
    for (size_t i = 0; i < qt_window_count; i++) {
        if (strcmp(qt_windows[i].name, name) != 0) {
            continue;
        }
        free(qt_windows[i].name);
        for (size_t b = 0; b < qt_windows[i].binding_count; b++) {
            free(qt_windows[i].bindings[b].subject);
            free(qt_windows[i].bindings[b].direction);
            value_free(qt_windows[i].bindings[b].record);
        }
        free(qt_windows[i].bindings);
        for (size_t j = i + 1; j < qt_window_count; j++) {
            qt_windows[j - 1] = qt_windows[j];
        }
        qt_window_count--;
        return;
    }
}

static QtOpenWindow *qt_window_entry(const char *name) {
    for (size_t i = 0; i < qt_window_count; i++) {
        if (strcmp(qt_windows[i].name, name) == 0) {
            return &qt_windows[i];
        }
    }
    return NULL;
}

/* WHICH WORDS BIND. `field` and `check` carry a value a person can change;
 * `button` and `stretch` do not, so a button named `save` inside a form is not
 * a binding that failed to find a field -- it is a button. Without this
 * distinction the refusal below would fire on the commonest form there is. */
static int qt_bindable_word(const char *word) {
    return word && (strcmp(word, "field") == 0 || strcmp(word, "check") == 0);
}

/* THE SUBJECT, WITHOUT RAISING. `env_get` raises `undefined variable: cust`,
 * which is true and says nothing about the form that wanted it -- so the walk
 * is done here and the message names the declaration. */
static Value *qt_subject_slot(const char *name) {
    for (Env *env = current_env; env; env = env->parent) {
        Symbol *symbol = env_find_in_frame(env, name);
        if (symbol) {
            return &symbol->value;
        }
    }
    return NULL;
}

/* A FIELD'S TYPE IS gBASIC'S OWN MODIFIER (§2.3), recorded by the declaration
 * as the modifier's NAME -- `type: {USD}` arrives as the text "USD" -- so
 * applying it is one call into the machinery an assignment clause already
 * uses. Nothing here reimplements parsing or validation, which is the whole
 * argument for `type:` being a modifier rather than a string. */
static Value qt_apply_type(const char *modifier_name, Value value) {
    AstModifierUse use;
    memset(&use, 0, sizeof(use));
    use.name = (char *)modifier_name;
    return apply_assignment_modifier(use, value);
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

/* A MENU IS BUILT BY ITS OWN WALK, because it is not laid out: its children are
 * actions and submenus rather than widgets in a box, and handing them to
 * `gb_qt_part` -- which maps a word to a widget -- has nothing to map. A
 * submenu recurses here, which is why `menu "Recent"` inside `menu "File"`
 * needed no vocabulary of its own. */
static int qt_build_menu(void *root, void *parent_menu, Value *menu) {
    char err[QT_ERRBUF];
    char message[512];
    const char *title = qt_record_text(menu, "label");
    err[0] = '\0';
    void *node = gb_qt_menu(root, parent_menu, title ? title : "", err,
                            sizeof(err));
    if (!node) {
        snprintf(message, sizeof(message), "window: %s",
                 err[0] ? err : "that menu could not be built");
        qt_raise(message);
        return 0;
    }
    RecordField *entries = record_find(menu, "items");
    if (!entries || !entries->value || entries->value->kind != VALUE_ARRAY) {
        return 1;
    }
    size_t count = entries->value->as.array.store
                       ? entries->value->as.array.store->count : 0;
    for (size_t i = 0; i < count; i++) {
        Value *part = &entries->value->as.array.store->items[i];
        if (part->kind != VALUE_RECORD) {
            continue;
        }
        const char *kind = qt_record_text(part, "kind");
        const char *word = qt_record_text(part, "word");
        if (kind && strcmp(kind, "handler") == 0) {
            continue;   /* connected in the second pass, like every other */
        }
        if (!word) {
            continue;
        }
        if (strcmp(word, "menu") == 0) {
            if (!qt_build_menu(root, node, part)) {
                return 0;
            }
            continue;
        }
        if (strcmp(word, "separator") == 0) {
            err[0] = '\0';
            if (!gb_qt_menu_separator(node, err, sizeof(err))) {
                snprintf(message, sizeof(message), "window: %s",
                         err[0] ? err : "that separator could not be added");
                qt_raise(message);
                return 0;
            }
            continue;
        }
        /* AN ITEM. Its `label:` is the text a person reads, consumed at
         * construction like a button's; everything else -- `shortcut:`,
         * `enabled:`, `checkable:` -- goes to the QAction's own QMetaObject by
         * name, which is the by-name route reaching a class nothing
         * anticipated. */
        const char *name = qt_record_text(part, "name");
        RecordField *opts = record_find(part, "options");
        const char *label = NULL;
        if (opts && opts->value && opts->value->kind == VALUE_RECORD) {
            label = qt_record_text(opts->value, "label");
        }
        err[0] = '\0';
        void *act = gb_qt_menu_item(root, node, name ? name : "",
                                    label ? label : "", err, sizeof(err));
        if (!act) {
            snprintf(message, sizeof(message), "window: %s",
                     err[0] ? err : "that item could not be added");
            qt_raise(message);
            return 0;
        }
        if (!qt_apply_options(act, opts ? opts->value : NULL, "item", 0)) {
            return 0;
        }
    }
    return 1;
}

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

    if (strcmp(kind, "container") == 0 && word && strcmp(word, "menu") == 0) {
        /* THE ROOT, NOT THE PARENT, because a menu bar belongs to the window
         * and not to whatever container the declaration happens to sit in --
         * the load-time pass already refused a menu anywhere a layout would
         * have placed it. */
        return qt_build_menu(root, NULL, item);
    }

    if (strcmp(kind, "container") == 0 && word && strcmp(word, "table") == 0) {
        /* A TABLE'S PARTS ARE COLUMNS, so it is built from its WHOLE field
         * list in one call and its children are NOT walked as widgets -- each
         * would otherwise become a QLineEdit standing in for a heading. The
         * caption is the column's `label:`, falling back to its name, which is
         * the same rule a form's row label follows. */
        RecordField *cols = record_find(item, "items");
        size_t ncols = (cols && cols->value && cols->value->kind == VALUE_ARRAY &&
                        cols->value->as.array.store)
                           ? cols->value->as.array.store->count : 0;
        const char **headers = ncols ? calloc(ncols, sizeof(char *)) : NULL;
        if (ncols && !headers) {
            abort();
        }
        size_t at = 0;
        for (size_t i = 0; i < ncols; i++) {
            Value *col = &cols->value->as.array.store->items[i];
            if (col->kind != VALUE_RECORD) {
                continue;
            }
            /* THE CAPTION IS IN `options`, which is where a LEAF's
             * `label:` lives -- the top-level `label` field carries the
             * `group "Credit"` string form instead. Reading the wrong one
             * fell back to the field's NAME and produced a table captioned
             * `placed`/`who`/`total` rather than `Date`/`Customer`/`Total`:
             * a perfectly plausible header row, and every other check in the
             * fixture passed. The mapping oracle caught it on its first run,
             * which is the whole reason that tier exists. */
            RecordField *copts = record_find(col, "options");
            const char *cap = NULL;
            if (copts && copts->value && copts->value->kind == VALUE_RECORD) {
                cap = qt_record_text(copts->value, "label");
            }
            if (!cap) {
                cap = qt_record_text(col, "label");
            }
            if (!cap) {
                cap = qt_record_text(col, "name");
            }
            headers[at++] = cap ? cap : "";
        }
        void *tnode = gb_qt_table(parent, name ? name : "", headers, (int)at,
                                  err, sizeof(err));
        free((void *)headers);
        if (!tnode) {
            snprintf(message, sizeof(message), "window: %s",
                     err[0] ? err : "that table could not be built");
            qt_raise(message);
            return 0;
        }
        return qt_apply_options(tnode, opts ? opts->value : NULL, "table", 1);
    }

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

/* BINDING IN A PASS OF ITS OWN, after the whole tree exists. Not for symmetry
 * with the handler pass: a form must be able to populate its parts, and the
 * build walk hands one widget at a time to the backend with no place to carry
 * a record down -- so threading the subject through `qt_build_items` would
 * change a signature every container shares in order to serve the one kind
 * that has a subject.
 *
 * ONLY A FORM'S DIRECT CHILDREN BIND. A `form` may hold a `row` holding
 * fields, and binding those would make the record's shape depend on the
 * LAYOUT -- so a nested container is walked for a form of its OWN and
 * otherwise left alone. That is a stated limit rather than a gap: the fix, if
 * a real case wants it, is for the inner container to say whose subject it
 * serves, which is what `form to edit` already spells. */
static int qt_bind_form(void *root, Value *form, const char *window_name) {
    char err[QT_ERRBUF];
    char message[512];
    const char *subject = qt_record_text(form, "subject");
    const char *direction = qt_record_text(form, "direction");
    if (!subject || !direction) {
        return 1;
    }
    Value *slot = qt_subject_slot(subject);
    if (!slot) {
        snprintf(message, sizeof(message),
                 "window `%s`: `form to %s %s` found no `%s` -- qt.open reads "
                 "the subject from the frame that calls it",
                 window_name, direction, subject, subject);
        qt_raise(message);
        return 0;
    }
    if (slot->kind != VALUE_RECORD) {
        snprintf(message, sizeof(message),
                 "window `%s`: `form to %s %s` needs a record, and `%s` holds "
                 "%s", window_name, direction, subject, subject,
                 value_kind_name(slot->kind));
        qt_raise(message);
        return 0;
    }
    QtOpenWindow *entry = qt_window_entry(window_name);
    /* ONE CAPTURE PER SUBJECT, not per form. Two forms may edit one record --
     * the fixture's own `cust` is split across a column and a group, which is
     * how a long form is laid out -- and a second capture of the same variable
     * would be a second answer to `qt.subject`. The FOLD still visits every
     * form carrying that subject, so a record spread over several groups is
     * gathered back whole. */
    int already = 0;
    for (size_t i = 0; entry && i < entry->binding_count; i++) {
        if (strcmp(entry->bindings[i].subject, subject) == 0) {
            already = 1;
            break;
        }
    }
    if (entry && !already) {
        QtBinding *grown = realloc(entry->bindings,
                                   sizeof(*entry->bindings) *
                                       (entry->binding_count + 1));
        if (!grown) {
            abort();
        }
        entry->bindings = grown;
        entry->bindings[entry->binding_count].subject = copy_string(subject);
        entry->bindings[entry->binding_count].direction = copy_string(direction);
        entry->bindings[entry->binding_count].record = value_copy(*slot);
        entry->binding_count++;
    }
    RecordField *entries = record_find(form, "items");
    if (!entries || !entries->value || entries->value->kind != VALUE_ARRAY) {
        return 1;
    }
    size_t count = entries->value->as.array.store
                       ? entries->value->as.array.store->count : 0;
    for (size_t i = 0; i < count; i++) {
        Value *part = &entries->value->as.array.store->items[i];
        if (part->kind != VALUE_RECORD) {
            continue;
        }
        const char *word = qt_record_text(part, "word");
        const char *name = qt_record_text(part, "name");
        if (!qt_bindable_word(word) || !name) {
            continue;
        }
        /* A `show` FORM'S PARTS TAKE NO TYPING. Done before the value is
         * written, so even a part with nothing to show is still display only
         * -- otherwise an absent field would be the one place a one-way form
         * silently accepted an edit. */
        if (strcmp(direction, "show") == 0) {
            err[0] = '\0';
            if (!gb_qt_set_display_only(root, name, 1, err, sizeof(err))) {
                snprintf(message, sizeof(message), "window `%s`: %s",
                         window_name, err[0] ? err : "that part could not be "
                                                      "made display only");
                qt_raise(message);
                return 0;
            }
        }
        RecordField *field = record_find(slot, name);
        if (!field || !field->value) {
            /* AN ABSENT FIELD LEAVES THE PART EMPTY AND DOES NOT RAISE,
             * because for `to edit` the form's job is to PRODUCE the record: a
             * field absent at open is the ordinary state of a form being
             * filled in, and the fold adds it, so by the time anybody reads
             * the record it is there. A "new customer" form has every field
             * absent until somebody types.
             *
             * NOT because it matches a SCALAR read -- a claim written here
             * first and MEASURED FALSE: `r.missing` on a record RAISES
             * `unknown record field`. What it does match is a PROJECTION,
             * where `rows.amount` answers `unknown` for a row that lacks the
             * field, for the reason stated there: raising would make it
             * unusable on real business data, which legitimately has holes.
             * A form populating a dozen fields is that situation and not the
             * scalar one. (The same false sentence was in `eval.c`'s own
             * projection comment and in the reference; both corrected, and
             * run_projection.sh now asserts the difference.)
             *
             * THE COST IS SHARPER THAN "NOT CAUGHT" AND IS PINNED BY A CHECK
             * IN THE FIXTURE: a MISSPELLED part name leaves the intended field
             * holding its CAPTURED value and adds a second field named after
             * the typo, so typing into that part changes nothing the author
             * meant and nothing says so -- the same silent-discard class that
             * `to show`'s read-only exists to prevent, arriving by another
             * door. The refusal that would catch it is the one that breaks the
             * new-record case, so what it wants is an opt-in from the author
             * (a form declaring its subject's fields fixed), not a rule. */
            continue;
        }
        /* THROUGH THE ONE RENDERER (PLAT-RENDER), so money, a date and a
         * duration reach the widget in the words the language itself uses.
         * `builtin_string_value` CONSUMES its argument, so the copy handed to
         * it is not freed here. */
        Value text = builtin_string_value(value_copy(*field->value));
        if (text.kind != VALUE_STRING) {
            value_free(text);
            continue;
        }
        err[0] = '\0';
        int ok = gb_qt_write(root, name, text.as.string, err, sizeof(err));
        value_free(text);
        if (!ok) {
            snprintf(message, sizeof(message),
                     "window `%s`: `%s` could not be shown -- %s", window_name,
                     name, err[0] ? err : "the part refused that value");
            qt_raise(message);
            return 0;
        }
    }
    return 1;
}

/* WHICH COLUMN A FIELD NAME IS, read from the declaration. A cell is addressed
 * by the name the AUTHOR gave the column rather than by its position, because
 * a position is the one thing about a table that changes when somebody
 * reorders the declaration -- and a silently shifted index reads a real value
 * from the wrong column. Returns -1 when the table declares no such field. */
static int qt_table_column(Value *table, const char *field) {
    RecordField *cols = record_find(table, "items");
    if (!cols || !cols->value || cols->value->kind != VALUE_ARRAY ||
        !cols->value->as.array.store) {
        return -1;
    }
    size_t n = cols->value->as.array.store->count;
    for (size_t i = 0; i < n; i++) {
        Value *col = &cols->value->as.array.store->items[i];
        if (col->kind != VALUE_RECORD) {
            continue;
        }
        const char *name = qt_record_text(col, "name");
        if (name && strcmp(name, field) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* THE TABLE A NAME REFERS TO, found in the declaration. The backend knows the
 * widget by name; what only the declaration holds is the COLUMN LIST, which is
 * what a cell's field name has to be resolved against. */
static Value *qt_find_table(Value *items, const char *name) {
    size_t count = items->as.array.store ? items->as.array.store->count : 0;
    for (size_t i = 0; i < count; i++) {
        Value *entry = &items->as.array.store->items[i];
        if (entry->kind != VALUE_RECORD) {
            continue;
        }
        const char *kind = qt_record_text(entry, "kind");
        if (!kind || strcmp(kind, "container") != 0) {
            continue;
        }
        const char *word = qt_record_text(entry, "word");
        const char *has = qt_record_text(entry, "name");
        if (word && strcmp(word, "table") == 0 && has && strcmp(has, name) == 0) {
            return entry;
        }
        RecordField *nested = record_find(entry, "items");
        if (nested && nested->value && nested->value->kind == VALUE_ARRAY) {
            Value *found = qt_find_table(nested->value, name);
            if (found) {
                return found;
            }
        }
    }
    return NULL;
}

/* A TABLE FOLDS BACK DIFFERENTLY FROM A FORM (a list of rows, not one record),
 * and the walk that dispatches between them sits above both. */
static int qt_fold_table(void *root, Value *table, Value *out,
                         const char *window_name);

/* READ THE WIDGETS BACK INTO THE RECORD. Walks the same shape the bind pass
 * walks and for the same reason: only a form's DIRECT children bind, so only
 * they fold back, and the record's shape therefore does not depend on the
 * layout.
 *
 * WHAT DECIDES THE KIND, in order: a declared `type:` (the author said so), a
 * `check` (a box is boolean whatever the record held), then the CAPTURED
 * field's own kind, then text. Without that last chain `{ id: 42 }` comes back
 * as `{ id: "42" }` and every comparison against it changes answer. */
static int qt_fold_form(void *root, Value *form, Value *out,
                        const char *window_name) {
    char message[512];
    RecordField *entries = record_find(form, "items");
    if (!entries || !entries->value || entries->value->kind != VALUE_ARRAY) {
        return 1;
    }
    size_t count = entries->value->as.array.store
                       ? entries->value->as.array.store->count : 0;
    for (size_t i = 0; i < count; i++) {
        Value *part = &entries->value->as.array.store->items[i];
        if (part->kind != VALUE_RECORD) {
            continue;
        }
        const char *word = qt_record_text(part, "word");
        const char *name = qt_record_text(part, "name");
        if (!qt_bindable_word(word) || !name) {
            continue;
        }
        char *text = gb_qt_read(root, name);
        if (!text) {
            continue;
        }
        RecordField *opts = record_find(part, "options");
        const char *declared = NULL;
        if (opts && opts->value && opts->value->kind == VALUE_RECORD) {
            declared = qt_record_text(opts->value, "type");
        }
        RecordField *was = record_find(out, name);
        Value folded;
        if (strcmp(word, "check") == 0) {
            folded = value_bool(strcmp(text, "true") == 0);
        } else if (declared) {
            folded = qt_apply_type(declared, value_string(text));
        } else if (was && was->value && was->value->kind == VALUE_NUMBER) {
            folded = qt_apply_type("number", value_string(text));
        } else {
            folded = value_string(text);
        }
        free(text);
        /* A `type:` OR A NUMERIC FIELD CAN REFUSE WHAT WAS TYPED -- "abc" is
         * not money -- and the modifier's own sentence is the CAUSE, so it
         * propagates untouched: PLAT-ERR's first-raise-wins rule exists
         * because replacing a cause with a symptom is how a diagnostic comes
         * to name the one thing that is not at fault.
         *
         * WHAT IS MISSING IS STATED RATHER THAN GUESSED AT: the message does
         * not name the PART, so with several `type:` fields in one form the
         * author is told `money text is not a number` and must work out which.
         * Naming it means ADDING CONTEXT to a raise already in flight, which
         * no module here does and which touches `raise_in_flight` -- and
         * getting that wrong makes an error VANISH, the one failure the whole
         * error model is built to prevent. So it waits for a helper of its own
         * with its own tier, rather than being improvised here. */
        if (error_action_pending()) {
            value_free(folded);
            return 0;
        }
        if (folded.kind == VALUE_NULL) {
            snprintf(message, sizeof(message),
                     "window `%s`: `%s` does not hold a %s", window_name, name,
                     declared ? declared : "number");
            qt_raise(message);
            value_free(folded);
            return 0;
        }
        record_set(out, name, folded);
    }
    return 1;
}

static int qt_fold_items(void *root, Value *items, const char *subject,
                         Value *out, const char *window_name) {
    size_t count = items->as.array.store ? items->as.array.store->count : 0;
    for (size_t i = 0; i < count; i++) {
        Value *entry = &items->as.array.store->items[i];
        if (entry->kind != VALUE_RECORD) {
            continue;
        }
        const char *kind = qt_record_text(entry, "kind");
        if (!kind || strcmp(kind, "container") != 0) {
            continue;
        }
        const char *has = qt_record_text(entry, "subject");
        if (has) {
            if (strcmp(has, subject) == 0) {
                const char *word = qt_record_text(entry, "word");
                int ok = (word && strcmp(word, "table") == 0)
                             ? qt_fold_table(root, entry, out, window_name)
                             : qt_fold_form(root, entry, out, window_name);
                if (!ok) {
                    return 0;
                }
            }
            continue;
        }
        RecordField *nested = record_find(entry, "items");
        if (nested && nested->value && nested->value->kind == VALUE_ARRAY) {
            if (!qt_fold_items(root, nested->value, subject, out,
                               window_name)) {
                return 0;
            }
        }
    }
    return 1;
}

/* A TABLE'S SUBJECT IS A LIST, which is the one structural difference from a
 * form: a form shows ONE record and a table shows MANY, so the capture is an
 * array and each `field` is a COLUMN read from every element rather than a
 * widget read once.
 *
 * AN ELEMENT THAT IS NOT A RECORD IS REFUSED AND NAMED, matching what a
 * projection does for the same mistake (`rows.amount` over a list someone
 * built from numbers says which element broke it) -- a table of numbers would
 * otherwise render as a grid of blanks, which looks like no data. */
static int qt_bind_table(void *root, Value *table, const char *window_name) {
    char err[QT_ERRBUF];
    char message[512];
    const char *name = qt_record_text(table, "name");
    const char *subject = qt_record_text(table, "subject");
    const char *direction = qt_record_text(table, "direction");
    if (!name || !subject || !direction) {
        return 1;
    }
    Value *slot = qt_subject_slot(subject);
    if (!slot) {
        snprintf(message, sizeof(message),
                 "window `%s`: `table %s to %s %s` found no `%s` -- qt.open "
                 "reads the subject from the frame that calls it",
                 window_name, name, direction, subject, subject);
        qt_raise(message);
        return 0;
    }
    if (slot->kind != VALUE_ARRAY) {
        snprintf(message, sizeof(message),
                 "window `%s`: `table %s to %s %s` needs a list of records, and "
                 "`%s` holds %s", window_name, name, direction, subject,
                 subject, value_kind_name(slot->kind));
        qt_raise(message);
        return 0;
    }
    QtOpenWindow *entry = qt_window_entry(window_name);
    int already = 0;
    for (size_t i = 0; entry && i < entry->binding_count; i++) {
        if (strcmp(entry->bindings[i].subject, subject) == 0) {
            already = 1;
            break;
        }
    }
    if (entry && !already) {
        QtBinding *grown = realloc(entry->bindings,
                                   sizeof(*entry->bindings) *
                                       (entry->binding_count + 1));
        if (!grown) {
            abort();
        }
        entry->bindings = grown;
        entry->bindings[entry->binding_count].subject = copy_string(subject);
        entry->bindings[entry->binding_count].direction = copy_string(direction);
        entry->bindings[entry->binding_count].record = value_copy(*slot);
        entry->binding_count++;
    }
    if (strcmp(direction, "show") == 0) {
        err[0] = '\0';
        if (!gb_qt_set_display_only(root, name, 1, err, sizeof(err))) {
            snprintf(message, sizeof(message), "window `%s`: %s", window_name,
                     err[0] ? err : "that table could not be made display only");
            qt_raise(message);
            return 0;
        }
    }
    RecordField *cols = record_find(table, "items");
    if (!cols || !cols->value || cols->value->kind != VALUE_ARRAY) {
        return 1;
    }
    size_t ncols = cols->value->as.array.store
                       ? cols->value->as.array.store->count : 0;
    size_t nrows = slot->as.array.store ? slot->as.array.store->count : 0;
    err[0] = '\0';
    if (!gb_qt_table_size(root, name, (int)nrows, err, sizeof(err))) {
        snprintf(message, sizeof(message), "window `%s`: %s", window_name,
                 err[0] ? err : "that table could not be sized");
        qt_raise(message);
        return 0;
    }
    for (size_t r = 0; r < nrows; r++) {
        Value *row = &slot->as.array.store->items[r];
        if (row->kind != VALUE_RECORD) {
            snprintf(message, sizeof(message),
                     "window `%s`: `%s` shows records, and element %zu of `%s` "
                     "is %s", window_name, name, r, subject,
                     value_kind_name(row->kind));
            qt_raise(message);
            return 0;
        }
        for (size_t c = 0; c < ncols; c++) {
            Value *col = &cols->value->as.array.store->items[c];
            if (col->kind != VALUE_RECORD) {
                continue;
            }
            const char *field = qt_record_text(col, "name");
            if (!field) {
                continue;
            }
            RecordField *has = record_find(row, field);
            const char *shown = "";
            Value text = value_null();
            if (has && has->value) {
                /* THROUGH THE ONE RENDERER (PLAT-RENDER), and the copy is not
                 * freed here because `builtin_string_value` consumes it. */
                text = builtin_string_value(value_copy(*has->value));
                if (text.kind == VALUE_STRING) {
                    shown = text.as.string;
                }
            }
            err[0] = '\0';
            int ok = gb_qt_table_set(root, name, (int)r, (int)c, shown, err,
                                     sizeof(err));
            value_free(text);
            if (!ok) {
                snprintf(message, sizeof(message),
                         "window `%s`: `%s` could not show row %zu -- %s",
                         window_name, name, r,
                         err[0] ? err : "that cell was refused");
                qt_raise(message);
                return 0;
            }
        }
    }
    return 1;
}

/* READ A TABLE BACK. Each row becomes a record carrying the column names, and
 * the kind rule is the form's exactly: `type:` where declared, the ORIGINAL
 * element's field otherwise, text last. A row the person added is not
 * possible yet -- the view has no way to grow -- so the fold writes over the
 * capture row for row. */
static int qt_fold_table(void *root, Value *table, Value *out,
                         const char *window_name) {
    char message[512];
    const char *name = qt_record_text(table, "name");
    RecordField *cols = record_find(table, "items");
    if (!name || !cols || !cols->value || cols->value->kind != VALUE_ARRAY) {
        return 1;
    }
    size_t ncols = cols->value->as.array.store
                       ? cols->value->as.array.store->count : 0;
    int rows = gb_qt_table_rows(root, name);
    if (rows < 0) {
        return 1;
    }
    size_t have = (out->kind == VALUE_ARRAY && out->as.array.store)
                      ? out->as.array.store->count : 0;
    for (int r = 0; r < rows && (size_t)r < have; r++) {
        Value *row = &out->as.array.store->items[r];
        if (row->kind != VALUE_RECORD) {
            continue;
        }
        for (size_t c = 0; c < ncols; c++) {
            Value *col = &cols->value->as.array.store->items[c];
            if (col->kind != VALUE_RECORD) {
                continue;
            }
            const char *field = qt_record_text(col, "name");
            if (!field) {
                continue;
            }
            char *cell = gb_qt_table_get(root, name, r, (int)c);
            if (!cell) {
                continue;
            }
            RecordField *opts = record_find(col, "options");
            const char *declared = NULL;
            if (opts && opts->value && opts->value->kind == VALUE_RECORD) {
                declared = qt_record_text(opts->value, "type");
            }
            RecordField *was = record_find(row, field);
            Value folded;
            if (declared) {
                folded = qt_apply_type(declared, value_string(cell));
            } else if (was && was->value && was->value->kind == VALUE_NUMBER) {
                folded = qt_apply_type("number", value_string(cell));
            } else {
                folded = value_string(cell);
            }
            free(cell);
            if (error_action_pending()) {
                value_free(folded);
                return 0;
            }
            if (folded.kind == VALUE_NULL) {
                snprintf(message, sizeof(message),
                         "window `%s`: row %d of `%s` does not hold a %s",
                         window_name, r, name, declared ? declared : "number");
                qt_raise(message);
                value_free(folded);
                return 0;
            }
            record_set(row, field, folded);
        }
    }
    return 1;
}

static int qt_bind_items(void *root, Value *items, const char *window_name) {
    size_t count = items->as.array.store ? items->as.array.store->count : 0;
    for (size_t i = 0; i < count; i++) {
        Value *entry = &items->as.array.store->items[i];
        if (entry->kind != VALUE_RECORD) {
            continue;
        }
        const char *kind = qt_record_text(entry, "kind");
        if (!kind || strcmp(kind, "container") != 0) {
            continue;
        }
        const char *word = qt_record_text(entry, "word");
        if (qt_record_text(entry, "subject")) {
            int ok = (word && strcmp(word, "table") == 0)
                         ? qt_bind_table(root, entry, window_name)
                         : qt_bind_form(root, entry, window_name);
            if (!ok) {
                return 0;
            }
            continue;
        }
        RecordField *entries = record_find(entry, "items");
        if (entries && entries->value && entries->value->kind == VALUE_ARRAY) {
            if (!qt_bind_items(root, entries->value, window_name)) {
                return 0;
            }
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
        for (size_t b = 0; b < qt_windows[i].binding_count; b++) {
            free(qt_windows[i].bindings[b].subject);
            free(qt_windows[i].bindings[b].direction);
            value_free(qt_windows[i].bindings[b].record);
        }
        free(qt_windows[i].bindings);
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
        /* REMEMBERED BEFORE BINDING, because a form's capture is kept on the
         * registry entry; forgotten again if binding refuses, so a failed
         * open leaves nothing behind. */
        if (name) {
            qt_window_remember(name, root);
        }
        RecordField *items = record_find(&win, "items");
        if (items && items->value && items->value->kind == VALUE_ARRAY) {
            if (!qt_build_items(root, items->value, root) ||
                !qt_connect_items(root, items->value) ||
                !qt_bind_items(root, items->value, name ? name : "")) {
                if (name) {
                    qt_window_forget(name);
                }
                value_free(win);
                return value_null();
            }
        }
        /* SHOWN LAST, so a bound form appears filled in rather than appearing
         * empty and then populating. */
        gb_qt_show(root);
        value_free(win);
        return value_bool(1);
    }

    /* ONE CELL, BY ROW AND BY THE COLUMN'S OWN NAME. This exists for two
     * reasons and the first is the product one: after somebody has typed into
     * a `to edit` table, what one cell now holds is a question a handler asks
     * without wanting the whole list folded -- and for a `to show` table it is
     * the only way to ask what is on the screen at all.
     *
     * THE SECOND REASON IS THAT WITHOUT IT A `show` TABLE'S POPULATION IS
     * UNASSERTABLE: only an `edit` table's fold reads cells, so a backend that
     * built the right grid and filled none of it passed every check a show
     * table could make. The by-name/by-property reader had exactly that gap
     * one increment ago, which is why this is being noticed rather than found.
     *
     * ADDRESSED BY NAME, NOT BY COLUMN NUMBER, because a number is the one
     * thing that changes when the declaration is reordered -- and then it
     * reads a real value from the wrong column, which is a plausible answer. */
    if (strcmp(verb, "cell") == 0) {
        if (argc != 4) {
            qt_raise("qt.cell expects a window, a table, a row and a column name");
            return value_null();
        }
        Value win = eval_expr(expr->as.call.args.items[0]);
        Value which = eval_expr(expr->as.call.args.items[1]);
        Value rowv = eval_expr(expr->as.call.args.items[2]);
        Value colv = eval_expr(expr->as.call.args.items[3]);
        if (error_action_pending()) {
            value_free(win); value_free(which);
            value_free(rowv); value_free(colv);
            return value_null();
        }
        if (!qt_window_record(&win, "cell") || which.kind != VALUE_STRING ||
            colv.kind != VALUE_STRING || rowv.kind != VALUE_NUMBER) {
            if (!error_action_pending()) {
                qt_raise("qt.cell expects the table's name as text, a row "
                         "number and the column's name as text");
            }
            value_free(win); value_free(which);
            value_free(rowv); value_free(colv);
            return value_null();
        }
        const char *wname = qt_record_text(&win, "name");
        void *root = wname ? qt_window_find(wname) : NULL;
        if (!root) {
            snprintf(message, sizeof(message), "window `%s` is not open",
                     wname ? wname : "");
            qt_raise(message);
            value_free(win); value_free(which);
            value_free(rowv); value_free(colv);
            return value_null();
        }
        RecordField *items = record_find(&win, "items");
        Value *table = (items && items->value &&
                        items->value->kind == VALUE_ARRAY)
                           ? qt_find_table(items->value, which.as.string)
                           : NULL;
        if (!table) {
            snprintf(message, sizeof(message),
                     "qt.cell: window `%s` declares no table named `%s`",
                     wname ? wname : "", which.as.string);
            qt_raise(message);
            value_free(win); value_free(which);
            value_free(rowv); value_free(colv);
            return value_null();
        }
        int col = qt_table_column(table, colv.as.string);
        if (col < 0) {
            snprintf(message, sizeof(message),
                     "qt.cell: table `%s` has no `%s` column",
                     which.as.string, colv.as.string);
            qt_raise(message);
            value_free(win); value_free(which);
            value_free(rowv); value_free(colv);
            return value_null();
        }
        char *text = gb_qt_table_get(root, which.as.string,
                                     (int)rowv.as.num.value, col);
        snprintf(message, sizeof(message),
                 "qt.cell: table `%s` has no row %ld", which.as.string,
                 (long)rowv.as.num.value);
        value_free(win); value_free(which);
        value_free(rowv); value_free(colv);
        if (!text) {
            qt_raise(message);
            return value_null();
        }
        Value out = value_string(text);
        free(text);
        return out;
    }

    /* A COLUMN'S CAPTION, by the column's own name. THE REASON IS AN ORACLE,
     * and this module first argued against adding it on the grounds that a
     * program which set a caption already knows it. That was the wrong test:
     * the caption comes from `label:` and a cell's data from `name` -- TWO
     * DIFFERENT SOURCES in the declaration -- so the caption is the only thing
     * that pins the column MAPPING independently of the mapping itself.
     *
     * WITHOUT IT, population and `qt.cell` both map a column to a name, and
     * two perturbations -- "every column reads the first field" and "a cell
     * ignores the name and reads column 0" -- produced IDENTICAL output, which
     * is the signature of a check that cannot separate them. If both were
     * permuted the same way, every value would sit under the wrong heading and
     * nothing would notice. Agreement is not correctness (PLAT-NUMFMT). */
    /* THE MENU BAR'S OWN TOP-LEVEL CAPTIONS, as a list. THE REASON IS AN
     * ORACLE and it was found by perturbation rather than by design: an item
     * keeps its name wherever its menu ends up, so a SUBMENU hung off the bar
     * instead of off its parent passed EVERY check in the fixture -- measured,
     * zero mismatches. Nothing could tell `File > Recent` from `File` and
     * `Recent` side by side.
     *
     * THE CAPTIONS ARE WHAT DISTINGUISHES THEM, and as a list they pin the
     * COUNT and the ORDER as well -- a submenu that escaped shows up as a
     * third entry, and two menus that swapped show up reordered. Same shape as
     * `qt.heading` one directive over, found the same way. */
    if (strcmp(verb, "menus") == 0) {
        /* ONE ARGUMENT gives the BAR's captions; TWO gives the ENTRIES of the
         * menu at that position -- the same oracle one level in, which closes
         * the case that is structurally worse than a missing separator: an
         * item added to the WRONG menu keeps its name, so nothing else in the
         * fixture can see it. */
        if (argc != 1 && argc != 2) {
            qt_raise("qt.menus expects a window, and optionally which menu's "
                     "entries to read");
            return value_null();
        }
        Value win = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(win);
            return value_null();
        }
        if (!qt_window_record(&win, "menus")) {
            value_free(win);
            return value_null();
        }
        const char *wname = qt_record_text(&win, "name");
        void *root = wname ? qt_window_find(wname) : NULL;
        if (!root) {
            snprintf(message, sizeof(message), "window `%s` is not open",
                     wname ? wname : "");
            qt_raise(message);
            value_free(win);
            return value_null();
        }
        int which = -1;
        if (argc == 2) {
            Value idx = eval_expr(expr->as.call.args.items[1]);
            if (error_action_pending()) {
                value_free(win);
                value_free(idx);
                return value_null();
            }
            if (idx.kind != VALUE_NUMBER) {
                qt_raise("qt.menus expects the menu's position as a number");
                value_free(win);
                value_free(idx);
                return value_null();
            }
            which = (int)idx.as.num.value;
            value_free(idx);
        }
        value_free(win);
        int n = (which < 0) ? gb_qt_menu_count(root)
                            : gb_qt_menu_entry_count(root, which);
        if (n < 0) {
            if (which < 0) {
                qt_raise("qt.menus: that window has no menu bar to read");
            } else {
                snprintf(message, sizeof(message),
                         "qt.menus: this window has no menu at position %d",
                         which);
                qt_raise(message);
            }
            return value_null();
        }
        /* AN EMPTY ARRAY FOR A WINDOW WITH NO MENUS, not a refusal: a window
         * without a menu bar is ordinary, and "there are none" is the honest
         * answer to the question that was asked. */
        Value *titles = n ? malloc(sizeof(Value) * (size_t)n) : NULL;
        if (n && !titles) {
            abort();
        }
        for (int i = 0; i < n; i++) {
            int is_sep = 0;
            char *t = (which < 0)
                          ? gb_qt_menu_title(root, i)
                          : gb_qt_menu_entry_title(root, which, i, &is_sep);
            /* A SEPARATOR ANSWERS `unknown`, NOT "". It HAS no caption, where
             * `item x( label: "" )` has an empty one -- the absent/empty
             * distinction this language makes everywhere else, and the only
             * thing that tells a real line from an item that lost its text. */
            titles[i] = is_sep ? value_unknown() : value_string(t ? t : "");
            free(t);
        }
        return value_array(titles, (size_t)n);
    }

    /* THE APPLICATION'S THEME. One argument SETS it, none READS it back.
     *
     * PER-WIDGET STYLING ALREADY WORKED and this is deliberately not that:
     * `styleSheet` is a Q_PROPERTY of QWidget, so `button go( styleSheet:
     * "..." )` has been reachable since the by-name property route shipped,
     * with no vocabulary of its own. What that route cannot reach is ONE sheet
     * for the whole application, which is what anybody means by a theme -- and
     * Qt resolves the cascade between the two itself, so there is no
     * precedence rule here to invent.
     *
     * IT DOES NOT NEED A DISPLAY and does not open one: the sheet is
     * remembered and applied when a QApplication is constructed. A program
     * therefore sets its theme first and asks `qt.can_display()` after, and
     * the first window is built already themed instead of appearing plain and
     * then changing.
     *
     * THE THEME IS TEXT, NOT A FILE, which is the platform/policy split this
     * tree keeps making: `qt.theme(read({file}"dark.qss"))` composes out of
     * what the language already has, where a file argument would put file
     * resolution, missing-file behaviour and a search path inside a GUI
     * module that has no business owning them. */
    if (strcmp(verb, "theme") == 0) {
        if (argc > 1) {
            qt_raise("qt.theme takes a stylesheet to set, or nothing to read "
                     "the current one");
            return value_null();
        }
        if (argc == 0) {
            char *css = gb_qt_theme_get();
            Value out = value_string(css ? css : "");
            free(css);
            return out;
        }
        Value v = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(v);
            return value_null();
        }
        if (v.kind != VALUE_STRING) {
            qt_raise("qt.theme expects a stylesheet as text -- "
                     "`qt.theme(read({file}\"dark.qss\"))` to load one");
            value_free(v);
            return value_null();
        }
        err[0] = '\0';
        int ok = gb_qt_theme_set(v.as.string, err, sizeof(err));
        value_free(v);
        if (!ok) {
            snprintf(message, sizeof(message), "qt.theme: %s",
                     err[0] ? err : "that stylesheet was refused");
            qt_raise(message);
            return value_null();
        }
        return value_bool(1);
    }

    if (strcmp(verb, "heading") == 0) {
        if (argc != 3) {
            qt_raise("qt.heading expects a window, a table and a column name");
            return value_null();
        }
        Value win = eval_expr(expr->as.call.args.items[0]);
        Value which = eval_expr(expr->as.call.args.items[1]);
        Value colv = eval_expr(expr->as.call.args.items[2]);
        if (error_action_pending()) {
            value_free(win); value_free(which); value_free(colv);
            return value_null();
        }
        if (!qt_window_record(&win, "heading") || which.kind != VALUE_STRING ||
            (colv.kind != VALUE_STRING && colv.kind != VALUE_NUMBER)) {
            if (!error_action_pending()) {
                qt_raise("qt.heading expects the table's name as text and the "
                         "column's name, or its position as a number");
            }
            value_free(win); value_free(which); value_free(colv);
            return value_null();
        }
        const char *wname = qt_record_text(&win, "name");
        void *root = wname ? qt_window_find(wname) : NULL;
        RecordField *items = record_find(&win, "items");
        Value *table = (root && items && items->value &&
                        items->value->kind == VALUE_ARRAY)
                           ? qt_find_table(items->value, which.as.string)
                           : NULL;
        if (!root || !table) {
            snprintf(message, sizeof(message),
                     root ? "qt.heading: window `%s` declares no table named `%s`"
                          : "window `%s` is not open",
                     wname ? wname : "", which.as.string);
            qt_raise(message);
            value_free(win); value_free(which); value_free(colv);
            return value_null();
        }
        /* BY NAME **OR** BY POSITION, and the second form is what makes the
         * first an oracle rather than a restatement. Asked by NAME this goes
         * through `qt_table_column`, which is the same resolution `qt.cell`
         * uses -- so a PERMUTED resolution answers both consistently and
         * neither can witness it. Asked by POSITION it reads the header
         * directly, in the order the declaration wrote it, so requiring the
         * two forms to AGREE for every column pins the name-to-column mapping
         * against something that does not depend on it. MEASURED: with only
         * the by-name form, two different perturbations gave identical output
         * and the tier added nothing. */
        int col = (colv.kind == VALUE_NUMBER)
                      ? (int)colv.as.num.value
                      : qt_table_column(table, colv.as.string);
        if (col < 0) {
            snprintf(message, sizeof(message),
                     "qt.heading: table `%s` has no `%s` column",
                     which.as.string,
                     colv.kind == VALUE_STRING ? colv.as.string : "that");
            qt_raise(message);
            value_free(win); value_free(which); value_free(colv);
            return value_null();
        }
        char *text = gb_qt_table_heading(root, which.as.string, col);
        value_free(win); value_free(which); value_free(colv);
        if (!text) {
            qt_raise("qt.heading: that column has no caption");
            return value_null();
        }
        Value out = value_string(text);
        free(text);
        return out;
    }

    /* CHOOSE A ROW FROM THE PROGRAM -- a window that opens with yesterday's
     * order highlighted, or a row just added being made current. `unknown`
     * clears the selection, so "nobody has chosen" is sayable in both
     * directions rather than only readable. */
    if (strcmp(verb, "select") == 0) {
        if (argc != 3) {
            qt_raise("qt.select expects a window, a table's name and a row");
            return value_null();
        }
        Value win = eval_expr(expr->as.call.args.items[0]);
        Value which = eval_expr(expr->as.call.args.items[1]);
        Value rowv = eval_expr(expr->as.call.args.items[2]);
        if (error_action_pending()) {
            value_free(win); value_free(which); value_free(rowv);
            return value_null();
        }
        if (!qt_window_record(&win, "select") || which.kind != VALUE_STRING ||
            (rowv.kind != VALUE_NUMBER && rowv.kind != VALUE_UNKNOWN)) {
            if (!error_action_pending()) {
                qt_raise("qt.select expects the table's name as text and a row "
                         "number, or `unknown` to choose nothing");
            }
            value_free(win); value_free(which); value_free(rowv);
            return value_null();
        }
        const char *wname = qt_record_text(&win, "name");
        void *root = wname ? qt_window_find(wname) : NULL;
        if (!root) {
            snprintf(message, sizeof(message), "window `%s` is not open",
                     wname ? wname : "");
            qt_raise(message);
            value_free(win); value_free(which); value_free(rowv);
            return value_null();
        }
        int row = (rowv.kind == VALUE_UNKNOWN) ? -1 : (int)rowv.as.num.value;
        err[0] = '\0';
        int ok = gb_qt_table_select(root, which.as.string, row, err,
                                    sizeof(err));
        value_free(win); value_free(which); value_free(rowv);
        if (!ok) {
            snprintf(message, sizeof(message), "qt.select: %s",
                     err[0] ? err : "that row could not be chosen");
            qt_raise(message);
            return value_null();
        }
        return value_bool(1);
    }

    /* WHICH ROW A PERSON HAS CHOSEN. Answers the INDEX rather than the record,
     * so it composes with the subject the program already holds
     * (`lines[qt.selected(win, "orders")]`) and does not become a second,
     * staler copy of a row.
     *
     * NOTHING SELECTED ANSWERS `unknown`, NOT -1 AND NOT 0. Both of those are
     * valid-looking indices -- 0 is the first row, and -1 would index from the
     * end in a language that allowed it -- so either would let a program act
     * on a row nobody picked. `unknown` is the answer `materiality` and
     * `principal()` already give for "nobody said". */
    if (strcmp(verb, "selected") == 0) {
        if (argc != 2) {
            qt_raise("qt.selected expects a window and a table's name");
            return value_null();
        }
        Value win = eval_expr(expr->as.call.args.items[0]);
        Value which = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending()) {
            value_free(win);
            value_free(which);
            return value_null();
        }
        if (!qt_window_record(&win, "selected") || which.kind != VALUE_STRING) {
            if (which.kind != VALUE_STRING && !error_action_pending()) {
                qt_raise("qt.selected expects the table's name as text");
            }
            value_free(win);
            value_free(which);
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
            return value_null();
        }
        int row = gb_qt_table_rows(root, which.as.string);
        if (row < 0) {
            /* A PART THAT IS NOT A TABLE IS ITS OWN SENTENCE. `qt.selected`
             * on a field is a mistake about what the thing IS, and answering
             * `unknown` would read as "nothing is selected" -- true of a field
             * and not what the author asked. */
            snprintf(message, sizeof(message),
                     "qt.selected: `%s` is not a table in window `%s`",
                     which.as.string, wname ? wname : "");
            qt_raise(message);
            value_free(win);
            value_free(which);
            return value_null();
        }
        int at = gb_qt_table_selected(root, which.as.string);
        value_free(win);
        value_free(which);
        if (at < 0) {
            return value_unknown();
        }
        return value_number((double)at);
    }

    /* THE RECORD THE FORM IS WORKING ON. For `to edit` the widgets are read AT
     * THIS MOMENT and folded into the capture, which is what makes `form to
     * edit cust` genuinely two-way (§2.2); for `to show` the capture is
     * returned untouched, because a one-way form is display only and a record
     * that followed it would mean the direction said nothing.
     *
     * THE FOLD-BACK KEEPS THE FIELD'S KIND, and that is the part that would be
     * wrong in the obvious implementation: a widget holds TEXT, so folding
     * back blindly turns `{ id: 42 }` into `{ id: "42" }` -- a record whose
     * shape silently changed, which PLAT-EQ's own history says nothing
     * downstream would notice. So the kind comes from `type:` where the author
     * declared one, from the CAPTURED field otherwise, and from a check being
     * a check in the one case where the widget decides. */
    if (strcmp(verb, "subject") == 0) {
        if (argc != 2) {
            qt_raise("qt.subject expects a window and the subject's name");
            return value_null();
        }
        Value win = eval_expr(expr->as.call.args.items[0]);
        Value which = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending()) {
            value_free(win);
            value_free(which);
            return value_null();
        }
        if (!qt_window_record(&win, "subject") || which.kind != VALUE_STRING) {
            if (which.kind != VALUE_STRING && !error_action_pending()) {
                qt_raise("qt.subject expects the subject's name as text");
            }
            value_free(win);
            value_free(which);
            return value_null();
        }
        const char *wname = qt_record_text(&win, "name");
        QtOpenWindow *entry = wname ? qt_window_entry(wname) : NULL;
        if (!entry) {
            snprintf(message, sizeof(message), "window `%s` is not open",
                     wname ? wname : "");
            qt_raise(message);
            value_free(win);
            value_free(which);
            return value_null();
        }
        QtBinding *binding = NULL;
        for (size_t i = 0; i < entry->binding_count; i++) {
            if (strcmp(entry->bindings[i].subject, which.as.string) == 0) {
                binding = &entry->bindings[i];
                break;
            }
        }
        if (!binding) {
            snprintf(message, sizeof(message),
                     "window `%s` has no form bound to `%s`", wname,
                     which.as.string);
            qt_raise(message);
            value_free(win);
            value_free(which);
            return value_null();
        }
        Value out = value_copy(binding->record);
        if (strcmp(binding->direction, "edit") != 0) {
            value_free(win);
            value_free(which);
            return out;
        }
        RecordField *items = record_find(&win, "items");
        if (items && items->value && items->value->kind == VALUE_ARRAY) {
            if (!qt_fold_items(entry->node, items->value, which.as.string,
                               &out, wname)) {
                value_free(out);
                value_free(win);
                value_free(which);
                return value_null();
            }
        }
        value_free(win);
        value_free(which);
        return out;
    }

    if (strcmp(verb, "read") == 0 || strcmp(verb, "write") == 0) {
        int writing = strcmp(verb, "write") == 0;
        /* `qt.read` TAKES AN OPTIONAL PROPERTY NAME, which is the read half of
         * the by-name route the declaration's options already take. Two
         * arguments ask what the part is SHOWING; three ask one named property
         * of the widget. */
        int reading_prop = (!writing && argc == 3);
        if (writing ? argc != 3u : (argc != 2u && argc != 3u)) {
            snprintf(message, sizeof(message),
                     writing ? "qt.write expects a window, a name and a value"
                             : "qt.read expects a window, a name and "
                               "optionally a property");
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
        if (reading_prop) {
            Value prop = eval_expr(expr->as.call.args.items[2]);
            if (error_action_pending()) {
                value_free(win);
                value_free(which);
                value_free(prop);
                return value_null();
            }
            if (prop.kind != VALUE_STRING) {
                qt_raise("qt.read expects the property's name as text");
                value_free(win);
                value_free(which);
                value_free(prop);
                return value_null();
            }
            char kind = 't';
            double num = 0.0;
            char *text = NULL;
            err[0] = '\0';
            int ok = gb_qt_get(root, which.as.string, prop.as.string, &kind,
                               &num, &text, err, sizeof(err));
            value_free(win);
            value_free(which);
            value_free(prop);
            if (!ok) {
                snprintf(message, sizeof(message), "qt.read: %s",
                         err[0] ? err : "that property could not be read");
                qt_raise(message);
                free(text);
                return value_null();
            }
            if (kind == 'b') {
                return value_bool(num != 0.0);
            }
            if (kind == 'n') {
                return value_number(num);
            }
            Value out = value_string(text ? text : "");
            free(text);
            return out;
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
