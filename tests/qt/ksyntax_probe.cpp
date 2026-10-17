/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
 *
 * DOES THE `editors/kate/gbasic.xml` THIS REPO ALREADY SHIPS WORK WITH THE
 * ENGINE A QT EDITOR WOULD USE?
 *
 * docs/qt_gui_design.md §0.1 records that Studio is to move from GTK to Qt, and
 * that its EDITOR is the piece with no Qt counterpart: GTK's answer is
 * GtkSourceView plus a `.lang` file. The nearest Qt answer is KDE's
 * KSyntaxHighlighting, which IS the extracted Kate highlighting engine -- and
 * `editors/kate/gbasic.xml` was written for Kate, so the question is whether
 * that file is already the input this engine wants.
 *
 * NO QApplication AND NO WIDGET. `AbstractHighlighter` is a pure callback
 * interface, so highlighting is testable HEADLESSLY and DETERMINISTICALLY --
 * which is worth more than the widget question, because it means a gBASIC
 * highlighter can be gated like anything else in this tree rather than behind a
 * display.
 *
 * THIS IS A PROBE, NOT THE START OF ANYTHING. No gBASIC code is linked and
 * nothing here is an API to code against.
 */
/* DOES THE gbasic.xml THIS REPO ALREADY SHIPS ACTUALLY WORK?
 *
 * No QApplication and no widget: KSyntaxHighlighting::AbstractHighlighter is a
 * pure callback interface, so highlighting is testable HEADLESSLY and
 * deterministically -- which is worth more than the widget question, because it
 * means a gBASIC highlighter could be gated like anything else in this tree. */
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/State>
#include <KSyntaxHighlighting/Theme>
#include <QString>
#include <QStringList>
#include <cstdio>

using namespace KSyntaxHighlighting;

class Recorder : public AbstractHighlighter {
public:
    QStringList seen;
    QString line;
    /* highlightLine is PROTECTED -- it is meant to be driven by the subclass,
     * which is the design: a highlighter owns its own iteration. */
    State run(const QString &text, const State &in) { return highlightLine(text, in); }
protected:
    void applyFormat(int offset, int length, const Format &f) override {
        seen << QString("%1:%2=%3").arg(offset).arg(length).arg(f.name());
    }
};

int main() {
    Repository repo;
    repo.addCustomSearchPath(QStringLiteral("custom"));
    /* BY EXTENSION it is ambiguous -- a BUNDLED definition claims *.bas. */
    Definition by_ext = repo.definitionForFileName(QStringLiteral("program.bas"));
    printf("by_extension=%s priority=%d\n",
           by_ext.isValid() ? by_ext.name().toUtf8().constData() : "NONE",
           by_ext.isValid() ? by_ext.priority() : -999);
    /* BY NAME is unambiguous, which is how a host that knows its own language
     * must ask. */
    Definition def = repo.definitionForName(QStringLiteral("gBASIC"));
    printf("by_name=%s priority=%d\n",
           def.isValid() ? def.name().toUtf8().constData() : "NONE",
           def.isValid() ? def.priority() : -999);
    for (const auto &d : repo.definitions()) {
        if (d.extensions().contains(QStringLiteral("*.bas")))
            printf("claims *.bas: %-12s priority=%d\n",
                   d.name().toUtf8().constData(), d.priority());
    }
    printf("definition_for_bas=%s\n", def.isValid() ? def.name().toUtf8().constData() : "NONE");
    if (!def.isValid()) return 1;
    printf("translated_name=%s author=%s\n",
           def.translatedName().toUtf8().constData(), def.author().toUtf8().constData());

    Recorder r;
    r.setDefinition(def);
    r.setTheme(repo.defaultTheme());
    State s;
    const char *lines[] = {
        "function total(rows)",
        "    t {USD}= 0.00",
        "    ' a comment",
        "    print \"hello\"",
        "end function",
    };
    for (int i = 0; i < 5; i++) {
        r.line = QString::fromUtf8(lines[i]);
        s = r.run(r.line, s);
    }
    printf("formats=%d\n", (int)r.seen.size());
    /* Which named formats did it produce? That is the real question -- a
     * definition that loads and classifies NOTHING is worse than none. */
    QStringList names;
    for (const QString &e : r.seen) {
        QString n = e.section('=', 1);
        if (!names.contains(n)) names << n;
    }
    names.sort();
    printf("kinds=%s\n", names.join(QStringLiteral(",")).toUtf8().constData());
    return 0;
}
