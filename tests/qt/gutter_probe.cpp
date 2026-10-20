/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
 *
 * DOES THE MARGIN REALLY PAINT? The claim gBASIC cannot make, and it has to be
 * made somewhere: `qt.marks` answers WHAT WAS SET, so a backend that stored a
 * list and painted nothing satisfies every check in the language -- which is
 * the green-perturbation shape this backend is tested against, and the reason
 * §0.20-§0.26 produced five verbs by asking what would be observable if the
 * increment did nothing. Here the honest answer is that a painted glyph is not
 * observable from gBASIC at all: a QTextLayout's format ranges are reachable
 * (that is what source_probe.cpp reads) and a QWidget's PIXELS are not.
 *
 * IT LINKS THE REAL BACKEND rather than reimplementing the setup in C++, which
 * is the difference from source_probe.cpp and is strictly stronger: everything
 * below goes through the same `gb_qt_*` seam `src/modules/qt.c` uses, so what
 * is measured is the painter the product runs and not a second copy of it that
 * could be right while the first is wrong.
 *
 * NOTHING PAINTS WITHOUT A REAL TURN OF QT'S LOOP, which was measured before
 * this was written: a bare `repaint()` on an offscreen window painted 0 gutter
 * lines and `update()` followed by `processEvents()` painted 24. So the probe
 * hands the thread over, and a count of zero means the measurement did not
 * happen rather than that the painter is broken -- which is why the control
 * comes first and is required to be non-zero.
 */
#include "qt_backend.h"

#include <QApplication>
#include <QWidget>
#include <cstdio>
#include <cstring>

static void no_invoke(const char *) {}
static int no_tick(void) { return 1; }
static void no_closed(void *) {}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    if (!gb_qt_can_display()) {
        printf("display=no\n");
        return 2;
    }
    char err[256];
    err[0] = '\0';
    gb_qt_set_callbacks(no_invoke, no_tick, no_closed, nullptr);
    if (!gb_qt_begin(err, sizeof(err))) {
        printf("begin=failed %s\n", err);
        return 3;
    }
    void *win = gb_qt_window("probe", "Gutter probe", 600, 400, 0);
    if (!win) {
        printf("window=failed\n");
        return 4;
    }
    void *col = gb_qt_container(win, "column", nullptr, nullptr, err,
                                sizeof(err));
    if (!col) {
        printf("column=failed %s\n", err);
        return 5;
    }
    /* TWO EDITORS IN ONE PROCESS, which is the whole arrangement: the second
     * has no margin at all, so the comparison does not rest on two runs being
     * otherwise identical. */
    void *code = gb_qt_part(col, "source", "code", nullptr, err, sizeof(err));
    void *bare = gb_qt_part(col, "source", "bare", nullptr, err, sizeof(err));
    if (!code || !bare) {
        printf("parts=failed %s\n", err);
        return 6;
    }
    /* FAR MORE LINES THAN FIT, deliberately: the viewport shows about two
     * dozen of 200, so a painter that walked real block geometry paints only
     * what is visible while one that looped over the document paints 200.
     * That difference is what says these counters came from a paint at all. */
    QString text;
    for (int i = 1; i <= 200; i++) {
        text += QString("line %1\n").arg(i);
    }
    gb_qt_write(win, "code", text.toUtf8().constData(), err, sizeof(err));
    gb_qt_write(win, "bare", text.toUtf8().constData(), err, sizeof(err));

    if (!gb_qt_gutter_enable(win, "code", 1, 1, err, sizeof(err))) {
        printf("enable=failed %s\n", err);
        return 7;
    }
    int width = 0, numbers = 0, marks = 0;
    gb_qt_gutter_info(win, "code", &numbers, &marks, &width, err, sizeof(err));

    /* ONE MARK PER CATEGORY on five distinct lines, so the painted count is
     * the number of CATEGORIES rather than one list drawn five times -- plus a
     * SIXTH line carrying TWO of them, which is what makes the priority rule
     * observable: six marked lines must paint SIX glyphs, where a painter
     * drawing every category's mark on a shared line would paint seven. */
    /* EVERY MARKED LINE IS INSIDE THE VISIBLE RANGE, which the first run of
     * this probe got wrong and the numbers said so: with the shared line at 20
     * and thirteen lines visible, the priority case sat BELOW THE FOLD and the
     * count was four. A check whose subject is off-screen measures nothing. */
    int sect[] = {3, 11};
    int errl[] = {6, 11};
    int warn[] = {9};
    int note[] = {12};
    int brk[]  = {10};
    gb_qt_mark_set(win, "code", gb_qt_mark_kind("section"), sect, 2, err,
                   sizeof(err));
    gb_qt_mark_set(win, "code", gb_qt_mark_kind("error"), errl, 2, err,
                   sizeof(err));
    gb_qt_mark_set(win, "code", gb_qt_mark_kind("warning"), warn, 1, err,
                   sizeof(err));
    gb_qt_mark_set(win, "code", gb_qt_mark_kind("note"), note, 1, err,
                   sizeof(err));
    gb_qt_mark_set(win, "code", gb_qt_mark_kind("break"), brk, 1, err,
                   sizeof(err));

    gb_qt_show(win);
    /* BEFORE ANY TURN OF THE LOOP, which is what makes the counts below
     * evidence rather than numbers: §0.25's lesson measured here again, that
     * nothing paints offscreen without a real turn of Qt's loop. If this is
     * already non-zero the counters are being set by something other than the
     * painter and everything after it is worthless. */
    int before_lines = -1, before_marks = -1;
    gb_qt_gutter_painted(&before_lines, &before_marks);
    printf("before_lines=%d before_marks=%d\n", before_lines, before_marks);

    for (int i = 0; i < 12; i++) {
        QApplication::processEvents();
    }

    int painted_lines = 0, painted_marks = 0;
    gb_qt_gutter_painted(&painted_lines, &painted_marks);

    printf("display=yes width=%d numbers=%d marks=%d total=200\n", width,
           numbers, marks);
    printf("painted_lines=%d painted_marks=%d\n", painted_lines, painted_marks);

    /* THE CONTROL, IN THE SAME PROCESS so the comparison does not rest on two
     * environments being identical: a margin with NUMBERS AND NO MARKS must
     * paint numbers and NO glyphs. Without it "six glyphs were painted" is
     * equally satisfied by a painter that draws one on every line, and the
     * mark lists would be measuring nothing. */
    if (!gb_qt_gutter_enable(win, "bare", 1, 0, err, sizeof(err))) {
        printf("bare_enable=failed %s\n", err);
        return 8;
    }
    int bsect[] = {2, 5};
    gb_qt_mark_set(win, "bare", gb_qt_mark_kind("section"), bsect, 2, err,
                   sizeof(err));
    for (int i = 0; i < 8; i++) {
        QApplication::processEvents();
    }
    int bare_lines = -1, bare_marks = -1;
    gb_qt_gutter_painted(&bare_lines, &bare_marks);
    printf("bare_lines=%d bare_marks=%d\n", bare_lines, bare_marks);
    return 0;
}
