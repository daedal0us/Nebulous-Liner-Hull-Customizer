#include <QApplication>
#include <QTimer>

#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("NebulousLinerHullCustomizer"));
    QApplication::setApplicationName(QStringLiteral("NLHC"));

    MainWindow window;
    window.show();

    if (app.arguments().contains(QStringLiteral("--self-test"))) {
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    }
    return app.exec();
}
