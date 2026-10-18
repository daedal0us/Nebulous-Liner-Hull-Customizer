#pragma once

#include <QString>

class QApplication;
class QTranslator;

namespace nlhc::i18n {

enum class Language { System, English, Chinese };

void apply(QApplication *app, Language language);
Language storedLanguage();
void storeLanguage(Language language);
QString languageCode(Language language);

} // namespace nlhc::i18n
