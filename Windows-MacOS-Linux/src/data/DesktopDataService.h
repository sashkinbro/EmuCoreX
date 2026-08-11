#pragma once

#include <QObject>
#include <QVariantList>

class SettingsStore;

class DesktopDataService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString dataRoot READ dataRoot NOTIFY dataRootChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit DesktopDataService(SettingsStore* settings, QObject* parent = nullptr);

    QString dataRoot() const;
    QString lastError() const { return m_lastError; }

    Q_INVOKABLE QString categoryPath(const QString& category) const;
    Q_INVOKABLE QVariantList entries(const QString& category) const;
    Q_INVOKABLE bool importFile(const QString& category, const QString& sourceUrl);
    Q_INVOKABLE bool removeEntry(const QString& path);
    Q_INVOKABLE bool renameEntry(const QString& path, const QString& newName);
    Q_INVOKABLE bool duplicateEntry(const QString& path, const QString& newName);
    Q_INVOKABLE bool exportEntry(const QString& path, const QString& destinationDirectory);
    Q_INVOKABLE bool backupCategory(const QString& category, const QString& destinationDirectory);
    Q_INVOKABLE bool restoreCategory(const QString& category, const QString& sourceDirectory);
    Q_INVOKABLE bool createMemoryCard(const QString& name, int sizeMb, bool folderCard = false);
    Q_INVOKABLE bool reveal(const QString& path) const;
    Q_INVOKABLE bool ensureDirectories();

signals:
    void dataRootChanged();
    void contentsChanged(const QString& category);
    void lastErrorChanged();

private:
    QString normalizedCategory(const QString& category) const;
    QString localPath(const QString& value) const;
    bool copyRecursively(const QString& source, const QString& destination);
    bool setError(const QString& message);
    void clearError();

    SettingsStore* m_settings = nullptr;
    QString m_lastError;
};
