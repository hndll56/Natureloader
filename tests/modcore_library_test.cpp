#include "core.h"
#include "modcore_library.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <archive.h>
#include <archive_entry.h>
#include <cstdio>
#include <stdexcept>

namespace {
void check(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
template<class F> void rejectsAt(int line, F action) {
    bool rejected = false;
    try { action(); } catch (const std::exception &) { rejected = true; }
    if (!rejected) throw std::runtime_error("Expected rejection at line " + std::to_string(line));
}
#define rejects(...) rejectsAt(__LINE__, __VA_ARGS__)
QByteArray json(const QJsonObject &object) { return QJsonDocument(object).toJson(); }
QJsonObject manifest(const QString &id, const QJsonObject &extra = {}) {
    QJsonObject result{{"schema", 2}, {"id", id}, {"name", id}, {"version", "1.0.0"},
                       {"game", "SLUS_011.15"}, {"api", ">=1.0 <2"}};
    for (auto it = extra.begin(); it != extra.end(); ++it) result.insert(it.key(), it.value());
    return result;
}
void zip(const QString &path, const QList<QPair<QString, QByteArray>> &files, bool link = false) {
    archive *output = archive_write_new();
    archive_write_set_format_zip(output);
    check(archive_write_open_filename(output, QFile::encodeName(path)) == ARCHIVE_OK, "Open ZIP");
    for (const auto &file : files) {
        archive_entry *entry = archive_entry_new();
        const bool directory = file.first.endsWith('/');
        archive_entry_set_pathname(entry, file.first.toUtf8());
        archive_entry_set_filetype(entry, link ? AE_IFLNK : (directory ? AE_IFDIR : AE_IFREG));
        archive_entry_set_perm(entry, directory ? 0755 : 0644);
        archive_entry_set_size(entry, (directory || link) ? 0 : file.second.size());
        if (link) archive_entry_set_symlink(entry, "../outside");
        check(archive_write_header(output, entry) == ARCHIVE_OK, "Write ZIP header");
        if (!directory && !link)
            check(archive_write_data(output, file.second.constData(), file.second.size()) == file.second.size(), "Write ZIP data");
        archive_entry_free(entry);
    }
    check(archive_write_close(output) == ARCHIVE_OK, "Close ZIP");
    archive_write_free(output);
    // The ZIP writer normalizes backslashes. Restore literal member names for
    // the traversal fixtures before testing the reader's normalized paths.
    for (const auto &file : files) if (file.first.contains('\\')) {
        auto bytes = nature::readFile(path);
        const auto normalized = QString(file.first).replace('\\', '/').toUtf8();
        bytes.replace(normalized, file.first.toUtf8());
        nature::writeFile(path, bytes);
    }
}
QJsonObject textMod(const QString &id, const QString &value) {
    return manifest(id, {{"texts", QJsonObject{{"messages.selectAMemoryCardSlot", value}}}});
}
QJsonObject itemMod(const QString &id, const QJsonObject &extra = {}) {
    auto result = manifest(id, extra);
    result.insert("content", QJsonObject{{"item", QJsonObject{{"new-item",
                  QJsonObject{{"name", "New Item"}, {"sell_price", 10}}}}}});
    return result;
}
int handle(const nature::ModcoreLibrary &library, const QString &id) {
    return library.inspect().value("records").toObject().value(id).toObject().value("handle").toInt(-1);
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temporary;
        check(temporary.isValid(), "Temporary workspace");
        const QString root = temporary.path() + "/data";
        const QString package = temporary.path() + "/package.zip";
        nature::ModcoreLibrary library(root);
        check(library.mods().isEmpty(), "Initial library empty");
        check(library.inspect().value("ok").toBool(), "Empty session valid");
        check(!library.inspect().value("contentGameplayReady").toBool(), "Authoring is not gameplay");

        // A dependent can be installed before its prerequisite, but cannot enable yet.
        zip(package, {{"mod.json", json(itemMod("dependent", {{"depends", QJsonObject{{"foundation", "^1.0"}}}}))}});
        check(!library.installZip(package).enabled, "Installed disabled");
        rejects([&] { library.setEnabled("dependent", true); });
        check(!QFileInfo::exists(root + "/profile-v2.json"), "Failed enable does not write profile");
        zip(package, {{"mod.json", json(itemMod("foundation"))}});
        library.installZip(package);
        library.setEnabled("foundation", true);
        library.setEnabled("dependent", true);
        auto initial = library.inspect();
        check(initial.value("ok").toBool(), "Dependency session valid");
        check(initial.value("loaded").toArray() == QJsonArray({"foundation", "dependent"}), "Dependency load order");
        const auto firstHandle = handle(library, "foundation:new-item");
        const auto secondHandle = handle(library, "dependent:new-item");
        check(firstHandle >= 512 && secondHandle > firstHandle, "New handles allocated");
        const auto unchanged = nature::readFile(root + "/profile-v2.json");
        rejects([&] { library.setEnabled("foundation", false); });
        rejects([&] { library.removeMod("foundation"); });
        check(nature::readFile(root + "/profile-v2.json") == unchanged, "Rejected profile stays byte-identical");
        check(QFileInfo::exists(root + "/mods-v2/foundation/mod.json"), "Rejected removal retains files");
        library.moveMod("dependent", -1);
        check(handle(library, "foundation:new-item") == firstHandle && handle(library, "dependent:new-item") == secondHandle,
              "Reordering preserves authoring handles");
        check(nature::ModcoreLibrary(root).inspect().value("ok").toBool(), "Restart retains valid profile");
        check(handle(nature::ModcoreLibrary(root), "foundation:new-item") == firstHandle, "Restart retains handle");
        rejects([&] { library.nativeTexts(); });
        library.setEnabled("dependent", false);
        const auto recovery = library.removeMod("foundation");
        check(QFileInfo::exists(recovery + "/mod.json"), "Removed files retained in recovery");
        check(!QFileInfo::exists(root + "/mods-v2/foundation"), "Removed installation absent");
        zip(package, {{"mod.json", json(itemMod("foundation"))}});
        library.installZip(package);
        library.setEnabled("foundation", true);
        check(handle(library, "foundation:new-item") == firstHandle, "Reinstalled mod recovers prior handle");
        library.setEnabled("foundation", false);

        // Text-only schema 2 packs can reach the existing native text exporter.
        zip(package, {{"mod.json", json(textMod("first-text", "First"))}});
        library.installZip(package);
        library.setEnabled("first-text", true);
        zip(package, {{"mod.json", json(textMod("last-text", "Last"))}});
        library.installZip(package);
        library.setEnabled("last-text", true);
        check(library.nativeTexts().value("messages.selectAMemoryCardSlot") == QJsonValue("Last"), "Last resolved text wins");
        library.moveMod("last-text", -1);
        check(library.nativeTexts().value("messages.selectAMemoryCardSlot") == QJsonValue("First"), "Text priority follows profile");
        rejects([&] { library.installZip(package); });

        // Nested directories are supported without granting scripts or native code execution.
        zip(package, {{"mod.json", json(manifest("scripted", {{"scripts", QJsonArray{"scripts/main.json"}},
                      {"hooks", QJsonArray{QJsonObject{{"on", "game.start"}, {"handler", "start"}}}}}))},
                      {"scripts/", {}}, {"scripts/main.json", "{\"handlers\":{\"start\":[{\"do\":\"set_result\",\"key\":\"ran\",\"value\":true}]}}"}});
        library.installZip(package);
        library.setEnabled("scripted", true);
        check(library.inspect().value("ok").toBool(), "Script parsed for authoring without execution");
        rejects([&] { library.nativeTexts(); });
        library.setEnabled("scripted", false);

        // An asset-only pack cannot silently boot as a text-only mod.
        zip(package, {{"mod.json", json(manifest("assets-only"))}, {"assets/map.dat", "map placeholder"}});
        library.installZip(package);
        library.setEnabled("assets-only", true);
        rejects([&] { library.nativeTexts(); });
        library.setEnabled("assets-only", false);

        zip(package, {{"mod.json", json(manifest("bad-key", {{"texts", QJsonObject{{"not-a-real-key", "Bad"}}}}))}});
        library.installZip(package);
        rejects([&] { library.setEnabled("bad-key", true); });
        zip(package, {{"mod.json", json(textMod("long-text", QString(16385, 'x')))}});
        library.installZip(package);
        rejects([&] { library.setEnabled("long-text", true); });

        // Unsafe ZIPs leave no installed package or partially written profile.
        const auto beforeBadZip = nature::readFile(root + "/profile-v2.json");
        for (const auto &bad : {"../escape", "/absolute", "C:/file", "CON", "folder\\..\\escape", "a/../b"}) {
            zip(package, {{"mod.json", json(manifest("unsafe"))}, {bad, "x"}});
            try { rejects([&] { library.installZip(package); }); }
            catch (const std::exception &error) { throw std::runtime_error(std::string(bad) + ": " + error.what()); }
        }
        zip(package, {{"mod.json", json(manifest("unsafe"))}, {"Assets/A", "a"}, {"assets/b", "b"}});
        rejects([&] { library.installZip(package); });
        zip(package, {{"mod.json", json(manifest("unsafe"))}, {"file", "x"}, {"file/child", "x"}});
        rejects([&] { library.installZip(package); });
        zip(package, {{"mod.json", "link"}}, true);
        rejects([&] { library.installZip(package); });
        auto fractionalSchema = manifest("fractional");
        fractionalSchema.insert("schema", 2.5);
        zip(package, {{"mod.json", json(fractionalSchema)}});
        rejects([&] { library.installZip(package); });
        zip(package, {{"mod.json", json(manifest("unsafe"))}, {"large", QByteArray(32 * 1024 * 1024 + 1, 'x')}});
        rejects([&] { library.installZip(package); });
        check(!QFileInfo::exists(root + "/mods-v2/unsafe"), "Invalid ZIP installation is atomic");
        check(nature::readFile(root + "/profile-v2.json") == beforeBadZip, "Invalid ZIP leaves profile unchanged");

        // Profile growth must not create a file that readObject can no longer load.
        zip(package, {{"mod.json", json(itemMod("profile-growth"))}});
        library.installZip(package);
        const auto smallProfile = nature::readFile(root + "/profile-v2.json");
        auto largeProfile = nature::readObject(root + "/profile-v2.json");
        auto state = largeProfile.value("authoringSave").toObject();
        state.insert("flags", QJsonObject{{"test:filler", ""}});
        largeProfile.insert("authoringSave", state);
        const int fillerSize = 2 * 1024 * 1024 - json(largeProfile).size() - 4;
        state.insert("flags", QJsonObject{{"test:filler", QString(fillerSize, 'x')}});
        largeProfile.insert("authoringSave", state);
        const auto largeBytes = json(largeProfile);
        check(largeBytes.size() < 2 * 1024 * 1024, "Test profile initially readable");
        nature::writeFile(root + "/profile-v2.json", largeBytes);
        rejects([&] { library.setEnabled("profile-growth", true); });
        check(nature::readFile(root + "/profile-v2.json") == largeBytes, "Over-limit profile preserves previous state");
        nature::writeFile(root + "/profile-v2.json", smallProfile);

        // Disk changes and corrupt authoring state produce a report rather than silent success.
        nature::writeFile(root + "/mods-v2/first-text/mod.json", "bad JSON");
        check(!library.inspect().value("ok").toBool(), "Changed enabled manifest reported");
        rejects([&] { library.nativeTexts(); });
        bool brokenListed = false, healthyListed = false;
        for (const auto &mod : library.mods()) {
            if (mod.id == "first-text") brokenListed = mod.enabled && !mod.error.isEmpty();
            if (mod.id == "last-text") healthyListed = mod.enabled && mod.error.isEmpty();
        }
        check(brokenListed && healthyListed, "Broken manifest does not hide other mods");
        library.setEnabled("first-text", false);
        check(library.inspect().value("ok").toBool(), "Broken active mod can be disabled");
        const auto brokenRecovery = library.removeMod("first-text");
        check(nature::readFile(brokenRecovery + "/mod.json") == QByteArray("bad JSON"), "Broken package recoverable");
        check(library.nativeTexts().value("messages.selectAMemoryCardSlot") == QJsonValue("Last"), "Healthy text mod still active");
        nature::writeFile(root + "/profile-v2.json", "bad JSON");
        check(!library.inspect().value("ok").toBool(), "Corrupt profile reported");
        rejects([&] { library.setEnabled("last-text", false); });
        check(nature::readFile(root + "/profile-v2.json") == QByteArray("bad JSON"), "Corrupt profile not overwritten");
        std::puts("modcore library tests passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "modcore library tests failed: %s\n", error.what());
        return 1;
    }
}
