/* src/modules/rowmodel.c — NAP-12 native row-model adapter (`rowmodel.*`).
 *
 * This is a TRANSLATION-UNIT INCLUDE: it is `#include`d into src/eval.c (not a
 * separate object file) so it can use eval.c's file-static Value API
 * (value_number, value_bool, gi_canonical_wrap, runtime_error_raise, ...),
 * exactly like src/modules/xml.c. See that file's header for the rationale.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS IS THE ONE JUSTIFIED NATIVE COMPONENT
 * ---------------------------------------------------------------------------
 * Everything else in the Native Application Platform is driven from gBASIC
 * through the generic `gi` bridge, with no new C. A virtualized grid cannot be,
 * for one specific reason: GTK4's `GtkColumnView`/`GtkListView` only virtualize
 * when fed a `GListModel`, and `GListModel` is a GObject *interface*. Providing
 * one from gBASIC would require implementing an interface's vfuncs at runtime —
 * i.e. runtime subclassing (WI-9), which the platform deliberately does NOT do.
 *
 * So this file contributes exactly two fixed, hand-written GTypes and nothing
 * else. It is not a DataGrid; it is the smallest possible bridge that lets a
 * DataGrid be written in gBASIC (see stdlib/datagrid.bas):
 *
 *   GbRowModel  — implements GListModel over a plain row COUNT. Holds no data.
 *   GbRow       — a tiny row proxy GObject carrying {index, grid-id}.
 *
 * ---------------------------------------------------------------------------
 * THE KEY DESIGN DECISION: NO INTERPRETER RE-ENTRY FROM MODEL VFUNCS
 * ---------------------------------------------------------------------------
 * The obvious design has the model call a gBASIC `count()`/`row(i)` accessor.
 * This file deliberately does NOT do that. GTK calls `g_list_model_get_n_items`
 * and `get_item` from deep inside its own layout/measure machinery, at high
 * frequency, and re-entering a tree-walking interpreter there means an
 * interpreter raise could unwind through GTK C frames.
 *
 * Instead the model is DATA-FREE. It knows only:
 *   - how many rows there are (a count gBASIC sets explicitly), and
 *   - which grid it belongs to (an opaque integer id).
 *
 * `get_item(i)` therefore allocates a 2-field proxy object and returns — no
 * interpreter involvement, no gBASIC value touched, nothing that can raise.
 * The actual data lookup happens later, in the `GtkSignalListItemFactory`
 * "bind" signal handler, which is ordinary gBASIC on the ordinary gi.connect
 * signal path (already proven, already error-contained by gi_signal_marshal).
 *
 * The consequence is the ownership story is trivial: this module retains NO
 * gBASIC Value and NO gBASIC function reference. There is nothing to copy, free,
 * or tear down at interpreter shutdown, and no lifetime coupling between the
 * GTK object graph and the interpreter heap. The backing data stays in gBASIC,
 * owned by gBASIC, and is never duplicated into a native structure.
 *
 * Threading: the model is main-thread-only, like all of `gi`. It takes no locks
 * and creates no threads. GTK only touches a model from the thread running the
 * main loop, which is the interpreter's own thread.
 */
#if HAVE_GIR && HAVE_GIO

#include <gio/gio.h>

#define ROWMODEL_ERROR_CODE 6101

static void rowmodel_raise(const char *message) {
    runtime_error_raise(message, ROWMODEL_ERROR_CODE, "rowmodel");
}

/* ---------------------------------------------------------------------------
 * GbRow — the row proxy.
 *
 * One of these exists per row currently realized by GTK (i.e. bounded by the
 * visible window plus GTK's recycling slack), NOT per row in the dataset. It
 * carries no cell data at all: just the row index and the id of the grid it came
 * from. A gBASIC "bind" handler reads both and fetches the real values from
 * whatever gBASIC-side source that grid uses.
 *
 * `grid-id` exists because gBASIC functions are references, not closures
 * (docs/first_class_functions_design.md) — a shared bind handler cannot capture
 * its grid, so the row must tell it which grid to consult.
 * ------------------------------------------------------------------------- */

#define GB_TYPE_ROW (gb_row_get_type())
G_DECLARE_FINAL_TYPE(GbRow, gb_row, GB, ROW, GObject)

struct _GbRow {
    GObject parent_instance;
    guint index;
    guint grid_id;
};

G_DEFINE_TYPE(GbRow, gb_row, G_TYPE_OBJECT)

enum { ROW_PROP_0, ROW_PROP_INDEX, ROW_PROP_GRID_ID, ROW_N_PROPS };
static GParamSpec *gb_row_props[ROW_N_PROPS];

