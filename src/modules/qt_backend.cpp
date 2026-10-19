/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
 *
 * THE QT BACKEND. docs/qt_gui_design.md §0.2 shipped the declarative half with
 * no Qt -- a `window` block that parses, validates, refuses and binds an
 * introspectable record -- and said in as many words what was missing: "what
 * does not exist is anything that RENDERS it". This is that, and it is
 * assembled from pieces that were measured first rather than designed here.
 *
 * FOUR THINGS COME FROM MEASUREMENT AND ARE NOT RE-ARGUED:
 *
 * 1. QT OWNS THE LOOP (§5.1, direction A). gBASIC's own loop is one `poll()`
 *    and Qt's is the one that must wait, because Qt opens NESTED loops -- a
 *    modal dialog, QMessageBox, QMenu::exec(), drag-and-drop -- and under the
 *    other direction gBASIC's poll never runs for the dialog's whole life
 *    (measured: a 651 ms gap over a 600 ms dialog, a webserver answering
 *    nothing). So gBASIC's sources ride a Qt timer here, which is serviced
 *    inside a nested loop as a notifier is.
 *
 * 2. EVERY SOURCE MUST BE DRAINED. A `QSocketNotifier` is level-triggered and
 *    the probe that did not read its pipe fired 545,066 times for one byte.
 *    The tick below cannot have that defect by construction -- it runs
 *    gBASIC's real loop body, which reads -- and the contract is written down
 *    because the next source added here may not be a timer.
 *
 * 3. NO DISPLAY IS A SENTENCE, NEVER AN ABORT (§0.3). `gb_qt_can_display()`
 *    asks Qt in a forked child, because `qFatal` cannot be caught and the
 *    environment cannot be read instead. NOTHING IN THIS FILE MAY CONSTRUCT A
 *    QApplication WITHOUT THAT ANSWER FIRST -- `gb_qt_begin` is where the
 *    order is enforced.
 *
 * 4. CONSTRUCTION IS A TABLE; EVERYTHING ELSE IS BY NAME (§1). The 2026-10-14
 *    probe drove real widgets through `QMetaObject`: properties set and read
 *    by name with a misspelling refused, a signal connected by name to one
 *    generic relay. Only CONSTRUCTION needs a table, because a QWidget
 *    constructor is not Q_INVOKABLE -- and the table is eight words long,
 *    against the 75 properties QPushButton alone exposes. That ratio is the
 *    whole argument for the by-name route.
 *
 * WHAT IS DELIBERATELY NOT HERE, so nobody reads its absence as an oversight:
 * two-way binding (a `form to edit` currently populates and is read back on
 * demand -- the watcher pair is the next increment), `tabs`/`cells`/`table`
 * (not in the validated vocabulary either), and the data-driven builder (§4).
 */
#include "qt_backend.h"

#include <QApplication>
#include <QCheckBox>
#include <QEvent>
#include <QSplitter>
#include <QScrollArea>
#include <QDialog>
#include <QComboBox>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QGridLayout>
#include <QTabWidget>
#include <QTableWidget>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QMessageBox>
#include <QPointer>
#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QCursor>
#include <QFileDialog>
#include <QFileInfo>
#include <QDialogButtonBox>
#if HAVE_KSYNTAX
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/SyntaxHighlighter>
#include <KSyntaxHighlighting/Theme>
#endif
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QMetaMethod>
#include <QMetaObject>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <QVector>
#include <QWidget>

#include <QGuiApplication>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* The display question. Separate from everything else, and callable
 * before any Qt object exists -- which is the requirement.            */
/* ------------------------------------------------------------------ */

extern "C" int gb_qt_can_display(void) {
    /* Cached: the answer cannot change inside one process, and the caller
     * would otherwise pay a fork for the refusal check and another for the
     * one `gb_qt_begin` makes on its own behalf. */
    static int answer = -1;
    if (answer >= 0) {
        return answer;
    }
    /* THE CHILD IS THE ONE THAT MAY DIE. It constructs a real
     * QGuiApplication -- the thing that would abort -- and exits 0 only if
     * that succeeded. The parent reads a status rather than sharing the fate,
     * so it survives to print one sentence. */
    pid_t pid = fork();
    if (pid < 0) {
        answer = 0;
        return 0;
    }
    if (pid == 0) {
        /* Qt's own complaint on the way down would reach the user's stderr,
         * and replacing it with a sentence of ours is the point. */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, 2);
        }
        int argc = 1;
        static char arg0[] = "gbasic";
        char *argv[] = { arg0, nullptr };
        QGuiApplication probe(argc, argv);
        _exit(0);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        answer = 0;
        return 0;
    }
    answer = (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 1 : 0;
    return answer;
}

/* ------------------------------------------------------------------ */

static GbQtInvokeFn g_invoke = nullptr;
static GbQtTickFn g_tick = nullptr;
static GbQtClosedFn g_closed = nullptr;

extern "C" void gb_qt_set_callbacks(GbQtInvokeFn invoke, GbQtTickFn tick,
                                    GbQtClosedFn closed) {
    g_invoke = invoke;
    g_tick = tick;
    g_closed = closed;
}

/* ONE GENERIC RELAY per connection. A widget's signal reaches `fire()` and
 * `fire()` calls back into gBASIC with the handler's NAME -- which is what
 * `gi_signal_marshal` already does for GTK, so this is a shipped pattern in a
 * new translation unit rather than a new idea. */
class GbQtRelay : public QObject {
    Q_OBJECT
public:
    QByteArray handler;
public slots:
    void fire();
    void tick();
};

/* A SUBCLASS ONLY FOR THE FALLBACK. accept() and reject() are both PROTECTED
 * on QFileDialog -- a QMessageBox's buttons hid that, since a box can be
 * answered from outside by clicking the real one, where a chooser's buttons are
 * private children.
 *
 * BUT THE REAL BUTTONS ARE STILL THE ROUTE, measured: the chooser's name field
 * is `fileNameEdit` and its button box is `buttonBox`, so typing a path and
 * pressing the AcceptRole button (labelled Open, Save or Choose by mode) is
 * exactly what a person does -- the same rule `gb_qt_answer` already follows,
 * and it buys something a direct accept() does not. QT DISABLES THAT BUTTON
 * for a path that is not there, so the refusal comes from Qt's own rule rather
 * than from a reimplementation of it, and the warning box accept() would have
 * put up never appears. That box was a HANG rather than a failure: it opens a
 * second nested loop from inside the first one's pump tick, which Qt will not
 * re-enter, so nothing could reach either dialog.
 *
 * Declared here rather than beside the dialog code so moc's output, included a
 * few lines below, can see it. */
class GbQtChooser : public QFileDialog {
    Q_OBJECT
public:
    using QFileDialog::QFileDialog;
    void drive_reject() { reject(); }
};

void GbQtRelay::fire() {
    if (g_invoke) {
        g_invoke(handler.constData());
    }
}

void GbQtRelay::tick() {
    /* ONE NON-BLOCKING ITERATION OF gBASIC'S LOOP. It drains by construction,
     * running the real loop body; see note 2 at the top of this file. */
    if (g_tick && !g_tick()) {
        /* `exit(0)` for the reason `gb_qt_stop` gives: `quit()` closes every
         * window, which is not what "gBASIC's loop has no more work" means. */
        QCoreApplication::exit(0);
    }
}

#include "qt_backend.moc"

/* A node is whatever a handle points at: a window, a container, or a part.
 * One struct for all three so the C side can pass any of them as a parent
 * without a second handle kind. `layout` is where children go and is null on
 * a part; `root` is the owning window, which is what makes a part findable by
 * name from a handler's target. */
struct GbQtNode {
    QWidget *widget = nullptr;
    QLayout *layout = nullptr;
    /* A NODE THAT IS A QObject AND NOT A WIDGET -- a menu `item`, which is a
     * QAction. Kept in its own field rather than widening `widget`, so every
     * door that needs a real widget keeps a type that says so and cannot be
     * handed an action by accident. */
    QObject *object = nullptr;
    /* A `row` INSIDE `cells` IS NOT A WIDGET EITHER: it is a ROW OF THE GRID,
     * so it has no box of its own and its children go into the parent's
     * QGridLayout at (grid_row, grid_col++). `grid_row` is -1 on everything
     * that is not one, which is how a leaf written directly inside `cells` --
     * where there is no row to put it in -- is refused rather than placed at
     * some default position. `grid_next_row` belongs to the `cells` node and
     * is what makes each `row` the next one down. */
    int grid_row = -1;
    int grid_col = 0;
    int grid_next_row = 0;
    GbQtNode *root = nullptr;
    bool is_form = false;
    /* ROOT ONLY. `closed_handler` is the function `on app.closed` named, held
     * here rather than connected because A QWidget HAS NO `closed` SIGNAL --
     * closing is an EVENT, so the by-name signal route has nothing to find and
     * `gb_qt_connect` would refuse a handler that is perfectly well formed.
     *
     * `closing` is a RE-ENTRY GUARD and not bookkeeping: the handler runs while
     * the window is still registered (which is what makes reading the form in
     * it possible), so a handler that closes the window again would arrive back
     * here through `QWidget::close()`. One flag, set before the handler runs. */
    QByteArray closed_handler;
    bool closing = false;
    /* ADDRESSED BY NAME, AND NOT EVERY NAMED THING IS A WIDGET: a menu `item`
     * is a QAction, which descends from QObject and not from QWidget. Holding
     * QObject* is what lets one registry serve both -- properties, signals and
     * `qt.read` all work through QObject, so the three doors that genuinely
     * need a widget (`grow`, a view's edit triggers, a table's cells) cast
     * and refuse instead of there being a second registry to keep in step. */
    QHash<QString, QObject *> parts;   /* root only */
};

static QApplication *g_app = nullptr;
static QVector<GbQtNode *> g_nodes;
static int g_fake_argc = 1;
static char g_fake_arg0[] = "gbasic";
static char *g_fake_argv[] = { g_fake_arg0, nullptr };

static void say(char *err, size_t err_size, const QByteArray &text) {
    if (err && err_size) {
        snprintf(err, err_size, "%s", text.constData());
    }
}

static GbQtNode *node_new(void) {
    GbQtNode *n = new GbQtNode();
    g_nodes.append(n);
    return n;
}

/* EVERY ROOT WINDOW CARRIES ONE, whether or not a handler was declared -- and
 * that is the load-bearing half. The BOOKKEEPING is unconditional, because
 * gBASIC's registry has to stop reporting a dismissed window as open however
 * it was dismissed; the HANDLER is the optional part. A filter installed only
 * where `on app.closed` appears would leave a program that declared no handler
 * reading from a window nobody can see.
 *
 * NO Q_OBJECT: `eventFilter` is a virtual, so there is no signal or slot here
 * for moc to generate, and this class is declared after GbQtNode -- which is
 * below the moc include -- precisely because it needs one. */
class GbQtCloser : public QObject {
public:
    GbQtNode *node = nullptr;
protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
};

/* DROP A WINDOW'S NODES. Reopening was impossible before this increment (a
 * second `qt.open` met "already open"), so repeated open/close is a new shape
 * and would otherwise grow `g_nodes` by one entry per part per open, for ever.
 * Deleting the node structs is safe because nothing outside holds one: the
 * caller's handle is the registry entry, which has already been forgotten, and
 * the build pass holds parents only within a single call. */
static void node_retire(GbQtNode *root) {
    for (int i = g_nodes.size() - 1; i >= 0; i--) {
        if (g_nodes[i]->root == root) {
            delete g_nodes[i];
            g_nodes.remove(i);
        }
    }
}

/* A WINDOW CLOSED BY SOMETHING OTHER THAN `gb_qt_close` cannot be deleted
 * where it is noticed: the filter runs inside that widget's own event
 * dispatch, and `QWidget::close()` goes on to hide it afterwards. So it waits
 * here and is deleted at the next `gb_qt_window`, which is both a safe moment
 * -- a fresh call from gBASIC, inside nobody's dispatch -- and the one that
 * matters, since reopening is what would otherwise accumulate.
 *
 * `deleteLater()` WAS THE OBVIOUS ANSWER AND IS NOT RELIED ON: a deferred
 * delete needs an event loop to spin, and the commonest shape here closes the
 * LAST window, which RETURNS from exec() -- so the queue may never be
 * processed. Draining it ourselves asks nothing about whether a loop is
 * running. Anything still waiting at teardown is deleted by `gb_qt_end`,
 * which is why a program that never opens another window leaks nothing. */
static QVector<GbQtNode *> g_retired;

static void retired_drain(void) {
    QVector<GbQtNode *> list = g_retired;
    g_retired.clear();
    for (int i = 0; i < list.size(); i++) {
        QWidget *w = list[i]->widget;
        list[i]->widget = nullptr;
        delete w;
        node_retire(list[i]);
    }
}

/* THE WINDOW HAS ENDED. ONE COPY, CALLED FROM TWO PLACES, AND IDEMPOTENT --
 * which is what `closing` is for.
 *
 * IT IS A FUNCTION RATHER THAN THE FILTER'S BODY BECAUSE QT DOES NOT ALWAYS
 * DELIVER THE EVENT, measured 2026-10-19 and finer than the guess that
 * prompted the measurement: `close()` delivers a QEvent::Close on a visible
 * widget AND on a merely hidden one, and delivers NOTHING on a QDialog that
 * has already been accepted or rejected -- `done()` marks it closed, so a
 * later `close()` is a no-op. A modal window therefore ends without an event
 * on the path that matters most, and bookkeeping hung off the filter alone
 * would silently not happen: `qt.is_open` would go on answering true for a
 * window the program had finished with.
 *
 * THE HANDLER RUNS FIRST, WHILE THE WINDOW IS STILL REGISTERED, and that
 * ordering is the whole of what makes `on app.closed` useful: it is the one
 * place a program can still read what the person typed, because the next line
 * is what stops every verb finding the window. */
