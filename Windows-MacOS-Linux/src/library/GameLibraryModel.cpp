#include "GameLibraryModel.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>

GameLibraryModel::GameLibraryModel(const CoreRuntime* core, QObject* parent)
    : QAbstractListModel(parent)
    , m_settings(QSettings::IniFormat, QSettings::UserScope,
          QCoreApplication::organizationName(), QCoreApplication::applicationName())
    , m_folders(m_settings.value("library/folders").toStringList())
    , m_metadataProvider(core)
{
    for (QString& folder : m_folders) {
        const QString canonical = QFileInfo(folder).canonicalFilePath();
        folder = canonical.isEmpty() ? QDir::cleanPath(folder) : canonical;
    }
    m_folders.removeDuplicates();

    const QStringList favorites = m_settings.value("library/favorites").toStringList();
    for (const QString& favorite : favorites) {
        const QString canonical = QFileInfo(favorite).canonicalFilePath();
        m_favoritePaths.insert(canonical.isEmpty() ? QDir::cleanPath(favorite) : canonical);
    }
    refresh();
}

int GameLibraryModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_visibleGames.size();
}

int GameLibraryModel::favoriteCount() const
{
    return static_cast<int>(std::count_if(m_allGames.cbegin(), m_allGames.cend(),
        [](const DesktopGame& game) { return game.favorite; }));
}

QVariant GameLibraryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visibleGames.size())
        return {};
    const DesktopGame& game = m_visibleGames.at(index.row());
    switch (role) {
    case TitleRole: return game.title;
    case PathRole: return game.path;
    case SerialRole: return game.serial;
    case RegionRole: return game.region;
    case SizeRole: return game.size;
    case ModifiedRole: return game.modified;
    case FavoriteRole: return game.favorite;
    default: return {};
    }
}

QHash<int, QByteArray> GameLibraryModel::roleNames() const
{
    return {{TitleRole, "title"}, {PathRole, "path"}, {SerialRole, "serial"},
        {RegionRole, "region"}, {SizeRole, "fileSize"}, {ModifiedRole, "modified"},
        {FavoriteRole, "favorite"}};
}

bool GameLibraryModel::isSupportedImage(const QString& path)
{
    static const QSet<QString> suffixes = {
        "iso", "bin", "img", "mdf", "nrg", "chd", "cso", "gz", "zso", "elf", "gs", "dump"
    };
    return suffixes.contains(QFileInfo(path).suffix().toLower());
}

QString GameLibraryModel::normalizedSerial(const QString& serial)
{
    QString normalized = serial.toUpper();
    normalized.remove(QRegularExpression(QStringLiteral("[^A-Z0-9]")));
    return normalized;
}

void GameLibraryModel::addFolder(const QUrl& folderUrl)
{
    QString path = folderUrl.isLocalFile() ? folderUrl.toLocalFile() : folderUrl.toString();
    path = QDir::cleanPath(path);
    const QString canonical = QFileInfo(path).canonicalFilePath();
    if (!canonical.isEmpty())
        path = canonical;
    if (path.isEmpty() || !QDir(path).exists() || m_folders.contains(path))
        return;
    m_folders.append(path);
    saveFolders();
    emit foldersChanged();
    refresh();
}

void GameLibraryModel::removeFolder(int index)
{
    if (index < 0 || index >= m_folders.size())
        return;
    m_folders.removeAt(index);
    saveFolders();
    emit foldersChanged();
    refresh();
}

