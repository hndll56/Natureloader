#include "modcore_library.h"

#include "core.h"
#include "native_mods.h"
#include "mod_migration.h"
#include <QDirIterator>
#include "modcore_catalog.h"
#include "session.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <QTemporaryDir>
#include <QUuid>
#include <archive.h>
#include <archive_entry.h>
#include <memory>
#include <stdexcept>

namespace nature {
namespace {
constexpr qint64 FileLimit = 32LL * 1024 * 1024;
constexpr qint64 PackageLimit = 64LL * 1024 * 1024;
[[noreturn]] void fail(const QString &message) { throw std::runtime_error(message.toStdString()); }
QString string(const std::string &value) { return QString::fromUtf8(value.data(), qsizetype(value.size())); }
void checkModId(const QString &id) {
    safePath(id);
    if (!nm::validLocalId(id.toStdString()) || id == "base") fail("Invalid mod ID: " + id);
}
QJsonObject object(const nm::Json &value) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(value.dump())).object();
}
QStringList enabledIds(const QJsonObject &profile) {
    QStringList ids;
    if (profile.value("schema") != QJsonValue(2) || !profile.value("enabled").isArray() ||
        !profile.value("authoringSave").isObject()) fail("Invalid unified mod profile.");
    for (const auto &entry : profile.value("enabled").toArray()) {
        const QString id = entry.toString();
        if (!entry.isString() || !nm::validLocalId(id.toStdString()) || id == "base" || ids.contains(id))
            fail("Invalid or duplicate mod ID in profile.");
        safePath(id);
        ids.append(id);
    }
    return ids;
}
nm::SaveExtension authoringState(const QJsonObject &profile) {
    nm::SaveExtension save;
    std::string error;
    const auto bytes = QJsonDocument(profile.value("authoringSave").toObject()).toJson(QJsonDocument::Compact);
    if (!nm::SaveExtension::fromJson(nm::Json::parse(bytes.toStdString()), save, error))
        fail("Invalid authoring ID state: " + string(error));
    return save;
}
void ensureDirectory(const QString &path) {
    const QFileInfo info(path);
    if (info.isSymLink() || (info.exists() && !info.isDir()) || !QDir().mkpath(path))
        fail("Cannot use mod workspace directory: " + path);
}
ModcoreMod readMod(const QString &directory) {
    const QFileInfo info(directory);
    if (info.isSymLink() || !info.isDir() || QFileInfo(directory + "/mod.json").isSymLink())
        fail("Mod must be a local directory with a regular manifest: " + directory);
    const auto bytes = readFile(directory + "/mod.json", 1024 * 1024);
    const auto document = nm::Json::parse(bytes.toStdString());
    if (document["schema"].isInt() && document["schema"].asInt() == 1) {
        const auto manifest = QJsonDocument::fromJson(bytes).object();
        const auto legacy = validateLegacyMod(manifest, directory);
        checkModId(legacy.id);
        return {legacy.id, legacy.name, legacy.version, directory, false, manifest};
    }
    const auto parsed = nm::parseManifest(document);
    if (!parsed.ok) {
        QStringList errors;
        for (const auto &error : parsed.errors) errors.append(string(error));
        fail("Invalid schema 2 manifest: " + errors.join("; "));
    }
    if (parsed.manifest.schema != 2 || parsed.manifest.game != GameId)
        fail("Mod must use schema 1 or 2 and game SLUS_011.15.");
    const auto id = string(parsed.manifest.id);
    safePath(id);
    return {id, string(parsed.manifest.name), string(parsed.manifest.versionText), directory,
            false, QJsonDocument::fromJson(bytes).object()};
}

