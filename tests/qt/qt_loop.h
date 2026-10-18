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
/* The C face of the Qt spike. Nothing here mentions Qt: the whole point is
 * that the C++/moc translation unit is reachable from C, which is what
 * docs/qt_gui_design.md §5 calls the unproven half. */
#ifndef QT_SPIKE_H
#define QT_SPIKE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*qt_spike_cb)(const char *what);

/* CAN THIS MACHINE OPEN A WINDOW AT ALL? Asked in a FORKED CHILD, and that is
 * not caution but the only thing that works: with no display Qt calls qFatal,
 * which ABORTS -- measured, SIGABRT and a core dump -- and qFatal cannot be
 * caught, so by the time our process knows, it is gone. A crash also takes
 * block-buffered stdout with it, so a program printing its progress prints
 * NOTHING and the failure reads as line 1. The same defect GTK produced
 * (run_gi.sh: a widget built before gtk_init, exit 139).
 *
 * AND IT CANNOT BE DONE BY READING THE ENVIRONMENT, which was the obvious
 * guess and is wrong: measured, Qt found Wayland through XDG_RUNTIME_DIR with
 * WAYLAND_DISPLAY unset, so its detection is richer than any reimplementation
 * of it would be. Asking Qt itself, in a process we can afford to lose, is the
 * only answer that stays true as Qt changes.
 *
 * 1 yes, 0 no. One fork per program that opens a window, cached by the caller. */
int  qt_spike_can_display(void);
int  qt_spike_init(qt_spike_cb cb);       /* 0 ok */
void qt_spike_show(void);
int  qt_spike_pump(int max_ms);           /* DIRECTION B: gBASIC owns the loop */
int  qt_spike_exec(void);                 /* DIRECTION A: Qt owns the loop */
void qt_spike_watch_fd(int fd);           /* DIRECTION A: a gBASIC source in Qt */
void qt_spike_click_soon(int ms);         /* a real signal, no human needed */
void qt_spike_modal_soon(int ms);         /* the nested loop that could kill B */
void qt_spike_quit_soon(int ms);
int  qt_spike_timer_ticks(void);
void qt_spike_shutdown(void);

#ifdef __cplusplus
}
#endif
#endif
