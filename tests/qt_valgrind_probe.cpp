// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
//
// THE EVIDENCE BEHIND tests/qt.supp, as tests/odbc_driver_probe.c is behind
// tests/odbc.supp: a suppression is reproduced with NO gBASIC in the stack
// before it is written, or it is indistinguishable from hiding our own bug.
//
// A window built the way gBASIC builds one -- a form of labels and fields, a
// table given a model, a row of buttons -- shown offscreen and closed. On the
// s390x VM (Debian 13, Qt 6.8.2) valgrind reports "Conditional jump or move
// depends on uninitialised value(s)" inside Qt's own text shaping and layout
// sizing here, the same five functions it reported under every run_qt_* suite
// (2026-11-14); on x86 it reports nothing. Three more paths came from
// run_ui_backend and run_ui_notebook and are made here too: a line edit given
// its text through setProperty (QWidgetLineControl::finishChange), an image
// scaled smoothly (QTransform::operator*, via QImage::trueMatrix) and a tool
// button's menu arrow painted (an unnamed function under
// QRasterPaintEngine::fillPolygon). --track-origins put every one of the
// uninitialised values in a Qt allocation: a local of internalSetText, a local
// of trueMatrix, a heap block under Qt's own painting. And run_ui_backend's
// menu, tabs and cells fixtures added the sizing of a menu bar, a tab bar and
// a grid layout, their values allocated by QMenuBar's and QGridLayout's own
// constructors; a main window with a menu, a tab widget and a grid are here.
// The gate of 2026-11-15 added four more, each made here as gBASIC makes it:
// a label's style sheet set through setProperty (QWidget::setStyleSheet), a
// combo box filled and its popup shown and hidden (QComboBox::hidePopup, and
// the rows inserted), a key typed into a line edit (internalInsert, its value
// a local of QLineEdit::deselect) and Qt's own file dialog, non-native as
// ui.choose_file forces it (QFileDialogPrivate::restoreFromSettings); every
// origin was in QLabel's, QComboBox's or QFileDialog's own construction.
// Whether it is Qt's s390x build or valgrind's s390x support was not separated.
//
// AND ONE ON EVERY ARCHITECTURE, tests/qt_hash.supp's: the application's style
// sheet with `#id` selectors, set last below. On x86 (Qt 6.10.2), 2026-11-16,
// valgrind reported "Invalid read of size 16" under QCss::StyleSheet::
// buildIndexes, 18 bytes into a 28-byte QString Qt's own CSS lexer allocated,
// in 13 of 20 runs of this step alone and about 1 in 10 of run_ui_capture's
// access tier; with QT_HASH_SEED fixed (0 or 1) in none of 20. So it is Qt's
// seeded string hash reading a selector's name 16 bytes at a time.
//
//   g++ -o probe tests/qt_valgrind_probe.cpp $(pkg-config --cflags --libs Qt6Widgets)
//   QT_QPA_PLATFORM=offscreen QT_ENABLE_REGEXP_JIT=0 valgrind --suppressions=tests/qt.supp ./probe
//
// The JIT is off as in every Qt valgrind tier: the file dialog's filters are
// regular expressions, and PCRE2's JIT reads past its subject by design.
#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QKeyEvent>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QTabWidget>
#include <QHBoxLayout>
#include <QImage>
#include <QMenu>
#include <QToolButton>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QWidget win;
    auto *outer = new QVBoxLayout(&win);
    auto *form = new QFormLayout;
    form->addRow("Account", new QLineEdit("A-1041"));
    form->addRow("Name", new QLineEdit("Ada Lovelace"));
    outer->addLayout(form);
    auto *model = new QStandardItemModel(3, 2, &win);
    model->setHorizontalHeaderLabels({"Item", "Price"});
    auto *table = new QTableView;
    table->setModel(model);
    outer->addWidget(table);
    auto *row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(new QPushButton("Save"));
    row->addWidget(new QPushButton("Cancel"));
    outer->addLayout(row);
    win.show();
    // and a bare table, alone, which is where QLayout::totalMaximumSize came from
    QStandardItemModel bare(3, 3);
    bare.setHorizontalHeaderLabels({"Name", "Qty", "Price"});
    QTableView alone;
    alone.setModel(&bare);
    alone.show();
    // a line edit given its text the way gBASIC's binding gives it
    QLineEdit bound;
    bound.setProperty("text", QVariant(QString("ada@example.com")));
    // an image scaled the way the notebook measures a block's figure
    QImage img(40, 30, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QImage small = img.scaled(20, 15, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    (void)small;
    // a tool button with a menu, painted, so its arrow is drawn
    QToolButton tool;
    QMenu menu;
    menu.addAction("one");
    tool.setMenu(&menu);
    tool.setPopupMode(QToolButton::MenuButtonPopup);
    tool.setText("Run");
    tool.show();
    (void)tool.grab();
    // a main window with a menu bar, a tab widget and a grid of cells
    QMainWindow main;
    main.menuBar()->addMenu("File")->addAction("Open");
    auto *tabs = new QTabWidget;
    auto *page = new QWidget;
    auto *grid = new QGridLayout(page);
    grid->addWidget(new QLabel("a"), 0, 0);
    grid->addWidget(new QLabel("b"), 0, 1);
    grid->addWidget(new QLabel("c"), 1, 0);
    tabs->addTab(page, "First");
    // and a grid inside a box layout, as gBASIC's `cells` builds it
    auto *second = new QWidget;
    auto *box = new QVBoxLayout(second);
    auto *cells = new QWidget;
    auto *cgrid = new QGridLayout(cells);
    cgrid->addWidget(new QLineEdit("x"), 0, 0);
    cgrid->addWidget(new QLineEdit("y"), 0, 1);
    box->addWidget(cells);
    tabs->addTab(second, "Second");
    main.setCentralWidget(tabs);
    main.show();
    // a label whose style sheet is set the way ui.set sets a property
    QLabel styled("Total");
    styled.setProperty("styleSheet", QVariant(QString("color: #336; font-weight: bold")));
    styled.show();
    // a combo box filled as a `choice` is, and its popup shown and hidden
    QComboBox choice;
    choice.addItem("Net 30");
    choice.addItem("Net 60");
    choice.show();
    choice.showPopup();
    choice.hidePopup();
    // a key typed into a line edit, the event ui.key sends
    QLineEdit typed;
    typed.show();
    QKeyEvent press(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, "a");
    QApplication::sendEvent(&typed, &press);
    // Qt's own file dialog, which ui.choose_file forces over the native one
    QFileDialog chooser(nullptr, "Open", "/tmp");
    chooser.setOption(QFileDialog::DontUseNativeDialog);
    chooser.show();
    // the application's style sheet with ID selectors, as ui.theme(text) sets
    // it and run_ui_capture writes it (tests/qt_hash.supp)
    styled.setObjectName("heading");
    choice.setObjectName("note");
    app.setStyleSheet("#heading { background: #ff0000; } #note { background: #0000ff; }");
    QTimer::singleShot(200, &app, &QApplication::quit);
    return app.exec();
}