struct BuiltSession {
    // Session stores a pointer to save; destroy the session first.
    nm::SaveExtension save;
    std::unique_ptr<nm::Session> session;
    QStringList errors;
    bool ok = false;
};
std::unique_ptr<BuiltSession> build(const QString &root, const QJsonObject &profile) {
    auto result = std::make_unique<BuiltSession>();
    result->save = authoringState(profile);
    nm::HostInfo host;
    host.exeSha256 = ExeHash;
    host.locale = "id";
    result->session = std::make_unique<nm::Session>(host, nm::Json::parse(natureBaseCatalog));
    std::vector<std::string> order;
    QSet<QString> textKeys;
    for (const auto &entry : textLayout()) textKeys.insert(entry.toObject().value("name").toString());
    for (const auto &id : enabledIds(profile)) {
        order.push_back(id.toStdString());
        try {
            const auto mod = readMod(root + "/mods/" + id);
            if (mod.id != id) fail("Mod directory and manifest ID differ: " + id);
            if (mod.manifest.contains("texts") && !mod.manifest.value("texts").isObject())
                fail("Text overrides must be an object: " + id);
            const auto texts = mod.manifest.value("texts").toObject();
            for (auto entry = texts.begin(); entry != texts.end(); ++entry)
                if (!textKeys.contains(entry.key()) || !entry.value().isString() || entry.value().toString().size() > 16384)
                    fail("Unknown text key or invalid override in " + id + ": " + entry.key());
            if (mod.manifest.value("schema") == QJsonValue(1)) {
                // Legacy content remains an exact, untyped preview. Its manifest still
                // participates in dependency and text ordering through the shared resolver.
                auto metadata = mod.manifest;
                metadata.remove("content");
                auto pack = std::make_shared<nm::MemPackSource>(mod.directory.toStdString());
                pack->put("mod.json", QJsonDocument(metadata).toJson(QJsonDocument::Compact).toStdString());
                result->session->addPack(pack);
            } else result->session->addPack(std::make_shared<nm::DirPackSource>(mod.directory.toStdString()));
        } catch (const std::exception &error) { result->errors.append(string(error.what())); }
    }
    result->session->setUserOrder(order);
    const bool loaded = result->session->load(&result->save);
    for (const auto &message : result->session->report().messages)
        if (message.level == nm::Message::Error)
            result->errors.append((message.mod.empty() ? QString() : string(message.mod) + ": ") + string(message.text));
    result->ok = loaded && result->errors.isEmpty() &&
                 result->session->report().loaded.size() == size_t(order.size());
    return result;
}

const std::vector<std::string> &recordKinds() {
    static const std::vector<std::string> kinds{"item", "npc", "area", "shop", "crop", "building", "event", "recipe",
                                               "dialog", "schedule", "sprite", "asset_override", "language", "rules", "ui_theme"};
    return kinds;
}

void validateNativeItemField(const nm::Registry &registry, const std::string &id,
                             const std::string &field, const nm::Json &value) {
    const auto *record = registry.find(id);
    if (!record || record->owner != "base" || record->kind != "item" || record->handle < 0 || record->handle >= 380)
        fail("Native item patch requires a retail item table row (0..379): " + string(id));
    qint64 minimum, maximum;
    if (field == "buy_price" || field == "sell_price") { minimum = -32768; maximum = 32767; }
    else if (field == "stamina" || field == "fatigue") { minimum = -128; maximum = 127; }
    else fail("Unsupported native item field: " + string(id) + "." + string(field));
    if (!value.isInt() || value.asInt() < minimum || value.asInt() > maximum)
        fail("Invalid native item value: " + string(id) + "." + string(field));
}

