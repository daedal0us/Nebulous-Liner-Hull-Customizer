#include <QApplication>
#include <QFile>
#include <QImage>
#include <QTimer>

#include "DemoAssembly.h"
#include "MainWindow.h"
#include "nlhc/Version.h"
#include "view/OffscreenRenderer.h"
#include "view/RenderStyle.h"

namespace {

QString argumentValue(const QStringList &arguments, const QString &name)
{
    const int index = arguments.indexOf(name);
    if (index < 0 || index + 1 >= arguments.size())
        return {};
    return arguments.at(index + 1);
}

int rememberDemo(const QString &output, const QString &error)
{
    QFile log(output + QStringLiteral(".log"));
    if (log.open(QIODevice::WriteOnly | QIODevice::Truncate))
        log.write(error.toUtf8());
    return 1;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("NebulousLinerHullCustomizer"));
    QApplication::setApplicationName(QStringLiteral("NLHC"));

    const QStringList args = app.arguments();

    if (args.contains(QStringLiteral("--render-demo"))) {
        const QString output = argumentValue(args, QStringLiteral("--render-demo"));
        const nlhc::AssemblyResult assembly = nlhc::demo::buildDemoAssembly();
        nlhc::RenderStyle style;
        QString error;
        const QImage image = nlhc::OffscreenRenderer::renderSplit(nullptr, assembly, style, QSize(1280, 720), &error);
        if (image.isNull())
            return rememberDemo(output, QStringLiteral("render failed: %1\n").arg(error));
        if (!image.save(output))
            return rememberDemo(output, QStringLiteral("could not write %1\n").arg(output));
        return 0;
    }

    nlhc::MainWindow window;
    window.show();

    const QString fleet = argumentValue(args, QStringLiteral("--open"));
    if (!fleet.isEmpty())
        window.openFleet(fleet);

    const QString shipIndex = argumentValue(args, QStringLiteral("--ship"));
    if (!shipIndex.isEmpty())
        window.selectShipByIndex(shipIndex.toInt());

    const QString debugView = argumentValue(args, QStringLiteral("--debug-view"));
    if (!debugView.isEmpty())
        window.setDebugViewName(debugView);

    const QString screenshot = argumentValue(args, QStringLiteral("--screenshot"));
    if (!screenshot.isEmpty()) {
        QTimer::singleShot(600, &app, [&window, screenshot] {
            const bool saved = window.grab().save(screenshot);
            QFile log(screenshot + QStringLiteral(".log"));
            if (log.open(QIODevice::WriteOnly | QIODevice::Truncate))
                log.write(QStringLiteral("%1 %2\n%3\n%4")
                              .arg(saved ? QStringLiteral("ok") : QStringLiteral("grab failed"),
                                   window.debugLayoutSummary(),
                                   window.debugAssemblySummary(),
                                   window.debugRenderLegend())
                              .toUtf8());
            QCoreApplication::quit();
        });
    } else if (args.contains(QStringLiteral("--self-test"))) {
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    }

    return app.exec();
}
