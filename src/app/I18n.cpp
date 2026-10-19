#include "I18n.h"

#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace nlhc::i18n {
namespace {

QTranslator *g_translator = nullptr;
QTranslator *g_qtTranslator = nullptr;

QString resolveCode(Language language)
{
    switch (language) {
    case Language::Chinese:
        return QStringLiteral("zh_CN");
    case Language::English:
        return QStringLiteral("en");
    case Language::System:
        break;
    }
    const QString name = QLocale::system().name();
    return name.startsWith(QStringLiteral("zh")) ? QStringLiteral("zh_CN") : QStringLiteral("en");
}

} // namespace

QString languageCode(Language language)
{
    return resolveCode(language);
}

void apply(QApplication *app, Language language)
{
    if (!app)
        return;

    QCoreApplication::removeTranslator(g_translator);
    QCoreApplication::removeTranslator(g_qtTranslator);
    delete g_translator;
    delete g_qtTranslator;
    g_translator = nullptr;
    g_qtTranslator = nullptr;

    const QString code = resolveCode(language);
    if (code.startsWith(QStringLiteral("zh"))) {
        auto *translator = new QTranslator(app);
        if (translator->load(QStringLiteral(":/i18n/nlhc_%1.qm").arg(code))) {
            app->installTranslator(translator);
            g_translator = translator;
        } else {
            delete translator;
        }

        // windeployqt ships one merged qt_<code>.qm catalog; a plain Qt install keeps
        // the per-module files apart, so both names are worth a try.
        const QString translationsDir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
        auto *qtTranslator = new QTranslator(app);
        const bool loaded = qtTranslator->load(QStringLiteral("qt_%1").arg(code), translationsDir)
            || qtTranslator->load(QStringLiteral("qtbase_%1").arg(code), translationsDir);
        if (loaded) {
            app->installTranslator(qtTranslator);
            g_qtTranslator = qtTranslator;
        } else {
            delete qtTranslator;
        }
    }
}

Language storedLanguage()
{
    QSettings settings;
    const int value = settings.value(QStringLiteral("ui/language"), int(Language::System)).toInt();
    switch (value) {
    case int(Language::English):
        return Language::English;
    case int(Language::Chinese):
        return Language::Chinese;
    default:
        return Language::System;
    }
}

void storeLanguage(Language language)
{
    QSettings settings;
    settings.setValue(QStringLiteral("ui/language"), int(language));
}

} // namespace nlhc::i18n