QJsonObject nativePlanFor(const QString &root, const BuiltSession &checked) {
    if (!checked.ok) fail("Active mods cannot boot:\n" + checked.errors.join('\n'));
    const auto &registry = checked.session->registry();
    for (const auto &kind : recordKinds())
        for (const auto *record : registry.all(kind))
            if (record->owner != "base")
                fail("New gameplay records are not supported by native BTN: " + string(record->id));
    QJsonObject texts;
    QMap<QString, int> capacities;
    for (const auto &value : textLayout()) {
        const auto entry = value.toObject();
        capacities.insert(entry.value("name").toString(), entry.value("capacity").toInt());
    }
    for (const auto &id : checked.session->report().loaded) {
        const auto *manifest = checked.session->manifest(id);
        if (!manifest) fail("Loaded manifest is unavailable.");
        const auto pack = nm::DirPackSource((root + "/mods/" + string(id)).toStdString());
        if (manifest->schema == 1 && !readMod(root + "/mods/" + string(id)).manifest.value("content").toObject().isEmpty())
            fail("Legacy content remains a preview; disable content mod before native launch: " + string(id));
        if (!manifest->scriptFiles.empty() || !manifest->hooks.empty() || !manifest->plugins.empty() ||
            !pack.list(manifest->assetRoot + "/").empty())
            fail("Native BTN does not support active assets/scripts/hooks/plugins: " + string(id));
        std::vector<nm::Json> documents{manifest->inlineContent};
        for (const auto &file : manifest->contentFiles) {
            std::string source;
            if (!pack.read(file, source)) fail("Native content file is unavailable: " + string(id) + "/" + string(file));
            documents.push_back(nm::Json::parse(source));
        }
        // Check declarations as well as effective registry patches: an unsupported
        // authored record must not disappear merely because another mod overrides it.
        for (const auto &document : documents) {
            if (!document.isObject()) fail("Native content document must be an object: " + string(id));
            for (const auto &entry : document.members()) {
                if (entry.first != "$patch")
                    fail("Native content supports only retail item $patch records: " + string(id) + "/" + string(entry.first));
                if (!entry.second.isObject()) fail("Native $patch must be an object: " + string(id));
                for (const auto &target : entry.second.members()) {
                    if (!target.second.isObject() || target.second.members().empty())
                        fail("Native item patch must contain fields: " + string(target.first));
                    for (const auto &field : target.second.members())
                        validateNativeItemField(registry, target.first, field.first, field.second);
                }
            }
        }
        for (const auto &entry : manifest->texts.members()) {
            const auto key = string(entry.first), value = string(entry.second.asString());
            if (!entry.second.isString() || !capacities.contains(key) || value.size() > 16384)
                fail("Unknown text key or invalid override in " + string(id) + ": " + key);
            texts.insert(key, value);
        }
    }
    // The final winner must fit the real retail slot and its glyph table.
    // Use the exporter's encoder so preflight and RAM output cannot drift.
    for (auto entry = texts.begin(); entry != texts.end(); ++entry)
        encodeNativeText(entry.value().toString(), capacities.value(entry.key()), entry.key());
    QMap<QString, QJsonObject> effective;
    for (const auto &patch : registry.patches()) {
        const auto *record = registry.find(patch.target);
        if (!record) fail("Native patch target disappeared: " + string(patch.target));
        const auto &value = record->data[patch.field];
        validateNativeItemField(registry, patch.target, patch.field, value);
        effective.insert(string(patch.target) + ":" + string(patch.field),
            {{"id", string(patch.target)}, {"handle", record->handle}, {"field", string(patch.field)},
             {"value", qint64(value.asInt())}, {"provider", string(patch.owner)}});
    }
    QJsonArray items;
    for (const auto &patch : effective) items.append(patch);
    return {{"texts", texts}, {"items", items}};
}

