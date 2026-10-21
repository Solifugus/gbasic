/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
 *
 * WHICH OF THE CLIPBOARD'S TWO DOORS SHORTENED THE TEXT -- the one thing
 * tests/qt_clipboard_test.bas cannot say, measured rather than assumed.
 *
 * THAT FIXTURE ASSERTS A ROUND TRIP, so it goes red if EITHER door loses the
 * bytes and it cannot tell which: measured, the perturbation that measures the
 * WRITE with strlen and the one that hands the READ back as a C string produce
 * IDENTICAL output (`got 1, want 3`). Two halves of one path agreeing with
 * each other perfectly is exactly the shape that let `webclient` hide the same
 * defect for as long as it did, and PLAT-NUL's standing lesson is that a
 * truncation found at one door is evidence about every door -- so each one is
 * exercised here ON ITS OWN, with Qt's clipboard as the other side.
 *
 * IT LINKS THE REAL BACKEND OBJECT and drives the same `gb_qt_*` seam
 * `src/modules/qt.c` uses -- tests/qt/gutter_probe.cpp's arrangement and for
 * its reason: what is measured is the code the product runs rather than a
 * second copy that could be right while the first is wrong.
 *
 * IT CONSTRUCTS NO QApplication OF ITS OWN, deliberately: `gb_qt_copy` begins
 * Qt itself, and the backend's own pointer is what `gb_qt_begin` tests, so a
 * QApplication made here would be invisible to it and the second one would be
 * refused by Qt.
 */
#include "qt_backend.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <cstdio>
#include <cstdlib>
#include <cstring>

/* `a` NUL `b` -- three bytes, and the one byte in the middle is what every
 * strlen-measured copy loses. */
static const char kThree[4] = { 'a', '\0', 'b', '\0' };

static void hex(const char *tag, const char *bytes, int n) {
    printf("%s=", tag);
    for (int i = 0; i < n; i++) {
        printf("%02x", (unsigned char)bytes[i]);
    }
    printf("\n");
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    char err[512];
    err[0] = '\0';
    if (!gb_qt_can_display()) {
        printf("display=no\n");
        return 0;
    }

    /* ---- DOOR ONE: the WRITE, judged by QT rather than by our reader ---- */
    if (!gb_qt_copy(kThree, 3, err, sizeof(err))) {
        printf("copy_refused=%s\n", err);
        return 1;
    }
    QClipboard *c = QGuiApplication::clipboard();
    if (!c) {
        printf("no_clipboard=yes\n");
        return 1;
    }
    QByteArray held = c->text().toUtf8();
    printf("write_len=%d\n", (int)held.size());
    hex("write_bytes", held.constData(), (int)held.size());

    /* ---- DOOR TWO: the READ, from a clipboard QT filled ----------------
     * Set directly, so nothing of ours is on the writing side. */
    c->setText(QString::fromUtf8("x\0y", 3));
    char *back = nullptr;
    long len = 0;
    int rc = gb_qt_clipboard(&back, &len, err, sizeof(err));
    /* ONE FIELD PER LINE, which the runner's own extractor forced: two values
     * on one line is the trap tests/run_ksyntax.sh records (a whole-value
     * reader answered the first field for both). */
    printf("read_rc=%d\n", rc);
    printf("read_len=%ld\n", len);
    if (rc == 1 && back) {
        hex("read_bytes", back, (int)len);
        free(back);
    }

    /* ---- AND THE ABSENT/EMPTY DISTINCTION, which is Qt's and not a guess:
     * a cleared clipboard has no text at all, where one set to "" has text of
     * length zero. Both answers travel through the same return value, so the
     * two cases are the only thing that says it is not guessing. */
    c->clear();
    back = nullptr; len = -1;
    rc = gb_qt_clipboard(&back, &len, err, sizeof(err));
    printf("cleared_rc=%d\n", rc);
    if (back) { free(back); }
    c->setText(QString());
    back = nullptr; len = -1;
    rc = gb_qt_clipboard(&back, &len, err, sizeof(err));
    printf("empty_rc=%d\n", rc);
    printf("empty_len=%ld\n", len);
    if (back) { free(back); }

    printf("done\n");
    return 0;
}
