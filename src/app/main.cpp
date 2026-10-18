#include <QApplication>
#include <QFile>
#include <QImage>
#include <QTimer>

#include "DemoAssembly.h"
#include "MainWindow.h"
#include "nlhc/Version.h"
#include "view/OffscreenRenderer.h"
#include "view/RenderStyle.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("NebulousLinerHullCustomizer"));
    QApplication::setApplicationName(QStringLiteral("NLHC"));

    const QStringList args = app.arguments();
    const int demoIndex = args.indexOf(QStringLiteral("--render-demo"));
    if (demoIndex >= 0) {
        const QString output = demoIndex + 1 < args.size() ? args.at(demoIndex + 1) : QStringLiteral("demo.png");
        const nlhc::AssemblyResult assembly = nlhc::demo::buildDemoAssembly();
        nlhc::RenderStyle style;
        QString error;
        const QImage image = nlhc::OffscreenRenderer::renderSplit(nullptr, assembly, style, QSize(1280, 720), &error);
        if (image.isNull()) {
            QFile log(output + QStringLiteral(".log"));
            if (log.open(QIODevice::WriteOnly | QIODevice::Truncate))
                log.write(QStringLiteral("render failed: %1\n").arg(error).toUtf8());
            return 1;
        }
        if (!image.save(output)) {
            QFile log(output + QStringLiteral(".log"));
            if (log.open(QIODevice::WriteOnly | QIODevice::Truncate))
                log.write(QStringLiteral("could not write %1\n").arg(output).toUtf8());
            return 1;
        }
        return 0;
    }

    nlhc::MainWindow window;
    window.show();

    if (args.contains(QStringLiteral("--self-test"))) {
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    }
    return app.exec();
}
