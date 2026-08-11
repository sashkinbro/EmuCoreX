#include "GameCatalogModel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>

namespace {
qint64 jsonIgdbId(const QJsonValue& value)
{
    if (value.isDouble())
        return value.toInteger();
    if (value.isObject())
        return value.toObject().value(QStringLiteral("igdb_id")).toInteger();
    return 0;
}
}

GameCatalogModel::GameCatalogModel(QObject* parent)
    : QAbstractListModel(parent)
    , m_connectionName(QStringLiteral("emucorex-catalog-%1").arg(QUuid::createUuid().toString(QUuid::Id128)))
{
    loadIdentityIndex();
    openDatabase();
    refresh();
}

GameCatalogModel::~GameCatalogModel()
{
    if (m_database.isValid())
        m_database.close();
    m_database = {};
    QSqlDatabase::removeDatabase(m_connectionName);
}

int GameCatalogModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_games.size();
}

QVariant GameCatalogModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_games.size())
        return {};
    const CatalogGame& game = m_games.at(index.row());
    switch (role) {
    case IdRole: return game.id;
    case NameRole: return game.name;
    case YearRole: return game.year;
    case RatingRole: return game.rating;
    case SummaryRole: return game.summary;
    case StorylineRole: return game.storyline;
    case CoverUrlRole: return game.coverUrl;
    case HeroUrlRole: return game.heroUrl;
    case GenresRole: return game.genres;
    case PrimarySerialRole: return game.primarySerial;
    default: return {};
    }
}

QHash<int, QByteArray> GameCatalogModel::roleNames() const
{
    return {
        {IdRole, "catalogId"}, {NameRole, "catalogName"}, {YearRole, "catalogYear"},
        {RatingRole, "catalogRating"}, {SummaryRole, "catalogSummary"},
        {StorylineRole, "catalogStoryline"}, {CoverUrlRole, "catalogCoverUrl"},
        {HeroUrlRole, "catalogHeroUrl"}, {GenresRole, "catalogGenres"},
        {PrimarySerialRole, "catalogPrimarySerial"}
    };
}

void GameCatalogModel::setSearchQuery(const QString& query)
{
    const QString trimmed = query.trimmed();
    if (m_searchQuery == trimmed)
        return;
    m_searchQuery = trimmed;
    emit searchQueryChanged();
    refresh();
}

bool GameCatalogModel::openDatabase()
{
    const QString dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dataDirectory.isEmpty())
        return false;
    QDir().mkpath(dataDirectory + QStringLiteral("/catalog"));
    const QString targetPath = dataDirectory + QStringLiteral("/catalog/games.db");
    QFile bundled(QStringLiteral(":/catalog/games.db"));
    const QFileInfo targetInfo(targetPath);
    if (!targetInfo.isFile() || targetInfo.size() != bundled.size()) {
        QFile::remove(targetPath);
        if (!bundled.copy(targetPath))
            return false;
    }

    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(targetPath);
    m_database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    const bool opened = m_database.open();
    if (m_available != opened) {
        m_available = opened;
        emit availableChanged();
    }
    return opened;
}

void GameCatalogModel::loadIdentityIndex()
{
    const auto merge = [this](const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return;
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        const QJsonObject serials = root.value(QStringLiteral("serial_to_igdb")).toObject();
        for (auto it = serials.constBegin(); it != serials.constEnd(); ++it) {
            const qint64 id = jsonIgdbId(it.value());
            if (id > 0)
                m_serialToId.insert(normalizedSerial(it.key()).remove('-'), id);
        }
        const QJsonObject titles = root.value(QStringLiteral("title_to_igdb")).toObject();
        for (auto it = titles.constBegin(); it != titles.constEnd(); ++it) {
            const qint64 id = jsonIgdbId(it.value());
            if (id > 0)
                m_titleToId.insert(normalizeTitle(it.key()), id);
        }
        const QJsonObject games = root.value(QStringLiteral("games")).toObject();
        for (auto it = games.constBegin(); it != games.constEnd(); ++it) {
            const QJsonObject game = it.value().toObject();
            const qint64 id = game.value(QStringLiteral("igdb_id")).toInteger(it.key().toLongLong());
            const QString name = game.value(QStringLiteral("name")).toString();
            const QString serial = normalizedSerial(game.value(QStringLiteral("primary_serial")).toString());
            if (id > 0 && !name.isEmpty())
                m_titleToId.insert(normalizeTitle(name), id);
            if (id > 0 && !serial.isEmpty())
                m_primarySerials.insert(id, serial);
        }
    };
    merge(QStringLiteral(":/catalog/rom_identity_index.json"));
    merge(QStringLiteral(":/catalog/rom_identity_overrides.json"));
}

