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

static const char *qt_record_text(Value *record, const char *field) {
    RecordField *f = record_find(record, field);
    if (!f || !f->value || f->value->kind != VALUE_STRING) {
        return NULL;
    }
    return f->value->as.string;
}

/* ---- `qt.check`: the declaration's own rules, over a window a program BUILT
 *
 * §4 of docs/qt_gui_design.md says the block is SUGAR OVER A BUILDER, because
 * a declaration cannot contain a loop and so cannot generate a row per
 * invoice. MEASURED 2026-10-18, THAT DOOR WAS ALREADY OPEN AND NOBODY KNEW:
 * `qt.open` reads the record the declaration binds and asks only that `kind`
 * is `"window"`, so a record a program builds opens, renders, reads back and
 * fires handlers exactly as a declared one. The builder needed no verb.
 *
 * WHAT IT NEEDED WAS THIS. A declared window is validated at LOAD time and a
 * built one was validated not at all -- which is not a missing nicety but the
 * defect `web.configure` was built to close, where the record could already be
 * written directly and that path checked nothing, so `app.options.workesr = 4`
 * was accepted in silence. Measured here before a line was written:
 *
 *   a part with NO NAME               opened quietly, unaddressable forever
 *   TWO PARTS SHARING A NAME          opened quietly, the second shadowing
 *   `word: "feild"`                   refused, by the BACKEND, for having no
 *                                     widget wired -- never "did you mean
 *                                     'field'?"
 *   `item` in a `column`              same: the right refusal, wrong sentence
 *   a handler naming nothing          refused for carrying no function, never
 *                                     "there is nothing called `nobody`"
 *
 * The last is the one load-time validation exists for, so a builder without it
 * is a builder whose mistakes surface as a window that looks fine.
 *
 * THE RULES ARE NOT COPIED HERE. They live in src/window_check.c and this is
 * an adapter over a record; src/frontend.c has one over the AST. See
 * include/window_check.h for the argument, which is `would`/`execute`'s:
 * if the two could disagree, a check would describe a different window from
 * the one that opens.
 *
 * IT NEEDS NO QT AND NO DISPLAY, and that is deliberate rather than
 * incidental -- it is the `web.routes` property, the same one that let the
 * whole declaration ship before any Qt existed. So it sits OUTSIDE the
 * `#if HAVE_QT` guard, which is this project's own stated convention read the
 * other way: code that is not optional must stay out of those guards.
 *
 * AND THE FIELD NAMES ARE CLOSED, which the vocabulary rules cannot cover
 * because they are about the REPRESENTATION rather than about windows:
 * `{ kind: "part", word: "field", nmae: "x" }` is a part with no name and a
 * stray key, and reporting only the first would send a reader looking for a
 * name they can see they wrote. */

typedef struct {
    char message[512];
    int count;
    int reported;          /* 1 once `message` holds the first problem */
    const char *path[16];  /* where we are, for a record with no line */
    size_t depth;
} QtCheckSink;

static QtCheckSink *qt_check_sink = NULL;

typedef struct {
    Value *value;
    size_t depth;
    char where[192];
} QtCheckItem;

/* A RECORD HAS NO LINE, so what locates a problem is the PATH to it. A
 * declaration's refusal sends a reader to the line they must edit; the
 * nearest honest equivalent here names the chain of words that got us there,
 * which is what a reader of the building code has in front of them. */
static void qt_check_path(char *out, size_t cap, const char *parent,
                          const char *word, const char *name, size_t index) {
    char leaf[96];
    if (name && *name) {
        snprintf(leaf, sizeof(leaf), "%s %s", word ? word : "?", name);
    } else if (word && *word) {
        snprintf(leaf, sizeof(leaf), "%s[%zu]", word, index);
    } else {
        snprintf(leaf, sizeof(leaf), "items[%zu]", index);
    }
    if (parent && *parent) {
        snprintf(out, cap, "%s > %s", parent, leaf);
    } else {
        snprintf(out, cap, "%s", leaf);
    }
}

static int qr_kind(const void *p) {
    const QtCheckItem *it = (const QtCheckItem *)p;
    const char *kind = qt_record_text(it->value, "kind");
    if (kind && strcmp(kind, "handler") == 0) {
        return GB_WIN_HANDLER;
    }
    if (kind && strcmp(kind, "container") == 0) {
        return GB_WIN_CONTAINER;
    }
    return GB_WIN_LEAF;
}

static const char *qr_field(const void *p, const char *field) {
    const QtCheckItem *it = (const QtCheckItem *)p;
    return qt_record_text(it->value, field);
}
static const char *qr_word(const void *p)      { return qr_field(p, "word"); }
static const char *qr_name(const void *p)      { return qr_field(p, "name"); }
static const char *qr_label(const void *p)     { return qr_field(p, "label"); }
static const char *qr_subject(const void *p)   { return qr_field(p, "subject"); }
static const char *qr_direction(const void *p) { return qr_field(p, "direction"); }
static const char *qr_target(const void *p)    { return qr_field(p, "target"); }
static const char *qr_signal(const void *p)    { return qr_field(p, "signal"); }

/* A RECORD HAS NO `end` WORD, there being no source to close, so this is
 * always absent -- which the shared checker reads as "nothing to disagree
 * with" rather than as an empty closer. */
static const char *qr_close_word(const void *p) {
    (void)p;
    return NULL;
}

static int qr_handler_callable(const void *p) {
    const QtCheckItem *it = (const QtCheckItem *)p;
    RecordField *f = record_find(it->value, "handler");
    return (f && f->value && f->value->kind == VALUE_FUNCTION) ? 1 : 0;
}

/* A CHILD IS MATERIALISED ON DEMAND, because the model hands back a pointer
 * the checker keeps for the length of one call and a record field is a
 * `Value *` with no view struct of its own. ONE SLOT PER DEPTH, which is what
 * the checker's lifetime actually requires: it holds a PARENT across the loop
 * over that parent's children and a CHILD during one call, so slots must
 * outlive siblings and may be reused only when the walk leaves a level.
 *
 * MY FIRST VERSION WAS A RING OF SIXTEEN with a comment claiming the checker
 * "holds at most a parent and a child at any moment". THAT WAS FALSE and the
 * sixteen was the giveaway: at the seventeenth SIBLING the ring recycled the
 * PARENT'S slot while the walk was still iterating it, so `qr_entry` read a
 * path out of a slot that had become a different item. It SEGFAULTED at
 * nineteen generated parts -- which the fixture had, §2.4's rejected keypad
 * being nineteen buttons -- and a crash takes block-buffered stdout with it,
 * so the run printed everything up to the tier and nothing after it, reading
 * exactly like a hang. The same shape §0.3 of the design describes for a
 * display-less Qt abort, arriving from my own code.
 *
 * THE DEPTH IS BOUNDED AND THAT IS NOT CAUTION. A DECLARED window's nesting is
 * bounded by its source, and a BUILT one is bounded by nothing -- a loop can
 * nest a container inside itself a million deep, and `gb_window_check`
 * recurses, so the refusal is in front of a C stack overflow rather than in
 * front of a slot overflow. It is checked in the SHAPE walk, which runs first
 * and recurses the same way, so by the time the rules run no depth beyond the
 * cap exists.
 *
 * THE MARGIN IS MEASURED, not assumed, which is the standard `rules` set for
 * its own cap: with the cap lifted this walk survives 5,000 and SEGFAULTS at
 * 10,000, so 64 sits some 80x short of the crash it stands in front of. The
 * CONTROL is what makes that a statement about this code rather than about
 * gBASIC -- building the same 10,000-deep record and NOT checking it exits 0,
 * where a nested value around 22,000 deep is separately known to kill the
 * interpreter at teardown. 64 nested containers is already far past any
 * layout, so the cap is cheap; the margin is what says a reader who raises it
 * is trading something real. */
#define QT_CHECK_MAX_DEPTH 64
static QtCheckItem qt_check_slots[QT_CHECK_MAX_DEPTH + 2];

static const void *qt_check_wrap(const QtCheckItem *parent, Value *v,
                                 size_t index) {
    size_t depth = parent ? parent->depth + 1 : 1;
    if (depth > QT_CHECK_MAX_DEPTH + 1) {
        depth = QT_CHECK_MAX_DEPTH + 1;   /* unreachable: the shape walk refused */
    }
    QtCheckItem *slot = &qt_check_slots[depth];
    slot->value = v;
    slot->depth = depth;
    qt_check_path(slot->where, sizeof(slot->where),
                  parent ? parent->where : NULL,
                  qt_record_text(v, "word"), qt_record_text(v, "name"), index);
    return slot;
}

static size_t qr_items_of(Value *v) {
    RecordField *f = record_find(v, "items");
    if (!f || !f->value || f->value->kind != VALUE_ARRAY) {
        return 0;
    }
    return f->value->as.array.store->count;
}

static const void *qr_item_of(const QtCheckItem *owner, size_t i) {
    RecordField *f = record_find(owner->value, "items");
    return qt_check_wrap(owner->depth == 0 ? NULL : owner,
                         &f->value->as.array.store->items[i], i);
}

