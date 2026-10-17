#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
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
/* THE HARNESS STANDS IN FOR gBASIC'S LOOP, and it is the same shape:
 * webserver_run_event_loop is ONE poll() over the webserver's descriptors,
 * libcurl's descriptors and the root mailbox, with timers riding the poll
 * timeout. So a stand-in needs a real poll over a real descriptor and a
 * deadline -- nothing about gBASIC itself is required to answer the loop
 * question, which is why this spike links no gBASIC code. */
#include "qt_loop.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <poll.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>

static int clicked = 0, ticks_seen = 0, source_seen = 0, modal_depth = 0;
static long modal_enter_ms = 0, modal_leave_ms = 0;

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void on_event(const char *what) {
    if (strcmp(what, "clicked") == 0)       clicked++;
    else if (strcmp(what, "tick") == 0)     ticks_seen++;
    else if (strcmp(what, "gbasic-source") == 0) source_seen++;
    else if (strcmp(what, "modal-enter") == 0) { modal_depth++; modal_enter_ms = now_ms(); }
    else if (strcmp(what, "modal-leave") == 0) { modal_depth--; modal_leave_ms = now_ms(); }
}

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "B";
    int pipefd[2];
    if (pipe(pipefd) != 0) { perror("pipe"); return 2; }
    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);

    if (qt_spike_init(on_event) != 0) { fprintf(stderr, "init failed\n"); return 2; }
    qt_spike_show();

    if (strcmp(mode, "A") == 0) {
        /* DIRECTION A: Qt owns the loop and a gBASIC source is registered with
         * it. This is what a GUI application wants -- Qt's dispatcher handles
         * input with its own priorities -- and it needs every gBASIC source to
         * be a descriptor, which the poll above establishes they are. */
        int want_modal = (argc > 2 && strcmp(argv[2], "modal") == 0);
        qt_spike_watch_fd(pipefd[0]);
        qt_spike_click_soon(150);
        if (want_modal) qt_spike_modal_soon(250);
        qt_spike_quit_soon(1500);
        /* FOUR WRITES, three of them DURING the modal dialog if one is asked
         * for -- which is the decisive comparison with direction B, where the
         * nested loop stalled gBASIC's poll for the dialog's whole life. */
        if (fork() == 0) {
            for (int i = 0; i < 4; i++) {
                usleep(300000);
                ssize_t n = write(pipefd[1], "x", 1);
                (void)n;
            }
            _exit(0);
        }
        qt_spike_exec();
        printf("mode=A clicked=%d qt_ticks=%d gbasic_source_served=%d",
               clicked, qt_spike_timer_ticks(), source_seen);
        if (want_modal) printf(" modal_blocked_ms=%ld", modal_leave_ms - modal_enter_ms);
        printf("\n");
        /* THE ASSERTION IS THAT THE SOURCE WAS SERVED AT LEAST THREE TIMES --
         * i.e. during the dialog -- and that the notifier did NOT spin. */
        return !(clicked == 1 && source_seen >= 3 && source_seen < 50
                 && qt_spike_timer_ticks() >= 5);
    }

    /* DIRECTION B: gBASIC owns the loop and Qt is pumped inside it. */
    int want_modal = (argc > 2 && strcmp(argv[2], "modal") == 0);
    qt_spike_click_soon(150);
    if (want_modal) qt_spike_modal_soon(300);
    long start = now_ms(), last_poll = start, worst_gap = 0, polls = 0;
    while (now_ms() - start < 1500) {
        struct pollfd p = { pipefd[0], POLLIN, 0 };
        poll(&p, 1, 50);            /* gBASIC's own wait, 50ms like the real one */
        long t = now_ms();
        long gap = t - last_poll;
        if (polls > 0 && gap > worst_gap) worst_gap = gap;
        last_poll = t; polls++;
        qt_spike_pump(0);           /* give Qt its turn */
    }
    printf("mode=B clicked=%d qt_ticks=%d gbasic_polls=%ld worst_gbasic_gap_ms=%ld",
           clicked, qt_spike_timer_ticks(), polls, worst_gap);
    if (want_modal) {
        printf(" modal_blocked_ms=%ld", modal_leave_ms - modal_enter_ms);
    }
    printf("\n");
    qt_spike_shutdown();
    /* The gBASIC loop must have kept running (its own poll count) AND Qt must
     * have made progress (its timer, and the click). */
    return !(clicked == 1 && qt_spike_timer_ticks() >= 10 && polls >= 20);
}