/* ONE GUARD, HERE, AND IT ANSWERS WHETHER IT DID THE WORK -- which a
 * perturbation forced. Written with the same `closing` test in BOTH this
 * function and the filter below, NEITHER COPY WAS LOAD-BEARING: removing the
 * one here changed nothing, because the filter guarded itself, so the suite
 * stayed GREEN on a build whose ending was no longer idempotent. The invariant
 * belongs to this function, so it lives in exactly one place and the filter
 * asks. */
static bool node_closed(GbQtNode *node) {
    if (!node || node->closing) {
        return false;
    }
    node->closing = true;
    if (!node->closed_handler.isEmpty() && g_invoke) {
        g_invoke(node->closed_handler.constData());
    }
    if (g_closed) {
        g_closed(node);
    }
    return true;
}

bool GbQtCloser::eventFilter(QObject *obj, QEvent *event) {
    if (event->type() == QEvent::Close && node_closed(node)) {
        /* STILL HOLDING A WIDGET means we did not arrive through
         * `gb_qt_close`, which clears it -- so this is the window manager's
         * button or an invoked `close`, and the widget is ours to retire. It is
         * inside the guard because appending twice would retire the node twice. */
        if (node->widget) {
            g_retired.append(node);
        }
    }
    return QObject::eventFilter(obj, event);
}

/* THE THEME IS REMEMBERED UNTIL THERE IS A QApplication TO PUT IT ON. A
 * program sets a theme before it opens anything, which is also when there is
 * no QApplication -- so holding it here is what lets the first window appear
 * already themed instead of appearing plain and then changing. */
static QString g_theme;

extern "C" int gb_qt_theme_set(const char *css, char *err, size_t err_size) {
    (void)err;
    (void)err_size;
    g_theme = QString::fromUtf8(css ? css : "");
    if (g_app) {
        g_app->setStyleSheet(g_theme);
    }
    return 1;
}

extern "C" char *gb_qt_theme_get(void) {
    /* ASKED OF QT where there is a QApplication, and of the remembered copy
     * only where there is not -- so this is a round trip through Qt's own
     * state rather than a variable answering with what it was handed. */
    QByteArray bytes = (g_app ? g_app->styleSheet() : g_theme).toUtf8();
    char *out = (char *)malloc((size_t)bytes.size() + 1);
    if (!out) {
        return nullptr;
    }
    memcpy(out, bytes.constData(), (size_t)bytes.size());
    out[bytes.size()] = '\0';
    return out;
}

extern "C" int gb_qt_begin(char *err, size_t err_size) {
    if (g_app) {
        return 1;
    }
    /* THE ORDER IS THE REQUIREMENT, not a defensive check: constructing a
     * QApplication with no display aborts the process, uncatchably, so the
     * answer has to be in hand before this line. Asked here as well as by the
     * caller, because this is the one place that cannot be reached any other
     * way. */
    if (!gb_qt_can_display()) {
        say(err, err_size,
            "no display is available, so no window can be opened");
        return 0;
    }
    g_app = new QApplication(g_fake_argc, g_fake_argv);
    /* THE REMEMBERED THEME, APPLIED BEFORE ANY WINDOW EXISTS, which is the
     * whole point of remembering it: the first window is then built already
     * themed rather than appearing plain and changing. */
    if (!g_theme.isEmpty()) {
        g_app->setStyleSheet(g_theme);
    }
    return 1;
}

extern "C" void *gb_qt_window(const char *name, const char *title, int width,
                              int height, int modal) {
    /* A WINDOW CLOSED EARLIER IS DELETED HERE -- see `retired_drain`. Done
     * before the new one is built so a program that opens, closes and reopens
     * holds at most one retired window at a time. */
    retired_drain();
    GbQtNode *n = node_new();
    /* A QDialog WHEN THE WINDOW WILL BE SHOWN MODALLY, and a plain QWidget
     * otherwise. That one choice is all construction has to know: a QDialog IS
     * a QWidget, so every container, part, menu and property below is reached
     * by exactly the same code -- measured on a real one before relying on it.
     * What it adds is `exec()`, `accept()` and `reject()`, and what it costs is
     * nothing, because a QDialog shown with `show()` is an ordinary window
     * (`isModal` is false until `exec()` runs). */
    QWidget *w = modal ? (QWidget *)new QDialog() : new QWidget();
    if (title && *title) {
        w->setWindowTitle(QString::fromUtf8(title));
    }
    if (width > 0 && height > 0) {
        w->resize(width, height);
    } else if (width > 0) {
        w->resize(width, w->sizeHint().height());
    } else if (height > 0) {
        w->resize(w->sizeHint().width(), height);
    }
    n->widget = w;
    n->root = n;
    /* A window needs a layout of its own or a single child sits at its
     * natural size in the corner. A column is the declaration's own default
     * shape and costs nothing when the block supplies one. */
    QVBoxLayout *box = new QVBoxLayout(w);
    n->layout = box;
    /* THE WINDOW IS A PART OF ITSELF, in the only table that matters: one
     * insert and `qt.read`, `qt.set`, `qt.invoke` and `on <name>.<signal>`
     * all reach it, none of them knowing anything new. The load-time pass
     * refuses a part that shares the name, so this cannot shadow one. */
    if (name && *name) {
        n->parts.insert(QString::fromUtf8(name), w);
    }
    GbQtCloser *closer = new GbQtCloser();
    closer->node = n;
    closer->setParent(w);
    w->installEventFilter(closer);
    return n;
}

/* The container table. Four words, each deciding its own layout -- which is
 * the one thing a table is genuinely needed for (see note 4). */
extern "C" void *gb_qt_container(void *parent, const char *word,
                                 const char *name, const char *title,
                                 char *err, size_t err_size) {
    GbQtNode *p = (GbQtNode *)parent;
    if (!p) {
        say(err, err_size, "that container cannot hold anything");
        return nullptr;
    }
    /* A `tab` IS THE ONE CONTAINER WHOSE PARENT HAS NO LAYOUT, because a
     * QTabWidget's children are PAGES rather than things in a box -- so the
     * layout requirement is asked of every other word and not of this one.
     * Found by running it: with the guard first, `tab "General"` inside
     * `tabs` was refused with "that container cannot hold anything", a true
     * sentence about the wrong thing. */
    /* A SPLITTER PARENT HAS NO LAYOUT EITHER, and unlike a QTabWidget it needs
     * nothing else: MEASURED, simply PARENTING a widget to a QSplitter enrols
     * it as a pane -- `count()` went to 2 from two `new QLabel(..., sp)` calls
     * with no `addWidget` anywhere. So the only change a splitter needs on this
     * side is for the layout requirement not to be asked of its children. */
    if (!p->layout && strcmp(word, "tab") != 0 &&
        !qobject_cast<QSplitter *>(p->widget)) {
        say(err, err_size, "that container cannot hold anything");
        return nullptr;
    }
    GbQtNode *n = node_new();
    n->root = p->root;

    QGridLayout *parent_grid = qobject_cast<QGridLayout *>(p->layout);
    if (parent_grid && strcmp(word, "row") == 0 && p->grid_row < 0) {
        /* A ROW OF THE GRID, not a box. It is NOT a widget: its children are
         * placed straight into the parent's QGridLayout, which is the whole
         * point -- a row of boxes would not align its columns with the row
         * above, which is the one thing `cells` exists to do. The grid is
         * shared as this node's `layout` so `gb_qt_part` finds it, and
         * `grid_row` is what says which row it is. */
        n->layout = parent_grid;
        n->grid_row = p->grid_next_row++;
        n->grid_col = 0;
        return n;
    }
    if (strcmp(word, "row") == 0 || strcmp(word, "column") == 0) {
        QWidget *host = new QWidget(p->widget);
        QLayout *box = (strcmp(word, "row") == 0)
                           ? (QLayout *)new QHBoxLayout(host)
                           : (QLayout *)new QVBoxLayout(host);
        box->setContentsMargins(0, 0, 0, 0);
        n->widget = host;
        n->layout = box;
    } else if (strcmp(word, "form") == 0) {
        QWidget *host = new QWidget(p->widget);
        QFormLayout *form = new QFormLayout(host);
        form->setContentsMargins(0, 0, 0, 0);
        n->widget = host;
        n->layout = form;
        n->is_form = true;
    } else if (strcmp(word, "group") == 0) {
        QGroupBox *box = new QGroupBox(p->widget);
        if (title && *title) {
            box->setTitle(QString::fromUtf8(title));
        }
        QVBoxLayout *inner = new QVBoxLayout(box);
        n->widget = box;
        n->layout = inner;
    } else if (strcmp(word, "cells") == 0) {
        /* TABULAR PLACEMENT WITH SPANS (§2.5), and it holds `row`s -- which is
         * the decision worth naming: a grid IS rows of cells, so reusing `row`
         * costs no vocabulary and reads as what it does. The refinement is
         * that inside `cells` a row's columns ALIGN with the rows above it,
         * where a `row` elsewhere is a box that does not. */
        QWidget *host = new QWidget(p->widget);
        QGridLayout *grid = new QGridLayout(host);
        grid->setContentsMargins(0, 0, 0, 0);
        n->widget = host;
        n->layout = grid;
    } else if (strcmp(word, "split") == 0) {
        /* A ROW OR COLUMN YOU CAN DRAG. `title` carries the DIRECTION here --
         * the declaration has one free slot and spends it on that (§0.8), so
         * the C side is handed the word rather than a name.
         *
         * `orientation` IS A Q_PROPERTY (measured, index 67, Horizontal=1
         * Vertical=2, and it accepts the string "Vertical" too), so the
         * direction costs one setProperty and nothing had to be plumbed.
         *
         * NO LAYOUT, deliberately: a QSplitter's children are PANES, and
         * leaving `layout` null is what makes the guard above the one place
         * that decides what may hold what. */
        QSplitter *sp = new QSplitter(p->widget);
        sp->setOrientation((title && strcmp(title, "down") == 0)
                               ? Qt::Vertical
                               : Qt::Horizontal);
        n->widget = sp;
    } else if (strcmp(word, "scroll") == 0) {
        /* A SCROLLED REGION, which is what a long list of ordinary widgets
         * needs -- `source`, `memo` and `table` scroll themselves, so this is
         * for everything else.
         *
         * `widgetResizable` DEFAULTS TO FALSE AND THAT IS THE TRAP: left alone,
         * the inner widget keeps its original size for ever, so content never
         * reflows to the viewport's width and a window that is resized shows a
         * stripe of widgets beside a grey expanse. Measured true: 40 rows give
         * an inner height of 932 against a 258 viewport, and the scrollbar
         * appears.
         *
         * IT HOLDS MANY THINGS THROUGH AN INNER COLUMN, the choice a `tab`
         * already makes: a QScrollArea takes exactly ONE widget, so a bare one
         * would force every author to open a `column` first. */
        QScrollArea *area = new QScrollArea(p->widget);
        QWidget *inner = new QWidget(area);
        QVBoxLayout *box = new QVBoxLayout(inner);
        box->setContentsMargins(0, 0, 0, 0);
        area->setWidget(inner);
        area->setWidgetResizable(true);
        n->widget = area;
        n->layout = box;
    } else if (strcmp(word, "tabs") == 0) {
        /* A QTabWidget HAS NO LAYOUT OF ITS OWN -- its children are PAGES,
         * added with addTab. So the node carries no `layout`, which is what
         * makes anything but a `tab` inside it refuse rather than being
         * silently dropped: `gb_qt_part` and `gb_qt_container` both begin by
         * asking the parent for a layout. The load-time pass refuses it first;
         * this is the floor under that. */
        n->widget = new QTabWidget(p->widget);
    } else if (strcmp(word, "tab") == 0) {
        QTabWidget *book = qobject_cast<QTabWidget *>(p->widget);
        if (!book) {
            say(err, err_size, "a `tab` needs `tabs` around it");
            return nullptr;
        }
        /* A PAGE IS A COLUMN, which is the choice worth naming: a tab holds
         * several things stacked, so giving it a QVBoxLayout means `field`,
         * `row`, `group` and `table` all work inside one with no further
         * vocabulary -- a page that was a bare QWidget would need every
         * author to open a `column` first. */
        QWidget *page = new QWidget(book);
        QVBoxLayout *inner = new QVBoxLayout(page);
        n->widget = page;
        n->layout = inner;
        book->addTab(page, QString::fromUtf8(title ? title : ""));
        /* ADDED BY addTab, so it must NOT also be added to a parent layout
         * below -- a page belongs to the book, not to the box the book sits
         * in. Returning here is what keeps it out of that path. */
        return n;
    } else {
        say(err, err_size,
            QByteArray("no Qt container is wired for `") + word + "`");
        return nullptr;
    }

    if (name && *name && n->root && n->widget) {
        n->root->parts.insert(QString::fromUtf8(name), n->widget);
    }

    QBoxLayout *pb = qobject_cast<QBoxLayout *>(p->layout);
    QFormLayout *pf = qobject_cast<QFormLayout *>(p->layout);
    if (pb) {
        pb->addWidget(n->widget);
    } else if (pf) {
        pf->addRow(n->widget);
    }
    return n;
}

