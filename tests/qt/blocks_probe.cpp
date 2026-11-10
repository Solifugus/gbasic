/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
 *
 * BLOCKS ARE NOT TEXT (docs/qt_gui_design.md §0.38 A1): the claims gBASIC
 * cannot make about Studio's hard requirement. Setting, changing and clearing
 * blocks and folds must leave plainText, the undo stack, the modified flag,
 * the document's revision, the caret and the line count exactly as they were;
 * copying across a block copies only code; a click below a gap and the Down
 * key land on the right line; the scrollbar's end still shows the last line;
 * and a replace that sends the same block again keeps its widget (no
 * flicker). None of that is readable from gBASIC.
 *
 * IT LINKS THE REAL BACKEND and goes through the same gb_qt_* seam qt.c uses,
 * as gutter_probe.cpp does, so what is measured is what the product runs.
 * Prints `ok`/`BAD` lines and `checks N bad M`.
 */
#include "qt_backend.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QWidget>
#include <cstdio>
#include <cstring>

static void no_invoke(const char *) {}
static int no_tick(void) { return 1; }
static void no_closed(void *) {}

static int checks = 0, bad = 0;
static void check(const char *label, bool ok, const QString &detail = QString()) {
    checks++;
    if (!ok) {
        bad++;
    }
    printf("%s %s %s\n", ok ? "ok  " : "BAD ", label, qPrintable(detail));
}

