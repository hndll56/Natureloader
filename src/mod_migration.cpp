#include "mod_migration.h"
#include "core.h"
#include "save.h"
#include "util.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUuid>
#include <stdexcept>

namespace nature {
namespace {
[[noreturn]] void fail(const QString &s) { throw std::runtime_error(s.toStdString()); }
QStringList packFiles(const QString &root) {
    QStringList result;
    QDirIterator it(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
    qint64 total = 0;
    int count = 0;
    while (it.hasNext()) {
        it.next();
        const auto info = it.fileInfo();
        const auto relative = safePath(QDir(root).relativeFilePath(info.filePath()));
        if (++count > 4096 || info.isSymLink() || (!info.isDir() && !info.isFile()))
            fail("Cannot migrate unsafe mod member: " + relative);
        if (!info.isFile()) continue;
        total += info.size();
        if (info.size() > 32LL * 1024 * 1024 || total > 64LL * 1024 * 1024)
            fail("Mod migration exceeds package limits.");
        result.append(relative);
    }
    result.sort();
    return result;
}
QStringList profileIds(const QJsonObject &profile) {
    if (!profile.value("enabled").isArray()) fail("Invalid profile during mod migration.");
    QStringList result;
    for (const auto &value : profile.value("enabled").toArray()) {
        const auto id = value.toString();
        safePath(id);
        if (!value.isString() || !nm::validLocalId(id.toStdString()) || id == "base" || result.contains(id))
            fail("Invalid mod ID during migration.");
        result.append(id);
    }
    return result;
}
}

void migrateModWorkspace(const QString &root) {
    const QString profilePath = root + "/profile.json", oldV2Profile = root + "/profile-v2.json";
    if (QFileInfo(profilePath).isSymLink() || QFileInfo(oldV2Profile).isSymLink() ||
        QFileInfo(root + "/mods-v2").isSymLink()) fail("Cannot migrate symlinked mod workspace.");
    const bool hadProfile = QFileInfo::exists(profilePath);
    auto profile = hadProfile ? readObject(profilePath) : QJsonObject{{"enabled", QJsonArray()}};
    if (profile.value("unified").toBool()) return;
    const bool hadV2 = QFileInfo::exists(oldV2Profile);
    const auto directories = QDir(root + "/mods-v2").entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    if (!hadProfile && !hadV2 && directories.isEmpty()) return;
    if (hadProfile && profile.value("schema") != QJsonValue(1)) fail("Unsupported existing mod profile.");
    QStringList enabled = profileIds(profile);
    nm::SaveExtension empty;
    QJsonObject state = QJsonDocument::fromJson(QByteArray::fromStdString(empty.toJson().dump())).object();
    if (hadV2) {
        const auto old = readObject(oldV2Profile);
        if (old.value("schema") != QJsonValue(2) || !old.value("authoringSave").isObject())
            fail("Invalid old v2 profile; migration left its files intact.");
        std::string error;
        nm::SaveExtension parsed;
        if (!nm::SaveExtension::fromJson(nm::Json::parse(QJsonDocument(old.value("authoringSave").toObject()).toJson().toStdString()), parsed, error))
            fail("Invalid old mod ID state: " + QString::fromStdString(error));
        state = old.value("authoringSave").toObject();
        for (const auto &id : profileIds(old)) if (!enabled.contains(id)) enabled.append(id);
    }
    QTemporaryDir staging(root + "/.merge-mods-XXXXXX");
    if (!staging.isValid()) fail("Cannot stage unified mod workspace.");
    QStringList copied;
    for (const auto &id : directories) {
        safePath(id);
        if (!nm::validLocalId(id.toStdString()) || id == "base") fail("Invalid mod directory during migration: " + id);
        const auto source = root + "/mods-v2/" + id, target = root + "/mods/" + id;
        if (QFileInfo(source).isSymLink()) fail("Cannot migrate symlinked mod: " + id);
        const auto files = packFiles(source);
        if (QFileInfo::exists(target) || QFileInfo(target).isSymLink()) {
            if (QFileInfo(target).isSymLink() || !QFileInfo(target).isDir() || packFiles(target) != files)
                fail("Mod ID collision during migration: " + id);
            for (const auto &file : files)
                if (readFile(source + "/" + file) != readFile(target + "/" + file))
                    fail("Different packages share mod ID during migration: " + id);
            continue;
        }
        for (const auto &file : files) {
            const auto output = staging.path() + "/" + id + "/" + file;
            if (!QDir().mkpath(QFileInfo(output).path()) || !QFile::copy(source + "/" + file, output))
                fail("Cannot copy mod during migration: " + id);
        }
        if (files.isEmpty() && !QDir().mkpath(staging.path() + "/" + id)) fail("Cannot stage mod directory.");
        copied.append(id);
    }
    profile.insert("schema", 2);
    profile.insert("unified", true);
    profile.insert("enabled", QJsonArray::fromStringList(enabled));
    profile.insert("authoringSave", state);
    const auto backup = root + "/mod-migration-backups/" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!QDir().mkpath(backup)) fail("Cannot create mod migration backup.");
    if (hadProfile && !QFile::copy(profilePath, backup + "/profile.json")) fail("Cannot back up original mod profile.");
    if (hadV2 && !QFile::copy(oldV2Profile, backup + "/profile-v2.json")) fail("Cannot back up v2 mod profile.");
    profile.insert("migrationBackup", backup);
    const auto bytes = QJsonDocument(profile).toJson();
    if (bytes.size() > 2 * 1024 * 1024) fail("Merged mod profile exceeds 2 MiB.");
    QStringList committed;
    try {
        for (const auto &id : copied) {
            if (!QDir().rename(staging.path() + "/" + id, root + "/mods/" + id)) fail("Cannot commit migrated mod: " + id);
            committed.append(id);
        }
        writeFile(profilePath, bytes);
    } catch (...) {
        for (const auto &id : committed)
            if (!QDir().rename(root + "/mods/" + id, staging.path() + "/" + id))
                fail("Migration failed; copied mod retained at " + root + "/mods/" + id);
        throw;
    }
}
}