extern "C" void *gb_qt_part(void *parent, const char *word, const char *name,
                            const char *label, char *err, size_t err_size) {
    GbQtNode *p = (GbQtNode *)parent;
    /* A SPLITTER PARENT HAS NO LAYOUT and still holds things -- see
     * `gb_qt_container`: parenting a widget to a QSplitter enrols it as a
     * pane, measured. So a `field` or a `source` can be a pane directly,
     * which is the common case (Studio's editor is one). */
    QSplitter *psplit = p ? qobject_cast<QSplitter *>(p->widget) : nullptr;
    if (!p || (!p->layout && !psplit)) {
        say(err, err_size, "that container cannot hold anything");
        return nullptr;
    }
    /* A PANE HAS NOWHERE TO PUT A LABEL, and this is the silent-discard class
     * rather than a tidiness rule. Most leaves consume their own caption at
     * construction -- a `button`, a `check`, a `text`, a `memo` and a `source`
     * all call setText or setPlainText below -- but a `field` and a `choice`
     * have theirs placed by the PARENT LAYOUT, and a splitter has none. So
     * `field who( label: "Name" )` as a direct pane renders the field and
     * DROPS THE WORD, with nothing raised: exactly how a leaf's `label:`
     * reached no widget at all for four increments (§0.8).
     *
     * NARROW, AND DERIVED FROM THE CONSTRUCTION BELOW rather than guessed --
     * the two words whose label the parent places are the two that are
     * refused, and the suite asserts a `check` keeps its caption as a pane so
     * the rule cannot widen into one that fires on correct declarations. */
    if (!p->layout && psplit && label && *label &&
        (strcmp(word, "field") == 0 || strcmp(word, "choice") == 0)) {
        say(err, err_size,
            QByteArray("a pane has nowhere to put a label, so `") + word +
                "` cannot carry one here -- put it in a `form` or a `column`");
        return nullptr;
    }
    QBoxLayout *pb = qobject_cast<QBoxLayout *>(p->layout);
    QFormLayout *pf = qobject_cast<QFormLayout *>(p->layout);
    QGridLayout *pg = qobject_cast<QGridLayout *>(p->layout);
    /* A GRID PARENT MUST BE A ROW OF ONE. A leaf written directly inside
     * `cells`, where there is no row to put it in, would otherwise be placed
     * at whatever position happened to be current -- a widget appearing
     * somewhere nobody asked for. The load-time pass refuses it first; this is
     * the floor under that, and says the same thing. */
    if (pg && p->grid_row < 0) {
        say(err, err_size,
            QByteArray("`") + word + "` needs a `row` inside `cells`");
        return nullptr;
    }

    if (strcmp(word, "stretch") == 0) {
        /* A spacer absorbs the leftover space and is the reason the
         * vocabulary carries no alignment properties at all (§2.5). It is not
         * a widget, so it has no node and nothing can be set on it. */
        if (pb) {
            pb->addStretch(1);
            /* A NODE WITH NO WIDGET, deliberately, rather than the parent
             * handed back: a stretch is not a widget, so anything set on it
             * must be refused rather than quietly applied to the row it sits
             * in -- which is what returning the parent would do. */
            GbQtNode *s = node_new();
            s->root = p->root;
            return s;
        }
        say(err, err_size, "`stretch` needs a `row` or a `column` around it");
        return nullptr;
    }

    GbQtNode *n = node_new();
    n->root = p->root;
    QWidget *w = nullptr;
    if (strcmp(word, "field") == 0) {
        w = new QLineEdit(p->widget);
    } else if (strcmp(word, "button") == 0) {
        QPushButton *b = new QPushButton(p->widget);
        if (label && *label) {
            b->setText(QString::fromUtf8(label));
        }
        w = b;
    } else if (strcmp(word, "check") == 0) {
        QCheckBox *c = new QCheckBox(p->widget);
        if (label && *label) {
            c->setText(QString::fromUtf8(label));
        }
        w = c;
    } else if (strcmp(word, "text") == 0) {
        /* A SENTENCE ON A WINDOW, which nothing could display: the leaf
         * vocabulary reached QLineEdit, QPushButton and QCheckBox and no
         * more, so a heading, an instruction or a room description had
         * nowhere to go. MEASURED before adding it -- the nearest substitute
         * is a read-only frameless QLineEdit, which cannot WRAP and is still
         * a text entry, so a paragraph was not expressible at all.
         *
         * IT WRAPS BY DEFAULT, deliberately: a label that does not wrap
         * silently truncates the thing it exists to show, and the author who
         * wants one line has `wordWrap: false` through the by-name route.
         * Selectable, because text a person cannot copy is a nuisance and the
         * widget is read-only either way. */
        QLabel *l = new QLabel(p->widget);
        l->setWordWrap(true);
        l->setTextInteractionFlags(Qt::TextBrowserInteraction |
                                   Qt::TextSelectableByKeyboard);
        if (label && *label) {
            l->setText(QString::fromUtf8(label));
        }
        w = l;
    } else if (strcmp(word, "choice") == 0) {
        /* ONE OF A LIST, which had no substitute at all: a `field` means
         * typing the thing's id and a `table` is a grid rather than a
         * dropdown. MEASURED against a real program -- BAG's area editor,
         * where each of six exits names another area -- rather than chosen
         * from Qt's catalogue, which is why `list` was NOT added: a
         * one-column `table` already answers a picker and `qt.selected`
         * addresses it.
         *
         * ITS ITEMS COME FROM DATA, so they are filled by `qt.options` rather
         * than guessed here; a declared `options:` literal is handled above in
         * qt.c, for the fixed case where typing them out is the honest thing. */
        w = new QComboBox(p->widget);
    } else if (strcmp(word, "memo") == 0) {
        /* MANY LINES, which `field` is not: a QLineEdit holds one, so a
         * description, a note or a game's output log had nowhere to go
         * either. QPlainTextEdit rather than QTextEdit because the subject is
         * TEXT -- rich text would make `qt.read` answer markup for something
         * a program put in as a string, and nothing in this vocabulary asks
         * for formatting. */
        QPlainTextEdit *m = new QPlainTextEdit(p->widget);
        if (label && *label) {
            m->setPlainText(QString::fromUtf8(label));
        }
        w = m;
    } else if (strcmp(word, "source") == 0) {
        /* CODE, which a `memo` is not -- and the difference is four things a
         * person editing a program notices within a minute, none of them
         * reachable through the by-name property route because each is a
         * fact about the WIDGET'S SETUP rather than a Q_PROPERTY:
         *
         *   a MONOSPACED font, asked of the system rather than named, since
         *   "Courier" is not installed everywhere and a hardcoded family is
         *   how a port gets a fallback nobody chose;
         *
         *   NO WRAPPING, because a wrapped line of code makes the line
         *   numbers stop matching the file -- the opposite default from
         *   `text`, where wrapping is what stops a paragraph truncating;
         *
         *   A TAB THAT IS FOUR COLUMNS WIDE rather than eight, which is the
         *   width every gBASIC file in this tree is written at;
         *
         *   and the HIGHLIGHTER, attached separately because it is an object
         *   rather than a property (see gb_qt_highlight).
         *
         * IT IS A QPlainTextEdit LIKE `memo`, deliberately, so `qt.read` and
         * `qt.write` reach it through the `plainText` branch that already
         * exists and a form can bind it with nothing new. Rich text would
         * make a program that put source in as a string read markup back. */
        QPlainTextEdit *se = new QPlainTextEdit(p->widget);
        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        se->setFont(mono);
        se->setLineWrapMode(QPlainTextEdit::NoWrap);
        se->setTabStopDistance(4 * QFontMetricsF(mono).horizontalAdvance(QLatin1Char(' ')));
        if (label && *label) {
            se->setPlainText(QString::fromUtf8(label));
        }
        w = se;
    } else {
        say(err, err_size,
            QByteArray("no Qt widget is wired for `") + word + "`");
        return nullptr;
    }
    n->widget = w;

    /* WHERE THE LABEL GOES IS DECIDED BY THE PARENT, not by the word. In a
     * `form` it is the row's label, which is the whole of what QFormLayout
     * does; anywhere else a `field` would otherwise lose its caption
     * silently, so one is placed beside it. */
    if (pg) {
        /* ONE CELL, AND THE LABEL GOES IN ITS OWN CELL BEFORE IT -- which is
         * what a form does with a QFormLayout and what an author writing a
         * labelled grid expects. It costs a column, which is why a `field`
         * with no label takes one cell and one with a label takes two. */
        if (strcmp(word, "field") == 0 && label && *label) {
            pg->addWidget(new QLabel(QString::fromUtf8(label), p->widget),
                          p->grid_row, p->grid_col++);
        }
        /* THE GRID AND THE POSITION ARE KEPT ON THE PART, because that is
         * what `across:`/`down:` need to re-add it -- and a part whose
         * `grid_row` is -1 is one that is not in a grid at all, which is how
         * those options are refused outside `cells` rather than ignored. */
        n->layout = pg;
        n->grid_row = p->grid_row;
        n->grid_col = p->grid_col;
        pg->addWidget(w, p->grid_row, p->grid_col++);
    } else if (strcmp(word, "field") == 0 && pf) {
        pf->addRow(label && *label ? QString::fromUtf8(label) : QString(), w);
    } else if (pf) {
        pf->addRow(w);
    } else if (pb) {
        if (strcmp(word, "field") == 0 && label && *label) {
            pb->addWidget(new QLabel(QString::fromUtf8(label), p->widget));
        }
        pb->addWidget(w);
    }

    if (name && *name && n->root) {
        n->root->parts.insert(QString::fromUtf8(name), w);
    }
    return n;
}

/* PROPERTIES BY NAME -- AND THE METAOBJECT LOOKUP IS ABOUT THE MESSAGE, which
 * is a correction to what this comment first claimed.
 *
 * It said `setProperty` on an undeclared name would SUCCEED, storing a dynamic
 * property nothing reads. Measured, it does not: Qt returns false for a
 * property the metaobject does not declare, so the `!setProperty` branch below
 * refuses it either way -- and says "would not take that value for `texx`",
 * which is FALSE. The value was fine; the name was not. That is the
 * reports-the-wrong-cause class this tree has named repeatedly, so the lookup
 * stays and the SUITE ASSERTS THE WORDING, because with the lookup removed and
 * only the refusal asserted every check stayed green. */
static int set_property(QObject *widget, const char *prop, const QVariant &v,
                        char *err, size_t err_size) {
    if (!widget) {
        say(err, err_size, "there is no widget to set that on");
        return 0;
    }
    const QMetaObject *mo = widget->metaObject();
    if (mo->indexOfProperty(prop) < 0) {
        say(err, err_size,
            QByteArray("a ") + mo->className() + " has no `" + prop +
                "` property");
        return 0;
    }
    if (!widget->setProperty(prop, v)) {
        say(err, err_size,
            QByteArray("a ") + mo->className() + " would not take that value for `" +
                prop + "`");
        return 0;
    }
    return 1;
}

/* THE THING AN OPTION IS SET ON: the widget where there is one, the QObject
 * where there is not. A menu `item` is a QAction, so `shortcut:`, `enabled:`
 * and `checkable:` reach it through exactly the same by-name door every widget
 * property goes through -- which is the whole saving of that design, since a
 * QAction's properties were never anticipated anywhere. */
static QObject *settable(GbQtNode *n) {
    if (!n) {
        return nullptr;
    }
    return n->widget ? (QObject *)n->widget : n->object;
}

extern "C" int gb_qt_set_text(void *widget, const char *prop,
                              const char *value, char *err, size_t err_size) {
    return set_property(settable((GbQtNode *)widget), prop,
                        QVariant(QString::fromUtf8(value)), err, err_size);
}

extern "C" int gb_qt_set_number(void *widget, const char *prop, double value,
                                char *err, size_t err_size) {
    return set_property(settable((GbQtNode *)widget), prop, QVariant(value),
                        err, err_size);
}

extern "C" int gb_qt_set_bool(void *widget, const char *prop, int value,
                              char *err, size_t err_size) {
    return set_property(settable((GbQtNode *)widget), prop,
                        QVariant(value != 0), err, err_size);
}

extern "C" int gb_qt_layout_number(void *container, const char *prop,
                                   long value, char *err, size_t err_size) {
    GbQtNode *n = (GbQtNode *)container;
    if (!n || !n->layout) {
        say(err, err_size,
            QByteArray("`") + prop + "` belongs to a layout, and that is not one");
        return 0;
    }
    if (strcmp(prop, "margin") == 0) {
        n->layout->setContentsMargins((int)value, (int)value, (int)value,
                                      (int)value);
        return 1;
    }
    if (strcmp(prop, "spacing") == 0) {
        n->layout->setSpacing((int)value);
        return 1;
    }
    say(err, err_size, QByteArray("no layout setting is named `") + prop + "`");
    return 0;
}

extern "C" int gb_qt_grow(void *widget, long weight, char *err,
                          size_t err_size) {
    GbQtNode *n = (GbQtNode *)widget;
    if (!n || !n->widget) {
        say(err, err_size, "there is nothing for `grow` to apply to");
        return 0;
    }
    /* A STRETCH FACTOR IS HELD BY THE PARENT LAYOUT, not by the widget, which
     * is why this cannot go through the property door. */
    QWidget *parent = n->widget->parentWidget();
    QBoxLayout *box = parent ? qobject_cast<QBoxLayout *>(parent->layout())
                             : nullptr;
    if (!box) {
        say(err, err_size,
            "`grow` needs a `row` or a `column` around it");
        return 0;
    }
    int at = box->indexOf(n->widget);
    if (at < 0) {
        say(err, err_size, "`grow` could not find that part in its layout");
        return 0;
    }
    box->setStretch(at, (int)weight);
    return 1;
}

/* A SIGNAL BY NAME, through one generic relay. The signal is looked up in the
 * widget's own QMetaObject by NAME rather than by full signature, because
 * `clicked` on a QPushButton is really `clicked(bool)` with a default -- so a
 * signature-exact lookup for `clicked()` finds nothing, which is a wrong
 * answer about a real signal. A name that is not a signal at all is still
 * refused, naming the class. */
extern "C" int gb_qt_span(void *widget, long across, long down, char *err,
                          size_t err_size) {
    GbQtNode *n = (GbQtNode *)widget;
    if (!n || !n->widget) {
        say(err, err_size, "there is nothing there to span");
        return 0;
    }
    QGridLayout *g = qobject_cast<QGridLayout *>(n->layout);
    if (!g || n->grid_row < 0) {
        say(err, err_size,
            "`across:` and `down:` only mean something inside `cells`");
        return 0;
    }
    if (across < 1 || down < 1) {
        say(err, err_size, "a span covers at least one cell");
        return 0;
    }
    /* RE-ADDED AT THE POSITION IT ALREADY HAS. QGridLayout has no way to
     * change an existing item's span, so the widget is removed and put back
     * with one -- which is safe because nothing else has been added to those
     * cells yet: options are applied immediately after the part is built. */
    g->removeWidget(n->widget);
    g->addWidget(n->widget, n->grid_row, n->grid_col, (int)down, (int)across);
    return 1;
}

