// THE ORACLE for the terminal's highlighter: KDE's own KSyntaxHighlighting,
// asked what it does with a line, so our engine is held to it and not to itself.
//
//   ksh_probe extract DIR            copy every definition and theme it holds
//                                    (compiled into the library) to
//                                    DIR/syntax and DIR/themes; print
//                                    "name<TAB>file" per definition
//   ksh_probe lookup NAME...         what definitionForName answers
//   ksh_probe hl LANG THEME FILE...  each line's formats, a codepoint at a time
//                                    run-length coded: "L<n> <count>:<format>:<fg>:<bg>:<biusk> ..."
#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/State>
#include <KSyntaxHighlighting/Theme>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <cstdio>
#include <vector>

using namespace KSyntaxHighlighting;

struct Cell {
    QString text;
};

class Probe : public AbstractHighlighter {
public:
    std::vector<QString> cells;   // per UTF-16 unit
    State line(QStringView text, const State &st) { return highlightLine(text, st); }
    void applyFormat(int offset, int length, const Format &f) override {
        if (!f.isValid()) {
            return;
        }
        const Theme th = theme();
        auto hex = [](const QColor &c) { return QString::asprintf("%06x", c.rgb() & 0xffffff); };
        QString fg = hex(f.textColor(th));
        QString bg = f.hasBackgroundColor(th) ? hex(f.backgroundColor(th)) : QString("-");
        QString st;
        st += f.isBold(th) ? 'b' : '.';
        st += f.isItalic(th) ? 'i' : '.';
        st += f.isUnderline(th) ? 'u' : '.';
        st += f.isStrikeThrough(th) ? 's' : '.';
        QString c = f.name() + ":" + fg + ":" + bg + ":" + st;
        for (int i = offset; i < offset + length && i < (int)cells.size(); i++) {
            cells[i] = c;
        }
    }
};

static int extract(Repository &repo, const QString &dir) {
    QDir().mkpath(dir + "/syntax");
    QDir().mkpath(dir + "/themes");
    QTextStream out(stdout);
    for (const Definition &d : repo.definitions()) {
        if (d.filePath().isEmpty()) {
            out << "nofile\t" << d.name() << "\n";
            continue;
        }
        QFileInfo fi(d.filePath());
        QString to = dir + "/syntax/" + fi.fileName();
        QFile::remove(to);
        if (!QFile::copy(d.filePath(), to)) {
            fprintf(stderr, "cannot copy %s\n", qPrintable(d.filePath()));
            return 1;
        }
        QFile::setPermissions(to, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
        out << d.name() << "\t" << fi.fileName() << "\n";
    }
    for (const Theme &t : repo.themes()) {
        QFileInfo fi(t.filePath());
        QString to = dir + "/themes/" + fi.fileName();
        QFile::remove(to);
        QFile::copy(t.filePath(), to);
        QFile::setPermissions(to, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
        out << "theme\t" << t.name() << "\t" << fi.fileName() << "\n";
    }
    return 0;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        return 2;
    }
    Repository repo;
    if (qEnvironmentVariableIsSet("KSH_PATH")) {
        repo.addCustomSearchPath(qEnvironmentVariable("KSH_PATH"));
    }
    QString cmd = argv[1];
    if (cmd == "extract" && argc == 3) {
        return extract(repo, QString::fromUtf8(argv[2]));
    }
    if (cmd == "lookup") {
        for (int i = 2; i < argc; i++) {
            Definition d = repo.definitionForName(QString::fromUtf8(argv[i]));
            printf("%s\t%s\n", argv[i], d.isValid() ? qPrintable(d.name()) : "-");
        }
        return 0;
    }
    if (cmd == "hl" && argc >= 5) {
        Definition d = repo.definitionForName(QString::fromUtf8(argv[2]));
        if (!d.isValid()) {
            fprintf(stderr, "no definition %s\n", argv[2]);
            return 1;
        }
        Theme th = repo.theme(QString::fromUtf8(argv[3]));
        if (!th.isValid()) {
            fprintf(stderr, "no theme %s\n", argv[3]);
            return 1;
        }
        Probe p;
        p.setDefinition(d);
        p.setTheme(th);
        for (int a = 4; a < argc; a++) {
            QFile f(QString::fromUtf8(argv[a]));
            if (!f.open(QIODevice::ReadOnly)) {
                return 1;
            }
            QString all = QString::fromUtf8(f.readAll());
            QStringList lines = all.split('\n');
            if (!lines.isEmpty() && lines.last().isEmpty()) {
                lines.removeLast();
            }
            State st;
            printf("F %s\n", argv[a]);
            for (int n = 0; n < lines.size(); n++) {
                const QString &line = lines[n];
                p.cells.assign(line.size(), QString());
                st = p.line(line, st);
                // to codepoints, then run-length
                QString out = QString("L%1").arg(n + 1);
                QString prev;
                int count = 0;
                for (int i = 0; i < line.size(); i++) {
                    if (line[i].isLowSurrogate()) {
                        continue;
                    }
                    const QString &c = p.cells[i];
                    if (count && c == prev) {
                        count++;
                    } else {
                        if (count) {
                            out += QString(" %1|%2").arg(count).arg(prev);
                        }
                        prev = c;
                        count = 1;
                    }
                }
                if (count) {
                    out += QString(" %1|%2").arg(count).arg(prev);
                }
                printf("%s\n", out.toUtf8().constData());
            }
        }
        return 0;
    }
    return 2;
}
