/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
 *
 * IS THE TEXT IN A `source` WIDGET REALLY COLOURED? -- the one claim about
 * docs/qt_gui_design.md §0.14 that tests/qt_source_test.bas cannot make,
 * because gBASIC cannot read a QTextLayout's format ranges.
 *
 * AND IT IS THE CLAIM THAT MATTERS, because of what was measured on
 * 2026-10-18 while the widget was being designed: an INVALID definition still
 * applies the theme's normal-text format to every line. So a `source` whose
 * language is not installed is not blank, not plain and not obviously wrong --
 * it is an ordinary-looking editor with ordinary-looking text, and every
 * assertion of the shape "formats were applied" PASSES on it. The number of
 * RANGES is what separates the two: a definition that classifies gives several
 * per line, one that does not gives one.
 *
 * BOTH HALVES IN ONE PROCESS, deliberately. The alternative is two runs with
 * XDG_DATA_HOME moved, and then the comparison rests on two environments
 * being otherwise identical; here the control is `definitionForName` on a
 * language that does not exist, which is exactly the Definition a machine with
 * nothing installed produces, and nothing about the run differs but that.
 *
 * NO QApplication-FREE SHORTCUT: unlike tests/qt/ksyntax_probe.cpp, which asks
 * AbstractHighlighter directly and needs no widget, this one is about the
 * WIDGET -- so it constructs one, which is why it runs offscreen.
 */
#include <QApplication>
#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextLayout>
#include <cstdio>

#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/SyntaxHighlighter>
#include <KSyntaxHighlighting/Theme>

/* Five lines of gBASIC carrying a comment, a keyword, a string and a number --
 * the four kinds editors/kate/gbasic.xml classifies. A one-line sample would
 * make the range count turn on which single kind it happened to contain. */
static const char *kSample =
    "' a comment\n"
    "program main()\n"
    "    x = 42\n"
    "    print \"hello\"\n"
    "end program\n";

static int ranges_with(KSyntaxHighlighting::Repository &repo,
                       const KSyntaxHighlighting::Definition &def,
                       int *lines_out) {
    QPlainTextEdit ed;
    ed.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    auto *hl = new KSyntaxHighlighting::SyntaxHighlighter(ed.document());
    hl->setDefinition(def);
    hl->setTheme(repo.defaultTheme(KSyntaxHighlighting::Repository::DarkTheme));
    ed.setPlainText(QString::fromUtf8(kSample));

    int ranges = 0, lines = 0;
    for (QTextBlock b = ed.document()->begin(); b.isValid(); b = b.next()) {
        if (!b.layout()) {
            continue;
        }
        const auto fr = b.layout()->formats();
        if (!fr.isEmpty()) {
            lines++;
        }
        ranges += fr.size();
    }
    if (lines_out) {
        *lines_out = lines;
    }
    return ranges;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    KSyntaxHighlighting::Repository repo;
    for (int i = 1; i < argc; i++) {
        repo.addCustomSearchPath(QString::fromUtf8(argv[i]));
    }

    auto def = repo.definitionForName(QStringLiteral("gBASIC"));
    printf("definition=%s\n", def.isValid() ? "found" : "absent");
    printf("file=%s\n",
           def.isValid() ? def.filePath().toUtf8().constData() : "-");

    int lines = 0;
    int with = def.isValid() ? ranges_with(repo, def, &lines) : -1;
    printf("with_lines=%d with_ranges=%d\n", lines, with);

    /* THE CONTROL. An invalid Definition is what a machine with nothing
     * installed hands the widget, and the whole point is that it does NOT
     * produce zero. */
    int ctrl_lines = 0;
    auto none = repo.definitionForName(QStringLiteral("NoSuchLanguageAnywhere"));
    int without = ranges_with(repo, none, &ctrl_lines);
    printf("without_valid=%d without_lines=%d without_ranges=%d\n",
           (int)none.isValid(), ctrl_lines, without);
    return 0;
}