extern "C" int gb_qt_cell_of(void *window, const char *name, int *row,
                             int *col, int *across, int *down) {
    GbQtNode *root = (GbQtNode *)window;
    QObject *o = root ? root->parts.value(QString::fromUtf8(name), nullptr)
                      : nullptr;
    QWidget *w = qobject_cast<QWidget *>(o);
    if (!w) {
        return 0;
    }
    /* ASKED OF THE LAYOUT THE WIDGET IS ACTUALLY IN, found by walking up from
     * the widget rather than from our own record of where we put it -- which
     * is what makes this an oracle: a position we remembered would agree with
     * a placement that was wrong. */
    QWidget *host = w->parentWidget();
    QGridLayout *g = host ? qobject_cast<QGridLayout *>(host->layout())
                          : nullptr;
    if (!g) {
        return 0;
    }
    for (int i = 0; i < g->count(); i++) {
        if (g->itemAt(i)->widget() != w) {
            continue;
        }
        int r = 0, c = 0, rs = 0, cs = 0;
        g->getItemPosition(i, &r, &c, &rs, &cs);
        *row = r;
        *col = c;
        *down = rs;
        *across = cs;
        return 1;
    }
    return 0;
}

/* Returned to the C side as malloc'd text, the convention every other reader
 * here follows. */
static char *dup_utf8(const QString &text) {
    QByteArray bytes = text.toUtf8();
    char *out = (char *)malloc((size_t)bytes.size() + 1);
    if (!out) {
        return nullptr;
    }
    memcpy(out, bytes.constData(), (size_t)bytes.size());
    out[bytes.size()] = '\0';
    return out;
}

extern "C" int gb_qt_pane_of(void *window, const char *name, char **kind,
                             char **direction, int *panes, int *index,
                             int *resizable) {
    GbQtNode *root = (GbQtNode *)window;
    QObject *o = root ? root->parts.value(QString::fromUtf8(name), nullptr)
                      : nullptr;
    QWidget *w = qobject_cast<QWidget *>(o);
    if (!w) {
        return 0;
    }
    /* WALK UP FROM THE WIDGET, not from any record of where we put it -- which
     * is what makes this an oracle rather than an echo. A pane may be a
     * container, so the thing the splitter holds is an ANCESTOR of the named
     * part rather than the part itself: the walk keeps the last widget it came
     * through, and that one is the pane. */
    QWidget *child = w;
    for (QWidget *up = w->parentWidget(); up; up = up->parentWidget()) {
        if (QSplitter *sp = qobject_cast<QSplitter *>(up)) {
            if (kind) { *kind = dup_utf8(QStringLiteral("split")); }
            if (direction) {
                *direction = dup_utf8(sp->orientation() == Qt::Vertical
                                          ? QStringLiteral("down")
                                          : QStringLiteral("across"));
            }
            if (panes) { *panes = sp->count(); }
            if (index) { *index = sp->indexOf(child); }
            if (resizable) { *resizable = 0; }
            return 1;
        }
        if (QScrollArea *sc = qobject_cast<QScrollArea *>(up)) {
            if (kind) { *kind = dup_utf8(QStringLiteral("scroll")); }
            if (direction) { *direction = nullptr; }
            if (panes) { *panes = 0; }
            if (index) { *index = -1; }
            if (resizable) { *resizable = sc->widgetResizable() ? 1 : 0; }
            return 1;
        }
        child = up;
    }
    return 0;
}

extern "C" int gb_qt_connect(void *window, const char *target,
                             const char *signal, const char *handler,
                             char *err, size_t err_size) {
    GbQtNode *root = (GbQtNode *)window;
    if (!root) {
        say(err, err_size, "there is no window to connect that to");
        return 0;
    }
    QObject *w = root->parts.value(QString::fromUtf8(target), nullptr);
    if (!w) {
        /* UNREACHABLE FROM A DECLARATION and kept anyway: the load-time pass
         * already refuses `on cancel.clicked` with no `cancel` (§0.2), so
         * arriving here means the record and the build disagree. */
        say(err, err_size,
            QByteArray("this window has no `") + target + "` to connect to");
        return 0;
    }
    /* `closed` IS AN EVENT, NOT A SIGNAL, and that is a fact about Qt rather
     * than a choice here: a QWidget emits nothing when it closes, it RECEIVES
     * a QCloseEvent -- so the by-name signal route below has nothing to find
     * and would refuse `on app.closed` with "a QWidget emits no `closed`
     * signal", which is true and useless. It is answered here instead, and
     * only for the window itself: a `button` has no close worth reporting, so
     * `on save.closed` falls through and gets that refusal, correctly. */
    if (w == (QObject *)root->widget && strcmp(signal, "closed") == 0) {
        root->closed_handler = QByteArray(handler);
        return 1;
    }
    const QMetaObject *mo = w->metaObject();
    int si = -1;
    for (int i = 0; i < mo->methodCount(); i++) {
        QMetaMethod m = mo->method(i);
        if (m.methodType() == QMetaMethod::Signal &&
            m.name() == QByteArray(signal)) {
            si = i;
            break;
        }
    }
    if (si < 0) {
        say(err, err_size,
            QByteArray("a ") + mo->className() + " emits no `" + signal +
                "` signal");
        return 0;
    }
    /* DECLARING THE HANDLER IS WHAT ASKS FOR A CUSTOM CONTEXT MENU. Measured,
     * every widget kind starts at `DefaultContextMenu`, and under that policy
     * `customContextMenuRequested` IS NEVER EMITTED -- so a handler written
     * for it without a second line setting the policy is a handler connected
     * to a signal nothing emits, which raises nothing and looks exactly like a
     * right-click that did not happen. That is the silent class this backend
     * is tested against, arriving from a Qt default.
     *
     * SET HERE AND NOT ASKED FOR IN THE DECLARATION'S OPTIONS, because the
     * author has already said what they want by naming the handler, and a
     * vocabulary where the request takes two lines is one where it gets
     * written as one. The cost is stated rather than hidden: this REPLACES the
     * menu Qt would have put up itself, so a `memo` or `field` with a context
     * handler loses its own cut/copy/paste -- which is why the policy is
     * touched only where a handler exists, and a part with none keeps Qt's. */
    if (strcmp(signal, "customContextMenuRequested") == 0) {
        if (QWidget *cw = qobject_cast<QWidget *>(w)) {
            cw->setContextMenuPolicy(Qt::CustomContextMenu);
        }
    }
    GbQtRelay *relay = new GbQtRelay();
    relay->setParent(w);
    relay->handler = QByteArray(handler);
    int ri = relay->metaObject()->indexOfSlot("fire()");
    if (ri < 0 || !QMetaObject::connect(w, si, relay, ri)) {
        say(err, err_size,
            QByteArray("Qt would not connect `") + target + "." + signal + "`");
        return 0;
    }
    return 1;
}

extern "C" int gb_qt_invoke(void *window, const char *target,
                            const char *method, char *err, size_t err_size) {
    GbQtNode *root = (GbQtNode *)window;
    QObject *w = root ? root->parts.value(QString::fromUtf8(target), nullptr)
                      : nullptr;
    if (!w) {
        say(err, err_size,
            QByteArray("this window has no `") + target + "`");
        return 0;
    }
    const QMetaObject *mo = w->metaObject();
    /* BY NAME, not by signature, for the reason gb_qt_connect gives: a Qt
     * method commonly carries defaulted arguments, so a signature-exact
     * lookup answers "no such method" about a method that is there. Only the
     * no-argument form is invoked, which is what a declaration can express. */
    for (int i = 0; i < mo->methodCount(); i++) {
        QMetaMethod m = mo->method(i);
        if (m.name() != QByteArray(method)) {
            continue;
        }
        if (m.methodType() != QMetaMethod::Method &&
            m.methodType() != QMetaMethod::Slot) {
            continue;
        }
        if (m.parameterCount() != 0) {
            continue;
        }
        if (m.invoke(w, Qt::DirectConnection)) {
            return 1;
        }
        say(err, err_size,
            QByteArray("Qt would not invoke `") + target + "." + method + "`");
        return 0;
    }
    say(err, err_size,
        QByteArray("a ") + mo->className() + " has no `" + method +
            "` method that takes no arguments");
    return 0;
}

/* NULL MEANS TWO THINGS HERE AND TODAY THAT IS SAFE -- a note for whoever adds
 * the next widget kind. The caller turns NULL into "this window has no part
 * named X", which is true for an absent part and would be FALSE for a part
 * that is there and carries no readable property. It cannot happen yet because
 * every one of the three leaf words maps to a widget with `text` or `checked`.
 * A widget without either needs the two answers separated, or the message
 * becomes a lie about a part the author can see on the screen -- the
 * reports-the-wrong-cause class. */
extern "C" char *gb_qt_read(void *window, const char *name) {
    GbQtNode *root = (GbQtNode *)window;
    if (!root) {
        return nullptr;
    }
    QObject *w = root->parts.value(QString::fromUtf8(name), nullptr);
    if (!w) {
        return nullptr;
    }
    QVariant v;
    if (qobject_cast<QLineEdit *>(w)) {
        v = w->property("text");
    } else if (qobject_cast<QCheckBox *>(w)) {
        v = w->property("checked");
    } else if (qobject_cast<QComboBox *>(w)) {
        /* THE CHOSEN TEXT, which is what a program asked "which one?" wants.
         * A QComboBox has no `text` property either, so the fallthrough would
         * have reported a real `choice` as an absent part -- the same defect a
         * `memo` had, one widget along. The INDEX is reachable through the
         * by-name route as `currentIndex`. */
        v = w->property("currentText");
    } else if (qobject_cast<QPlainTextEdit *>(w)) {
        /* A QPlainTextEdit HAS NO `text` PROPERTY -- it is `plainText` -- and
         * the fallthrough below would have answered an INVALID QVariant,
         * which this function reports as an absent part. So `qt.read` on a
         * perfectly real `memo` would have said the window has no such thing.
         * Named here rather than left to the fallthrough for that reason. */
        v = w->property("plainText");
    } else {
        v = w->property("text");
    }
    if (!v.isValid()) {
        return nullptr;
    }
    QByteArray bytes = v.toString().toUtf8();
    char *out = (char *)malloc((size_t)bytes.size() + 1);
    if (!out) {
        return nullptr;
    }
    memcpy(out, bytes.constData(), (size_t)bytes.size());
    out[bytes.size()] = '\0';
    return out;
}

extern "C" int gb_qt_write(void *window, const char *name, const char *text,
                           char *err, size_t err_size) {
    GbQtNode *root = (GbQtNode *)window;
    QObject *w = root ? root->parts.value(QString::fromUtf8(name), nullptr)
                      : nullptr;
    if (!w) {
        say(err, err_size,
            QByteArray("this window has no `") + name + "`");
        return 0;
    }
    if (qobject_cast<QCheckBox *>(w)) {
        QString s = QString::fromUtf8(text);
        return set_property(w, "checked",
                            QVariant(s == "true" || s == "1"), err, err_size);
    }
    if (QComboBox *box = qobject_cast<QComboBox *>(w)) {
        /* CHOOSE AN EXISTING ITEM BY ITS TEXT, and REFUSE one that is not
         * there. `setCurrentText` on an uneditable box silently does NOTHING
         * in Qt, so a program setting a value that is not in the list would
         * read back the old one and nothing would say so -- a plausible wrong
         * answer, which is this backend's whole failure class. */
        int at = box->findText(QString::fromUtf8(text));
        if (at < 0) {
            say(err, err_size,
                QByteArray("`") + name + "` has no choice `" + text +
                "` -- fill its list with qt.options first");
            return 0;
        }
        box->setCurrentIndex(at);
        return 1;
    }
    if (qobject_cast<QPlainTextEdit *>(w)) {
        /* THE WRITE HALF OF THE SAME FACT. `set_property(w, "text", ...)` is
         * refused by the QMetaObject door, so a `qt.write` to a memo would
         * have reported an unknown property for the one thing a memo is for. */
        return set_property(w, "plainText",
                            QVariant(QString::fromUtf8(text)), err, err_size);
    }
    return set_property(w, "text", QVariant(QString::fromUtf8(text)), err,
                        err_size);
}

/* THE PART A NAME REFERS TO, for the run-time property doors. One helper so
 * the refusal for an absent part is one sentence rather than three. */
static QObject *part_named(void *window, const char *name, char *err,
                           size_t err_size) {
    GbQtNode *root = (GbQtNode *)window;
    QObject *o = root ? root->parts.value(QString::fromUtf8(name), nullptr)
                      : nullptr;
    if (!o) {
        say(err, err_size, QByteArray("this window has no `") + name + "`");
    }
    return o;
}

extern "C" int gb_qt_set_part_text(void *window, const char *name,
                                   const char *prop, const char *value,
                                   char *err, size_t err_size) {
    QObject *o = part_named(window, name, err, err_size);
    return o ? set_property(o, prop, QVariant(QString::fromUtf8(value)), err,
                            err_size)
             : 0;
}

extern "C" int gb_qt_set_part_number(void *window, const char *name,
                                     const char *prop, double value,
                                     char *err, size_t err_size) {
    QObject *o = part_named(window, name, err, err_size);
    return o ? set_property(o, prop, QVariant(value), err, err_size) : 0;
}

