#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QSqlDatabase>
#include <QVariantMap>
#include <QStringList>

struct CatalogGame
{
    qint64 id = 0;
    QString name;
    int year = 0;
    double rating = 0.0;
    QString summary;
    QString storyline;
    QString coverUrl;
    QString heroUrl;
    QString genres;
    QString primarySerial;
    QStringList serials;
    QStringList screenshots;
    QStringList videos;
};

class GameCatalogModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchQueryChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        YearRole,
        RatingRole,
        SummaryRole,
        StorylineRole,
        CoverUrlRole,
        HeroUrlRole,
        GenresRole,
        PrimarySerialRole
    };
    Q_ENUM(Role)

    explicit GameCatalogModel(QObject* parent = nullptr);
    ~GameCatalogModel() override;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool available() const { return m_available; }
    QString searchQuery() const { return m_searchQuery; }
    void setSearchQuery(const QString& query);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantMap detailsForId(qint64 id) const;
    Q_INVOKABLE QVariantMap matchGame(const QString& serial, const QString& title) const;
    Q_INVOKABLE QString coverUrlForSerial(const QString& serial, int style) const;
    Q_INVOKABLE QString normalizedSerial(const QString& serial) const;

signals:
    void countChanged();
    void availableChanged();
    void searchQueryChanged();

private:
    bool openDatabase();
    void loadIdentityIndex();
    CatalogGame readGame(QSqlQuery& query) const;
    QVariantMap toMap(const CatalogGame& game) const;
    void loadExtendedDetails(CatalogGame& game) const;
    qint64 findBestMatchId(const QString& serial, const QString& title) const;
    static QString normalizeTitle(QString title);
    static QString highResolutionImageUrl(QString url);

    QList<CatalogGame> m_games;
    QSqlDatabase m_database;
    QHash<QString, qint64> m_serialToId;
    QHash<QString, qint64> m_titleToId;
    QHash<qint64, QString> m_primarySerials;
    QString m_connectionName;
    QString m_searchQuery;
    bool m_available = false;
};
