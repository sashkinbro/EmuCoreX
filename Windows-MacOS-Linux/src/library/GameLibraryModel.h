#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QSettings>
#include <QSet>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include "../core/GameMetadataProvider.h"

class CoreRuntime;

struct DesktopGame
{
    QString title;
    QString path;
    QString serial;
    QString region;
    qint64 size = 0;
    QDateTime modified;
    bool favorite = false;
};

class GameLibraryModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)
    Q_PROPERTY(int favoriteCount READ favoriteCount NOTIFY countChanged)
    Q_PROPERTY(QStringList folders READ folders NOTIFY foldersChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchQueryChanged)
    Q_PROPERTY(bool favoritesOnly READ favoritesOnly WRITE setFavoritesOnly NOTIFY favoritesOnlyChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(int coverRevision READ coverRevision NOTIFY coverRevisionChanged)

public:
    enum Role {
        TitleRole = Qt::UserRole + 1,
        PathRole,
        SerialRole,
        RegionRole,
        SizeRole,
        ModifiedRole,
        FavoriteRole
    };
    Q_ENUM(Role)

    explicit GameLibraryModel(const CoreRuntime* core, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int totalCount() const { return m_allGames.size(); }
    QStringList folders() const { return m_folders; }
    QString searchQuery() const { return m_searchQuery; }
    bool favoritesOnly() const { return m_favoritesOnly; }
    bool scanning() const { return m_scanning; }
    int coverRevision() const { return m_coverRevision; }
    int favoriteCount() const;

    Q_INVOKABLE void addFolder(const QUrl& folderUrl);
    Q_INVOKABLE void removeFolder(int index);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString pathAt(int index) const;
    Q_INVOKABLE QVariantMap gameForSerials(const QVariantList& serials) const;
    Q_INVOKABLE bool toggleFavoritePath(const QString& path);
    Q_INVOKABLE void toggleFavorite(int index);
    Q_INVOKABLE void invalidateCovers();
    void setSearchQuery(const QString& query);
    void setFavoritesOnly(bool favoritesOnly);

signals:
    void countChanged();
    void foldersChanged();
    void searchQueryChanged();
    void favoritesOnlyChanged();
    void scanningChanged();
    void coverRevisionChanged();

private:
    static bool isSupportedImage(const QString& path);
    static QString normalizedSerial(const QString& serial);
    void applyFilter();
    void saveFolders();
    void saveFavorites();

    QSettings m_settings;
    GameMetadataProvider m_metadataProvider;
    QList<DesktopGame> m_allGames;
    QList<DesktopGame> m_visibleGames;
    QStringList m_folders;
    QSet<QString> m_favoritePaths;
    QString m_searchQuery;
    bool m_favoritesOnly = false;
    bool m_scanning = false;
    int m_coverRevision = 0;
};