extern "C" int gb_qt_set_part_bool(void *window, const char *name,
                                   const char *prop, int value, char *err,
                                   size_t err_size) {
    QObject *o = part_named(window, name, err, err_size);
    return o ? set_property(o, prop, QVariant(value != 0), err, err_size) : 0;
}

/* READ A PROPERTY BY NAME. The same QMetaObject door `set_property` goes
 * through, so a misspelling is refused HERE TOO -- which matters more than it
 * looks: the setters refuse an unknown name, and a reader that answered
 * `unknown` for one instead would let a typo'd probe report an absence rather
 * than a mistake, so the two halves of one route would disagree about what a
 * property name is.
 *
 * THE KIND COMES FROM THE QVariant, not from the caller. A bool answered as the
 * text "false" would be TRUE in gBASIC, every non-empty string being true, so
 * the one property most likely to be read in an `if` would read backwards. */
extern "C" int gb_qt_get(void *window, const char *name, const char *prop,
                         char *kind, double *num, char **text, char *err,
                         size_t err_size) {
    GbQtNode *root = (GbQtNode *)window;
    QObject *w = root ? root->parts.value(QString::fromUtf8(name), nullptr)
                      : nullptr;
    if (!w) {
        say(err, err_size, QByteArray("this window has no `") + name + "`");
        return 0;
    }
    const QMetaObject *mo = w->metaObject();
    if (mo->indexOfProperty(prop) < 0) {
        say(err, err_size,
            QByteArray("a ") + mo->className() + " has no `" + prop +
                "` property");
        return 0;
    }
    QVariant v = w->property(prop);
    if (!v.isValid()) {
        say(err, err_size,
            QByteArray("a ") + mo->className() + " gave no value for `" + prop +
                "`");
        return 0;
    }
    switch (v.typeId()) {
    case QMetaType::Bool:
        *kind = 'b';
        *num = v.toBool() ? 1.0 : 0.0;
        return 1;
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float:
        *kind = 'n';
        *num = v.toDouble();
        return 1;
    default:
        break;
    }
    /* EVERYTHING ELSE AS TEXT, deliberately rather than a kind per Qt type: a
     * QFont, a QSize and an enum all render through QVariant::toString, and
     * growing a case per Qt type is the table the by-name design exists to
     * avoid. A caller wanting more than the text of a QFont is asking for a
     * widget vocabulary this module does not have. */
    {
        QByteArray bytes = v.toString().toUtf8();
        char *out = (char *)malloc((size_t)bytes.size() + 1);
        if (!out) {
            say(err, err_size, "out of memory reading that property");
            return 0;
        }
        memcpy(out, bytes.constData(), (size_t)bytes.size());
        out[bytes.size()] = '\0';
        *kind = 't';
        *text = out;
    }
    return 1;
}

/* ONE TABLE, BUILT FROM ITS WHOLE COLUMN LIST. QTableWidget is the item-based
 * view, chosen for a first increment because a business list is hundreds of
 * rows and it needs no model class -- THE COST IS STATED: it materialises
 * every row, so this is not the path for the million-row case run_datagrid.sh
 * proves with a native row model, and that path is a model behind
 * QTableView rather than a bigger version of this.
 *
 * ROWS ARE NOT SORTABLE BY CLICKING, deliberately: sorting the VIEW while the
 * subject keeps its own order would make `qt.selected`'s row index mean a
 * different row in each, and an index that silently means two things is worse
 * than no sorting. */
/* THE WINDOW'S MENU BAR, made once and kept as a child of the window. A plain
 * QWidget has no `menuBar()` the way a QMainWindow does, so it is created on
 * demand and added to the TOP of the window's own layout -- which is where a
 * menu bar goes and is also why a menu is placed by nothing in the
 * declaration: it does not sit in the `row`/`column` tree at all. */
static QMenuBar *menu_bar_of(GbQtNode *root) {
    if (!root || !root->widget) {
        return nullptr;
    }
    QMenuBar *bar = root->widget->findChild<QMenuBar *>(
        QString(), Qt::FindDirectChildrenOnly);
    if (bar) {
        return bar;
    }
    bar = new QMenuBar(root->widget);
    if (QBoxLayout *b = qobject_cast<QBoxLayout *>(root->layout)) {
        b->insertWidget(0, bar);
    }
    return bar;
}

extern "C" void *gb_qt_menu(void *window, void *parent_menu, const char *title,
                            char *err, size_t err_size) {
    GbQtNode *root = (GbQtNode *)window;
    if (!root) {
        say(err, err_size, "there is no window to put a menu on");
        return nullptr;
    }
    QMenu *made = nullptr;
    if (parent_menu) {
        GbQtNode *pm = (GbQtNode *)parent_menu;
        QMenu *outer = qobject_cast<QMenu *>(pm->widget);
        if (!outer) {
            say(err, err_size, "a submenu needs a menu around it");
            return nullptr;
        }
        made = outer->addMenu(QString::fromUtf8(title ? title : ""));
    } else {
        QMenuBar *bar = menu_bar_of(root);
        if (!bar) {
            say(err, err_size, "this window cannot hold a menu bar");
            return nullptr;
        }
        made = bar->addMenu(QString::fromUtf8(title ? title : ""));
    }
    GbQtNode *n = node_new();
    n->root = root;
    n->widget = made;
    return n;
}

extern "C" void *gb_qt_menu_item(void *window, void *menu, const char *name,
                                 const char *label, char *err,
                                 size_t err_size) {
    GbQtNode *root = (GbQtNode *)window;
    GbQtNode *m = (GbQtNode *)menu;
    QMenu *owner = m ? qobject_cast<QMenu *>(m->widget) : nullptr;
    if (!owner) {
        say(err, err_size, "an item needs a menu around it");
        return nullptr;
    }
    QAction *act = owner->addAction(QString::fromUtf8(label ? label : ""));
    GbQtNode *n = node_new();
    n->root = root;
    /* NO `widget`, BECAUSE IT IS NOT ONE. A QAction is a QObject, so anything
     * that genuinely needs a widget -- `grow:`, a view's edit triggers --
     * refuses on this node rather than quietly applying to the menu it sits
     * in, which is the rule `stretch` already follows for the same reason. */
    n->object = act;
    if (name && *name && root) {
        root->parts.insert(QString::fromUtf8(name), act);
    }
    return n;
}

extern "C" int gb_qt_menu_separator(void *menu, char *err, size_t err_size) {
    GbQtNode *m = (GbQtNode *)menu;
    QMenu *owner = m ? qobject_cast<QMenu *>(m->widget) : nullptr;
    if (!owner) {
        say(err, err_size, "a separator needs a menu around it");
        return 0;
    }
    owner->addSeparator();
    return 1;
}

/* A TOP-LEVEL POPUP MENU -- parented to the window and registered by NAME, so
 * `qt.popup` can find it and `qt.set(win, "row_actions", ...)` reaches it like
 * anything else named.
 *
 * NOT ADDED TO THE BAR, which is the whole difference, and the layout does not
 * take it: measured, parenting a QMenu to a widget that has a layout left that
 * layout's count unchanged. So a popup is placed by nothing -- the same
 * property the bar has, reached by a different route. */
extern "C" void *gb_qt_popup_menu(void *window, const char *name, char *err,
                                  size_t err_size) {
    GbQtNode *root = (GbQtNode *)window;
    if (!root || !root->widget) {
        say(err, err_size, "there is no window to put a popup menu on");
        return nullptr;
    }
    QMenu *made = new QMenu(root->widget);
    GbQtNode *n = node_new();
    n->root = root;
    n->widget = made;
    if (name && *name) {
        root->parts.insert(QString::fromUtf8(name), made);
    }
    return n;
}

/* HOW MANY OF A MENU'S ENTRIES A PERSON WOULD SEE, ASKED OF THE WHOLE TREE. A
 * separator is furniture, so a menu holding nothing but separators shows
 * nothing -- and asking it this way, rather than looking at the menu after it
 * was popped, means the answer does not depend on whether it has been shown.
 *
 * IT RECURSES, AND THAT WAS FOUND BY THE BROWSER EXAMPLE RATHER THAN REASONED.
 * Hiding every item INSIDE a submenu does not hide the submenu: measured, with
 * one all-hidden submenu left, Qt reports `isEmpty()` FALSE and SHOWS a 120x25
 * menu holding nothing but an arrow leading nowhere -- which is "reads as a
 * control that failed" one level in, and is exactly what the boolean exists to
 * prevent. Qt's own test cannot see it; it declines only once the submenu's
 * own action is hidden too.
 *
 * A SUBMENU IS TOLD BY `menu()` BEING NON-NULL, measured against a plain
 * item's nil, so this needs nothing remembered on our side. */
static int menu_visible_entries(QMenu *m) {
    int shown = 0;
    for (QAction *a : m->actions()) {
        if (!a->isVisible() || a->isSeparator()) {
            continue;
        }
        if (QMenu *sub = a->menu()) {
            /* A SUBMENU COUNTS FOR WHAT IT LEADS TO, not for itself. */
            shown += menu_visible_entries(sub);
            continue;
        }
        shown++;
    }
    return shown;
}

extern "C" int gb_qt_popup(void *window, const char *name, int *shown,
                           char *err, size_t err_size) {
    GbQtNode *root = (GbQtNode *)window;
    if (shown) {
        *shown = 0;
    }
    if (!root || !root->widget) {
        say(err, err_size, "there is no window to pop a menu up on");
        return 0;
    }
    QObject *o = root->parts.value(QString::fromUtf8(name ? name : ""), nullptr);
    if (!o) {
        say(err, err_size,
            QByteArray("this window has no `") + (name ? name : "") + "`");
        return 0;
    }
    QMenu *m = qobject_cast<QMenu *>(o);
    if (!m) {
        /* NAMES THE KIND IT FOUND, because the mistake this catches is naming
         * a button or a table where a `popup` was meant, and "no such thing"
         * would be false about a name that is right there. */
        say(err, err_size,
            QByteArray("`") + (name ? name : "") + "` is a " +
                o->metaObject()->className() + ", not a popup menu");
        return 0;
    }
    if (menu_visible_entries(m) == 0) {
        /* NOT A REFUSAL. A row that offers nothing is ordinary, and Qt itself
         * declines to show the menu, so the truthful report is `false` rather
         * than an error about a program that did nothing wrong. */
        return 1;
    }
    m->popup(QCursor::pos());
    if (shown) {
        *shown = 1;
    }
    return 1;
}

/* THE WIDGET A MOUSE EVENT WOULD LAND ON. For a scroll area that is the
 * VIEWPORT and not the widget, which is the measurement this verb exists for:
 * a context-menu event delivered to a QTableWidget fired its signal 0 times
 * and propagated to the parent, and the same event delivered to that widget's
 * viewport fired 1. */
static QWidget *mouse_target(QWidget *w) {
    QAbstractScrollArea *sa = qobject_cast<QAbstractScrollArea *>(w);
    return sa ? sa->viewport() : w;
}