void GameLibraryModel::refresh()
{
    if (m_scanning)
        return;
    m_scanning = true;
    emit scanningChanged();

    QList<DesktopGame> games;
    QSet<QString> paths;
    for (const QString& folder : std::as_const(m_folders)) {
        QDirIterator iterator(folder, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString path = iterator.next();
            if (!isSupportedImage(path))
                continue;
            const QFileInfo info(path);
            const QString canonical = info.canonicalFilePath();
            if (canonical.isEmpty() || paths.contains(canonical))
                continue;
            paths.insert(canonical);

            const GameImageMetadata metadata = m_metadataProvider.inspect(canonical);
            DesktopGame game;
            game.title = metadata.title;
            game.path = canonical;
            game.serial = metadata.authoritative ? metadata.serial : QString();
            game.region = metadata.authoritative ? metadata.region : QString();
            game.size = metadata.totalSize > 0 ? metadata.totalSize : info.size();
            game.modified = info.lastModified();
            game.favorite = m_favoritePaths.contains(canonical);
            games.append(std::move(game));
        }
    }
    std::sort(games.begin(), games.end(), [](const DesktopGame& left, const DesktopGame& right) {
        return left.title.localeAwareCompare(right.title) < 0;
    });
    m_allGames = std::move(games);
    applyFilter();
    m_scanning = false;
    emit scanningChanged();
}

QString GameLibraryModel::pathAt(int index) const
{
    return (index >= 0 && index < m_visibleGames.size()) ? m_visibleGames.at(index).path : QString();
}

QVariantMap GameLibraryModel::gameForSerials(const QVariantList& serials) const
{
    QSet<QString> wanted;
    for (const QVariant& serial : serials) {
        const QString normalized = normalizedSerial(serial.toString());
        if (!normalized.isEmpty())
            wanted.insert(normalized);
    }
    if (wanted.isEmpty())
        return {};
    for (const DesktopGame& game : m_allGames) {
        if (!wanted.contains(normalizedSerial(game.serial)))
            continue;
        return {{QStringLiteral("title"), game.title}, {QStringLiteral("path"), game.path},
            {QStringLiteral("serial"), game.serial}, {QStringLiteral("region"), game.region},
            {QStringLiteral("size"), game.size}, {QStringLiteral("favorite"), game.favorite}};
    }
    return {};
}

bool GameLibraryModel::toggleFavoritePath(const QString& path)
{
    bool changed = false;
    bool favorite = false;
    for (DesktopGame& game : m_allGames) {
        if (game.path != path)
            continue;
        game.favorite = !game.favorite;
        favorite = game.favorite;
        changed = true;
        break;
    }
    if (!changed)
        return false;
    if (favorite)
        m_favoritePaths.insert(path);
    else
        m_favoritePaths.remove(path);
    saveFavorites();
    applyFilter();
    return favorite;
}

void GameLibraryModel::toggleFavorite(int index)
{
    if (index >= 0 && index < m_visibleGames.size())
        toggleFavoritePath(m_visibleGames.at(index).path);
}

void GameLibraryModel::invalidateCovers()
{
    ++m_coverRevision;
    emit coverRevisionChanged();
}

void GameLibraryModel::setSearchQuery(const QString& query)
{
    if (m_searchQuery == query)
        return;
    m_searchQuery = query;
    emit searchQueryChanged();
    applyFilter();
}

void GameLibraryModel::setFavoritesOnly(bool favoritesOnly)
{
    if (m_favoritesOnly == favoritesOnly)
        return;
    m_favoritesOnly = favoritesOnly;
    emit favoritesOnlyChanged();
    applyFilter();
}

void GameLibraryModel::applyFilter()
{
    beginResetModel();
    m_visibleGames.clear();
    const QString query = m_searchQuery.trimmed();
    for (const DesktopGame& game : std::as_const(m_allGames)) {
        if (m_favoritesOnly && !game.favorite)
            continue;
        if (!query.isEmpty() && !game.title.contains(query, Qt::CaseInsensitive)
            && !game.serial.contains(query, Qt::CaseInsensitive))
            continue;
        m_visibleGames.append(game);
    }
    endResetModel();
    emit countChanged();
}

void GameLibraryModel::saveFolders()
{
    m_settings.setValue("library/folders", m_folders);
    m_settings.sync();
}

void GameLibraryModel::saveFavorites()
{
    QStringList favorites(m_favoritePaths.cbegin(), m_favoritePaths.cend());
    favorites.sort(Qt::CaseInsensitive);
    m_settings.setValue("library/favorites", favorites);
    m_settings.sync();
}
