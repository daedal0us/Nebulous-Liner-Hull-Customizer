#include <QtTest>

#include <QImage>
#include <QVector2D>
#include <QVector3D>

#include <algorithm>

#include "DemoAssembly.h"
#include "nlhc/hull/HullAssembler.h"
#include "view/OffscreenRenderer.h"
#include "view/RenderStyle.h"

namespace {

double foregroundRatio(const QImage &image, const QVector<QColor> &backgrounds)
{
    if (image.isNull())
        return 0.0;
    const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
    qint64 hits = 0;
    for (int y = 0; y < rgb.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
        for (int x = 0; x < rgb.width(); ++x) {
            const QColor pixel(line[x]);
            const bool isBackground = std::any_of(backgrounds.begin(), backgrounds.end(),
                                                  [&pixel](const QColor &background) {
                                                      return qAbs(pixel.red() - background.red()) <= 8
                                                          && qAbs(pixel.green() - background.green()) <= 8
                                                          && qAbs(pixel.blue() - background.blue()) <= 8;
                                                  });
            if (!isBackground)
                ++hits;
        }
    }
    return double(hits) / double(rgb.width() * rgb.height());
}

// Share of the lit pixels that lean warm (trim) and cool (base). The livery is a camouflage
// field, so a correct render carries both palette colours instead of one dominant one.
QPair<double, double> warmCoolShare(const QImage &image)
{
    const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
    qint64 warm = 0;
    qint64 cool = 0;
    qint64 lit = 0;
    for (int y = 0; y < rgb.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
        for (int x = 0; x < rgb.width(); ++x) {
            const QColor pixel(line[x]);
            if (pixel.lightness() < 40)
                continue;
            ++lit;
            if (pixel.red() > pixel.blue() + 20)
                ++warm;
            else if (pixel.blue() > pixel.red() + 20)
                ++cool;
        }
    }
    if (lit == 0)
        return {0.0, 0.0};
    return {double(warm) / double(lit), double(cool) / double(lit)};
}

QColor averageColor(const QImage &image)
{
    const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
    qint64 r = 0, g = 0, b = 0, count = 0;
    for (int y = 0; y < rgb.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
        for (int x = 0; x < rgb.width(); ++x) {
            const QColor pixel(line[x]);
            if (pixel.lightness() < 40)
                continue;
            r += pixel.red();
            g += pixel.green();
            b += pixel.blue();
            ++count;
        }
    }
    if (count == 0)
        return {};
    return QColor(int(r / count), int(g / count), int(b / count));
}

} // namespace

class TstRenderSmoke : public QObject
{
    Q_OBJECT

private slots:
    void rendersSplitViewport()
    {
        const nlhc::AssemblyResult assembly = nlhc::demo::buildDemoAssembly();
        nlhc::RenderStyle style;
        QString error;
        const QImage image = nlhc::OffscreenRenderer::renderSplit(nullptr, assembly, style, QSize(640, 360), &error);
        QVERIFY2(!image.isNull(), qPrintable(error));
        QCOMPARE(image.size(), QSize(640, 360));

        const double ratio = foregroundRatio(image, {QColor(28, 31, 36), QColor(23, 26, 31)});
        QVERIFY2(ratio > 0.05, qPrintable(QStringLiteral("foreground ratio %1").arg(ratio)));
        QVERIFY2(ratio < 0.95, qPrintable(QStringLiteral("foreground ratio %1").arg(ratio)));

        const QColor average = averageColor(image);
        QVERIFY(average.isValid());
        const QPair<double, double> share = warmCoolShare(image);
        QVERIFY2(share.first > 0.10, qPrintable(QStringLiteral("warm share %1").arg(share.first)));
        QVERIFY2(share.second > 0.10, qPrintable(QStringLiteral("cool share %1").arg(share.second)));
    }

    void baseColorDrivesPixels()
    {
        const nlhc::AssemblyResult assembly = nlhc::demo::buildDemoAssembly();
        nlhc::RenderStyle red;
        red.baseColor = QColor(0xd0, 0x30, 0x30);
        red.trimColor = QColor(0xd0, 0x30, 0x30);
        nlhc::RenderStyle blue;
        blue.baseColor = QColor(0x20, 0x40, 0xd0);
        blue.trimColor = QColor(0x20, 0x40, 0xd0);

        QString error;
        const QImage redImage = nlhc::OffscreenRenderer::renderSplit(nullptr, assembly, red, QSize(320, 240), &error);
        QVERIFY2(!redImage.isNull(), qPrintable(error));
        const QImage blueImage = nlhc::OffscreenRenderer::renderSplit(nullptr, assembly, blue, QSize(320, 240), &error);
        QVERIFY2(!blueImage.isNull(), qPrintable(error));

        const QColor redAverage = averageColor(redImage);
        const QColor blueAverage = averageColor(blueImage);
        QVERIFY(redAverage.red() > blueAverage.red() + 20);
        QVERIFY(blueAverage.blue() > redAverage.blue() + 20);
    }

    // Unpainted props (copper doodads, deck plating) must ignore the livery colours entirely.
    void fixedMaterialIgnoresTrim()
    {
        const nlhc::AssemblyResult assembly = nlhc::demo::buildFixedOnlyAssembly();
        nlhc::RenderStyle green;
        green.trimColor = QColor(0x00, 0xff, 0x00);
        nlhc::RenderStyle magenta;
        magenta.trimColor = QColor(0xff, 0x00, 0xff);

        QString error;
        const QImage first = nlhc::OffscreenRenderer::renderSplit(nullptr, assembly, green, QSize(320, 240), &error);
        QVERIFY2(!first.isNull(), qPrintable(error));
        const QImage second = nlhc::OffscreenRenderer::renderSplit(nullptr, assembly, magenta, QSize(320, 240), &error);
        QVERIFY2(!second.isNull(), qPrintable(error));
        QCOMPARE(first, second);
    }

    void renderIsDeterministic()
    {
        const nlhc::AssemblyResult assembly = nlhc::demo::buildDemoAssembly();
        nlhc::RenderStyle style;
        QString error;
        const QImage first = nlhc::OffscreenRenderer::renderSplit(nullptr, assembly, style, QSize(320, 240), &error);
        QVERIFY2(!first.isNull(), qPrintable(error));
        const QImage second = nlhc::OffscreenRenderer::renderSplit(nullptr, assembly, style, QSize(320, 240), &error);
        QVERIFY2(!second.isNull(), qPrintable(error));
        QCOMPARE(first, second);
    }
};

QTEST_MAIN(TstRenderSmoke)

#include "tst_render_smoke.moc"
