#include "GameLibraryModel.h"
#include "GameCatalogModel.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>
#include <QUrl>

GameLibraryModel::GameLibraryModel(GameCatalogModel* catalog, QObject* parent)
    : QAbstractListModel(parent)
    , m_settings(QSettings::IniFormat, QSettings::UserScope,
          QCoreApplication::organizationName(), QCoreApplication::applicationName())
    , m_folders(m_settings.value("library/folders").toStringList())
    , m_catalog(catalog)
{
    const QStringList favorites = m_settings.value("library/favorites").toStringList();
    m_favoritePaths = QSet<QString>(favorites.cbegin(), favorites.cend());
    refresh();
}

int GameLibraryModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_visibleGames.size();
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
    case CoverUrlRole: return game.coverUrl;
    case HeroUrlRole: return game.heroUrl;
    case SummaryRole: return game.summary;
    case GenresRole: return game.genres;
    case CatalogIdRole: return game.catalogId;
    case YearRole: return game.year;
    case RatingRole: return game.rating;
    case SizeRole: return game.size;
    case ModifiedRole: return game.modified;
    case FavoriteRole: return game.favorite;
    default: return {};
    }
}

QHash<int, QByteArray> GameLibraryModel::roleNames() const
{
    return {
        {TitleRole, "title"}, {PathRole, "path"}, {SerialRole, "serial"},
        {RegionRole, "region"}, {CoverUrlRole, "coverUrl"}, {HeroUrlRole, "heroUrl"},
        {SummaryRole, "summary"}, {GenresRole, "genres"}, {CatalogIdRole, "catalogId"},
        {YearRole, "year"}, {RatingRole, "rating"}, {SizeRole, "fileSize"},
        {ModifiedRole, "modified"}, {FavoriteRole, "favorite"}
    };
}

bool GameLibraryModel::isSupportedImage(const QString& path)
{
    static const QSet<QString> suffixes = {
        "iso", "bin", "img", "mdf", "nrg", "chd", "cso", "gz", "zso", "elf", "gs", "dump"
    };
    return suffixes.contains(QFileInfo(path).suffix().toLower());
}

void GameLibraryModel::addFolder(const QUrl& folderUrl)
{
    QString path = folderUrl.isLocalFile() ? folderUrl.toLocalFile() : folderUrl.toString();
    path = QDir::cleanPath(path);
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
            game.serial = metadata.serial;
            game.region = metadata.region;
            game.size = info.size();
            game.modified = info.lastModified();
            game.favorite = m_favoritePaths.contains(canonical);

            if (m_catalog) {
                const QVariantMap catalogMatch = m_catalog->matchGame(game.serial, game.title);
                if (!catalogMatch.isEmpty()) {
                    game.catalogId = catalogMatch.value(QStringLiteral("id")).toLongLong();
                    game.title = catalogMatch.value(QStringLiteral("name"), game.title).toString();
                    game.year = catalogMatch.value(QStringLiteral("year")).toInt();
                    game.rating = catalogMatch.value(QStringLiteral("rating")).toDouble();
                    game.summary = catalogMatch.value(QStringLiteral("summary")).toString();
                    game.heroUrl = catalogMatch.value(QStringLiteral("heroUrl")).toString();
                    game.genres = catalogMatch.value(QStringLiteral("genres")).toString();
                    if (game.serial.isEmpty())
                        game.serial = catalogMatch.value(QStringLiteral("primarySerial")).toString();
                }
                game.coverUrl = m_catalog->coverUrlForSerial(game.serial, 1);
            }
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

void GameLibraryModel::toggleFavorite(int index)
{
    if (index < 0 || index >= m_visibleGames.size())
        return;
    const QString path = m_visibleGames.at(index).path;
    for (DesktopGame& game : m_allGames) {
        if (game.path == path) {
            game.favorite = !game.favorite;
            if (game.favorite)
                m_favoritePaths.insert(path);
            else
                m_favoritePaths.remove(path);
            break;
        }
    }
    saveFavorites();
    applyFilter();
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

void GameLibraryModel::applyFilter()
{
    beginResetModel();
    if (m_searchQuery.trimmed().isEmpty()) {
        m_visibleGames = m_allGames;
    } else {
        m_visibleGames.clear();
        for (const DesktopGame& game : std::as_const(m_allGames)) {
            if (game.title.contains(m_searchQuery, Qt::CaseInsensitive) || game.serial.contains(m_searchQuery, Qt::CaseInsensitive))
                m_visibleGames.append(game);
        }
    }
    endResetModel();
    emit countChanged();
}

void GameLibraryModel::saveFolders()
{
    m_settings.setValue("library/folders", m_folders);
}

void GameLibraryModel::saveFavorites()
{
    QStringList favorites(m_favoritePaths.cbegin(), m_favoritePaths.cend());
    favorites.sort(Qt::CaseInsensitive);
    m_settings.setValue("library/favorites", favorites);
}