void extractZip(const QString &zip, const QString &staging) {
    std::unique_ptr<archive, decltype(&archive_read_free)> input(archive_read_new(), archive_read_free);
    if (!input) fail("Cannot allocate ZIP reader.");
    archive_read_support_format_zip(input.get());
    const auto filename = zip.toStdWString();
    if (archive_read_open_filename_w(input.get(), filename.c_str(), 65536) != ARCHIVE_OK)
        fail("Cannot open mod ZIP.");
    QSet<QString> entries, files, directories;
    QMap<QString, QString> spelling;
    qint64 total = 0;
    int count = 0, status;
    archive_entry *entry = nullptr;
    while ((status = archive_read_next_header(input.get(), &entry)) == ARCHIVE_OK) {
        if (++count > 4096) fail("Mod ZIP exceeds 4096 entries.");
        const bool directory = archive_entry_filetype(entry) == AE_IFDIR;
        if (archive_entry_symlink(entry) || archive_entry_hardlink(entry) ||
            (!directory && archive_entry_filetype(entry) != AE_IFREG))
            fail("Mod ZIP permits only regular files and directories.");
        const char *rawName = archive_entry_pathname_utf8(entry);
        if (!rawName) fail("Mod ZIP entry has an invalid filename.");
        QString name = QString::fromUtf8(rawName);
        if (directory && name.endsWith('/')) name.chop(1);
        safePath(name);
        const auto folded = name.toCaseFolded();
        if (entries.contains(folded)) fail("Duplicate or case-colliding ZIP entry: " + name);
        entries.insert(folded);
        const auto parts = name.split('/');
        QString prefix;
        for (qsizetype i = 0; i < parts.size(); ++i) {
            if (!prefix.isEmpty()) prefix += '/';
            prefix += parts[i];
            const auto key = prefix.toCaseFolded();
            if (spelling.contains(key) && spelling.value(key) != prefix)
                fail("Case-colliding ZIP path: " + prefix);
            spelling.insert(key, prefix);
            if (i + 1 < parts.size()) {
                if (files.contains(key)) fail("ZIP file is also used as a directory: " + prefix);
                directories.insert(key);
            }
        }
        if ((directory && files.contains(folded)) || (!directory && directories.contains(folded)))
            fail("ZIP file/directory collision: " + name);
        const qint64 expected = archive_entry_size(entry);
        const qint64 limit = name == "mod.json" ? 1024 * 1024 : FileLimit;
        if (expected < 0 || expected > limit || (directory && expected != 0))
            fail("Mod ZIP file exceeds size limit: " + name);
        if (directory) {
            directories.insert(folded);
            if (!QDir().mkpath(staging + "/" + name)) fail("Cannot create staged mod directory.");
            if (archive_read_data_skip(input.get()) != ARCHIVE_OK) fail("Corrupt mod directory entry.");
            continue;
        }
        files.insert(folded);
        const QString destination = staging + "/" + name;
        if (!QDir().mkpath(QFileInfo(destination).path())) fail("Cannot create mod parent directory.");
        QFile output(destination);
        if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) fail("Cannot write staged mod file.");
        char buffer[65536];
        qint64 written = 0;
        la_ssize_t n;
        while ((n = archive_read_data(input.get(), buffer, sizeof buffer)) > 0) {
            written += n;
            total += n;
            if (written > limit || total > PackageLimit || output.write(buffer, n) != n)
                fail("Mod ZIP exceeds limits or a staged write failed.");
        }
        if (n < 0 || written != expected || !output.flush()) fail("Corrupt or incomplete mod ZIP file.");
    }
    if (status != ARCHIVE_EOF || !files.contains("mod.json"))
        fail("ZIP must contain mod.json at its root.");
}
} // namespace

ModcoreLibrary::ModcoreLibrary(QString root) : root_(QDir(root).absolutePath()) {
    ensureDirectory(root_);
    ensureDirectory(root_ + "/mods");
    migrateModWorkspace(root_);
}

QJsonObject ModcoreLibrary::profile() const {
    const QString path = root_ + "/profile.json";
    if (QFileInfo(path).isSymLink()) fail("Mod profile must not be a symlink.");
    if (!QFileInfo::exists(path))
        return {{"schema", 2}, {"unified", true}, {"enabled", QJsonArray()}, {"authoringSave", object(nm::SaveExtension().toJson())}};
    auto result = readObject(path);
    enabledIds(result);
    authoringState(result);
    return result;
}