static size_t qr_entry_count(const void *p) {
    return qr_items_of(((const QtCheckItem *)p)->value);
}
static const void *qr_entry(const void *p, size_t i) {
    return qr_item_of((const QtCheckItem *)p, i);
}

static size_t qr_win_item_count(const void *p) {
    return qr_items_of(((const QtCheckItem *)p)->value);
}
static const void *qr_win_item(const void *p, size_t i) {
    return qr_item_of((const QtCheckItem *)p, i);
}

static size_t qr_win_option_count(const void *p) {
    RecordField *f = record_find(((const QtCheckItem *)p)->value, "options");
    if (!f || !f->value || f->value->kind != VALUE_RECORD) {
        return 0;
    }
    return f->value->as.record.count;
}
static const char *qr_win_option_name(const void *p, size_t i) {
    RecordField *f = record_find(((const QtCheckItem *)p)->value, "options");
    return f->value->as.record.fields[i].name;
}

static void qr_report(void *ctx, const void *win, const void *item,
                      const char *message) {
    QtCheckSink *sink = (QtCheckSink *)ctx;
    (void)win;
    sink->count++;
    if (sink->reported) {
        return;
    }
    const QtCheckItem *it = (const QtCheckItem *)item;
    if (it && it->where[0]) {
        snprintf(sink->message, sizeof(sink->message), "%s (at %s)", message,
                 it->where);
    } else {
        snprintf(sink->message, sizeof(sink->message), "%s", message);
    }
    sink->reported = 1;
}

/* THE FIELD NAMES A RECORD MAY CARRY, per kind. Unknown ones are refused BY
 * NAME, the rule `webserver.listen` and `reasoning.check_context` already
 * follow -- and the reason is the same one `web.configure` measured: without
 * it, a misspelled key leaves an author believing they asked for something. */
static const char *qt_check_keys(const char *kind) {
    if (kind && strcmp(kind, "handler") == 0) {
        return "kind target signal handler";
    }
    if (kind && strcmp(kind, "container") == 0) {
        return "kind word name label subject direction options items";
    }
    return "kind word name label options";
}

static int qt_check_key_known(const char *allowed, const char *name) {
    size_t len = strlen(name);
    const char *p = allowed;
    while (*p) {
        const char *end = p;
        while (*end && *end != ' ') { end++; }
        if ((size_t)(end - p) == len && strncmp(p, name, len) == 0) {
            return 1;
        }
        p = (*end == ' ') ? end + 1 : end;
    }
    return 0;
}

/* The representation walk, kept SEPARATE from the rules: this says whether
 * the record is a window-shaped record at all, and gb_window_check says
 * whether the window is a legal window. Confusing the two is how a reader
 * ends up being told about a missing name when what they wrote was a typo'd
 * key. */
