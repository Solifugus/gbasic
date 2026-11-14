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
// run_qt_backend and run_qt_notebook and are made here too: a line edit given
// its text through setProperty (QWidgetLineControl::finishChange), an image
// scaled smoothly (QTransform::operator*, via QImage::trueMatrix) and a tool
// button's menu arrow painted (an unnamed function under
// QRasterPaintEngine::fillPolygon). --track-origins put every one of the
// uninitialised values in a Qt allocation: a local of internalSetText, a local
// of trueMatrix, a heap block under Qt's own painting. Whether it is Qt's s390x build or
// valgrind's s390x support was not separated.
//
//   g++ -o probe tests/qt_valgrind_probe.cpp $(pkg-config --cflags --libs Qt6Widgets)
//   QT_QPA_PLATFORM=offscreen valgrind --suppressions=tests/qt.supp ./probe
#include <QApplication>
#include <QFormLayout>
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
    QTimer::singleShot(200, &app, &QApplication::quit);
    return app.exec();
}
