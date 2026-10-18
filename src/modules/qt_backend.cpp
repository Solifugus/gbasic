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
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
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

extern "C" void gb_qt_set_callbacks(GbQtInvokeFn invoke, GbQtTickFn tick) {
    g_invoke = invoke;
    g_tick = tick;
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

void GbQtRelay::fire() {
    if (g_invoke) {
        g_invoke(handler.constData());
    }
}

void GbQtRelay::tick() {
    /* ONE NON-BLOCKING ITERATION OF gBASIC'S LOOP. It drains by construction,
     * running the real loop body; see note 2 at the top of this file. */
    if (g_tick && !g_tick()) {
        QCoreApplication::quit();
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
    GbQtNode *root = nullptr;
    bool is_form = false;
    QHash<QString, QWidget *> parts;   /* root only */
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
    return 1;
}

extern "C" void *gb_qt_window(const char *title, int width, int height) {
    GbQtNode *n = node_new();
    QWidget *w = new QWidget();
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
    return n;
}

/* The container table. Four words, each deciding its own layout -- which is
 * the one thing a table is genuinely needed for (see note 4). */
extern "C" void *gb_qt_container(void *parent, const char *word,
                                 const char *title, char *err,
                                 size_t err_size) {
    GbQtNode *p = (GbQtNode *)parent;
    if (!p || !p->layout) {
        say(err, err_size, "that container cannot hold anything");
        return nullptr;
    }
    GbQtNode *n = node_new();
    n->root = p->root;

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
    } else {
        say(err, err_size,
            QByteArray("no Qt container is wired for `") + word + "`");
        return nullptr;
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
    if (!p || !p->layout) {
        say(err, err_size, "that container cannot hold anything");
        return nullptr;
    }
    QBoxLayout *pb = qobject_cast<QBoxLayout *>(p->layout);
    QFormLayout *pf = qobject_cast<QFormLayout *>(p->layout);

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
    if (strcmp(word, "field") == 0 && pf) {
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

/* PROPERTIES BY NAME. `setProperty` on a name the widget's QMetaObject does
 * not carry would SUCCEED, storing a dynamic property nothing reads -- an
 * option silently doing nothing, which is the defect the window block's
 * closed vocabulary exists to prevent one level up. So the name is checked
 * against the metaobject first and refused by name. */
static int set_property(QWidget *widget, const char *prop, const QVariant &v,
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

extern "C" int gb_qt_set_text(void *widget, const char *prop,
                              const char *value, char *err, size_t err_size) {
    GbQtNode *n = (GbQtNode *)widget;
    return set_property(n ? n->widget : nullptr, prop,
                        QVariant(QString::fromUtf8(value)), err, err_size);
}

extern "C" int gb_qt_set_number(void *widget, const char *prop, double value,
                                char *err, size_t err_size) {
    GbQtNode *n = (GbQtNode *)widget;
    return set_property(n ? n->widget : nullptr, prop, QVariant(value), err,
                        err_size);
}

extern "C" int gb_qt_set_bool(void *widget, const char *prop, int value,
                              char *err, size_t err_size) {
    GbQtNode *n = (GbQtNode *)widget;
    return set_property(n ? n->widget : nullptr, prop, QVariant(value != 0),
                        err, err_size);
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
extern "C" int gb_qt_connect(void *window, const char *target,
                             const char *signal, const char *handler,
                             char *err, size_t err_size) {
    GbQtNode *root = (GbQtNode *)window;
    if (!root) {
        say(err, err_size, "there is no window to connect that to");
        return 0;
    }
    QWidget *w = root->parts.value(QString::fromUtf8(target), nullptr);
    if (!w) {
        /* UNREACHABLE FROM A DECLARATION and kept anyway: the load-time pass
         * already refuses `on cancel.clicked` with no `cancel` (§0.2), so
         * arriving here means the record and the build disagree. */
        say(err, err_size,
            QByteArray("this window has no `") + target + "` to connect to");
        return 0;
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
    QWidget *w = root ? root->parts.value(QString::fromUtf8(target), nullptr)
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

extern "C" char *gb_qt_read(void *window, const char *name) {
    GbQtNode *root = (GbQtNode *)window;
    if (!root) {
        return nullptr;
    }
    QWidget *w = root->parts.value(QString::fromUtf8(name), nullptr);
    if (!w) {
        return nullptr;
    }
    QVariant v;
    if (qobject_cast<QLineEdit *>(w)) {
        v = w->property("text");
    } else if (qobject_cast<QCheckBox *>(w)) {
        v = w->property("checked");
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
    QWidget *w = root ? root->parts.value(QString::fromUtf8(name), nullptr)
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
    return set_property(w, "text", QVariant(QString::fromUtf8(text)), err,
                        err_size);
}

extern "C" void gb_qt_show(void *window) {
    GbQtNode *n = (GbQtNode *)window;
    if (n && n->widget) {
        n->widget->show();
    }
}

extern "C" int gb_qt_run(int tick_ms) {
    if (!g_app) {
        return -1;
    }
    /* gBASIC'S SOURCES RIDE A QT TIMER. Qt owns the wait, so they cannot have
     * a poll of their own; a Qt timer is serviced inside a NESTED Qt loop
     * exactly as a QSocketNotifier is, which is the property that makes
     * direction A survive a modal dialog. Only armed when there is something
     * to service. */
    GbQtRelay *pump = nullptr;
    if (g_tick && tick_ms > 0) {
        pump = new GbQtRelay();
        pump->setParent(g_app);
        QTimer *t = new QTimer(pump);
        QObject::connect(t, &QTimer::timeout, pump, &GbQtRelay::tick);
        t->start(tick_ms);
    }
    int rc = g_app->exec();
    if (pump) {
        delete pump;
    }
    return rc;
}

extern "C" void gb_qt_stop(void) {
    if (g_app) {
        QCoreApplication::quit();
    }
}

extern "C" void gb_qt_end(void) {
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