static GbQtBlockItem text_item(const char *id, int line, const char *text) {
    GbQtBlockItem it;
    memset(&it, 0, sizeof(it));
    it.id = id;
    it.kind = "text";
    it.line = line;
    it.text = text;
    it.text_len = (long)strlen(text);
    it.max_height = 240;
    return it;
}

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
    void *win = gb_qt_window("probe", "Blocks probe", 600, 400, 0);
    void *col = gb_qt_container(win, "column", nullptr, nullptr, err, sizeof(err));
    void *ed = col ? gb_qt_part(col, "source", "ed", nullptr, err, sizeof(err)) : nullptr;
    if (!ed) {
        printf("parts=failed %s\n", err);
        return 4;
    }
    QString text;
    for (int i = 1; i <= 200; i++) {
        text += QString("line %1 = %1 * 2").arg(i);
        if (i < 200) {
            text += "\n";
        }
    }
    gb_qt_write(win, "ed", text.toUtf8().constData(), err, sizeof(err));
    gb_qt_show(win);
    QApplication::processEvents();
    QPlainTextEdit *e = nullptr;
    for (QWidget *w : QApplication::allWidgets()) {
        QPlainTextEdit *p = qobject_cast<QPlainTextEdit *>(w);
        if (p && p->document()->blockCount() == 200) {
            e = p;
        }
    }
    if (!e) {
        printf("editor=not found\n");
        return 5;
    }
    QTextDocument *doc = e->document();
    /* one real edit first, so "unchanged" is a stack with something on it */
    QTextCursor typed(doc->findBlockByNumber(199));
    typed.movePosition(QTextCursor::EndOfBlock);
    typed.insertText(" ");
    typed.deletePreviousChar();
    QTextCursor caret(doc->findBlockByNumber(9));
    caret.setPosition(caret.position() + 3);
    e->setTextCursor(caret);
    const QString plain0 = e->toPlainText();
    const int undo0 = doc->availableUndoSteps();
    const bool mod0 = doc->isModified();
    const int rev0 = doc->revision();
    const int caret0 = e->textCursor().position();

    GbQtBlockItem items[3] = { text_item("a", 5, "first\nblock"),
                               text_item("b", 50, "second"),
                               text_item("c", 120, "third") };
    gb_qt_blocks_set(win, "ed", items, 3, err, sizeof(err));
    QApplication::processEvents();
    items[1] = text_item("b", 51, "second, changed");
    gb_qt_blocks_set(win, "ed", items, 3, err, sizeof(err));
    int from[2] = { 20, 60 }, to[2] = { 24, 70 };
    gb_qt_fold_set(win, "ed", from, to, 2, err, sizeof(err));
    QApplication::processEvents();
    check("plainText unchanged by blocks and folds", e->toPlainText() == plain0);
    check("undo stack unchanged", doc->availableUndoSteps() == undo0,
          QString("%1 -> %2").arg(undo0).arg(doc->availableUndoSteps()));
    check("there was something on it to keep", undo0 > 0, QString::number(undo0));
    check("modified flag unchanged", doc->isModified() == mod0);
    check("revision unchanged", doc->revision() == rev0,
          QString("%1 -> %2").arg(rev0).arg(doc->revision()));
    check("caret unchanged", e->textCursor().position() == caret0);
    check("line count unchanged", doc->blockCount() == 200);

    /* the gap is there: line 6 begins below line 5's block */
    e->verticalScrollBar()->setValue(0);
    QApplication::processEvents();
    QRect r5 = e->cursorRect(QTextCursor(doc->findBlockByNumber(4)));
    QRect r6 = e->cursorRect(QTextCursor(doc->findBlockByNumber(5)));
    GbQtBlockInfo info[8];
    int n = gb_qt_blocks_get(win, "ed", info, 8, err, sizeof(err));
    int ha = n > 0 ? info[0].height : -1;
    check("line 6 is lower than line 5 by a line and block a's height",
          r6.top() - r5.top() >= ha + r5.height() - 1 && ha > 20,
          QString("5 at %1, 6 at %2, block %3").arg(r5.top()).arg(r6.top()).arg(ha));
    for (int i = 0; i < n; i++) { free(info[i].id); free(info[i].kind); }

    /* a click on line 6, below the gap, lands on line 6 */
    QPoint p6(20, r6.center().y());
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(p6), e->viewport()->mapToGlobal(QPointF(p6)),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(p6), e->viewport()->mapToGlobal(QPointF(p6)),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(e->viewport(), &press);
    QApplication::sendEvent(e->viewport(), &release);
    check("a click below the gap lands on that line", e->textCursor().blockNumber() == 5,
          QString("line %1").arg(e->textCursor().blockNumber() + 1));
    /* Down from line 5 is line 6, not into the block */
    e->setTextCursor(QTextCursor(doc->findBlockByNumber(4)));
    QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QApplication::sendEvent(e, &down);
    check("Down from a block's line goes to the next line", e->textCursor().blockNumber() == 5,
          QString("line %1").arg(e->textCursor().blockNumber() + 1));
    /* a fold keeps its first line (20) and hides 21-24: Down from 19 is the
     * first line, and Down from there skips the hidden ones to 25 */
    e->setTextCursor(QTextCursor(doc->findBlockByNumber(18)));
    QApplication::sendEvent(e, &down);
    check("Down onto a fold lands on its first line", e->textCursor().blockNumber() == 19,
          QString("line %1").arg(e->textCursor().blockNumber() + 1));
    QApplication::sendEvent(e, &down);
    check("and Down from there skips the folded lines", e->textCursor().blockNumber() == 24,
          QString("line %1").arg(e->textCursor().blockNumber() + 1));

    /* copying across a block copies only code */
    QTextCursor sel(doc->findBlockByNumber(3));
    sel.setPosition(doc->findBlockByNumber(6).position(), QTextCursor::KeepAnchor);
    e->setTextCursor(sel);
    e->copy();
    check("copying across a block copies only the code",
          QApplication::clipboard()->text() == "line 4 = 4 * 2\nline 5 = 5 * 2\nline 6 = 6 * 2\n",
          QApplication::clipboard()->text().left(80).replace("\n", "\\n"));

    /* the scrollbar's end still reaches the last line, with tall blocks in
     * the last screenful: the range has to include their heights (it did
     * not, until the layout announced a size change on every relayout) */
    GbQtBlockItem tail[3] = { text_item("t1", 185, "1\n2\n3\n4\n5\n6\n7\n8"),
                              text_item("t2", 190, "1\n2\n3\n4\n5\n6\n7\n8"),
                              text_item("t3", 195, "1\n2\n3\n4\n5\n6\n7\n8") };
    gb_qt_blocks_set(win, "ed", tail, 3, err, sizeof(err));
    QApplication::processEvents();
    QScrollBar *sb = e->verticalScrollBar();
    sb->setValue(sb->maximum());
    QApplication::processEvents();
    QRect last = e->cursorRect(QTextCursor(doc->lastBlock()));
    check("at the scrollbar's end the last line is in view",
          last.top() >= 0 && last.bottom() <= e->viewport()->height(),
          QString("last line %1..%2 of %3").arg(last.top()).arg(last.bottom()).arg(e->viewport()->height()));

    gb_qt_blocks_set(win, "ed", items, 3, err, sizeof(err));
    QApplication::processEvents();
    /* the same block sent again keeps its widget; a changed one does not */
    QWidget *before_a = nullptr, *before_c = nullptr;
    for (QObject *o : e->viewport()->children()) {
        QWidget *w = qobject_cast<QWidget *>(o);
        if (!w) continue;
        for (QPlainTextEdit *t : w->findChildren<QPlainTextEdit *>()) {
            if (t->toPlainText() == "first\nblock") before_a = w;
            if (t->toPlainText() == "third") before_c = w;
        }
    }
    items[2] = text_item("c", 120, "third, changed");
    gb_qt_blocks_set(win, "ed", items, 3, err, sizeof(err));
    QApplication::processEvents();
    QWidget *after_a = nullptr, *after_c = nullptr;
    for (QObject *o : e->viewport()->children()) {
        QWidget *w = qobject_cast<QWidget *>(o);
        if (!w) continue;
        for (QPlainTextEdit *t : w->findChildren<QPlainTextEdit *>()) {
            if (t->toPlainText() == "first\nblock") after_a = w;
            if (t->toPlainText() == "third, changed") after_c = w;
        }
    }
    check("an unchanged block keeps its widget across a replace", before_a && before_a == after_a);
    check("a changed one gets a new one", before_c && after_c && before_c != after_c);

    /* clearing everything leaves the document as it was */
    gb_qt_blocks_set(win, "ed", nullptr, 0, err, sizeof(err));
    gb_qt_fold_set(win, "ed", nullptr, nullptr, 0, err, sizeof(err));
    QApplication::processEvents();
    check("after clearing: plainText", e->toPlainText() == plain0);
    check("after clearing: undo stack", doc->availableUndoSteps() == undo0);
    check("after clearing: revision", doc->revision() == rev0);
    bool all_visible = true;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        all_visible = all_visible && b.isVisible();
    }
    check("after clearing: every line visible", all_visible);
    printf("checks %d bad %d\n", checks, bad);
    return bad ? 1 : 0;
}