static void gb_row_get_property(GObject *object, guint prop_id, GValue *value,
                                GParamSpec *pspec) {
    GbRow *self = GB_ROW(object);
    switch (prop_id) {
    case ROW_PROP_INDEX:   g_value_set_uint(value, self->index); break;
    case ROW_PROP_GRID_ID: g_value_set_uint(value, self->grid_id); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec); break;
    }
}

static void gb_row_class_init(GbRowClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->get_property = gb_row_get_property;
    /* Read-only: a row proxy is immutable once handed to GTK. The model makes a
     * fresh one per get_item rather than mutating a shared instance, so GTK's
     * recycling can never observe a row whose index changed underneath it. */
    gb_row_props[ROW_PROP_INDEX] =
        g_param_spec_uint("index", NULL, NULL, 0, G_MAXUINT, 0,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    gb_row_props[ROW_PROP_GRID_ID] =
        g_param_spec_uint("grid-id", NULL, NULL, 0, G_MAXUINT, 0,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_properties(object_class, ROW_N_PROPS, gb_row_props);
}

static void gb_row_init(GbRow *self) {
    self->index = 0;
    self->grid_id = 0;
}

static GbRow *gb_row_new(guint index, guint grid_id) {
    GbRow *row = g_object_new(GB_TYPE_ROW, NULL);
    row->index = index;
    row->grid_id = grid_id;
    return row;
}

/* ---------------------------------------------------------------------------
 * GbRowModel — the GListModel adapter.
 *
 * Implements exactly the three GListModel vfuncs. `n_items` is authoritative and
 * is set from gBASIC; the model never asks anyone how big it is. Changing it
 * goes through rowmodel.set_count / rowmodel.items_changed, which emit the
 * standard "items-changed" so GtkColumnView updates without rebuilding widgets.
 * ------------------------------------------------------------------------- */

#define GB_TYPE_ROW_MODEL (gb_row_model_get_type())
G_DECLARE_FINAL_TYPE(GbRowModel, gb_row_model, GB, ROW_MODEL, GObject)

struct _GbRowModel {
    GObject parent_instance;
    guint n_items;
    guint grid_id;
};

static void gb_row_model_list_model_init(GListModelInterface *iface);

G_DEFINE_TYPE_WITH_CODE(GbRowModel, gb_row_model, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(G_TYPE_LIST_MODEL,
                                              gb_row_model_list_model_init))

static GType gb_row_model_get_item_type(GListModel *list) {
    (void)list;
    return GB_TYPE_ROW;
}

static guint gb_row_model_get_n_items(GListModel *list) {
    return GB_ROW_MODEL(list)->n_items;
}

/* The virtualization hot path. Must be cheap, must never raise, must never
 * touch the interpreter. Allocating one small GObject is the entire cost, and
 * GTK calls it only for rows it is actually realizing. */
static gpointer gb_row_model_get_item(GListModel *list, guint position) {
    GbRowModel *self = GB_ROW_MODEL(list);
    if (position >= self->n_items) {
        return NULL;   /* GListModel contract: out of range yields NULL */
    }
    return gb_row_new(position, self->grid_id);   /* transfer-full */
}

static void gb_row_model_list_model_init(GListModelInterface *iface) {
    iface->get_item_type = gb_row_model_get_item_type;
    iface->get_n_items = gb_row_model_get_n_items;
    iface->get_item = gb_row_model_get_item;
}

static void gb_row_model_class_init(GbRowModelClass *klass) { (void)klass; }

static void gb_row_model_init(GbRowModel *self) {
    self->n_items = 0;
    self->grid_id = 0;
}

/* ---------------------------------------------------------------------------
 * gBASIC-facing builtins.
 *
 * Six calls, all thin. Anything that could be expressed in gBASIC is not here.
 * ------------------------------------------------------------------------- */

/* Shared argument plumbing: evaluate `expr`'s args, enforcing an exact arity. */
static int rowmodel_eval_args(AstExpr *expr, size_t want, Value *out,
                              const char *ctx) {
    size_t argc = expr->as.call.args.count;
    if (argc != want) {
        char msg[160];
        snprintf(msg, sizeof(msg), "%s expects %zu argument%s", ctx, want,
                 want == 1 ? "" : "s");
        rowmodel_raise(msg);
        return 0;
    }
    for (size_t i = 0; i < want; i++) {
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

/* Pull a non-negative whole number out of a Value, rejecting anything that would
 * silently truncate into a guint (negatives, fractions, overflow). */
static int rowmodel_uint_arg(Value v, const char *ctx, guint *out) {
    if (v.kind != VALUE_NUMBER) {
        char msg[160];
        snprintf(msg, sizeof(msg), "%s expects a number", ctx);
        rowmodel_raise(msg);
        return 0;
    }
    double d = v.as.number;
    if (d < 0 || d > (double)G_MAXUINT || d != (double)(guint)d) {
        char msg[160];
        snprintf(msg, sizeof(msg), "%s expects a non-negative whole number", ctx);
        rowmodel_raise(msg);
        return 0;
    }
    *out = (guint)d;
    return 1;
}

/* Pull a live GbRowModel out of a gBASIC gobject value. */
static int rowmodel_model_arg(Value v, const char *ctx, GbRowModel **out) {
    GObject *obj = NULL;
    if (!gi_object_arg(v, ctx, &obj)) {
        return 0;
    }
    if (!GB_IS_ROW_MODEL(obj)) {
        char msg[160];
        snprintf(msg, sizeof(msg), "%s expects a row model", ctx);
        rowmodel_raise(msg);
        return 0;
    }
    *out = GB_ROW_MODEL(obj);
    return 1;
}

/* rowmodel.new(grid_id) -> a GListModel with 0 rows, tagged with grid_id. */
static Value rowmodel_do_new(AstExpr *expr) {
    Value args[1];
    if (!rowmodel_eval_args(expr, 1, args, "rowmodel.new")) {
        return value_null();
    }
    guint grid_id = 0;
    if (!rowmodel_uint_arg(args[0], "rowmodel.new", &grid_id)) {
        value_free(args[0]);
        return value_null();
    }
    value_free(args[0]);
    GbRowModel *model = g_object_new(GB_TYPE_ROW_MODEL, NULL);
    model->grid_id = grid_id;
    /* have_ref: we hold the construction reference and hand it to the wrapper. */
    return gi_canonical_wrap(G_OBJECT(model), TRUE);
}

/* rowmodel.set_count(model, n) — set the row count and emit the single
 * items-changed that turns the old contents into the new ones. This is the
 * general "the data changed, reload" signal; GtkColumnView responds by
 * re-binding visible rows only, not by rebuilding widgets. */
static Value rowmodel_do_set_count(AstExpr *expr) {
    Value args[2];
    if (!rowmodel_eval_args(expr, 2, args, "rowmodel.set_count")) {
        return value_null();
    }
    GbRowModel *model = NULL;
    guint n = 0;
    if (!rowmodel_model_arg(args[0], "rowmodel.set_count", &model) ||
        !rowmodel_uint_arg(args[1], "rowmodel.set_count", &n)) {
        value_free(args[0]); value_free(args[1]);
        return value_null();
    }
    value_free(args[0]); value_free(args[1]);
    guint old = model->n_items;
    model->n_items = n;
    if (old || n) {
        g_list_model_items_changed(G_LIST_MODEL(model), 0, old, n);
    }
    return value_null();
}

/* rowmodel.count(model) -> current row count. */
static Value rowmodel_do_count(AstExpr *expr) {
    Value args[1];
    if (!rowmodel_eval_args(expr, 1, args, "rowmodel.count")) {
        return value_null();
    }
    GbRowModel *model = NULL;
    if (!rowmodel_model_arg(args[0], "rowmodel.count", &model)) {
        value_free(args[0]);
        return value_null();
    }
    value_free(args[0]);
    return value_number(model->n_items);
}

/* rowmodel.get_item(model, position) -> the row proxy at `position`, or nothing
 * when out of range.
 *
 * In production GTK calls the vfunc itself from C and gBASIC never needs this.
 * It exists because a hand-registered GType has no typelib, so the generic `gi`
 * method-call path cannot dispatch on it — without this accessor the adapter
 * would only be testable through a live GtkColumnView. It deliberately goes
 * through g_list_model_get_item (real interface dispatch, not a direct struct
 * read), so a headless test exercises exactly the code path GTK will. */
static Value rowmodel_do_get_item(AstExpr *expr) {
    Value args[2];
    if (!rowmodel_eval_args(expr, 2, args, "rowmodel.get_item")) {
        return value_null();
    }
    GbRowModel *model = NULL;
    guint position = 0;
    if (!rowmodel_model_arg(args[0], "rowmodel.get_item", &model) ||
        !rowmodel_uint_arg(args[1], "rowmodel.get_item", &position)) {
        value_free(args[0]); value_free(args[1]);
        return value_null();
    }
    value_free(args[0]); value_free(args[1]);
    GObject *row = g_list_model_get_item(G_LIST_MODEL(model), position);
    if (!row) {
        return value_null();
    }
    return gi_canonical_wrap(row, TRUE);   /* transfer-full from get_item */
}

/* rowmodel.items_changed(model, position, removed, added) — fine-grained
 * notification for callers that know exactly what moved, so GTK can splice
 * instead of reloading. The count is adjusted to match. */
static Value rowmodel_do_items_changed(AstExpr *expr) {
    Value args[4];
    if (!rowmodel_eval_args(expr, 4, args, "rowmodel.items_changed")) {
        return value_null();
    }
    GbRowModel *model = NULL;
    guint position = 0, removed = 0, added = 0;
    int ok = rowmodel_model_arg(args[0], "rowmodel.items_changed", &model) &&
             rowmodel_uint_arg(args[1], "rowmodel.items_changed", &position) &&
             rowmodel_uint_arg(args[2], "rowmodel.items_changed", &removed) &&
             rowmodel_uint_arg(args[3], "rowmodel.items_changed", &added);
    for (size_t i = 0; i < 4; i++) {
        value_free(args[i]);
    }
    if (!ok) {
        return value_null();
    }
    if (position > model->n_items || removed > model->n_items - position) {
        rowmodel_raise("rowmodel.items_changed: range outside the model");
        return value_null();
    }
    model->n_items = model->n_items - removed + added;
    g_list_model_items_changed(G_LIST_MODEL(model), position, removed, added);
    return value_null();
}

/* rowmodel.row_index(row) -> the 0-based row index a proxy stands for. */
static Value rowmodel_do_row_index(AstExpr *expr) {
    Value args[1];
    if (!rowmodel_eval_args(expr, 1, args, "rowmodel.row_index")) {
        return value_null();
    }
    GObject *obj = NULL;
    if (!gi_object_arg(args[0], "rowmodel.row_index", &obj)) {
        value_free(args[0]);
        return value_null();
    }
    if (!GB_IS_ROW(obj)) {
        value_free(args[0]);
        rowmodel_raise("rowmodel.row_index expects a row");
        return value_null();
    }
    guint index = GB_ROW(obj)->index;
    value_free(args[0]);
    return value_number(index);
}

/* rowmodel.row_grid(row) -> the grid id the proxy's model was tagged with. */
static Value rowmodel_do_row_grid(AstExpr *expr) {
    Value args[1];
    if (!rowmodel_eval_args(expr, 1, args, "rowmodel.row_grid")) {
        return value_null();
    }
    GObject *obj = NULL;
    if (!gi_object_arg(args[0], "rowmodel.row_grid", &obj)) {
        value_free(args[0]);
        return value_null();
    }
    if (!GB_IS_ROW(obj)) {
        value_free(args[0]);
        rowmodel_raise("rowmodel.row_grid expects a row");
        return value_null();
    }
    guint grid_id = GB_ROW(obj)->grid_id;
    value_free(args[0]);
    return value_number(grid_id);
}

/* rowmodel.is_row(v) -> true when v is a row proxy. Lets gBASIC bind handlers
 * validate what GTK handed them without risking a raise mid-render. */
static Value rowmodel_do_is_row(AstExpr *expr) {
    Value args[1];
    if (!rowmodel_eval_args(expr, 1, args, "rowmodel.is_row")) {
        return value_null();
    }
    int is_row = (args[0].kind == VALUE_GOBJECT && !args[0].as.gobject->closed &&
                  args[0].as.gobject->obj && GB_IS_ROW(args[0].as.gobject->obj));
    value_free(args[0]);
    return value_bool(is_row);
}

static Value rowmodel_eval_call(AstExpr *expr) {
    const char *name = expr->as.call.name;
    if (strcmp(name, "new") == 0)            return rowmodel_do_new(expr);
    if (strcmp(name, "set_count") == 0)      return rowmodel_do_set_count(expr);
    if (strcmp(name, "count") == 0)          return rowmodel_do_count(expr);
    if (strcmp(name, "get_item") == 0)       return rowmodel_do_get_item(expr);
    if (strcmp(name, "items_changed") == 0)  return rowmodel_do_items_changed(expr);
    if (strcmp(name, "row_index") == 0)      return rowmodel_do_row_index(expr);
    if (strcmp(name, "row_grid") == 0)       return rowmodel_do_row_grid(expr);
    if (strcmp(name, "is_row") == 0)         return rowmodel_do_is_row(expr);
    {
        char msg[160];
        snprintf(msg, sizeof(msg), "unknown rowmodel function: %s", name);
        rowmodel_raise(msg);
    }
    return value_null();
}

#endif /* HAVE_GIR && HAVE_GIO */
