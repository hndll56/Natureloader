#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace nature {
// Detection and storage are separate from native-runtime compatibility.
class RomCatalog {
public:
    explicit RomCatalog(QString root);
    QJsonObject importRom(const QString &source);
    QJsonArray entries() const;
    void select(const QString &id);
    QString selected() const;
    QJsonObject metadata(const QString &id) const;
    bool verify(const QString &id) const;
    QString profilePath(const QString &id) const;
private:
    QString root_;
    QString directory(const QString &id) const;
};
}