static int qt_check_shape(Value *v, const char *where, size_t depth,
                          QtCheckSink *sink) {
    if (depth > QT_CHECK_MAX_DEPTH) {
        sink->count++;
        if (!sink->reported) {
            snprintf(sink->message, sizeof(sink->message),
                     "window: containers are nested more than %d deep (at %s) "
                     "-- a layout that deep is a loop that built itself",
                     QT_CHECK_MAX_DEPTH, where && *where ? where : "items");
            sink->reported = 1;
        }
        return 0;
    }
    if (v->kind != VALUE_RECORD) {
        sink->count++;
        if (!sink->reported) {
            snprintf(sink->message, sizeof(sink->message),
                     "window: this is a %s, not an item record (at %s)",
                     value_kind_name(v->kind), where && *where ? where : "items");
            sink->reported = 1;
        }
        return 0;
    }
    const char *kind = qt_record_text(v, "kind");
    if (!kind || (strcmp(kind, "container") != 0 && strcmp(kind, "part") != 0 &&
                  strcmp(kind, "handler") != 0)) {
        sink->count++;
        if (!sink->reported) {
            snprintf(sink->message, sizeof(sink->message),
                     "window: every item needs `kind:` -- \"container\", "
                     "\"part\" or \"handler\" -- and this one says %s%s%s "
                     "(at %s)",
                     kind ? "\"" : "", kind ? kind : "nothing", kind ? "\"" : "",
                     where && *where ? where : "items");
            sink->reported = 1;
        }
        return 0;
    }
    const char *allowed = qt_check_keys(kind);
    for (size_t i = 0; i < v->as.record.count; i++) {
        const char *fname = v->as.record.fields[i].name;
        if (!qt_check_key_known(allowed, fname)) {
            sink->count++;
            if (!sink->reported) {
                snprintf(sink->message, sizeof(sink->message),
                         "window: a %s carries no '%s' (the fields are %s) "
                         "(at %s)", kind, fname, allowed,
                         where && *where ? where : "items");
                sink->reported = 1;
            }
            return 0;
        }
    }
    if (strcmp(kind, "container") == 0) {
        RecordField *f = record_find(v, "items");
        if (f && f->value && f->value->kind == VALUE_ARRAY) {
            char child[192];
            for (size_t i = 0; i < f->value->as.array.store->count; i++) {
                Value *c = &f->value->as.array.store->items[i];
                qt_check_path(child, sizeof(child), where,
                              c->kind == VALUE_RECORD ? qt_record_text(c, "word") : NULL,
                              c->kind == VALUE_RECORD ? qt_record_text(c, "name") : NULL,
                              i);
                if (!qt_check_shape(c, child, depth + 1, sink)) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

/* Returns the number of problems; `message` holds the first. ONE PROBLEM AND
 * NOT ALL OF THEM, deliberately and unlike `rules.validate`: this reports by
 * RAISING, and a raise carries one sentence. The count travels with it so the
 * caller is told there are more. */
static int qt_check_window_record(Value *win, char *message, size_t cap) {
    static const GbWinModel model = {
        qr_name, qr_subject, qr_direction, qr_close_word,
        qr_win_option_count, qr_win_option_name, qr_win_item_count,
        qr_win_item,
        qr_kind, qr_word, qr_name, qr_label, qr_subject, qr_direction,
        qr_close_word, qr_target, qr_signal, qr_handler_callable,
        qr_entry_count, qr_entry,
        qr_report, NULL
    };
    QtCheckSink sink;
    sink.message[0] = '\0';
    sink.count = 0;
    sink.reported = 0;
    sink.depth = 0;
    qt_check_sink = &sink;

    QtCheckItem root;
    root.value = win;
    root.depth = 0;
    root.where[0] = '\0';

    /* THE SHAPE FIRST, THEN THE RULES. A record that is not item-shaped
     * cannot be judged against window rules at all, and judging it anyway is
     * how a reader gets told their part has no name when what they wrote was
     * a string. */
    RecordField *items = record_find(win, "items");
    if (items && items->value && items->value->kind == VALUE_ARRAY) {
        char child[192];
        for (size_t i = 0; i < items->value->as.array.store->count; i++) {
            Value *c = &items->value->as.array.store->items[i];
            qt_check_path(child, sizeof(child), NULL,
                          c->kind == VALUE_RECORD ? qt_record_text(c, "word") : NULL,
                          c->kind == VALUE_RECORD ? qt_record_text(c, "name") : NULL,
                          i);
            if (!qt_check_shape(c, child, 1, &sink)) {
                snprintf(message, cap, "%s", sink.message);
                qt_check_sink = NULL;
                return sink.count;
            }
        }
    }

    GbWinModel m = model;
    m.ctx = &sink;
    gb_window_check(&m, &root);
    if (sink.count > 0) {
        snprintf(message, cap, "%s", sink.message);
    }
    qt_check_sink = NULL;
    return sink.count;
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
/* WHICH PARTS A FORM BINDS. THE RULE IS "IT HOLDS A VALUE A PERSON CAN
 * CHANGE", which is what decides the two new words rather than taste.
 *
 * A `memo` and a `choice` are bound: a description and a chosen value are both
 * data, and BAG's area editor wants exactly those -- `area.description` in a
 * memo and `area.north` in a dropdown. ADDING THEM TO THE VOCABULARY WITHOUT
 * ADDING THEM HERE LEFT THEM HALF-WIRED, rendering perfectly and binding not
 * at all, which is a window that looks right and a record that never fills:
 * found by folding one back and getting an empty string out of a widget that
 * plainly showed a value.
 *
 * A `text` IS NOT BOUND, and that is a decision rather than an omission: it
 * carries no value a person can change, so folding it is meaningless, and
 * populating it alone would make `to edit` and `to show` mean different things
 * depending on the word. A form wanting WRAPPED PROSE from its subject uses a
 * `memo` with `readOnly: true`, which binds and wraps -- the read-only `field`
 * §0.10 ruled out could do neither. */
static int qt_bindable_word(const char *word) {
    return word && (strcmp(word, "field") == 0 || strcmp(word, "check") == 0 ||
                    strcmp(word, "memo") == 0 || strcmp(word, "choice") == 0 ||
                    strcmp(word, "source") == 0);
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

/* The options this backend owns because they are NOT widget properties.
 * Everything else goes to the widget's own QMetaObject by name, which is the
 * whole argument for the by-name route (§1): QPushButton alone exposes 75
 * properties, and a table of them would be bindings forever. */
static int qt_structural_option(const char *name) {
    static const char *structural[] = {
        "label", "margin", "spacing", "grow", "show", "edit", "type",
        "across", "down", "options", "language", "theme", NULL
    };
    for (size_t i = 0; structural[i]; i++) {
        if (strcmp(name, structural[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* FILL A `choice`'S LIST FROM AN ARRAY. ONE PIECE OF CODE for both doors --
 * the declared `options:` literal and the run-time `qt.options` -- because a
 * choice filled one way and refilled the other must hold the same thing, which
 * is the rule `qt_populate_table` follows for a table's rows.
 *
 * EVERY ITEM GOES THROUGH THE ONE RENDERER (PLAT-RENDER), so a list of numbers,
 * dates or money reads as a person would see it printed rather than as whatever
 * a bare cast produced. */
static int qt_fill_choice(void *root, const char *name, Value *list,
                          const char *whose) {
    char err[QT_ERRBUF];
    char message[512];
    err[0] = '\0';
    if (!gb_qt_choice_clear(root, name, err, sizeof(err))) {
        snprintf(message, sizeof(message), "%s: %s", whose,
                 err[0] ? err : "that list could not be cleared");
        qt_raise(message);
        return 0;
    }
    size_t n = list->as.array.store ? list->as.array.store->count : 0;
    for (size_t i = 0; i < n; i++) {
        Value text = builtin_string_value(value_copy(list->as.array.store->items[i]));
        const char *shown = (text.kind == VALUE_STRING) ? text.as.string : "";
        err[0] = '\0';
        int ok = gb_qt_choice_add(root, name, shown, err, sizeof(err));
        value_free(text);
        if (!ok) {
            snprintf(message, sizeof(message), "%s: %s", whose,
                     err[0] ? err : "that choice could not be added");
            qt_raise(message);
            return 0;
        }
    }
    return 1;
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
        if (strcmp(f->name, "across") == 0 || strcmp(f->name, "down") == 0) {
            /* HANDLED TOGETHER, so `across: 2, down: 2` is one call rather
             * than two re-adds -- and so the second does not have to undo the
             * first. Whichever name arrives first reads the other out of the
             * same options record. */
            RecordField *af = record_find(options, "across");
            RecordField *df = record_find(options, "down");
            long across = 1, down = 1;
            if (af && af->value && af->value->kind == VALUE_NUMBER) {
                across = (long)af->value->as.num.value;
            } else if (af) {
                qt_raise("window: `across:` must be a number of columns");
                return 0;
            }
            if (df && df->value && df->value->kind == VALUE_NUMBER) {
                down = (long)df->value->as.num.value;
            } else if (df) {
                qt_raise("window: `down:` must be a number of rows");
                return 0;
            }
            if (strcmp(f->name, "across") == 0 || !af) {
                if (!gb_qt_span(node, across, down, err, sizeof(err))) {
                    snprintf(message, sizeof(message), "window: %s",
                             err[0] ? err : "that span was refused");
                    qt_raise(message);
                    return 0;
                }
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
    RecordField *opts = record_find(item, "options");
    /* THE CAPTION IS IN `options` FOR A LEAF AND AT THE TOP LEVEL FOR A
     * LABELLED CONTAINER, and reading only the second meant `label:` REACHED
     * NOTHING -- shipped in the first backend commit and alive through four
     * increments. `field who( label: "Customer" )` in a form got
     * `addRow(QString(), w)`, an EMPTY label, and `check hold( label: "On
     * hold" )` rendered with no text at all. Nothing caught it because every
     * check in every fixture read a VALUE or some other property, and an
     * unlabelled widget raises nothing.
     *
     * THE SAME DEFECT WAS FOUND AND FIXED LOCALLY FOR A TABLE'S COLUMN
     * HEADERS an hour earlier, and I did not sweep -- which is PLAT-NUL's
     * standing lesson, that a defect found in one place is evidence about
     * every place that reads the same thing. One lookup here now serves every
     * word, and the menu and table paths read the same way. */
    const char *label = NULL;
    if (opts && opts->value && opts->value->kind == VALUE_RECORD) {
        label = qt_record_text(opts->value, "label");
    }
    if (!label) {
        label = qt_record_text(item, "label");
    }
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
        void *node = gb_qt_container(parent, word ? word : "", name, label, err,
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
    if (!qt_apply_options(node, opts ? opts->value : NULL, word ? word : "", 0)) {
        return 0;
    }
    /* A DECLARED `options:` LIST, for the fixed case -- a direction, a status,
     * a currency -- where typing them out is the honest thing. The data-driven
     * case is `qt.options`, and BAG's editor needs both in one program: the
     * directions are fixed and the areas are not, which is the measurement
     * that said support both rather than one.
     *
     * AFTER the property pass, because `options` is a STRUCTURAL option (it is
     * no Qt property, a QComboBox's items not being one) and so was skipped
     * there; and refused on anything that is not a `choice`, since a list of
     * items means nothing to a field or a button. */
    if (opts && opts->value && opts->value->kind == VALUE_RECORD) {
        RecordField *list = record_find(opts->value, "options");
        if (list && list->value) {
            if (!word || strcmp(word, "choice") != 0) {
                snprintf(message, sizeof(message),
                         "window: only a `choice` holds a list of options, not "
                         "'%s'", word ? word : "?");
                qt_raise(message);
                return 0;
            }
            if (list->value->kind != VALUE_ARRAY) {
                snprintf(message, sizeof(message),
                         "window: `choice %s( options: ... )` needs a list, and "
                         "it holds %s", name ? name : "?",
                         value_kind_name(list->value->kind));
                qt_raise(message);
                return 0;
            }
            if (!qt_fill_choice(root, name, list->value, "window")) {
                return 0;
            }
        }
    }

    /* THE HIGHLIGHTER, which is the one thing in this vocabulary the by-name
     * property route cannot reach: it is an OBJECT attached to a document, so
     * `indexOfProperty` has nothing to find -- hence `language:` and `theme:`
     * are structural and handled here, the shape `options:` above follows.
     *
     * REFUSED ON ANYTHING BUT A `source`, because a language means nothing to
     * a button and `setProperty("language", ...)` would have stored a dynamic
     * property that nothing reads -- an option that silently does nothing,
     * which is the failure class this whole backend is tested against. */
    if (opts && opts->value && opts->value->kind == VALUE_RECORD) {
        RecordField *lang = record_find(opts->value, "language");
        RecordField *thm = record_find(opts->value, "theme");
        int is_source = word && strcmp(word, "source") == 0;
        if ((lang || thm) && !is_source) {
            snprintf(message, sizeof(message),
                     "window: only a `source` carries `%s`, not '%s'",
                     lang ? "language" : "theme", word ? word : "?");
            qt_raise(message);
            return 0;
        }
        if (thm && !lang) {
            /* A THEME WITH NOTHING TO COLOUR. Refused rather than ignored:
             * the theme is applied with the definition, so on its own it is
             * silent, and a `source` whose author asked for `theme: "dark"`
             * and got nothing has no way to tell. */
            snprintf(message, sizeof(message),
                     "window: `source %s( theme: ... )` needs a `language:` "
                     "too -- a theme colours a definition, and alone it would "
                     "do nothing", name ? name : "?");
            qt_raise(message);
            return 0;
        }
        if (is_source && lang && lang->value) {
            const char *theme_text = NULL;
            if (lang->value->kind != VALUE_STRING) {
                snprintf(message, sizeof(message),
                         "window: `source %s( language: ... )` needs the "
                         "language's name as text, and it holds %s",
                         name ? name : "?",
                         value_kind_name(lang->value->kind));
                qt_raise(message);
                return 0;
            }
            if (thm && thm->value) {
                if (thm->value->kind != VALUE_STRING) {
                    snprintf(message, sizeof(message),
                             "window: `source %s( theme: ... )` needs \"light\" "
                             "or \"dark\" as text, and it holds %s",
                             name ? name : "?",
                             value_kind_name(thm->value->kind));
                    qt_raise(message);
                    return 0;
                }
                theme_text = thm->value->as.string;
                /* SPELLED OUT, because an unrecognised word would fall
                 * through to asking Qt's palette -- which answers something
                 * reasonable, so `theme: "Dark"` would look like a theme that
                 * does not work rather than a word that is not one. */
                if (strcmp(theme_text, "light") != 0 &&
                    strcmp(theme_text, "dark") != 0) {
                    snprintf(message, sizeof(message),
                             "window: `source %s( theme: \"%s\" )` is not a "
                             "theme; write \"light\" or \"dark\", or leave it "
                             "out to follow the window's own colours",
                             name ? name : "?", theme_text);
                    qt_raise(message);
                    return 0;
                }
            }
            err[0] = '\0';
            if (!gb_qt_highlight(root, name, lang->value->as.string,
                                 theme_text, err, sizeof(err))) {
                /* A WARNING, NOT A RAISE, and the distinction is the whole
                 * reason this is affordable: the editor is built, editable
                 * and perfectly usable without highlighting, so stopping the
                 * program would be a worse answer than saying so -- the
                 * position `odbc.connect` takes on a driver that needs
                 * ClientCharset. AND IT MUST BE SAID: an absent definition
                 * still formats every line with the theme's normal text, so
                 * there is nothing on screen to notice. */
                warn_fmt(2115, "highlighting", "window: %s",
                         err[0] ? err : "the syntax definition could not be "
                                        "applied");
            }
        }
    }
    return 1;
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
/* FILL A FORM'S PARTS FROM ITS SUBJECT. Extracted from `qt_bind_form` for the
 * reason `qt_populate_table` was: OPENING a window and RE-SHOWING a form must
 * go through one piece of code, or a form renders one way at open and another
 * afterwards -- and a form that cannot be re-shown was a GAP, found by trying
 * to write BAG's editor, where choosing a row has to show that area.
 *
 * `filled` COUNTS WHAT IT WROTE, so `qt.refresh` can answer how many things it
 * drew rather than a bare success. That is the difference between "it ran" and
 * "it did something", and every defect this backend has is the first kind. */
static int qt_populate_form(void *root, Value *form, Value *slot,
                            const char *window_name, int *filled) {
    char err[QT_ERRBUF];
    char message[512];
    /* READ FROM THE FORM rather than passed in, so a refresh cannot be handed
     * a direction the declaration does not have. */
    const char *direction = qt_record_text(form, "direction");
    if (!direction) {
        direction = "edit";
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
        if (ok && filled) {
            *filled = *filled + 1;
        }
        if (!ok) {
            /* A `choice` WHOSE LIST IS NOT FILLED YET IS NOT A MISTAKE, and
             * the ordering makes it ordinary: a declared `options:` list is
             * filled at construction and so is there, but one filled by
             * `qt.options` can only be filled AFTER the window is open --
             * which is after this pass. So the subject's value has nothing to
             * match and the selection is simply left alone.
             *
             * It is the same ruling §0.5 already made one line up, where a
             * field the record does not have is left empty rather than
             * refused because that is what a form filling in a NEW record
             * needs. Every other word still raises, so a real refusal is not
             * swallowed. */
            const char *word = qt_record_text(part, "word");
            if (word && strcmp(word, "choice") == 0) {
                continue;
            }
            snprintf(message, sizeof(message),
                     "window `%s`: `%s` could not be shown -- %s", window_name,
                     name, err[0] ? err : "the part refused that value");
            qt_raise(message);
            return 0;
        }
    }
    return 1;
}

static int qt_bind_form(void *root, Value *form, const char *window_name) {
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
    int drawn = 0;
    return qt_populate_form(root, form, slot, window_name, &drawn);
}

static int qt_populate_table(void *root, Value *table, Value *slot,
                             const char *window_name);

/* HOW MANY PARTS A SUBJECT DRIVES, without touching any of them. Needed
 * BEFORE anything is written, so that an ambiguous name is refused rather
 * than half-acted-on. */
static int qt_count_subject(Value *items, const char *subject) {
    int found = 0;
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
        const char *mine = qt_record_text(entry, "subject");
        if (word && mine && strcmp(mine, subject) == 0 &&
            (strcmp(word, "form") == 0 || strcmp(word, "table") == 0)) {
            found++;
            continue;
        }
        RecordField *nested = record_find(entry, "items");
        if (nested && nested->value && nested->value->kind == VALUE_ARRAY) {
            found += qt_count_subject(nested->value, subject);
        }
    }
    return found;
}

/* EVERY FORM AND TABLE CARRYING ONE SUBJECT, RE-SHOWN. A subject can drive
 * several parts -- §0.5's own fixture splits `cust` across a column and a
 * group, which is how a long form is laid out -- and `qt.subject` already
 * folds across all of them, so re-showing has to visit all of them too or the
 * two directions would disagree about what a subject reaches.
 *
 * `drawn` accumulates across them; `found` counts the NODES, which is what
 * separates "nothing matched" from "matched and wrote nothing" -- a form whose
 * record has no fields yet is the second, and is ordinary. */
static int qt_reshow_subject(void *root, Value *items, const char *subject,
                             Value *slot, const char *window_name, int *drawn,
                             int *found) {
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
        const char *mine = qt_record_text(entry, "subject");
        if (word && mine && strcmp(mine, subject) == 0) {
            if (strcmp(word, "form") == 0) {
                if (slot->kind != VALUE_RECORD) {
                    char message[512];
                    snprintf(message, sizeof(message),
                             "qt.refresh: `%s` fills a form, so it needs a "
                             "record, and it holds %s", subject,
                             value_kind_name(slot->kind));
                    qt_raise(message);
                    return 0;
                }
                if (!qt_populate_form(root, entry, slot, window_name, drawn)) {
                    return 0;
                }
                *found = *found + 1;
                continue;
            }
            if (strcmp(word, "table") == 0) {
                if (slot->kind != VALUE_ARRAY) {
                    char message[512];
                    snprintf(message, sizeof(message),
                             "qt.refresh: `%s` fills a table, so it needs a "
                             "list of records, and it holds %s", subject,
                             value_kind_name(slot->kind));
                    qt_raise(message);
                    return 0;
                }
                if (!qt_populate_table(root, entry, slot, window_name)) {
                    return 0;
                }
                *drawn = *drawn + (int)(slot->as.array.store
                                            ? slot->as.array.store->count : 0);
                *found = *found + 1;
                continue;
            }
        }
        RecordField *nested = record_find(entry, "items");
        if (nested && nested->value && nested->value->kind == VALUE_ARRAY) {
            if (!qt_reshow_subject(root, nested->value, subject, slot,
                                   window_name, drawn, found)) {
                return 0;
            }
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
/* FILL A TABLE'S ROWS FROM ITS SUBJECT. Extracted from `qt_bind_table` so
 * that OPENING a window and REFRESHING a table go through one piece of code:
 * a second copy is a second thing that can disagree, and a disagreement here
 * means a table that renders one way at open and another way afterwards. The
 * rule `would`/`execute` share in `automation`, and the one
 * `webserver_loop_iterate` follows for its two callers.
 *
 * THE HEADINGS ARE NOT HERE, deliberately: a column's caption is set when the
 * widget is constructed and cannot change while the window is open, the
 * declaration being the only thing that names it. Only the ROWS follow the
 * list. */
static int qt_populate_table(void *root, Value *table, Value *slot,
                             const char *window_name) {
    char err[QT_ERRBUF];
    char message[512];
    const char *name = qt_record_text(table, "name");
    const char *subject = qt_record_text(table, "subject");
    if (!name || !subject) {
        return 1;
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
    return qt_populate_table(root, table, slot, window_name);
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

/* `qt.check` ANSWERS ON A BUILD WITH NO QT, like `can_display` and for a
 * stronger reason: it is a check on a DECLARATION, and the whole declarative
 * surface was built to be provable with no display and no toolkit. A program
 * that builds a window on a headless machine and validates it before shipping
 * it is doing exactly what this is for. */
static Value qt_check_verb(AstExpr *expr, size_t argc) {
    char message[512];
    if (argc != 1) {
        qt_raise("qt.check expects one window record");
        return value_null();
    }
    Value win = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(win);
        return value_null();
    }
    if (win.kind != VALUE_RECORD) {
        qt_raise("qt.check expects one window record");
        value_free(win);
        return value_null();
    }
    const char *kind = qt_record_text(&win, "kind");
    if (!kind || strcmp(kind, "window") != 0) {
        qt_raise("qt.check expects a window record -- one carrying "
                 "`kind: \"window\"`, which is what a `window` block binds");
        value_free(win);
        return value_null();
    }
    static const char *head[] = {"kind", "name", "options", "items",
                                 "subject", "direction", NULL};
    for (size_t i = 0; i < win.as.record.count; i++) {
        const char *fname = win.as.record.fields[i].name;
        int known = 0;
        for (size_t j = 0; head[j]; j++) {
            if (strcmp(head[j], fname) == 0) { known = 1; break; }
        }
        if (!known) {
            snprintf(message, sizeof(message),
                     "window: a window carries no '%s' (the fields are kind, "
                     "name, options, items, subject and direction)", fname);
            qt_raise(message);
            value_free(win);
            return value_null();
        }
    }
    message[0] = '\0';
    int problems = qt_check_window_record(&win, message, sizeof(message));
    value_free(win);
    if (problems > 0) {
        char full[640];
        if (problems == 1) {
            snprintf(full, sizeof(full), "%s", message);
        } else {
            /* THE COUNT TRAVELS WITH THE SENTENCE, because a raise carries
             * one problem and a reader who fixes it should not have to
             * discover the rest one run at a time. */
            snprintf(full, sizeof(full), "%s -- and %d more", message,
                     problems - 1);
        }
        qt_raise(full);
        return value_null();
    }
    return value_bool(1);
}

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
    /* ANSWERS WITHOUT QT, like can_display and for the same reason: "can this
     * build highlight?" is a question a program asks in order to choose, so
     * refusing it would make the choice unmakeable. */
    if (strcmp(verb, "can_highlight") == 0 && argc == 0) {
        return value_bool(0);
    }
    if (strcmp(verb, "check") == 0) {
        return qt_check_verb(expr, argc);
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

    /* A BUILD-LEVEL QUESTION, where `qt.highlighting` is a per-widget one, and
     * both are needed: this one tells a program whether to offer the feature
     * at all, that one tells it what a particular editor actually got -- which
     * a boolean cannot, since the definition can be the wrong one or somebody
     * else's copy of the right one. */
    if (strcmp(verb, "can_highlight") == 0) {
        if (argc != 0) {
            qt_raise("qt.can_highlight takes no arguments");
            return value_null();
        }
        return value_bool(gb_qt_has_highlighting() ? 1 : 0);
    }

    if (strcmp(verb, "check") == 0) {
        return qt_check_verb(expr, argc);
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
        /* THE SAME RULES, WHETHER OR NOT ANYBODY CALLED `qt.check`. A
         * declaration met them at LOAD time; a built record meets them here,
         * because a validator nobody is obliged to call is necessary and not
         * sufficient -- `rules.validate` refused a malformed stack all along
         * and `rules.evaluate` still answered `ok: true` for a string, since
         * nothing obliged a caller to validate first. A declared window has
         * already passed, so this costs one walk over a small record and
         * changes nothing it says. */
        message[0] = '\0';
        if (qt_check_window_record(&win, message, sizeof(message)) > 0) {
            qt_raise(message);
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
            /* WHERE A HIGHLIGHTING DEFINITION COMES FROM, and it is the same
             * two-step the stdlib loader uses: an environment override for the
             * source tree, then the path compiled in at build time. Registered
             * ONCE, here, because `gb_qt_syntax_path` is cheap and idempotent
             * and this is the one place that runs before any `source` exists.
             *
             * IT IS NOT OPTIONAL POLISH. Measured 2026-10-18: with no path at
             * all this machine still answers `gBASIC`, from a copy installed
             * by hand into ~/.local/share/org.kde.syntax-highlighting in June,
             * while a machine without that copy answers nothing -- and an
             * absent definition still formats every line with the theme's
             * normal text, so the editor looks entirely ordinary and
             * highlights nothing. The dev machine was the only one where this
             * would have appeared to work, which is the shape of defect this
             * tree has recorded three times from the other direction. */
            const char *syn = getenv("GBASIC_SYNTAX_PATH");
            if (syn && *syn) {
                gb_qt_syntax_path(syn);
            }
            /* RELATIVE TO THE BINARY BEFORE THE BAKED-IN PATH, which is
             * what a DOWNLOAD needs: a release tarball is relocatable and runs
             * where it is unpacked, so the absolute path compiled in at build
             * time names a directory that is not there. Same resolver the
             * stdlib uses, for the same reason. */
            const char *near = gb_exe_relative_syntax();
            if (near) {
                gb_qt_syntax_path(near);
            }
#ifdef GBASIC_DEFAULT_SYNTAX
            gb_qt_syntax_path(GBASIC_DEFAULT_SYNTAX);
#endif
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
    /* WHERE A PART SITS IN ITS GRID -- `{ row, column, across, down }`.
     *
     * THE REASON IS AN ORACLE, built before the fixture because the pattern
     * has now cost two increments: every part reads back BY NAME, so a widget
     * in the wrong CELL, or with the wrong SPAN, is invisible to every other
     * check -- and placement is the entire point of `cells`. The table's
     * captions and the menu bar's entries were the same problem.
     *
     * ASKED OF THE LAYOUT, walking up from the widget rather than from our own
     * record of where we put it: a remembered position would agree with a
     * placement that was wrong, which is no oracle at all. */
    if (strcmp(verb, "at") == 0) {
        if (argc != 2) {
            qt_raise("qt.at expects a window and a part's name");
            return value_null();
        }
        Value win = eval_expr(expr->as.call.args.items[0]);
        Value which = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending()) {
            value_free(win);
            value_free(which);
            return value_null();
        }
        if (!qt_window_record(&win, "at") || which.kind != VALUE_STRING) {
            if (!error_action_pending()) {
                qt_raise("qt.at expects the part's name as text");
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
        int row = 0, col = 0, across = 0, down = 0;
        int ok = gb_qt_cell_of(root, which.as.string, &row, &col, &across,
                               &down);
        if (!ok) {
            snprintf(message, sizeof(message),
                     "qt.at: `%s` is not in a `cells` in window `%s`",
                     which.as.string, wname ? wname : "");
            qt_raise(message);
            value_free(win);
            value_free(which);
            return value_null();
        }
        value_free(win);
        value_free(which);
        Value out = value_record(NULL, 0);
        record_set(&out, "row", value_number(row));
        record_set(&out, "column", value_number(col));
        record_set(&out, "across", value_number(across));
        record_set(&out, "down", value_number(down));
        return out;
    }

    /* SET ONE PROPERTY BY NAME -- the write half of the route `qt.read(win,
     * part, prop)` opened, and it was left out when that shipped.
     *
     * TWO REASONS, AND THE FIRST IS THE ORDINARY ONE: a program wants to
     * change a widget after something happened -- enable a button once a form
     * validates, grey a menu item, make a field read-only on a condition --
     * and until now the ONLY way to set a property was the declaration, which
     * is written before anything has happened. That was a gap with no
     * workaround, not a convenience.
     *
     * THE SECOND IS A MEASURED GRAMMAR LIMIT: a container may carry A NAME OR
     * OPTIONS AND NOT BOTH (`tabs settings( movable: true )` with a body costs
     * four reduce/reduce conflicts and bison calls the rule useless), so a
     * NAMED container cannot be configured in its head. With this it is
     * configured at run time instead, which makes that limit affordable rather
     * than something the author has to design around. */
    if (strcmp(verb, "set") == 0) {
        if (argc != 4) {
            qt_raise("qt.set expects a window, a part, a property and a value");
            return value_null();
        }
        Value win = eval_expr(expr->as.call.args.items[0]);
        Value which = eval_expr(expr->as.call.args.items[1]);
        Value prop = eval_expr(expr->as.call.args.items[2]);
        Value v = eval_expr(expr->as.call.args.items[3]);
        if (error_action_pending()) {
            value_free(win); value_free(which);
            value_free(prop); value_free(v);
            return value_null();
        }
        if (!qt_window_record(&win, "set") || which.kind != VALUE_STRING ||
            prop.kind != VALUE_STRING) {
            if (!error_action_pending()) {
                qt_raise("qt.set expects the part's name and the property's "
                         "name as text");
            }
            value_free(win); value_free(which);
            value_free(prop); value_free(v);
            return value_null();
        }
        const char *wname = qt_record_text(&win, "name");
        void *root = wname ? qt_window_find(wname) : NULL;
        if (!root) {
            snprintf(message, sizeof(message), "window `%s` is not open",
                     wname ? wname : "");
            qt_raise(message);
            value_free(win); value_free(which);
            value_free(prop); value_free(v);
            return value_null();
        }
        /* THROUGH THE SAME DOOR THE DECLARATION'S OPTIONS GO THROUGH, so a
         * property the widget does not carry is refused BY NAME here too --
         * the two halves of one route must agree about what a property name
         * is, which is the rule `qt.read`'s own refusal already follows. */
        err[0] = '\0';
        int ok = 0;
        switch (v.kind) {
        case VALUE_STRING:
            ok = gb_qt_set_part_text(root, which.as.string, prop.as.string,
                                     v.as.string, err, sizeof(err));
            break;
        case VALUE_NUMBER:
            ok = gb_qt_set_part_number(root, which.as.string, prop.as.string,
                                       v.as.num.value, err, sizeof(err));
            break;
        case VALUE_BOOL:
            ok = gb_qt_set_part_bool(root, which.as.string, prop.as.string,
                                     v.as.boolean, err, sizeof(err));
            break;
        default:
            snprintf(err, sizeof(err),
                     "a property takes text, a number or true/false");
            break;
        }
        value_free(win); value_free(which);
        value_free(prop); value_free(v);
        if (!ok) {
            snprintf(message, sizeof(message), "qt.set: %s",
                     err[0] ? err : "that property was refused");
            qt_raise(message);
            return value_null();
        }
        return value_bool(1);
    }

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

    /* `qt.refresh(win, table)` -- SHOW THE LIST AS IT IS NOW.
     *
     * MEASURED, AND IT WAS A GAP RATHER THAN A LIMIT: a `table to show`
     * captured its subject when the window opened and NEVER FOLLOWED IT, so
     * appending to the list left `rowCount` at its old value and a program had
     * no way to say otherwise. For a game that is awkward; for anything
     * list-driven -- orders arriving, a search result, a filtered view -- it
     * is fatal, and Studio's file tree is exactly that shape.
     *
     * IT IS THE SYMMETRIC HALF OF `qt.subject`, which is what settled the
     * design: §2.2's watcher pair could not do write-back, so a record is read
     * from the widgets AT THE MOMENT ASKED. This is the same principle the
     * other way -- the list is read from the frame AT THE MOMENT ASKED, with
     * nothing tracked in between, so no change can be missed.
     *
     * THE CAPTURED RECORD IS UPDATED TOO. A later `qt.subject` folds the
     * widgets back over what the form captured at open, and leaving that stale
     * would make a refreshed table fold into the OLD list -- a plausible
     * record built from two different moments. */
    /* `qt.options(win, part, list)` -- a `choice`'S LIST, FROM DATA.
     *
     * The same principle as `qt.refresh` one widget along: the list is read AT
     * THE MOMENT ASKED, nothing tracked in between. Here it is passed rather
     * than named, because a choice has no declared subject to re-read -- which
     * is the honest difference: a table says `to show lines` and a choice does
     * not say anything.
     *
     * CLEARING RESETS THE SELECTION and that is deliberate: a list that has
     * changed may not contain what was chosen, and keeping an index into a
     * list that has moved is how a form comes to report a value nobody picked.
     * A program that wants the old choice back asks for it with `qt.write`,
     * which refuses one that is not in the new list. */
    if (strcmp(verb, "options") == 0) {
        if (argc != 3) {
            qt_raise("qt.options expects a window, a part's name and a list");
            return value_null();
        }
        Value win = eval_expr(expr->as.call.args.items[0]);
        Value which = eval_expr(expr->as.call.args.items[1]);
        Value list = eval_expr(expr->as.call.args.items[2]);
        if (error_action_pending()) {
            value_free(win); value_free(which); value_free(list);
            return value_null();
        }
        if (!qt_window_record(&win, "options") || which.kind != VALUE_STRING ||
            list.kind != VALUE_ARRAY) {
            if (!error_action_pending()) {
                qt_raise("qt.options expects a window, the part's name as text "
                         "and a list");
            }
            value_free(win); value_free(which); value_free(list);
            return value_null();
        }
        const char *wname = qt_record_text(&win, "name");
        void *root = wname ? qt_window_find(wname) : NULL;
        if (!root) {
            snprintf(message, sizeof(message), "window `%s` is not open",
                     wname ? wname : "");
            qt_raise(message);
            value_free(win); value_free(which); value_free(list);
            return value_null();
        }
        if (!qt_fill_choice(root, which.as.string, &list, "qt.options")) {
            value_free(win); value_free(which); value_free(list);
            return value_null();
        }
        size_t n = list.as.array.store ? list.as.array.store->count : 0;
        value_free(win); value_free(which); value_free(list);
        return value_number((double)n);
    }

    if (strcmp(verb, "refresh") == 0) {
        if (argc != 2) {
            qt_raise("qt.refresh expects a window and the table's name");
            return value_null();
        }
        Value win = eval_expr(expr->as.call.args.items[0]);
        Value which = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending()) {
            value_free(win); value_free(which);
            return value_null();
        }
        if (!qt_window_record(&win, "refresh") || which.kind != VALUE_STRING) {
            if (!error_action_pending()) {
                qt_raise("qt.refresh expects a window and the table's name as "
                         "text");
            }
            value_free(win); value_free(which);
            return value_null();
        }
        const char *wname = qt_record_text(&win, "name");
        void *root = wname ? qt_window_find(wname) : NULL;
        RecordField *items = record_find(&win, "items");
        Value *table = (root && items && items->value &&
                        items->value->kind == VALUE_ARRAY)
                           ? qt_find_table(items->value, which.as.string)
                           : NULL;
        if (!root) {
            snprintf(message, sizeof(message), "window `%s` is not open",
                     wname ? wname : "");
            qt_raise(message);
            value_free(win); value_free(which);
            return value_null();
        }
        /* A TABLE'S NAME **OR** A FORM'S SUBJECT, because a form HAS no name
         * -- §0.6 refused one both ways, a form's parts being addressed by
         * their own names where a table shows many rows. So the one thing a
         * form can be addressed by is the subject it declares, and this verb
         * accepts either rather than growing a second one.
         *
         * AN AMBIGUITY IS REFUSED AND NOT RESOLVED. A table called `orders`
         * beside a form `to edit orders` is a window where the word means two
         * things, and picking one silently is how a program refreshes
         * something other than what the author named. */
        int subject_nodes = 0;
        if (items && items->value && items->value->kind == VALUE_ARRAY) {
            Value *probe = qt_subject_slot(which.as.string);
            int drawn_probe = 0;
            (void)probe; (void)drawn_probe;
            subject_nodes = qt_count_subject(items->value, which.as.string);
        }
        if (table && subject_nodes > 0) {
            snprintf(message, sizeof(message),
                     "qt.refresh: window `%s` has both a table named `%s` and "
                     "%d part(s) bound to a subject called `%s`, so the name "
                     "means two things here -- rename one",
                     wname ? wname : "", which.as.string, subject_nodes,
                     which.as.string);
            qt_raise(message);
            value_free(win); value_free(which);
            return value_null();
        }
        if (!table && subject_nodes == 0) {
            snprintf(message, sizeof(message),
                     "qt.refresh: window `%s` declares no table named `%s` and "
                     "nothing bound to a subject of that name",
                     wname ? wname : "", which.as.string);
            qt_raise(message);
            value_free(win); value_free(which);
            return value_null();
        }
        const char *subject = table ? qt_record_text(table, "subject") : NULL;
        if (table && !subject) {
            snprintf(message, sizeof(message),
                     "qt.refresh: table `%s` is bound to nothing to refresh "
                     "from", which.as.string);
            qt_raise(message);
            value_free(win); value_free(which);
            return value_null();
        }
        /* THE SAME NON-RAISING WALK `qt.open` USES, so the sentence about a
         * missing subject is the one an author has already met. */
        const char *from = table ? subject : which.as.string;
        Value *slot = qt_subject_slot(from);
        if (!slot) {
            snprintf(message, sizeof(message),
                     "qt.refresh: `%s` is not in scope here -- it is read from "
                     "the frame that calls qt.refresh, as qt.open reads it from "
                     "the frame that calls that", from);
            qt_raise(message);
            value_free(win); value_free(which);
            return value_null();
        }
        /* THE KIND IS CHECKED WHERE IT IS KNOWN. A table-by-name needs a
         * list; a subject may fill a form (a record) or a table (a list) and
         * the walker checks each against the node it found, so the sentence
         * names which of the two was wanted. */
        if (table && slot->kind != VALUE_ARRAY) {
            snprintf(message, sizeof(message),
                     "qt.refresh: `%s` needs a list of records, and it holds %s",
                     subject, value_kind_name(slot->kind));
            qt_raise(message);
            value_free(win); value_free(which);
            return value_null();
        }
        int drawn = 0;
        if (table) {
            if (!qt_populate_table(root, table, slot, wname ? wname : "")) {
                value_free(win); value_free(which);
                return value_null();
            }
            drawn = (int)(slot->as.array.store ? slot->as.array.store->count : 0);
        } else {
            int found = 0;
            if (!qt_reshow_subject(root, items->value, which.as.string, slot,
                                   wname ? wname : "", &drawn, &found)) {
                value_free(win); value_free(which);
                return value_null();
            }
        }
        /* AND THE CAPTURE MOVES WITH IT. */
        QtOpenWindow *entry = wname ? qt_window_entry(wname) : NULL;
        for (size_t i = 0; entry && i < entry->binding_count; i++) {
            if (strcmp(entry->bindings[i].subject, from) == 0) {
                value_free(entry->bindings[i].record);
                entry->bindings[i].record = value_copy(*slot);
                break;
            }
        }
        value_free(win); value_free(which);
        return value_number((double)drawn);
    }

    /* WHAT IS ACTUALLY HIGHLIGHTING THIS EDITOR -- the oracle, answering a
     * record so that each of the three things it witnesses is separable.
     *
     * `unknown` RATHER THAN A RECORD OF EMPTIES when nothing is applied,
     * because "no highlighting" and "highlighting with no name" are different
     * claims, and the first is an ordinary state: a `source` with no
     * `language:` is a perfectly good plain code editor.
     *
     * WHY A VERB AND NOT A PROPERTY: a highlighter is an object attached to a
     * document, so the by-name route has nothing to read. And why it answers
     * the FILE as well as the name -- a definition the user installed in their
     * own home SHADOWS the one we ship and reports the identical name
     * (measured, 7 ranges against 8), so the name alone cannot witness which
     * file was used. That precedence is deliberate, being the rule `load`
     * already follows; what would not be acceptable is being unable to see
     * it. */
    if (strcmp(verb, "highlighting") == 0) {
        if (argc != 2) {
            qt_raise("qt.highlighting expects a window and a part's name");
            return value_null();
        }
        Value win = eval_expr(expr->as.call.args.items[0]);
        Value which = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending()) {
            value_free(win); value_free(which);
            return value_null();
        }
        if (!qt_window_record(&win, "highlighting") ||
            which.kind != VALUE_STRING) {
            if (!error_action_pending()) {
                qt_raise("qt.highlighting expects a window and the part's "
                         "name as text");
            }
            value_free(win); value_free(which);
            return value_null();
        }
        const char *wname = qt_record_text(&win, "name");
        void *root = wname ? qt_window_find(wname) : NULL;
        if (!root) {
            snprintf(message, sizeof(message), "window `%s` is not open",
                     wname ? wname : "");
            qt_raise(message);
            value_free(win); value_free(which);
            return value_null();
        }
        char *lang = gb_qt_highlight_language(root, which.as.string);
        if (!lang) {
            value_free(win); value_free(which);
            return value_unknown();
        }
        char *file = gb_qt_highlight_file(root, which.as.string);
        Value out = value_record(NULL, 0);
        record_set(&out, "language", value_string(lang));
        record_set(&out, "file", value_string(file ? file : ""));
        free(lang);
        free(file);
        value_free(win); value_free(which);
        return out;
    }

    /* ---- DIALOGS (§0.15) --------------------------------------------------
     *
     * A DIALOG HANGS OFF A WINDOW, which is why each takes one: Qt centres the
     * box on it and treats it as modal to that window, and a box parented to
     * nothing appears wherever the platform feels like putting it.
     *
     * THE NESTED EVENT LOOP IS THE WHOLE HAZARD AND IT WAS PRICED BEFORE ANY
     * DIALOG EXISTED. §5.1 ruled out direction B on exactly this case --
     * gBASIC's own poll does not run for the dialog's life, measured at a
     * 651ms gap over a 600ms dialog, which is a webserver answering nothing --
     * while direction A serves the sources throughout. So a `timer`, a
     * webserver and an actor mailbox all keep running while a box is up, and
     * that is asserted through the interpreter rather than taken on trust.
     */
    if (strcmp(verb, "inform") == 0 || strcmp(verb, "confirm") == 0) {
        int asking = strcmp(verb, "confirm") == 0;
        if (argc != 2) {
            snprintf(message, sizeof(message),
                     "qt.%s expects a window and a message", verb);
            qt_raise(message);
            return value_null();
        }
        Value win = eval_expr(expr->as.call.args.items[0]);
        Value text = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending()) {
            value_free(win); value_free(text);
            return value_null();
        }
        if (!qt_window_record(&win, verb) || text.kind != VALUE_STRING) {
            if (!error_action_pending()) {
                snprintf(message, sizeof(message),
                         "qt.%s expects a window and the message as text", verb);
                qt_raise(message);
            }
            value_free(win); value_free(text);
            return value_null();
        }
        const char *wname = qt_record_text(&win, "name");
        void *root = wname ? qt_window_find(wname) : NULL;
        if (!root) {
            /* A BOX NEEDS A WINDOW TO HANG OFF, so "window `x` is not open" is
             * usually the fact to act on -- but NOT when the reason it is not
             * open is that there is no display, which is the commonest way to
             * arrive here and the one case where that sentence names the wrong
             * cause. This tree has recorded that class five times; it costs
             * three lines to avoid. */
            if (!gb_qt_can_display()) {
                snprintf(message, sizeof(message),
                         "no display is available, so qt.%s has no window to "
                         "put a dialog on. Guard with qt.can_display()", verb);
            } else {
                snprintf(message, sizeof(message), "window `%s` is not open",
                         wname ? wname : "");
            }
            qt_raise(message);
            value_free(win); value_free(text);
            return value_null();
        }
        err[0] = '\0';
        int answered = 0;
        int ok = asking
            ? gb_qt_confirm(root, text.as.string, &answered, err, sizeof(err))
            : gb_qt_inform(root, text.as.string, err, sizeof(err));
        if (!ok) {
            snprintf(message, sizeof(message), "qt.%s: %s", verb,
                     err[0] ? err : "the dialog could not be shown");
            qt_raise(message);
            value_free(win); value_free(text);
            return value_null();
        }
        value_free(win); value_free(text);
        /* A BOOLEAN, NOT A BUTTON CODE. Two buttons is the whole of what
         * `confirm` offers; anything richer is a window shown modally, which
         * is a different thing with a layout of its own. */
        return asking ? value_bool(answered) : value_bool(1);
    }

    /* ANSWER THE DIALOG THAT IS OPEN, from the program rather than a person.
     * Not a test hook, though it is what lets a suite drive one without a
     * human: a dialog that times out, one dismissed because the data under it
     * changed, and an unattended run all need it -- and `qt.invoke` is already
     * here on the same argument. It is only reachable from a `watch` body or a
     * handler, because that is the only code that runs while a box is up. */
    if (strcmp(verb, "answer") == 0) {
        if (argc != 1) {
            qt_raise("qt.answer expects one value -- true or false for a message box, or a path for a file chooser");
            return value_null();
        }
        Value v = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(v);
            return value_null();
        }
        /* A PATH ANSWERS A CHOOSER, because that dialog's answer is not yes
         * or no. The two kinds are told apart by the BACKEND rather than here,
         * so each refusal can name which dialog is actually open -- `true` on
         * a chooser does not say which file, and a path on a message box is
         * not one of its two buttons. Both are sentences rather than guesses.
         */
        /* A PATH MAY ARRIVE AS A REFERENCE here too, for the reason the
         * chooser's own options do: `{dir}` is what `temp_dir()` answers and
         * `{file}` is what an author holds after choosing one, so refusing
         * them would make the obvious spelling the wrong one. */
        const char *given = NULL;
        if (v.kind == VALUE_STRING)    { given = v.as.string; }
        else if (v.kind == VALUE_FILE) { given = v.as.file_path; }
        else if (v.kind == VALUE_DIR)  { given = v.as.dir_path; }
        if (given) {
            err[0] = '\0';
            int pok = gb_qt_answer_path(given, err, sizeof(err));
            value_free(v);
            if (!pok) {
                snprintf(message, sizeof(message), "qt.answer: %s",
                         err[0] ? err : "the dialog could not be answered");
                qt_raise(message);
                return value_null();
            }
            return value_bool(1);
        }
        /* `value_truthy` REPORTS A REFUSAL rather than guessing: gBASIC
         * raises on `not unknown` and friends, so a value that has no truth
         * must say so here instead of being silently read as `false` -- which
         * on a confirm would be the safe answer by accident rather than by
         * decision. */
        int refused = 0;
        int yes = value_truthy(v, &refused);
        value_free(v);
        if (refused || error_action_pending()) {
            if (!error_action_pending()) {
                qt_raise("qt.answer expects true or false, or a path for a file chooser");
            }
            return value_null();
        }
        err[0] = '\0';
        if (!gb_qt_answer(yes, err, sizeof(err))) {
            snprintf(message, sizeof(message), "qt.answer: %s",
                     err[0] ? err : "the dialog could not be answered");
            qt_raise(message);
            return value_null();
        }
        return value_bool(1);
    }

    /* ---- A FILE CHOOSER (§0.16) -------------------------------------------
     *
     * THREE VERBS, NOT ONE WITH A MODE, because they ask three different
     * questions -- "which existing file?", "what shall I call it?", "which
     * folder?" -- which is also how Qt splits them, and which lets each one
     * refuse the options that mean nothing to it rather than ignore them.
     *
     * `qt.open_file` WAS REJECTED AS A NAME: it reads as though it opens the
     * file, and `f = qt.open_file(app)` looks like it answers contents or a
     * handle. These verbs CHOOSE; the program does the reading.
     *
     * THE ANSWER IS TEXT, and that was a decision rather than the easy route.
     * `{file}` and `{dir}` both accept a path that is not there (measured), so
     * a typed answer was available -- but three sibling verbs would then answer
     * TWO kinds, and the rule this backend already follows is that a kind comes
     * from the DECLARATION and not from Qt (§0.5: a widget holds text, so
     * folding back blindly turns `{ id: 42 }` into `{ id: "42" }`). A chooser
     * has no declaration to read a kind from, and what Qt holds is a path. So
     * text, and `{file}qt.choose_file(app)` where a typed value is wanted --
     * one inline modifier, which is the spelling 0.5.0 added for exactly this.
     *
     * CANCELLING ANSWERS `unknown`, NEVER "". The empty string is a plausible
     * wrong answer: `if path = "" then` happens to work while
     * `read({file}path)` does not, so an author who forgot to check gets a
     * puzzle instead of a raise. `unknown` is what `qt.selected` already
     * answers for "nothing is chosen" and what `qt.highlighting` answers for
     * "no definition", and `if path then` on it RAISES, which is loud. */
    if (strcmp(verb, "choose_file") == 0 || strcmp(verb, "choose_save") == 0 ||
        strcmp(verb, "choose_folder") == 0) {
        int kind = GB_QT_PICK_OPEN;
        if (strcmp(verb, "choose_save") == 0)   { kind = GB_QT_PICK_SAVE; }
        if (strcmp(verb, "choose_folder") == 0) { kind = GB_QT_PICK_FOLDER; }
        if (argc < 1 || argc > 2) {
            snprintf(message, sizeof(message),
                     "qt.%s expects a window, and optionally a record of "
                     "options", verb);
            qt_raise(message);
            return value_null();
        }
        Value win = eval_expr(expr->as.call.args.items[0]);
        Value opts = value_null();
        if (argc == 2) {
            opts = eval_expr(expr->as.call.args.items[1]);
        }
        if (error_action_pending()) {
            value_free(win); value_free(opts);
            return value_null();
        }
        if (!qt_window_record(&win, verb)) {
            value_free(win); value_free(opts);
            return value_null();
        }
        if (argc == 2 && opts.kind != VALUE_RECORD) {
            snprintf(message, sizeof(message),
                     "qt.%s expects its options as a record -- "
                     "`qt.%s(app, { title: \"Open\" })`", verb, verb);
            qt_raise(message);
            value_free(win); value_free(opts);
            return value_null();
        }
        /* THE OPTION SET IS CLOSED AND REFUSED BY NAME, which is the rule this
         * backend follows everywhere a vocabulary can be closed -- and here it
         * can, there being four options rather than the 75 properties a
         * QPushButton exposes. An ignored misspelling is a chooser that opens
         * in the wrong folder, with the wrong filter, and says nothing. */
        static const char *const pick_opts[] = {
            "title", "folder", "name", "filter", NULL
        };
        const char *title = NULL, *folder = NULL, *pname = NULL;
        char filter[1024];
        filter[0] = '\0';
        if (argc == 2) {
            for (size_t i = 0; i < opts.as.record.count; i++) {
                RecordField *f = &opts.as.record.fields[i];
                const char *fname = f->name ? f->name : "";
                int known = 0;
                for (size_t k = 0; pick_opts[k]; k++) {
                    if (strcmp(pick_opts[k], fname) == 0) { known = 1; break; }
                }
                if (!known) {
                    const char *near = gb_nearest_word(fname, pick_opts);
                    if (near) {
                        snprintf(message, sizeof(message),
                                 "qt.%s does not know the option `%s` -- did "
                                 "you mean `%s`?", verb, fname, near);
                    } else {
                        snprintf(message, sizeof(message),
                                 "qt.%s does not know the option `%s`. It "
                                 "takes title, folder, name and filter", verb,
                                 fname);
                    }
                    qt_raise(message);
                    value_free(win); value_free(opts);
                    return value_null();
                }
                /* TWO OPTIONS MEAN NOTHING TO A FOLDER CHOOSER and Qt would
                 * IGNORE both, which is the silent-no-op this module refuses
                 * elsewhere: a folder has no name filter, and `folder:` is
                 * already how you say where to start, so a second spelling
                 * would be two ways to say one thing. */
                if (kind == GB_QT_PICK_FOLDER &&
                    (strcmp(fname, "filter") == 0 ||
                     strcmp(fname, "name") == 0)) {
                    snprintf(message, sizeof(message),
                             "qt.choose_folder has no use for `%s` -- a folder "
                             "has no name filter, and `folder:` is where it "
                             "starts", fname);
                    qt_raise(message);
                    value_free(win); value_free(opts);
                    return value_null();
                }
                if (strcmp(fname, "filter") == 0) {
                    /* TEXT OR A LIST. The list is joined with Qt's own `;;`
                     * HERE rather than being the author's problem -- two
                     * semicolons meaning "or" is a Qt-ism nobody should have
                     * to know, and a single string passes through so somebody
                     * who does know it loses nothing. */
                    if (f->value && f->value->kind == VALUE_STRING) {
                        snprintf(filter, sizeof(filter), "%s",
                                 f->value->as.string);
                    } else if (f->value && f->value->kind == VALUE_ARRAY) {
                        size_t used = 0;
                        for (size_t e = 0; e < f->value->as.array.store->count; e++) {
                            Value *el = &f->value->as.array.store->items[e];
                            if (el->kind != VALUE_STRING) {
                                snprintf(message, sizeof(message),
                                         "qt.%s expects every filter to be "
                                         "text, like \"Programs (*.bas)\"",
                                         verb);
                                qt_raise(message);
                                value_free(win); value_free(opts);
                                return value_null();
                            }
                            int wrote = snprintf(filter + used,
                                                 sizeof(filter) - used, "%s%s",
                                                 used ? ";;" : "",
                                                 el->as.string);
                            if (wrote < 0 ||
                                (size_t)wrote >= sizeof(filter) - used) {
                                break;
                            }
                            used += (size_t)wrote;
                        }
                    } else {
                        snprintf(message, sizeof(message),
                                 "qt.%s expects `filter` as text or a list of "
                                 "text, like \"Programs (*.bas)\"", verb);
                        qt_raise(message);
                        value_free(win); value_free(opts);
                        return value_null();
                    }
                    continue;
                }
                /* A PATH MAY ARRIVE AS A REFERENCE, which was a gap rather
                 * than a rule and the cookbook found it: `temp_dir()` answers
                 * a `{dir}`, so the most natural `folder:` there is was
                 * refused as "not text". Every path builtin in this language
                 * already takes a string, a `{file}` or a `{dir}` alike, and a
                 * GUI module is the wrong place to be stricter. `title` is
                 * prose and stays text. */
                const char *as_path = NULL;
                if (f->value) {
                    if (f->value->kind == VALUE_STRING) {
                        as_path = f->value->as.string;
                    } else if (f->value->kind == VALUE_FILE) {
                        as_path = f->value->as.file_path;
                    } else if (f->value->kind == VALUE_DIR) {
                        as_path = f->value->as.dir_path;
                    }
                }
                if (strcmp(fname, "title") == 0 &&
                    (!f->value || f->value->kind != VALUE_STRING)) {
                    snprintf(message, sizeof(message),
                             "qt.%s expects `title` as text", verb);
                    qt_raise(message);
                    value_free(win); value_free(opts);
                    return value_null();
                }
                if (!as_path) {
                    snprintf(message, sizeof(message),
                             "qt.%s expects `%s` as text, a {file} or a {dir}",
                             verb, fname);
                    qt_raise(message);
                    value_free(win); value_free(opts);
                    return value_null();
                }
                if (strcmp(fname, "title") == 0)  { title = as_path; }
                if (strcmp(fname, "folder") == 0) { folder = as_path; }
                if (strcmp(fname, "name") == 0)   { pname = as_path; }
            }
        }
        const char *wname = qt_record_text(&win, "name");
        void *root = wname ? qt_window_find(wname) : NULL;
        if (!root) {
            /* THE DISPLAY, NOT THE WINDOW, when that is the real cause -- the
             * correction §0.15 records, made once and then applied rather than
             * rediscovered. */
            if (!gb_qt_can_display()) {
                snprintf(message, sizeof(message),
                         "no display is available, so qt.%s has no window to "
                         "put a chooser on. Guard with qt.can_display()", verb);
            } else {
                snprintf(message, sizeof(message), "window `%s` is not open",
                         wname ? wname : "");
            }
            qt_raise(message);
            value_free(win); value_free(opts);
            return value_null();
        }
        err[0] = '\0';
        int cancelled = 0;
        char *path = gb_qt_choose(root, kind, title, folder, pname,
                                  filter[0] ? filter : NULL, &cancelled, err,
                                  sizeof(err));
        value_free(win); value_free(opts);
        if (!path) {
            if (cancelled) {
                return value_unknown();
            }
            snprintf(message, sizeof(message), "qt.%s: %s", verb,
                     err[0] ? err : "the chooser could not be shown");
            qt_raise(message);
            return value_null();
        }
        Value out = value_string(path);
        free(path);
        return out;
    }

    /* `qt.dialog()` -- WHAT IS OPEN, so a program driving its own dialogs is
     * not driving blind.
     *
     * BUILT BEFORE THE FIXTURE, which is the discipline §0.8 arrived at the
     * hard way: `title:` and `filter:` would otherwise be accepted and
     * unobservable, which is the hole a leaf's `label:` sat in for four
     * increments while every check passed. It is not a test hook for the same
     * reason `qt.highlighting` is not -- being unable to see what landed is
     * being unable to tell an option that worked from one that did nothing. */
    if (strcmp(verb, "dialog") == 0) {
        if (argc != 0) {
            qt_raise("qt.dialog takes nothing -- it answers the dialog that "
                     "is open");
            return value_null();
        }
        char *dkind = NULL, *dtitle = NULL, *dtext = NULL, *dfolder = NULL,
             *dfilter = NULL;
        int open = gb_qt_dialog_info(&dkind, &dtitle, &dtext, &dfolder,
                                     &dfilter);
        if (!open) {
            /* `unknown` RATHER THAN AN EMPTY RECORD, the distinction
             * `principal()` already makes: "nothing is open" and "a dialog
             * carrying no fields" are different claims. */
            return value_unknown();
        }
        Value out = value_record(NULL, 0);
        record_set(&out, "kind", value_string(dkind ? dkind : "dialog"));
        record_set(&out, "title", value_string(dtitle ? dtitle : ""));
        if (dtext) {
            record_set(&out, "text", value_string(dtext));
        }
        if (dfolder) {
            record_set(&out, "folder", value_string(dfolder));
        }
        if (dfilter) {
            record_set(&out, "filter", value_string(dfilter));
        }
        free(dkind); free(dtitle); free(dtext); free(dfolder); free(dfilter);
        return out;
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
