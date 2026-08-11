#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariant>

class TranslationManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentLanguage READ currentLanguage WRITE setCurrentLanguage NOTIFY currentLanguageChanged)
    Q_PROPERTY(QStringList availableLanguages READ availableLanguages CONSTANT)
    Q_PROPERTY(bool rightToLeft READ rightToLeft NOTIFY currentLanguageChanged)

public:
    explicit TranslationManager(QObject* parent = nullptr);

    QString currentLanguage() const { return m_currentLanguage; }
    QStringList availableLanguages() const;
    bool rightToLeft() const;

    Q_INVOKABLE QString get(const QString& key) const;
    Q_INVOKABLE QString format(const QString& key, const QVariantList& arguments) const;
    Q_INVOKABLE QString nativeName(const QString& code) const;
    Q_INVOKABLE void setCurrentLanguage(const QString& language);

signals:
    void currentLanguageChanged();
    void stringsChanged();

private:
    QHash<QString, QString> readLanguage(const QString& language) const;
    void loadLanguage(const QString& language);

    QHash<QString, QString> m_strings;
    QString m_currentLanguage = "en";
};
