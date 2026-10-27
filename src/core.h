#pragma once
#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QMap>

namespace nature {
constexpr auto GameId = "SLUS_011.15";
constexpr auto ExeHash = "34e3b35dcd3fb1cd9a7d4813b8a9ef987915826e3d436f1908ecbe2d4fdf1444";

struct Mod {
    QString id, name, version, directory;
    QJsonObject texts;
    QJsonObject content;
    bool enabled = false;
    int schema = 1;
    QString error;
};

// Read-only access to a single-track Mode 2/2352 disc or 2048-byte ISO.
class Disc {
public:
    explicit Disc(QString path);
    QByteArray read(qint64 offset, qint64 size) const;
    QMap<QString, QPair<quint32, quint32>> files() const;
private:
    QString path_;
    qint64 bytes_ = 0;
    int sector_ = 0, payload_ = 0;
};

QByteArray readFile(const QString &path, qint64 limit = 256 * 1024 * 1024);
void writeFile(const QString &path, const QByteArray &bytes);
QJsonObject readObject(const QString &path);
QString safePath(const QString &name);
QJsonArray textLayout();
// Preserve the legacy manifest contract while the shared loader handles packages.
Mod validateLegacyMod(const QJsonObject &manifest, const QString &directory);
QString decodeText(const QByteArray &bytes);

class Library {
public:
    explicit Library(QString root);
    QString root() const { return root_; }
    bool hasGame() const;
    QJsonObject importGame(const QString &source);
    Mod installMod(const QString &zip);
    QList<Mod> mods() const;
    void setEnabled(const QString &id, bool enabled);
    void moveMod(const QString &id, int delta);
    // Move an installed mod to local recovery storage and remove it from the profile.
    QString removeMod(const QString &id);
    QString text(const QString &key, QString *provider = nullptr) const;
    // Resolved content preview; new entities are not yet bound to native gameplay.
    QJsonObject content() const;
    QStringList conflicts() const;
    QJsonObject status() const;
private:
    QString root_;
    QList<Mod> resolvedMods() const;
};
}