void ModcoreLibrary::saveProfile(const QJsonObject &proposed) const {
    const auto checked = build(root_, proposed);
    if (!checked->ok) fail("Mod profile rejected:\n" + checked->errors.join('\n'));
    auto result = proposed;
    result.insert("authoringSave", object(checked->save.toJson()));
    const auto bytes = QJsonDocument(result).toJson();
    if (bytes.size() > 2 * 1024 * 1024) fail("Mod profile exceeds the 2 MiB persistence limit.");
    writeFile(root_ + "/profile.json", bytes);
}

ModcoreMod ModcoreLibrary::installZip(const QString &zip) {
    QTemporaryDir staging(root_ + "/mods/.install-XXXXXX");
    if (!staging.isValid()) fail("Cannot create mod staging directory.");
    extractZip(zip, staging.path());
    auto mod = readMod(staging.path());
    if (mod.manifest.value("schema") == QJsonValue(1)) {
        qint64 total = 0;
        QDirIterator it(staging.path(), QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const auto name = QDir(staging.path()).relativeFilePath(it.filePath());
            total += it.fileInfo().size();
            if (!it.fileInfo().isFile() || (name != "mod.json" && name != "README.md" && name != "LICENSE") ||
                it.fileInfo().size() > 1024 * 1024 || total > 2 * 1024 * 1024)
                fail("Legacy schema 1 permits only mod.json, README.md and LICENSE within its original size limits.");
        }
    }
    const QString destination = root_ + "/mods/" + mod.id;
    if (QFileInfo::exists(destination) || QFileInfo(destination).isSymLink()) fail("Mod ID already installed: " + mod.id);
    if (enabledIds(profile()).contains(mod.id)) fail("Profile already references missing mod ID: " + mod.id);
    if (!QDir().rename(staging.path(), destination)) fail("Cannot commit mod installation.");
    mod.directory = destination;
    return mod;
}

