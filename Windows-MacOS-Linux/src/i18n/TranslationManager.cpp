#include "TranslationManager.h"

#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QVariant>
#include <QXmlStreamReader>

TranslationManager::TranslationManager(QObject* parent)
    : QObject(parent)
{
    loadLanguage("en");
}

QStringList TranslationManager::availableLanguages() const
{
    return {"en", "uk", "ru", "es", "fr", "de", "pt", "it", "in", "hi", "zh", "ar", "fa", "ja", "ko", "pl", "cs", "tr"};
}

bool TranslationManager::rightToLeft() const
{
    return m_currentLanguage == "ar" || m_currentLanguage == "fa";
}

QString TranslationManager::get(const QString& key) const
{
    return m_strings.value(key, key);
}

QString TranslationManager::format(const QString& key, const QVariantList& arguments) const
{
    QString result = get(key);
    for (const QVariant& argument : arguments)
        result = result.arg(argument.toString());
    return result;
}

QString TranslationManager::nativeName(const QString& code) const
{
    static const QMap<QString, QString> names = {
        {"en", "English"}, {"uk", "Українська"}, {"ru", "Русский"}, {"es", "Español"},
        {"fr", "Français"}, {"de", "Deutsch"}, {"pt", "Português"}, {"it", "Italiano"},
        {"in", "Bahasa Indonesia"}, {"hi", "हिन्दी"}, {"zh", "中文"}, {"ar", "العربية"},
        {"fa", "فارسی"}, {"ja", "日本語"}, {"ko", "한국어"}, {"pl", "Polski"},
        {"cs", "Čeština"}, {"tr", "Türkçe"}
    };
    return names.value(code, code);
}

void TranslationManager::setCurrentLanguage(const QString& language)
{
    const QString supported = availableLanguages().contains(language) ? language : QStringLiteral("en");
    if (m_currentLanguage == supported && !m_strings.isEmpty())
        return;
    loadLanguage(supported);
    emit currentLanguageChanged();
    emit stringsChanged();
}

QHash<QString, QString> TranslationManager::readLanguage(const QString& language) const
{
    QHash<QString, QString> strings;
    QFile file(QString(":/translations/strings_%1.xml").arg(language));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return strings;

    QXmlStreamReader xml(&file);
    while (xml.readNextStartElement()) {
        if (xml.name() != QLatin1String("resources")) {
            xml.skipCurrentElement();
            continue;
        }
        while (xml.readNextStartElement()) {
            if (xml.name() != QLatin1String("string")) {
                xml.skipCurrentElement();
                continue;
            }
            const QString name = xml.attributes().value("name").toString();
            QString value = xml.readElementText(QXmlStreamReader::IncludeChildElements);
            value.replace("\\n", "\n").replace("\\'", "'").replace("\\\"", "\"");
            value.replace(QRegularExpression(QStringLiteral("%(\\d+)\\$[sdif]")), QStringLiteral("%\\1"));

            int sequentialArgument = 1;
            const QRegularExpression unnumberedPlaceholder(QStringLiteral("%[sdif]"));
            QRegularExpressionMatch match;
            qsizetype offset = 0;
            while ((offset = value.indexOf(unnumberedPlaceholder, offset, &match)) >= 0) {
                const QString replacement = QStringLiteral("%") + QString::number(sequentialArgument++);
                value.replace(offset, match.capturedLength(), replacement);
                offset += replacement.size();
            }
            value.replace(QStringLiteral("%%"), QStringLiteral("%"));
            strings.insert(name, value);
        }
    }
    return strings;
}

void TranslationManager::loadLanguage(const QString& language)
{
    m_strings = readLanguage("en");
    if (language != "en") {
        const auto localized = readLanguage(language);
        for (auto it = localized.constBegin(); it != localized.constEnd(); ++it)
            m_strings.insert(it.key(), it.value());
    }
    m_currentLanguage = language;
}
