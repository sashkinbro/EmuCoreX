#include "DesktopDataService.h"

#include "../settings/SettingsStore.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>

namespace {
constexpr qint64 kRawMemoryCardBytesPerMb = 1081344;
}

DesktopDataService::DesktopDataService(SettingsStore* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
    ensureDirectories();
    connect(settings, &SettingsStore::emulatorDataPathChanged, this, [this] {
        ensureDirectories();
        emit dataRootChanged();
        emit contentsChanged(QString());
    });
}

QString DesktopDataService::dataRoot() const
{
    return QDir::cleanPath(m_settings->emulatorDataPath());
}

QString DesktopDataService::normalizedCategory(const QString& category) const
{
    const QString normalized = category.trimmed().toLower();
    static const QStringList allowed {"sstates", "memcards", "textures", "cheats"};
    return allowed.contains(normalized) ? normalized : QString();
}

QString DesktopDataService::categoryPath(const QString& category) const
{
    const QString normalized = normalizedCategory(category);
    return normalized.isEmpty() ? QString() : QDir(dataRoot()).filePath(normalized);
}

QString DesktopDataService::localPath(const QString& value) const
{
    const QUrl url(value);
    if (url.isLocalFile())
        return QDir::toNativeSeparators(url.toLocalFile());
    return QDir::toNativeSeparators(value);
}

QVariantList DesktopDataService::entries(const QString& category) const
{
    QVariantList result;
    const QString directoryPath = categoryPath(category);
    if (directoryPath.isEmpty())
        return result;

    QDir directory(directoryPath);
    const QFileInfoList files = directory.entryInfoList(
        QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Readable,
        QDir::Time | QDir::IgnoreCase);
    for (const QFileInfo& info : files) {
        QVariantMap entry;
        entry.insert("name", info.fileName());
        entry.insert("baseName", info.completeBaseName());
        entry.insert("path", QDir::toNativeSeparators(info.absoluteFilePath()));
        entry.insert("url", QUrl::fromLocalFile(info.absoluteFilePath()).toString());
        entry.insert("directory", info.isDir());
        entry.insert("size", info.isDir() ? 0 : info.size());
        entry.insert("modified", info.lastModified().toString(Qt::ISODate));
        entry.insert("suffix", info.suffix().toLower());
        result.append(entry);
    }
    return result;
}

bool DesktopDataService::ensureDirectories()
{
    QDir root(dataRoot());
    if (!root.mkpath("."))
        return setError(QStringLiteral("Could not create the emulator data directory."));
    for (const QString& category : {QStringLiteral("sstates"), QStringLiteral("memcards"),
             QStringLiteral("textures"), QStringLiteral("cheats")}) {
        if (!root.mkpath(category))
            return setError(QStringLiteral("Could not create %1.").arg(category));
    }
    clearError();
    return true;
}

bool DesktopDataService::copyRecursively(const QString& source, const QString& destination)
{
    const QFileInfo sourceInfo(source);
    if (sourceInfo.isFile()) {
        if (QFile::exists(destination) && !QFile::remove(destination))
            return false;
        return QFile::copy(source, destination);
    }
    if (!sourceInfo.isDir())
        return false;
    QDir().mkpath(destination);
    const QDir sourceDir(source);
    for (const QFileInfo& child : sourceDir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot)) {
        if (!copyRecursively(child.absoluteFilePath(), QDir(destination).filePath(child.fileName())))
            return false;
    }
    return true;
}

bool DesktopDataService::importFile(const QString& category, const QString& sourceUrl)
{
    const QString destinationDirectory = categoryPath(category);
    const QString source = localPath(sourceUrl);
    const QFileInfo sourceInfo(source);
    if (destinationDirectory.isEmpty() || !sourceInfo.exists())
        return setError(QStringLiteral("The selected file does not exist."));
    QDir().mkpath(destinationDirectory);
    const QString destination = QDir(destinationDirectory).filePath(sourceInfo.fileName());
    if (!copyRecursively(sourceInfo.absoluteFilePath(), destination))
        return setError(QStringLiteral("The selected item could not be imported."));
    clearError();
    emit contentsChanged(normalizedCategory(category));
    return true;
}

bool DesktopDataService::removeEntry(const QString& path)
{
    const QFileInfo info(localPath(path));
    const QString canonicalRoot = QFileInfo(dataRoot()).canonicalFilePath();
    const QString canonicalTarget = info.canonicalFilePath();
    if (!info.exists() || canonicalRoot.isEmpty() || !canonicalTarget.startsWith(canonicalRoot + QDir::separator()))
        return setError(QStringLiteral("The selected item is outside the emulator data directory."));
    const bool removed = info.isDir() ? QDir(info.absoluteFilePath()).removeRecursively() : QFile::remove(info.absoluteFilePath());
    if (!removed)
        return setError(QStringLiteral("The selected item could not be removed."));
    clearError();
    emit contentsChanged(info.dir().dirName());
    return true;
}

