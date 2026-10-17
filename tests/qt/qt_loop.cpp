/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
 *
 * THE QT EVENT-LOOP PROBE. docs/qt_gui_design.md §5 names one thing as
 * unproven before any code is written: `QApplication::exec()` against gBASIC's
 * own event loop, "plus doing the re-entry from a C++/moc translation unit".
 * The signal path is NOT the risk -- `gi_signal_marshal` in src/eval.c has
 * shipped exactly that for GTK -- but the loop does not transfer, because
 * gBASIC's answer there is GLib's.
 *
 * IT LINKS NO gBASIC CODE, DELIBERATELY. The loop question is answerable in
 * isolation: gBASIC's loop is ONE poll() over the webserver's descriptors,
 * libcurl's descriptors and the root mailbox, with timers riding the poll
 * timeout (webserver_run_event_loop in src/eval.c), so every source is a
 * descriptor or a deadline and a stand-in needs only a real poll over a real
 * descriptor. Keeping gBASIC out means the answer cannot be confounded by
 * anything else, and it means this probe stays buildable when the interpreter
 * changes.
 *
 * THIS IS NOT THE START OF THE LIBRARY. Nothing here is an API anybody should
 * code against; it exists to produce the two numbers §5 asked for and to keep
 * producing them.
 */
#include "qt_loop.h"

#include <QApplication>
#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QTimer>
#include <QSocketNotifier>
#include <QDialog>
#include <QEventLoop>
#include <QObject>
#include <unistd.h>

/* ONE GENERIC RELAY, which is the shape the design proposes for every signal:
 * a widget's signal reaches this object, and it calls back into C. For the real
 * library this is where `invoke_function` goes -- `gi_signal_marshal` in
 * src/eval.c already does exactly that for GTK, which is why §5's corrected
 * text says the signal path is NOT the risk. */
class Relay : public QObject {
    Q_OBJECT
public:
    qt_spike_cb cb = nullptr;
    int ticks = 0;
    int watched_fd = -1;
public slots:
    void onClicked()  { if (cb) cb("clicked"); }
    void onTick()     { ticks++; if (cb) cb("tick"); }
    void onReadable() {
        /* DRAIN IT. A QSocketNotifier is LEVEL-TRIGGERED, so a callback that
         * does not consume the bytes is re-entered for as long as they sit
         * there -- measured at 545,066 activations for one byte before this
         * line existed. Whatever becomes of this spike, the contract for a
         * gBASIC source registered with Qt is that its handler drains. */
        char buf[256];
        while (watched_fd >= 0 && read(watched_fd, buf, sizeof buf) > 0) { }
        if (cb) cb("gbasic-source");
    }
    void onModal() {
        /* A MODAL DIALOG RUNS A NESTED QT EVENT LOOP. In direction B that loop
         * does not return until the dialog closes, so gBASIC's poll never runs
         * and every one of its sources starves -- a webserver stops answering
         * while a message box is up. This is the measurement, not a feature. */
        if (cb) cb("modal-enter");
        QDialog d;
        QTimer::singleShot(600, &d, &QDialog::accept);
        d.exec();
        if (cb) cb("modal-leave");
    }
};
#include "qt_loop.moc"

static QApplication *app = nullptr;
static QWidget *win = nullptr;
static QPushButton *button = nullptr;
static Relay *relay = nullptr;
static QSocketNotifier *notifier = nullptr;
static int fake_argc = 1;
static char fake_arg0[] = "gbasic";
static char *fake_argv[] = { fake_arg0, nullptr };

extern "C" int qt_spike_init(qt_spike_cb cb) {
    if (app) return 0;
    app = new QApplication(fake_argc, fake_argv);
    relay = new Relay();
    relay->cb = cb;
    win = new QWidget();
    button = new QPushButton("press me", win);
    QVBoxLayout *box = new QVBoxLayout(win);
    box->addWidget(button);
    QObject::connect(button, &QPushButton::clicked, relay, &Relay::onClicked);
    QTimer *t = new QTimer(relay);
    QObject::connect(t, &QTimer::timeout, relay, &Relay::onTick);
    t->start(100);
    return 0;
}

extern "C" void qt_spike_show(void) { if (win) win->show(); }

extern "C" int qt_spike_pump(int max_ms) {
    if (!app) return -1;
    QCoreApplication::processEvents(QEventLoop::AllEvents, max_ms);
    return 0;
}

extern "C" int qt_spike_exec(void) { return app ? app->exec() : -1; }

extern "C" void qt_spike_watch_fd(int fd) {
    relay->watched_fd = fd;
    notifier = new QSocketNotifier(fd, QSocketNotifier::Read, relay);
    QObject::connect(notifier, &QSocketNotifier::activated, relay, &Relay::onReadable);
}

extern "C" void qt_spike_click_soon(int ms) {
    QTimer::singleShot(ms, button, &QPushButton::click);
}
extern "C" void qt_spike_modal_soon(int ms) {
    QTimer::singleShot(ms, relay, &Relay::onModal);
}
extern "C" void qt_spike_quit_soon(int ms) {
    QTimer::singleShot(ms, app, &QApplication::quit);
}
extern "C" int qt_spike_timer_ticks(void) { return relay ? relay->ticks : -1; }
extern "C" void qt_spike_shutdown(void) { if (win) win->hide(); }
