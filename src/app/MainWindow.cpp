#include "MainWindow.h"

#include <QLabel>
#include <QMenuBar>
#include <QStatusBar>

#include "nlhc/Version.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Nebulous Liner Hull Customizer"));
    resize(1280, 800);
    buildMenus();

    auto *placeholder = new QLabel(tr("Asset package not loaded yet."), this);
    placeholder->setAlignment(Qt::AlignCenter);
    setCentralWidget(placeholder);
    statusBar()->showMessage(tr("NLHC %1").arg(nlhc::versionString()));
}

void MainWindow::buildMenus()
{
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    auto *quit = fileMenu->addAction(tr("E&xit"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About NLHC"));
}