extern "C" int gb_qt_right_click(void *window, const char *name, int row,
                                 char *err, size_t err_size) {
    GbQtNode *root = (GbQtNode *)window;
    if (!root) {
        say(err, err_size, "there is no window to click in");
        return 0;
    }
    QObject *o = root->parts.value(QString::fromUtf8(name ? name : ""), nullptr);
    if (!o) {
        say(err, err_size,
            QByteArray("this window has no `") + (name ? name : "") + "`");
        return 0;
    }
    QWidget *w = qobject_cast<QWidget *>(o);
    if (!w) {
        /* A menu `item` is a QAction, so there is nothing to click AT. Named
         * rather than silently doing nothing, which is what a cast to
         * QWidget* and a NULL check further down would have produced. */
        say(err, err_size,
            QByteArray("`") + (name ? name : "") + "` is a " +
                o->metaObject()->className() + ", which has nothing to click");
        return 0;
    }
    QWidget *target = mouse_target(w);
    QPoint at(4, 4);
    if (row >= 0) {
        QTableWidget *t = qobject_cast<QTableWidget *>(w);
        if (!t) {
            say(err, err_size,
                QByteArray("`") + (name ? name : "") +
                    "` is not a table, so it has no row to click");
            return 0;
        }
        if (row >= t->rowCount()) {
            say(err, err_size, "that table has no such row");
            return 0;
        }
        QTableWidgetItem *it = t->item(row, 0);
        if (!it) {
            say(err, err_size, "that row is empty");
            return 0;
        }
        at = t->visualItemRect(it).center();
    } else if (target->width() > 8 && target->height() > 8) {
        at = QPoint(target->width() / 2, target->height() / 2);
    }
    QPoint global = target->mapToGlobal(at);
    /* A PRESS AND A RELEASE, THEN THE CONTEXT EVENT, in the order a mouse
     * produces them -- the press is what MOVES THE SELECTION (measured: Qt
     * sets `currentRow()` to the row under a right-press), which is what lets
     * `qt.selected` answer "which row was right-clicked" with no new verb. A
     * bare context event would fire the handler over a stale selection. */
    QMouseEvent press(QEvent::MouseButtonPress, at, global, Qt::RightButton,
                      Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(target, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, at, global, Qt::RightButton,
                        Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(target, &release);
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, at, global);
    QApplication::sendEvent(target, &ctx);
    return 1;
}

extern "C" int gb_qt_menu_count(void *window) {
    GbQtNode *root = (GbQtNode *)window;
    if (!root || !root->widget) {
        return -1;
    }
    QMenuBar *bar = root->widget->findChild<QMenuBar *>(
        QString(), Qt::FindDirectChildrenOnly);
    if (!bar) {
        return 0;   /* a window with no menus has a bar with none, not an error */
    }
    return (int)bar->actions().size();
}

extern "C" char *gb_qt_menu_title(void *window, int index) {
    GbQtNode *root = (GbQtNode *)window;
    if (!root || !root->widget) {
        return nullptr;
    }
    QMenuBar *bar = root->widget->findChild<QMenuBar *>(
        QString(), Qt::FindDirectChildrenOnly);
    if (!bar || index < 0 || index >= bar->actions().size()) {
        return nullptr;
    }
    QByteArray bytes = bar->actions().at(index)->text().toUtf8();
    char *out = (char *)malloc((size_t)bytes.size() + 1);
    if (!out) {
        return nullptr;
    }
    memcpy(out, bytes.constData(), (size_t)bytes.size());
    out[bytes.size()] = '\0';
    return out;
}

/* THE MENU AT ONE POSITION ON THE BAR. Its own QMenu hangs off the bar's
 * action, which is how a top-level entry is both an action and a menu. */
static QMenu *bar_menu_at(void *window, int index) {
    GbQtNode *root = (GbQtNode *)window;
    if (!root || !root->widget) {
        return nullptr;
    }
    QMenuBar *bar = root->widget->findChild<QMenuBar *>(
        QString(), Qt::FindDirectChildrenOnly);
    if (!bar || index < 0 || index >= bar->actions().size()) {
        return nullptr;
    }
    return bar->actions().at(index)->menu();
}

/* ONE ENTRY'S CAPTION, from a QMenu however it was found -- off the bar by
 * position, or by NAME for a `popup`. Shared rather than copied, because the
 * two would drift in exactly the way that matters: a separator reported as a
 * line by one route and as an empty caption by the other. */
static char *menu_entry_title(QMenu *m, int entry, int *is_separator) {
    if (is_separator) {
        *is_separator = 0;
    }
    if (!m || entry < 0 || entry >= m->actions().size()) {
        return nullptr;
    }
    /* ASKED OF QT WHETHER IT IS A LINE, rather than inferred from an empty
     * caption. Those are different things: a separator HAS no caption where
     * `item x( label: "" )` has an empty one -- and a perturbation that let a
     * separator fall through and be added as an empty-texted ACTION was
     * invisible to a caption check, which is how this was found. */
    if (is_separator && m->actions().at(entry)->isSeparator()) {
        *is_separator = 1;
    }
    QByteArray bytes = m->actions().at(entry)->text().toUtf8();
    char *out = (char *)malloc((size_t)bytes.size() + 1);
    if (!out) {
        return nullptr;
    }
    memcpy(out, bytes.constData(), (size_t)bytes.size());
    out[bytes.size()] = '\0';
    return out;
}

extern "C" int gb_qt_menu_entry_count(void *window, int index) {
    QMenu *m = bar_menu_at(window, index);
    return m ? (int)m->actions().size() : -1;
}

extern "C" char *gb_qt_menu_entry_title(void *window, int index, int entry,
                                        int *is_separator) {
    return menu_entry_title(bar_menu_at(window, index), entry, is_separator);
}

/* A POPUP MENU BY NAME. THE ORACLE, and the hole it fills is the one a flat
 * by-name registry always leaves: an `item` keeps its name wherever its menu
 * ends up, so an item declared in the WRONG popup of two is invisible to every
 * check there is -- the same measurement the submenu oracle rests on, where
 * that perturbation produced ZERO mismatches. The captions are the only thing
 * that can say which popup an item landed in, and they pin the order. */
static QMenu *named_menu(void *window, const char *name) {
    GbQtNode *root = (GbQtNode *)window;
    if (!root || !name) {
        return nullptr;
    }
    QObject *o = root->parts.value(QString::fromUtf8(name), nullptr);
    return o ? qobject_cast<QMenu *>(o) : nullptr;
}

extern "C" int gb_qt_popup_entry_count(void *window, const char *name) {
    QMenu *m = named_menu(window, name);
    return m ? (int)m->actions().size() : -1;
}

extern "C" char *gb_qt_popup_entry_title(void *window, const char *name,
                                         int entry, int *is_separator) {
    return menu_entry_title(named_menu(window, name), entry, is_separator);
}

extern "C" void *gb_qt_table(void *parent, const char *name,
                             const char *const *headers, int columns,
                             char *err, size_t err_size) {
    GbQtNode *p = (GbQtNode *)parent;
    /* A SPLITTER PARENT, which has no layout and still holds things -- a table
     * as a direct pane is the ordinary shape (a result grid beside an editor),
     * and the add below does nothing in that case because parenting already
     * enrolled it. Third guard to learn this; they are counted in the suite so
     * a FOURTH door cannot be added without one. */
    if (!p || (!p->layout && !qobject_cast<QSplitter *>(p->widget))) {
        say(err, err_size, "that container cannot hold a table");
        return nullptr;
    }
    QTableWidget *t = new QTableWidget(0, columns, p->widget);
    QStringList caps;
    for (int i = 0; i < columns; i++) {
        caps << QString::fromUtf8(headers[i] ? headers[i] : "");
    }
    t->setHorizontalHeaderLabels(caps);
    t->verticalHeader()->setVisible(false);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->horizontalHeader()->setStretchLastSection(true);

    GbQtNode *n = node_new();
    n->root = p->root;
    n->widget = t;
    if (QFormLayout *pf = qobject_cast<QFormLayout *>(p->layout)) {
        pf->addRow(t);
    } else if (QBoxLayout *pb = qobject_cast<QBoxLayout *>(p->layout)) {
        pb->addWidget(t);
    }
    if (name && *name && n->root) {
        n->root->parts.insert(QString::fromUtf8(name), t);
    }
    return n;
}

/* ---- HIGHLIGHTING -----------------------------------------------------------
 *
 * ONE REPOSITORY FOR THE PROCESS, which is what KSyntaxHighlighting's own
 * header asks for ("keep it for the lifetime of your application"): it reads
 * several hundred definitions and every theme, so one per editor would pay for
 * that per widget. Constructed lazily, because a program with no `source` in
 * it must not pay at all -- and search paths added before the first use are
 * remembered until then.
 */
#if HAVE_KSYNTAX
static KSyntaxHighlighting::Repository *g_repo = nullptr;
static QStringList g_syntax_paths;

static KSyntaxHighlighting::Repository *repo(void) {
    if (!g_repo) {
        g_repo = new KSyntaxHighlighting::Repository();
        for (const QString &p : g_syntax_paths) {
            g_repo->addCustomSearchPath(p);
        }
    }
    return g_repo;
}
#endif

extern "C" int gb_qt_has_highlighting(void) {
#if HAVE_KSYNTAX
    return 1;
#else
    return 0;
#endif
}

extern "C" void gb_qt_syntax_path(const char *dir) {
#if HAVE_KSYNTAX
    if (!dir || !*dir) {
        return;
    }
    QString p = QString::fromUtf8(dir);
    if (g_syntax_paths.contains(p)) {
        return;
    }
    g_syntax_paths.append(p);
    /* ADDED NOW IF THE REPOSITORY IS ALREADY BUILT, remembered otherwise --
     * so the order in which a program opens a window and sets a path cannot
     * decide whether the path takes effect. */
    if (g_repo) {
        g_repo->addCustomSearchPath(p);
    }
#else
    (void)dir;
#endif
}

/* A `source` WIDGET'S DOCUMENT. It must really BE one: attaching a highlighter
 * to a `memo` would work (both are QPlainTextEdit) and attaching one to a
 * `field` would silently do nothing, so the kind is checked rather than
 * assumed from the caller's word. */
static QPlainTextEdit *source_named(void *window, const char *name, char *err,
                                    size_t err_size) {
    GbQtNode *root = (GbQtNode *)window;
    QObject *o = root ? root->parts.value(QString::fromUtf8(name), nullptr)
                      : nullptr;
    if (!o) {
        say(err, err_size, QByteArray("there is nothing called `") + name +
                               "` in this window");
        return nullptr;
    }
    QPlainTextEdit *e = qobject_cast<QPlainTextEdit *>(o);
    if (!e) {
        say(err, err_size, QByteArray("`") + name +
                               "` is not a text editor, so it cannot be "
                               "highlighted");
        return nullptr;
    }
    return e;
}

extern "C" int gb_qt_highlight(void *window, const char *name,
                               const char *language, const char *theme,
                               char *err, size_t err_size) {
    QPlainTextEdit *e = source_named(window, name, err, err_size);
    if (!e) {
        return 0;
    }
#if HAVE_KSYNTAX
    KSyntaxHighlighting::Definition def =
        repo()->definitionForName(QString::fromUtf8(language ? language : ""));
    if (!def.isValid()) {
        /* THE MESSAGE CARRIES THE PART'S NAME because its POSITION cannot:
         * a window record holds no line, so this warning is reported at
         * `qt.open` rather than at the declaration that named the language.
         * Naming the part is what makes the site not matter. */
        say(err, err_size,
            QByteArray("`source ") + name + "` asked for the `" +
                (language ? language : "") +
                "` syntax definition and none is installed, so it will work "
                "as a plain editor");
        return 0;
    }
    /* ONE HIGHLIGHTER PER DOCUMENT. Re-pointing an existing one rather than
     * adding a second, because two QSyntaxHighlighters on one document both
     * run and the later one wins per block -- which looks like the first
     * language having been ignored. */
    KSyntaxHighlighting::SyntaxHighlighter *hl =
        e->findChild<KSyntaxHighlighting::SyntaxHighlighter *>(
            QString(), Qt::FindDirectChildrenOnly);
    if (!hl) {
        hl = new KSyntaxHighlighting::SyntaxHighlighter(e->document());
        hl->setParent(e);
    }
    hl->setDefinition(def);
    KSyntaxHighlighting::Theme th;
    if (theme && strcmp(theme, "dark") == 0) {
        th = repo()->defaultTheme(KSyntaxHighlighting::Repository::DarkTheme);
    } else if (theme && strcmp(theme, "light") == 0) {
        th = repo()->defaultTheme(KSyntaxHighlighting::Repository::LightTheme);
    } else {
        th = repo()->themeForPalette(e->palette());
    }
    hl->setTheme(th);
    /* THE EDITOR'S OWN COLOURS MUST FOLLOW THE THEME, or a dark theme paints
     * dark text on the widget's light background and the result is less
     * readable than no highlighting at all -- the one case where doing half
     * the job is worse than doing none. */
    if (th.isValid()) {
        QColor bg(QRgb(th.editorColor(
            KSyntaxHighlighting::Theme::BackgroundColor)));
        QColor fg(QRgb(th.textColor(KSyntaxHighlighting::Theme::Normal)));
        QPalette pal = e->palette();
        pal.setColor(QPalette::Base, bg);
        pal.setColor(QPalette::Text, fg);
        e->setPalette(pal);
    }
    return 1;
#else
    (void)language;
    (void)theme;
    say(err, err_size,
        QByteArray("`source ") + name +
            "` cannot be highlighted: this gBASIC was built without "
            "KSyntaxHighlighting, so it will work as a plain editor");
    return 0;
#endif
}

#if HAVE_KSYNTAX
static char *dup_qbytes(const QByteArray &bytes) {
    char *out = (char *)malloc((size_t)bytes.size() + 1);
    if (!out) {
        return nullptr;
    }
    memcpy(out, bytes.constData(), (size_t)bytes.size());
    out[bytes.size()] = 0;
    return out;
}

static KSyntaxHighlighting::Definition applied_definition(void *window,
                                                          const char *name) {
    QPlainTextEdit *e = source_named(window, name, nullptr, 0);
    if (!e) {
        return KSyntaxHighlighting::Definition();
    }
    KSyntaxHighlighting::SyntaxHighlighter *hl =
        e->findChild<KSyntaxHighlighting::SyntaxHighlighter *>(
            QString(), Qt::FindDirectChildrenOnly);
    return hl ? hl->definition() : KSyntaxHighlighting::Definition();
}
#endif

extern "C" char *gb_qt_highlight_language(void *window, const char *name) {
#if HAVE_KSYNTAX
    KSyntaxHighlighting::Definition def = applied_definition(window, name);
    return def.isValid() ? dup_qbytes(def.name().toUtf8()) : nullptr;
#else
    (void)window;
    (void)name;
    return nullptr;
#endif
}

extern "C" char *gb_qt_highlight_file(void *window, const char *name) {
#if HAVE_KSYNTAX
    KSyntaxHighlighting::Definition def = applied_definition(window, name);
    return def.isValid() ? dup_qbytes(def.filePath().toUtf8()) : nullptr;
#else
    (void)window;
    (void)name;
    return nullptr;
#endif
}

static QTableWidget *table_of(void *window, const char *name) {
    GbQtNode *root = (GbQtNode *)window;
    if (!root) {
        return nullptr;
    }
    return qobject_cast<QTableWidget *>(
        root->parts.value(QString::fromUtf8(name), nullptr));
}

/* A `choice`'S LIST, filled from data. The part must really BE one: a program
 * that put items into a QLineEdit by mistake would get no error from Qt and a
 * widget that looks untouched. */
static QComboBox *choice_named(void *window, const char *name, char *err,
                               size_t err_size) {
    QObject *o = part_named(window, name, err, err_size);
    if (!o) {
        return nullptr;
    }
    QComboBox *box = qobject_cast<QComboBox *>(o);
    if (!box) {
        say(err, err_size,
            QByteArray("`") + name + "` is not a `choice`, so it holds no list");
    }
    return box;
}

extern "C" int gb_qt_choice_clear(void *window, const char *name, char *err,
                                  size_t err_size) {
    QComboBox *box = choice_named(window, name, err, err_size);
    if (!box) {
        return 0;
    }
    box->clear();
    return 1;
}

extern "C" int gb_qt_choice_add(void *window, const char *name,
                                const char *item, char *err, size_t err_size) {
    QComboBox *box = choice_named(window, name, err, err_size);
    if (!box) {
        return 0;
    }
    box->addItem(QString::fromUtf8(item));
    return 1;
}

extern "C" int gb_qt_table_size(void *window, const char *name, int rows,
                                char *err, size_t err_size) {
    QTableWidget *t = table_of(window, name);
    if (!t) {
        say(err, err_size, QByteArray("`") + name + "` is not a table here");
        return 0;
    }
    t->setRowCount(rows);
    return 1;
}

extern "C" int gb_qt_table_set(void *window, const char *name, int row,
                               int col, const char *text, char *err,
                               size_t err_size) {
    QTableWidget *t = table_of(window, name);
    if (!t) {
        say(err, err_size, QByteArray("`") + name + "` is not a table here");
        return 0;
    }
    if (row < 0 || row >= t->rowCount() || col < 0 || col >= t->columnCount()) {
        say(err, err_size, "that cell is outside the table");
        return 0;
    }
    t->setItem(row, col, new QTableWidgetItem(QString::fromUtf8(text)));
    return 1;
}

extern "C" char *gb_qt_table_get(void *window, const char *name, int row,
                                 int col) {
    QTableWidget *t = table_of(window, name);
    if (!t || row < 0 || row >= t->rowCount() || col < 0 ||
        col >= t->columnCount()) {
        return nullptr;
    }
    /* AN EMPTY CELL HAS NO ITEM AT ALL in QTableWidget, so "" is built here
     * rather than returned as NULL: absent and empty are different answers,
     * and NULL is reserved for a cell that is not in the table. */
    QTableWidgetItem *cell = t->item(row, col);
    QByteArray bytes = cell ? cell->text().toUtf8() : QByteArray();
    char *out = (char *)malloc((size_t)bytes.size() + 1);
    if (!out) {
        return nullptr;
    }
    memcpy(out, bytes.constData(), (size_t)bytes.size());
    out[bytes.size()] = '\0';
    return out;
}

extern "C" int gb_qt_table_rows(void *window, const char *name) {
    QTableWidget *t = table_of(window, name);
    return t ? t->rowCount() : -1;
}

extern "C" int gb_qt_table_selected(void *window, const char *name) {
    QTableWidget *t = table_of(window, name);
    if (!t) {
        return -1;
    }
    /* THE SELECTION MODEL FIRST, `currentRow` second, and the order was
     * decided by trying it: with SelectRows + SingleSelection a click sets
     * BOTH, so in ordinary use either answers -- but a selection made any
     * other way (a program's `selectAll`, a keyboard range) sets the selection
     * and leaves `currentRow` at -1, so asking only the current row reports
     * "nobody has chosen" about a visibly highlighted row. Asking what is
     * SELECTED is also the more faithful reading of the question. */
    if (QItemSelectionModel *sel = t->selectionModel()) {
        QModelIndexList rows = sel->selectedRows();
        if (!rows.isEmpty()) {
            return rows.first().row();
        }
    }
    return t->currentRow();
}

extern "C" char *gb_qt_table_heading(void *window, const char *name,
                                     int col) {
    QTableWidget *t = table_of(window, name);
    if (!t || col < 0 || col >= t->columnCount()) {
        return nullptr;
    }
    QTableWidgetItem *h = t->horizontalHeaderItem(col);
    QByteArray bytes = h ? h->text().toUtf8() : QByteArray();
    char *out = (char *)malloc((size_t)bytes.size() + 1);
    if (!out) {
        return nullptr;
    }
    memcpy(out, bytes.constData(), (size_t)bytes.size());
    out[bytes.size()] = '\0';
    return out;
}

extern "C" int gb_qt_table_select(void *window, const char *name, int row,
                                 char *err, size_t err_size) {
    QTableWidget *t = table_of(window, name);
    if (!t) {
        say(err, err_size, QByteArray("`") + name + "` is not a table here");
        return 0;
    }
    if (row < 0) {
        t->clearSelection();
        t->setCurrentCell(-1, -1);
        return 1;
    }
    if (row >= t->rowCount()) {
        say(err, err_size, "that row is outside the table");
        return 0;
    }
    /* BOTH, deliberately. `selectRow` highlights and `setCurrentCell` moves
     * the current index, and a click does both -- setting only one leaves the
     * table in a state no person could have put it in, which is exactly the
     * state that made `selectAll` useless for this (SingleSelection ignores
     * it, so the first attempt at this tier reported "nobody has chosen"
     * about a table it had just told to select everything). */
    t->selectRow(row);
    t->setCurrentCell(row, 0);
    return 1;
}

extern "C" int gb_qt_set_display_only(void *window, const char *name, int on,
                                      char *err, size_t err_size) {
    GbQtNode *root = (GbQtNode *)window;
    QObject *w = root ? root->parts.value(QString::fromUtf8(name), nullptr)
                      : nullptr;
    if (!w) {
        say(err, err_size, QByteArray("this window has no `") + name + "`");
        return 0;
    }
    /* A VIEW IS A THIRD ANSWER and it has to come first: a QTableWidget has no
     * `readOnly`, and DISABLING it would also stop SELECTION -- which a
     * display-only table still needs, since choosing a row is reading and not
     * editing. `editTriggers` is the property that says exactly what is meant. */
    if (QAbstractItemView *v = qobject_cast<QAbstractItemView *>(w)) {
        v->setEditTriggers(on ? QAbstractItemView::NoEditTriggers
                              : QAbstractItemView::DoubleClicked |
                                    QAbstractItemView::EditKeyPressed |
                                    QAbstractItemView::AnyKeyPressed);
        return 1;
    }
    /* `readOnly` WHERE IT EXISTS, because it leaves the text selectable and
     * copyable, which is what a display form is for; `enabled` otherwise,
     * since a QCheckBox has no readOnly and a disabled box is the honest
     * rendering of "you cannot change this". Asked of the metaobject rather
     * than of the declaration's word, so a widget kind added later gets the
     * better of the two without a second table. */
    if (w->metaObject()->indexOfProperty("readOnly") >= 0) {
        return set_property(w, "readOnly", QVariant(on != 0), err, err_size);
    }
    return set_property(w, "enabled", QVariant(on == 0), err, err_size);
}

/* ---- DIALOGS ---------------------------------------------------------------
 *
 * ONE STACK, because a dialog can legitimately open over another: a handler
 * fired from a timer during the first one may ask its own question. Refusing
 * the second would be a rule about this backend rather than about dialogs.
 */
/* QDialog RATHER THAN QMessageBox, because a file chooser is one too and
 * `qt.answer` has to be able to tell them apart: the answer to a message box
 * is yes or no and the answer to a chooser is a PATH, so handing one the
 * other's answer must be a sentence rather than a guess. */
static QVector<QDialog *> g_dialogs;      /* innermost last */

/* Defined with `gb_qt_run`, which is the other caller. Declared here because a
 * dialog arms the same pump and sits above it in the file. */
static void pump_arm(int tick_ms);
static void pump_disarm(void);

extern "C" int gb_qt_dialog_depth(void) { return g_dialogs.size(); }

/* The window a box hangs off, or null for an application-level one. Checked
 * rather than assumed: a caller that passed something else gets a sentence
 * instead of a box parented to nothing. */
static QWidget *dialog_parent(void *window, char *err, size_t err_size) {
    GbQtNode *root = (GbQtNode *)window;
    if (!root || !root->widget) {
        say(err, err_size, "that is not an open window");
        return nullptr;
    }
    return root->widget;
}

static int show_box(void *window, const char *text, bool ask, int *answer,
                    char *err, size_t err_size) {
    QWidget *parent = dialog_parent(window, err, err_size);
    if (!parent) {
        return 0;
    }
    /* ON THE HEAP AND HELD BY A QPointer, not on the stack. A stack-allocated
     * QObject GIVEN A PARENT is a double free waiting to happen: the parent
     * deletes its children, and then the stack unwinds and destroys it again.
     * The parent is wanted (Qt centres the box on it and makes it modal to
     * that window), so the ownership has to be the other way round -- and the
     * QPointer is how we find out whether the parent took it while `exec()`
     * was running, rather than assuming it did not. */
    QPointer<QMessageBox> box = new QMessageBox(
        ask ? QMessageBox::Question : QMessageBox::Information,
        parent->windowTitle(), QString::fromUtf8(text ? text : ""),
        ask ? (QMessageBox::Yes | QMessageBox::No) : QMessageBox::Ok,
        parent);
    if (ask) {
        /* THE SAFE ONE IS THE DEFAULT. A question reached by pressing Return
         * without reading should not be the destructive answer. */
        box->setDefaultButton(QMessageBox::No);
    }
    g_dialogs.append(box.data());
    /* 50ms IS gBASIC'S OWN POLL TIMEOUT, not a number chosen here: a source
     * serviced on this timer is serviced exactly as often as it would be with
     * Qt out of the picture. Harmless when `qt.run()` already armed one -- the
     * count simply goes deeper. */
    pump_arm(50);
    int rc = box->exec();                   /* NESTED EVENT LOOP */
    pump_disarm();
    g_dialogs.removeAll(box.data());
    if (answer) {
        *answer = (rc == QMessageBox::Yes) ? 1 : 0;
    }
    /* ONLY IF IT IS STILL THERE. */
    if (box) {
        delete box;
    }
    return 1;
}

extern "C" int gb_qt_inform(void *window, const char *text, char *err,
                            size_t err_size) {
    return show_box(window, text, false, nullptr, err, err_size);
}

extern "C" int gb_qt_confirm(void *window, const char *text, int *answer,
                             char *err, size_t err_size) {
    return show_box(window, text, true, answer, err, err_size);
}

/* ---- A FILE CHOOSER (§0.16) ------------------------------------------------
 *
 * accept() AND done() ARE BOTH PROTECTED ON QFileDialog, which a QMessageBox's
 * buttons hid: there a box can be answered from outside by clicking the real
 * button. A chooser's Open button is a private child, so the only honest route
 * is a subclass -- and we own the construction, so that costs nothing.
 *
 * accept() IS KEPT RATHER THAN done(), deliberately: it is the one that
 * VALIDATES, so an ExistingFile chooser still refuses a path that is not there
 * instead of handing back a name nothing can read. The cost is the hang
 * `gb_qt_answer_path` guards against, which is a better trade than a chooser
 * whose promise ("this file exists") is only true when a person used it. */

static GbQtChooser *innermost_chooser(void) {
    if (g_dialogs.isEmpty()) {
        return nullptr;
    }
    return qobject_cast<GbQtChooser *>(g_dialogs.last());
}

extern "C" int gb_qt_answer(int value, char *err, size_t err_size) {
    if (g_dialogs.isEmpty()) {
        say(err, err_size, "no dialog is open");
        return 0;
    }
    /* A CHOOSER'S ANSWER IS A PATH, so `true` has no meaning here -- accept
     * WHAT? -- while `false` does: it is a cancel, which is what a timeout or
     * a program giving up actually wants to say. Refusing the one and honouring
     * the other is the difference between a verb that reads clearly and one
     * that guesses which file you meant. */
    if (GbQtChooser *ch = innermost_chooser()) {
        if (!value) {
            /* THE REAL CANCEL BUTTON, for the reason just below: it is what a
             * person presses. `reject()` is the fallback for a layout that has
             * none, rather than the route. */
            QDialogButtonBox *bb =
                ch->findChild<QDialogButtonBox *>(QStringLiteral("buttonBox"));
            QAbstractButton *no = nullptr;
            if (bb) {
                const QList<QAbstractButton *> buttons = bb->buttons();
                for (QAbstractButton *b : buttons) {
                    if (bb->buttonRole(b) == QDialogButtonBox::RejectRole) {
                        no = b;
                        break;
                    }
                }
            }
            if (no) {
                no->click();
            } else {
                ch->drive_reject();
            }
            return 1;
        }
        say(err, err_size,
            "the open dialog is a file chooser, so `true` does not say which "
            "file -- give qt.answer the path instead");
        return 0;
    }
    QMessageBox *box = qobject_cast<QMessageBox *>(g_dialogs.last());
    if (!box) {
        say(err, err_size, "the open dialog cannot be answered");
        return 0;
    }
    /* CLICK THE REAL BUTTON rather than calling done() with a code: the button
     * is what a person presses, so pressing it exercises the same path and
     * cannot drift from what the box actually offers. A box with no such
     * button (an `inform` answered `false`) falls back to its only one, since
     * "answer it" is the request either way. */
    QAbstractButton *b = box->button(value ? QMessageBox::Yes : QMessageBox::No);
    if (!b) {
        b = box->button(QMessageBox::Ok);
    }
    if (!b) {
        say(err, err_size, "the open dialog has no button to press");
        return 0;
    }
    b->click();
    return 1;
}

/* The button a person presses to say yes, whatever this mode calls it: Open,
 * Save or Choose. Found BY ROLE rather than by label, because the label is
 * translated and the role is not. */
static QAbstractButton *chooser_accept_button(GbQtChooser *ch) {
    QDialogButtonBox *bb =
        ch->findChild<QDialogButtonBox *>(QStringLiteral("buttonBox"));
    if (!bb) {
        return nullptr;
    }
    const QList<QAbstractButton *> buttons = bb->buttons();
    for (QAbstractButton *b : buttons) {
        if (bb->buttonRole(b) == QDialogButtonBox::AcceptRole) {
            return b;
        }
    }
    return nullptr;
}

extern "C" int gb_qt_answer_path(const char *path, char *err,
                                 size_t err_size) {
    if (g_dialogs.isEmpty()) {
        say(err, err_size, "no dialog is open");
        return 0;
    }
    GbQtChooser *ch = innermost_chooser();
    if (!ch) {
        say(err, err_size,
            "the open dialog is a message box, which is answered with true or "
            "false rather than with a path");
        return 0;
    }
    QString p = QString::fromUtf8(path ? path : "");
    if (p.isEmpty()) {
        say(err, err_size, "qt.answer needs a path, or false to cancel");
        return 0;
    }
    /* TYPED IN, NOT selectFile()'d, and that was measured rather than chosen:
     * `selectFile` is IGNORED while the dialog is visible and its name field
     * has focus -- deliberately, so Qt never overwrites what somebody is
     * typing -- so the obvious implementation left the selection EMPTY and the
     * accept silently refused. Writing the field is what selectFile does
     * internally when it is allowed to. */
    QLineEdit *field = ch->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
    QAbstractButton *go = chooser_accept_button(ch);
    if (!field || !go) {
        /* A LAYOUT WE DO NOT RECOGNISE SAYS SO rather than doing nothing: a
         * silent no-op here leaves a program waiting for a person who is not
         * there, and a hang is not a failure. */
        say(err, err_size,
            "this chooser does not have the name field and button Qt usually "
            "gives it, so it cannot be answered from the program");
        return 0;
    }
    field->setText(p);
    /* QT'S OWN RULE DECIDES, not ours. It disables the button for a path this
     * mode cannot accept -- measured, an absent file in an `ExistingFile`
     * chooser -- so the refusal is the chooser's own and cannot drift from what
     * it would do for a person. The message asks QFileInfo only to say WHY. */
    if (!go->isEnabled()) {
        QFileInfo info(p);
        if (!info.exists()) {
            say(err, err_size,
                QByteArray("this chooser will not take `") + p.toUtf8()
                    + "`, which is not there");
        } else {
            say(err, err_size,
                QByteArray("this chooser will not take `") + p.toUtf8() + "`");
        }
        return 0;
    }
    go->click();
    if (ch->result() != QDialog::Accepted) {
        say(err, err_size,
            QByteArray("the chooser did not accept `") + p.toUtf8() + "`");
        return 0;
    }
    return 1;
}

extern "C" char *gb_qt_choose(void *window, int kind, const char *title,
                              const char *folder, const char *name,
                              const char *filter, int *cancelled, char *err,
                              size_t err_size) {
    if (cancelled) {
        *cancelled = 0;
    }
    QWidget *parent = dialog_parent(window, err, err_size);
    if (!parent) {
        return nullptr;
    }
    /* ON THE HEAP BEHIND A QPointer, for the reason `show_box` records: a
     * stack-allocated QObject GIVEN A PARENT is a double free, and the parent
     * is wanted so Qt centres the chooser on the window and makes it modal to
     * it. */
    QPointer<GbQtChooser> dlg = new GbQtChooser(parent);
    dlg->setOption(QFileDialog::DontUseNativeDialog, true);   /* measured; §0.16 */
    switch (kind) {
    case GB_QT_PICK_SAVE:
        dlg->setFileMode(QFileDialog::AnyFile);
        dlg->setAcceptMode(QFileDialog::AcceptSave);
        break;
    case GB_QT_PICK_FOLDER:
        dlg->setFileMode(QFileDialog::Directory);
        dlg->setOption(QFileDialog::ShowDirsOnly, true);
        break;
    default:
        dlg->setFileMode(QFileDialog::ExistingFile);
        dlg->setAcceptMode(QFileDialog::AcceptOpen);
        break;
    }
    /* THE WINDOW'S OWN TITLE WHEN NONE IS GIVEN, which is what `show_box`
     * already does: a dialog belonging to no named thing is a dialog a person
     * cannot place. */
    dlg->setWindowTitle(title && *title ? QString::fromUtf8(title)
                                        : parent->windowTitle());
    if (folder && *folder) {
        dlg->setDirectory(QString::fromUtf8(folder));
    }
    if (filter && *filter) {
        /* `;;` IS QT'S OWN SEPARATOR and splitting here is what lets the
         * gBASIC side take a LIST -- a caller should not have to know that
         * two semicolons mean "or". A single string passes through unchanged,
         * so somebody who does know Qt's spelling loses nothing. */
        dlg->setNameFilters(QString::fromUtf8(filter).split(QStringLiteral(";;"),
                                                            Qt::SkipEmptyParts));
    }
    if (name && *name) {
        dlg->selectFile(QString::fromUtf8(name));
    }
    g_dialogs.append(dlg.data());
    pump_arm(50);
    int rc = dlg->exec();                   /* NESTED EVENT LOOP */
    pump_disarm();
    g_dialogs.removeAll(dlg.data());
    char *out = nullptr;
    /* THE RESULT CODE DECIDES, NOT THE SELECTION. Measured: a REJECTED
     * QFileDialog still reports selectedFiles(), so reading the list without
     * asking whether it was accepted hands back a path for a dialog the person
     * cancelled -- an ordinary-looking answer that is simply not true. */
    if (rc == QDialog::Accepted && dlg) {
        QStringList chosen = dlg->selectedFiles();
        if (!chosen.isEmpty()) {
            out = strdup(chosen.first().toUtf8().constData());
        }
    }
    if (!out && cancelled) {
        *cancelled = 1;
    }
    if (dlg) {
        delete dlg;
    }
    return out;
}

/* WHAT IS OPEN. Built before the fixture, because `title:` and `filter:` are
 * otherwise accepted and unobservable -- the hole a leaf's `label:` sat in for
 * four increments (§0.8). */
extern "C" int gb_qt_dialog_info(char **kind, char **title, char **text,
                                 char **folder, char **filter) {
    if (kind)   { *kind = nullptr; }
    if (title)  { *title = nullptr; }
    if (text)   { *text = nullptr; }
    if (folder) { *folder = nullptr; }
    if (filter) { *filter = nullptr; }
    if (g_dialogs.isEmpty()) {
        return 0;
    }
    QDialog *d = g_dialogs.last();
    if (title) {
        *title = strdup(d->windowTitle().toUtf8().constData());
    }
    if (GbQtChooser *ch = qobject_cast<GbQtChooser *>(d)) {
        if (kind) {
            *kind = strdup("chooser");
        }
        if (folder) {
            *folder = strdup(ch->directory().absolutePath().toUtf8().constData());
        }
        if (filter) {
            *filter = strdup(ch->nameFilters().join(QStringLiteral(";;"))
                                 .toUtf8().constData());
        }
        return 1;
    }
    if (QMessageBox *box = qobject_cast<QMessageBox *>(d)) {
        if (kind) {
            *kind = strdup("message");
        }
        if (text) {
            *text = strdup(box->text().toUtf8().constData());
        }
        return 1;
    }
    if (kind) {
        *kind = strdup("dialog");
    }
    return 1;
}

extern "C" int gb_qt_close(void *window, char *err, size_t err_size) {
    GbQtNode *n = (GbQtNode *)window;
    /* A CLOSE ALREADY IN FLIGHT MAY NOT BE FINISHED FROM INSIDE ITSELF, and
     * that is the whole reason `closing` exists. A `closed` handler runs while
     * the window is still registered -- which is what makes it useful -- so it
     * can reach this function again, and on the path that did NOT come through
     * here (the window manager's button, or an invoked `close`) the widget is
     * still ours to delete. Deleting it there destroys the event filter
     * currently executing, the node the outer frame is about to hand to
     * `g_closed`, and the widget both are parented to.
     *
     * MEASURED, AND BY PERTURBATION RATHER THAN BY READING: with this guard
     * written as `!n->widget` alone it caught only the `qt.close` path, where
     * nulling the widget below already stops a second entry -- so removing the
     * flag left the suite GREEN and the event-driven case segfaulted with no
     * output at all, block-buffered stdout going with the crash. Both
     * conditions are checked, and they answer in the same words because from
     * the caller's side they are the same situation. */
    if (!n || !n->widget || n->closing) {
        say(err, err_size, "that window is already closing");
        return 0;
    }
    QWidget *w = n->widget;
    /* THE BOOKKEEPING FIRST AND DIRECTLY, not through the event. `node_closed`
     * sets `closing`, so the filter below becomes a no-op and the handler sees
     * exactly what it sees on the window manager's path -- a window still
     * registered and still whole. Doing it here rather than relying on
     * `close()` is what makes the accepted-modal case work at all, since Qt
     * delivers no event for it. */
    (void)node_closed(n);
    /* CLEARED BEFORE CLOSING, and that order is the guard: a handler can reach
     * this function again, and `w` is held locally so only one call owns the
     * delete. */
    n->widget = nullptr;
    w->close();          /* Qt's own hide and cleanup; the filter is spent */
    delete w;            /* measured safe: close() has returned */
    node_retire(n);
    return 1;
}

extern "C" int gb_qt_show_modal(void *window, int *accepted, char *err,
                                size_t err_size) {
    GbQtNode *n = (GbQtNode *)window;
    QDialog *d = n ? qobject_cast<QDialog *>(n->widget) : nullptr;
    if (!d) {
        say(err, err_size, "that window was not opened modally");
        return 0;
    }
    /* 50ms IS gBASIC'S OWN POLL TIMEOUT, the same number and the same reason
     * `show_box` gives: a source serviced on this timer is serviced exactly as
     * often as it would be with Qt out of the picture. The count simply goes
     * deeper when `qt.run()` already armed one. */
    pump_arm(50);
    int rc = d->exec();                     /* NESTED EVENT LOOP */
    pump_disarm();
    if (accepted) {
        *accepted = (rc == QDialog::Accepted) ? 1 : 0;
    }
    return 1;
}

extern "C" int gb_qt_answer_modal(void *window, int accept, char *err,
                                  size_t err_size) {
    GbQtNode *n = (GbQtNode *)window;
    QDialog *d = n ? qobject_cast<QDialog *>(n->widget) : nullptr;
    if (!d) {
        /* NAMED HERE RATHER THAN LEFT TO QT, although Qt would also refuse:
         * a QWidget has no `accept` at all, so the by-name route would answer
         * "a QWidget emits no..." about a method, which names the wrong thing.
         * What the author needs to hear is that this window was not opened
         * modally. */
        say(err, err_size, "that window was not opened modally");
        return 0;
    }
    /* `done()` RATHER THAN accept()/reject(), which are the same call with the
     * code baked in -- one path, so the two answers cannot diverge. */
    d->done(accept ? QDialog::Accepted : QDialog::Rejected);
    return 1;
}

extern "C" void gb_qt_show(void *window) {
    GbQtNode *n = (GbQtNode *)window;
    if (n && n->widget) {
        n->widget->show();
    }
}

/* gBASIC'S SOURCES RIDE A QT TIMER. Qt owns the wait, so they cannot have a
 * poll of their own; a Qt timer is serviced inside a NESTED Qt loop exactly as
 * a QSocketNotifier is, which is the property that makes direction A survive a
 * modal dialog. Only armed when there is something to service.
 *
 * REFERENCE-COUNTED, AND THAT IS NOT TIDINESS -- it is what makes the promise
 * UNIFORM. The pump used to live inside `gb_qt_run`, so a dialog opened
 * BEFORE the program handed over the thread had no pump at all: `exec()`
 * opened a nested loop with nothing servicing gBASIC, so a timer could not
 * fire and a program could not answer its own dialog. MEASURED -- the first
 * `qt.confirm` written outside `qt.run()` hung until a 30s timeout. A dialog
 * arms it too now, so "gBASIC's own work keeps running while a box is up" is
 * true wherever the box was opened from, rather than only in the arrangement
 * the pump happened to be written for. */
static GbQtRelay *g_pump = nullptr;
static int g_pump_depth = 0;

static void pump_arm(int tick_ms) {
    if (g_pump_depth++ > 0) {
        return;                      /* already running, inner loop or dialog */
    }
    if (!g_tick || tick_ms <= 0 || !g_app) {
        return;
    }
    g_pump = new GbQtRelay();
    g_pump->setParent(g_app);
    QTimer *t = new QTimer(g_pump);
    QObject::connect(t, &QTimer::timeout, g_pump, &GbQtRelay::tick);
    t->start(tick_ms);
}

static void pump_disarm(void) {
    if (--g_pump_depth > 0) {
        return;
    }
    if (g_pump) {
        delete g_pump;
        g_pump = nullptr;
    }
}

extern "C" int gb_qt_run(int tick_ms) {
    if (!g_app) {
        return -1;
    }
    pump_arm(tick_ms);
    int rc = g_app->exec();
    pump_disarm();
    return rc;
}

/* `exit(0)` AND NOT `quit()`, WHICH IS A CORRECTION RATHER THAN A PREFERENCE.
 * MEASURED on Qt 6.10.2: `QCoreApplication::quit()` CLOSES EVERY TOP-LEVEL
 * WINDOW (it calls closeAllWindows), where `exit(0)` ends the loop and leaves
 * them alone -- the widget is still visible afterwards.
 *
 * SO `qt.stop()` WAS DISMISSING THE PROGRAM'S WINDOWS as a side effect of
 * asking for the thread back, and nothing could see it: the windows were
 * merely HIDDEN, every part stayed readable, and no fixture asked whether the
 * window was still up. It surfaced only once a close became bookkeeping, when
 * a program that reads its window after `qt.run()` returned started being
 * told it was not open (measured: eight read one textually after `qt.run()`,
 * exactly one actually broke).
 *
 * `qt.stop()` MEANS "GIVE ME THE THREAD BACK" and nothing more. A program that
 * wants its window gone says `qt.close`, and the person who wants it gone
 * presses the button; neither is what `stop` asks for, and firing `on
 * <win>.closed` on an orderly shutdown would make that handler mean two
 * different things. */
extern "C" void gb_qt_stop(void) {
    if (g_app) {
        QCoreApplication::exit(0);
    }
}

extern "C" void gb_qt_end(void) {
    /* A WINDOW WAITING TO BE RETIRED is deleted by the loop below like any
     * other -- its node still holds the widget -- so the list is only cleared,
     * never drained, because draining it would delete the same widget twice. */
    g_retired.clear();
    /* A WINDOW IS THE ONE WIDGET WITH NO QT PARENT, so it is the one this has
     * to delete; everything else goes with it, Qt owning its children. The
     * nodes themselves are ours either way. */
    for (int i = 0; i < g_nodes.size(); i++) {
        if (g_nodes[i]->root == g_nodes[i] && g_nodes[i]->widget) {
            delete g_nodes[i]->widget;
            g_nodes[i]->widget = nullptr;
        }
    }
    for (int i = 0; i < g_nodes.size(); i++) {
        delete g_nodes[i];
    }
    g_nodes.clear();
    delete g_app;
    g_app = nullptr;
}