void GameCatalogModel::refresh()
{
    if (!m_database.isOpen() && !openDatabase())
        return;

    QSqlQuery query(m_database);
    const QString base = QStringLiteral(R"(
        SELECT g.igdb_id, g.name, COALESCE(g.year, 0), COALESCE(g.rating, 0),
               COALESCE(g.summary, ''), COALESCE(g.storyline, ''),
               COALESCE(g.cover_url, ''), COALESCE(g.hero_url, ''),
               COALESCE((SELECT group_concat(genre_name, ' · ')
                         FROM game_genres gg WHERE gg.igdb_id = g.igdb_id), '')
        FROM games g
    )");
    if (m_searchQuery.isEmpty()) {
        query.prepare(base + QStringLiteral(R"(
            ORDER BY CASE WHEN g.rating IS NULL THEN 1 ELSE 0 END,
                     g.rating DESC, g.year DESC, g.name COLLATE NOCASE
            LIMIT 240
        )"));
    } else {
        query.prepare(base + QStringLiteral(R"(
            WHERE g.normalized_name LIKE :prefix OR g.normalized_name LIKE :contains
                  OR g.name LIKE :raw
            ORDER BY CASE WHEN g.normalized_name LIKE :prefixOrder THEN 0 ELSE 1 END,
                     g.rating DESC, g.name COLLATE NOCASE
            LIMIT 240
        )"));
        const QString normalized = normalizeTitle(m_searchQuery);
        query.bindValue(QStringLiteral(":prefix"), normalized + '%');
        query.bindValue(QStringLiteral(":contains"), '%' + normalized + '%');
        query.bindValue(QStringLiteral(":raw"), '%' + m_searchQuery + '%');
        query.bindValue(QStringLiteral(":prefixOrder"), normalized + '%');
    }
    if (!query.exec())
        return;

    QList<CatalogGame> games;
    while (query.next())
        games.append(readGame(query));
    beginResetModel();
    m_games = std::move(games);
    endResetModel();
    emit countChanged();
}

CatalogGame GameCatalogModel::readGame(QSqlQuery& query) const
{
    CatalogGame game;
    game.id = query.value(0).toLongLong();
    game.name = query.value(1).toString();
    game.year = query.value(2).toInt();
    game.rating = query.value(3).toDouble();
    game.summary = query.value(4).toString();
    game.storyline = query.value(5).toString();
    game.coverUrl = highResolutionImageUrl(query.value(6).toString());
    game.heroUrl = highResolutionImageUrl(query.value(7).toString());
    game.genres = query.value(8).toString();
    game.primarySerial = m_primarySerials.value(game.id);
    return game;
}

QVariantMap GameCatalogModel::toMap(const CatalogGame& game) const
{
    return {{QStringLiteral("id"), game.id}, {QStringLiteral("name"), game.name},
        {QStringLiteral("year"), game.year}, {QStringLiteral("rating"), game.rating},
        {QStringLiteral("summary"), game.summary}, {QStringLiteral("storyline"), game.storyline},
        {QStringLiteral("coverUrl"), game.coverUrl}, {QStringLiteral("heroUrl"), game.heroUrl},
        {QStringLiteral("genres"), game.genres}, {QStringLiteral("primarySerial"), game.primarySerial},
        {QStringLiteral("serials"), game.serials}, {QStringLiteral("screenshots"), game.screenshots},
        {QStringLiteral("videos"), game.videos}};
}

void GameCatalogModel::loadExtendedDetails(CatalogGame& game) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT serial FROM game_serials WHERE igdb_id = :id ORDER BY serial"));
    query.bindValue(QStringLiteral(":id"), game.id);
    if (query.exec()) {
        while (query.next())
            game.serials.append(query.value(0).toString());
    }
    if (!game.primarySerial.isEmpty() && !game.serials.contains(game.primarySerial))
        game.serials.prepend(game.primarySerial);

    query.prepare(QStringLiteral("SELECT image_url FROM game_screenshots WHERE igdb_id = :id ORDER BY position"));
    query.bindValue(QStringLiteral(":id"), game.id);
    if (query.exec()) {
        while (query.next()) {
            QString image = query.value(0).toString();
            image.replace(QStringLiteral("/t_screenshot_big/"), QStringLiteral("/t_1080p/"));
            game.screenshots.append(image);
        }
    }

    query.prepare(QStringLiteral("SELECT youtube_id FROM game_videos WHERE igdb_id = :id ORDER BY position"));
    query.bindValue(QStringLiteral(":id"), game.id);
    if (query.exec()) {
        while (query.next())
            game.videos.append(query.value(0).toString());
    }
}

QVariantMap GameCatalogModel::detailsForId(qint64 id) const
{
    if (!m_database.isOpen() || id <= 0)
        return {};
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(R"(
        SELECT g.igdb_id, g.name, COALESCE(g.year, 0), COALESCE(g.rating, 0),
               COALESCE(g.summary, ''), COALESCE(g.storyline, ''),
               COALESCE(g.cover_url, ''), COALESCE(g.hero_url, ''),
               COALESCE((SELECT group_concat(genre_name, ' · ')
                         FROM game_genres gg WHERE gg.igdb_id = g.igdb_id), '')
        FROM games g WHERE g.igdb_id = :id LIMIT 1
    )"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec() || !query.next())
        return {};
    CatalogGame game = readGame(query);
    loadExtendedDetails(game);
    return toMap(game);
}

qint64 GameCatalogModel::findBestMatchId(const QString& serial, const QString& title) const
{
    const QString serialKey = normalizedSerial(serial).remove('-');
    if (!serialKey.isEmpty() && m_serialToId.contains(serialKey))
        return m_serialToId.value(serialKey);
    const QString titleKey = normalizeTitle(title);
    if (!titleKey.isEmpty() && m_titleToId.contains(titleKey))
        return m_titleToId.value(titleKey);
    if (!m_database.isOpen() || titleKey.isEmpty())
        return 0;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT igdb_id FROM games WHERE normalized_name = :title LIMIT 1"));
    query.bindValue(QStringLiteral(":title"), titleKey);
    return (query.exec() && query.next()) ? query.value(0).toLongLong() : 0;
}

QVariantMap GameCatalogModel::matchGame(const QString& serial, const QString& title) const
{
    return detailsForId(findBestMatchId(serial, title));
}

QString GameCatalogModel::normalizedSerial(const QString& serial) const
{
    static const QRegularExpression split(QStringLiteral("([A-Za-z]{4})[^A-Za-z0-9]*([0-9]{3})[^A-Za-z0-9]*([0-9]{2})"));
    static const QRegularExpression compact(QStringLiteral("([A-Za-z]{4})[^A-Za-z0-9]*([0-9]{5})"));
    const QString upper = serial.trimmed().toUpper();
    auto match = split.match(upper);
    if (match.hasMatch())
        return match.captured(1) + '-' + match.captured(2) + match.captured(3);
    match = compact.match(upper);
    if (match.hasMatch())
        return match.captured(1) + '-' + match.captured(2);
    QString clean = upper;
    clean.remove(QRegularExpression(QStringLiteral("[^A-Z0-9_-]")));
    return clean;
}

QString GameCatalogModel::normalizeTitle(QString title)
{
    title = title.normalized(QString::NormalizationForm_D).toLower();
    title.remove(QRegularExpression(QStringLiteral("[\\x{0300}-\\x{036f}]")));
    title.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral(" "));
    return title.simplified();
}

QString GameCatalogModel::highResolutionImageUrl(QString url)
{
    if (url.startsWith(QStringLiteral("//")))
        url.prepend(QStringLiteral("https:"));
    url.replace(QStringLiteral("/t_thumb/"), QStringLiteral("/t_cover_big_2x/"));
    url.replace(QStringLiteral("/t_cover_small/"), QStringLiteral("/t_cover_big_2x/"));
    url.replace(QStringLiteral("/t_cover_big/"), QStringLiteral("/t_cover_big_2x/"));
    return url;
}