QList<ModcoreMod> ModcoreLibrary::mods() const {
    const auto enabled = enabledIds(profile());
    QStringList ordered = enabled;
    for (const auto &id : QDir(root_ + "/mods").entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        if (!ordered.contains(id)) ordered.append(id);
    QList<ModcoreMod> result;
    for (const auto &id : ordered) {
        ModcoreMod mod;
        mod.id = mod.name = id;
        mod.directory = root_ + "/mods/" + id;
        try {
            checkModId(id);
            mod = readMod(mod.directory);
            if (mod.id != id) fail("Mod directory and manifest ID differ: " + id);
        } catch (const std::exception &error) {
            mod.id = mod.name = id;
            mod.directory = root_ + "/mods/" + id;
            mod.error = string(error.what());
        }
        mod.enabled = enabled.contains(id);
        result.append(mod);
    }
    return result;
}

void ModcoreLibrary::setEnabled(const QString &id, bool enabled) {
    checkModId(id);
    auto proposed = profile();
    auto ids = enabledIds(proposed);
    if (enabled) {
        if (readMod(root_ + "/mods/" + id).id != id) fail("Mod directory and manifest ID differ: " + id);
    } else if (!ids.contains(id) && !QFileInfo::exists(root_ + "/mods/" + id)) {
        fail("Unknown schema 2 mod: " + id);
    }
    if (enabled && !ids.contains(id)) ids.append(id);
    if (!enabled) ids.removeAll(id);
    proposed.insert("enabled", QJsonArray::fromStringList(ids));
    saveProfile(proposed);
}

void ModcoreLibrary::moveMod(const QString &id, int delta) {
    auto proposed = profile();
    auto ids = enabledIds(proposed);
    const auto index = ids.indexOf(id), next = index + delta;
    if ((delta != -1 && delta != 1) || index < 0 || next < 0 || next >= ids.size()) return;
    ids.swapItemsAt(index, next);
    proposed.insert("enabled", QJsonArray::fromStringList(ids));
    saveProfile(proposed);
}

QString ModcoreLibrary::removeMod(const QString &id) {
    checkModId(id);
    const QString source = root_ + "/mods/" + id;
    if (QFileInfo(source).isSymLink() || !QFileInfo(source).isDir())
        fail("Mod recovery requires a regular installed directory: " + id);
    auto proposed = profile();
    auto ids = enabledIds(proposed);
    ids.removeAll(id);
    proposed.insert("enabled", QJsonArray::fromStringList(ids));
    const auto checked = build(root_, proposed);
    if (!checked->ok) fail("Cannot remove required mod:\n" + checked->errors.join('\n'));
    ensureDirectory(root_ + "/removed-mods");
    const QString recovery = root_ + "/removed-mods/" + id + "-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!QDir().rename(source, recovery)) fail("Cannot move mod into recovery storage.");
    try { saveProfile(proposed); }
    catch (...) {
        if (!QDir().rename(recovery, source)) fail("Profile commit failed; mod retained in recovery: " + recovery);
        throw;
    }
    return recovery;
}

QJsonObject ModcoreLibrary::inspect() const {
    QJsonObject result{{"contentGameplayReady", false}, {"ok", false}, {"loaded", QJsonArray()},
                       {"enabled", QJsonArray()}, {"errors", QJsonArray()}, {"records", QJsonObject()},
                       {"nativeReady", false}, {"nativeErrors", QJsonArray()},
                       {"nativeItemPatchCount", 0}, {"nativeTextCount", 0}};
    try {
        const auto current = profile();
        result.insert("enabled", current.value("enabled"));
        const auto checked = build(root_, current);
        QJsonArray loaded;
        for (const auto &id : checked->session->report().loaded) loaded.append(string(id));
        QJsonObject records;
        QSet<QString> patchedTargets;
        for (const auto &patch : checked->session->registry().patches()) patchedTargets.insert(string(patch.target));
        for (const auto &kind : recordKinds())
            for (const auto *record : checked->session->registry().all(kind)) {
                if (record->owner == "base" && !patchedTargets.contains(string(record->id))) continue;
                const auto name = string(nm::Registry::localized(record->data["name"], "id"));
                records.insert(string(record->id), QJsonObject{{"type", string(kind)}, {"owner", string(record->owner)},
                    {"name", name.isEmpty() ? string(record->id) : name},
                    {"data", object(record->data)}, {"handle", record->handle}});
            }
        try {
            const auto plan = nativePlanFor(root_, *checked);
            result.insert("nativeReady", true);
            result.insert("nativeItemPatchCount", plan.value("items").toArray().size());
            result.insert("nativeTextCount", plan.value("texts").toObject().size());
        } catch (const std::exception &error) {
            result.insert("nativeErrors", QJsonArray{string(error.what())});
        }
        for (const auto &id : checked->session->report().loaded) {
            const auto mod = readMod(root_ + "/mods/" + string(id));
            if (mod.manifest.value("schema") != QJsonValue(1)) continue;
            const auto legacy = mod.manifest.value("content").toObject();
            for (auto entry = legacy.begin(); entry != legacy.end(); ++entry) records.insert(entry.key(), entry.value());
        }
        result.insert("ok", checked->ok);
        result.insert("loaded", loaded);
        result.insert("errors", QJsonArray::fromStringList(checked->errors));
        result.insert("report", string(checked->session->report().text()));
        result.insert("records", records);
    } catch (const std::exception &error) {
        result.insert("errors", QJsonArray{string(error.what())});
        result.insert("report", string(error.what()));
        result.insert("nativeErrors", QJsonArray{string(error.what())});
    }
    return result;
}

QJsonObject ModcoreLibrary::nativePlan() const {
    const auto checked = build(root_, profile());
    return nativePlanFor(root_, *checked);
}

QJsonObject ModcoreLibrary::nativeTexts() const {
    return nativePlan().value("texts").toObject();
}

} // namespace nature
