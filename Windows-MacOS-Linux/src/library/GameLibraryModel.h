#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QSettings>
#include <QSet>
#include <QUrl>
#include <QVariantMap>

#include "../core/GameMetadataProvider.h"

class GameCatalogModel;

struct DesktopGame
{
    QString title;
    QString path;
    QString serial;
    QString region;
    QString coverUrl;
    QString heroUrl;
    QString summary;
    QString genres;
    qint64 catalogId = 0;
    int year = 0;
    double rating = 0.0;
    qint64 size = 0;
    QDateTime modified;
    bool favorite = false;
};

class GameLibraryModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QStringList folders READ folders NOTIFY foldersChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchQueryChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(int coverRevision READ coverRevision NOTIFY coverRevisionChanged)

public:
    enum Role {
        TitleRole = Qt::UserRole + 1,
        PathRole,
        SerialRole,
        RegionRole,
        CoverUrlRole,
        HeroUrlRole,
        SummaryRole,
        GenresRole,
        CatalogIdRole,
        YearRole,
        RatingRole,
        SizeRole,
        ModifiedRole,
        FavoriteRole
    };
    Q_ENUM(Role)

    explicit GameLibraryModel(GameCatalogModel* catalog, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QStringList folders() const { return m_folders; }
    QString searchQuery() const { return m_searchQuery; }
    bool scanning() const { return m_scanning; }
    int coverRevision() const { return m_coverRevision; }

    Q_INVOKABLE void addFolder(const QUrl& folderUrl);
    Q_INVOKABLE void removeFolder(int index);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString pathAt(int index) const;
    Q_INVOKABLE QVariantMap gameForCatalogId(qint64 catalogId) const;
    Q_INVOKABLE bool toggleFavoritePath(const QString& path);
    Q_INVOKABLE void toggleFavorite(int index);
    Q_INVOKABLE void invalidateCovers();
    void setSearchQuery(const QString& query);

signals:
    void countChanged();
    void foldersChanged();
    void searchQueryChanged();
    void scanningChanged();
    void coverRevisionChanged();

private:
    static bool isSupportedImage(const QString& path);
    void applyFilter();
    void saveFolders();
    void saveFavorites();

    QSettings m_settings;
    GameCatalogModel* m_catalog = nullptr;
    GameMetadataProvider m_metadataProvider;
    QList<DesktopGame> m_allGames;
    QList<DesktopGame> m_visibleGames;
    QStringList m_folders;
    QSet<QString> m_favoritePaths;
    QString m_searchQuery;
    bool m_scanning = false;
    int m_coverRevision = 0;
};