bool DesktopDataService::renameEntry(const QString& path, const QString& newName)
{
    const QFileInfo info(localPath(path));
    const QString safeName = QFileInfo(newName.trimmed()).fileName();
    if (!info.exists() || safeName.isEmpty() || safeName == "." || safeName == "..")
        return setError(QStringLiteral("Choose a valid name."));
    const QString target = info.dir().filePath(safeName);
    if (QFileInfo::exists(target) || !QDir().rename(info.absoluteFilePath(), target))
        return setError(QStringLiteral("The selected item could not be renamed."));
    clearError();
    emit contentsChanged(info.dir().dirName());
    return true;
}

bool DesktopDataService::duplicateEntry(const QString& path, const QString& newName)
{
    const QFileInfo info(localPath(path));
    const QString safeName = QFileInfo(newName.trimmed()).fileName();
    if (!info.exists() || safeName.isEmpty())
        return setError(QStringLiteral("Choose a valid name."));
    const QString target = info.dir().filePath(safeName);
    if (QFileInfo::exists(target) || !copyRecursively(info.absoluteFilePath(), target))
        return setError(QStringLiteral("The selected item could not be duplicated."));
    clearError();
    emit contentsChanged(info.dir().dirName());
    return true;
}

bool DesktopDataService::exportEntry(const QString& path, const QString& destinationDirectory)
{
    const QFileInfo info(localPath(path));
    const QString destinationRoot = localPath(destinationDirectory);
    if (!info.exists() || destinationRoot.isEmpty())
        return setError(QStringLiteral("Choose a valid export location."));
    QDir().mkpath(destinationRoot);
    const QString target = QDir(destinationRoot).filePath(info.fileName());
    if (!copyRecursively(info.absoluteFilePath(), target))
        return setError(QStringLiteral("The selected item could not be exported."));
    clearError();
    return true;
}

bool DesktopDataService::backupCategory(const QString& category, const QString& destinationDirectory)
{
    const QString source = categoryPath(category);
    const QString destinationRoot = localPath(destinationDirectory);
    if (source.isEmpty() || destinationRoot.isEmpty())
        return setError(QStringLiteral("Choose a valid backup location."));
    const QString folderName = QStringLiteral("EmuCoreX-%1-%2")
        .arg(normalizedCategory(category), QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const QString target = QDir(destinationRoot).filePath(folderName);
    if (!copyRecursively(source, target))
        return setError(QStringLiteral("The backup could not be created."));
    clearError();
    return true;
}

bool DesktopDataService::restoreCategory(const QString& category, const QString& sourceDirectory)
{
    const QString destination = categoryPath(category);
    const QString source = localPath(sourceDirectory);
    if (destination.isEmpty() || !QFileInfo(source).isDir())
        return setError(QStringLiteral("Choose a valid backup folder."));
    const QDir sourceDir(source);
    for (const QFileInfo& child : sourceDir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot)) {
        if (!copyRecursively(child.absoluteFilePath(), QDir(destination).filePath(child.fileName())))
            return setError(QStringLiteral("The backup could not be restored."));
    }
    clearError();
    emit contentsChanged(normalizedCategory(category));
    return true;
}

bool DesktopDataService::createMemoryCard(const QString& name, int sizeMb, bool folderCard)
{
    QString safeName = QFileInfo(name.trimmed()).fileName();
    if (safeName.isEmpty())
        return setError(QStringLiteral("Choose a valid memory card name."));
    if (!safeName.endsWith(".ps2", Qt::CaseInsensitive))
        safeName += ".ps2";
    const QString target = QDir(categoryPath("memcards")).filePath(safeName);
    if (QFileInfo::exists(target))
        return setError(QStringLiteral("A memory card with this name already exists."));

    if (folderCard) {
        if (!QDir().mkpath(target))
            return setError(QStringLiteral("The folder memory card could not be created."));
    } else {
        const int boundedSize = qBound(8, sizeMb, 64);
        QSaveFile file(target);
        if (!file.open(QIODevice::WriteOnly))
            return setError(QStringLiteral("The memory card file could not be created."));
        const QByteArray emptyBlock(1024 * 1024, static_cast<char>(0xFF));
        qint64 remaining = boundedSize * kRawMemoryCardBytesPerMb;
        while (remaining > 0) {
            const qint64 count = qMin<qint64>(remaining, emptyBlock.size());
            if (file.write(emptyBlock.constData(), count) != count)
                return setError(QStringLiteral("The memory card file could not be written."));
            remaining -= count;
        }
        if (!file.commit())
            return setError(QStringLiteral("The memory card file could not be saved."));
    }
    clearError();
    emit contentsChanged(QStringLiteral("memcards"));
    return true;
}

bool DesktopDataService::reveal(const QString& path) const
{
    QFileInfo info(localPath(path));
    const QString target = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    return QDesktopServices::openUrl(QUrl::fromLocalFile(target));
}

bool DesktopDataService::setError(const QString& message)
{
    if (m_lastError != message) {
        m_lastError = message;
        emit lastErrorChanged();
    }
    return false;
}

void DesktopDataService::clearError()
{
    if (m_lastError.isEmpty())
        return;
    m_lastError.clear();
    emit lastErrorChanged();
}
