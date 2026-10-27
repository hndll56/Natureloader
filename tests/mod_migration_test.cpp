#include "core.h"
#include "modcore_library.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cstdio>
#include <stdexcept>

static void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
static void put(const QString &path, const QJsonObject &object) {
    check(QDir().mkpath(QFileInfo(path).path()), "Create fixture directory");
    nature::writeFile(path, QJsonDocument(object).toJson());
}
template<class F> static void rejects(F action) {
    bool failed = false;
    try { action(); } catch (const std::exception &) { failed = true; }
    check(failed, "Expected transactional rejection");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir tmp;
        check(tmp.isValid(), "Temporary root");
        const auto root = tmp.path() + "/data";
        const QJsonObject legacy{{"schema", 1}, {"id", "legacy"}, {"name", "Legacy"}, {"version", "1.0.0"},
            {"game", nature::GameId}, {"texts", QJsonObject{{"messages.selectAMemoryCardSlot", "Legacy"}}}};
        const QJsonObject modern{{"schema", 2}, {"id", "modern"}, {"name", "Modern"}, {"version", "1.0.0"},
            {"game", nature::GameId}, {"api", "^1.0"}, {"depends", QJsonObject{{"legacy", "^1.0"}}},
            {"content", QJsonObject{{"item", QJsonObject{{"new-item", QJsonObject{{"name", "New"}, {"sell_price", 5}}}}}}}};
        const QJsonObject state{{"format", 1}, {"base_save_hash", ""}, {"mods", QJsonArray()},
            {"idtable", QJsonObject{{"ids", QJsonObject{{"item", QJsonObject{{"modern:new-item", 777}}}}},
                                   {"next", QJsonObject{{"item", 778}}}}},
            {"mod_data", QJsonObject{{"modern", QJsonObject{{"kept", 42}}}}},
            {"flags", QJsonObject{{"modern:kept", true}}}};
        put(root + "/mods/legacy/mod.json", legacy);
        put(root + "/mods-v2/modern/mod.json", modern);
        put(root + "/profile.json", {{"schema", 1}, {"enabled", QJsonArray{"legacy"}}});
        put(root + "/profile-v2.json", {{"schema", 2}, {"enabled", QJsonArray{"modern"}}, {"authoringSave", state}});
        const auto oldProfile = nature::readFile(root + "/profile.json");
        const auto oldV2 = nature::readFile(root + "/profile-v2.json");
        const auto oldPack = nature::readFile(root + "/mods-v2/modern/mod.json");
        nature::Library library(root);
        auto report = library.status();
        check(report.value("ok").toBool(), "Merged profile validates");
        check(library.mods().size() == 2, "Both formats appear in the ordinary mod list");
        check(report.value("loaded").toArray() == QJsonArray({"legacy", "modern"}), "Shared resolver follows cross-format dependency");
        check(library.content().value("modern:new-item").toObject().value("handle").toInt() == 777, "Existing authoring handles preserved");
        const auto profile = nature::readObject(root + "/profile.json");
        check(profile.value("unified").toBool(), "Migration committed once");
        const auto backup = profile.value("migrationBackup").toString();
        check(nature::readFile(backup + "/profile.json") == oldProfile, "Legacy profile backed up byte-identically");
        check(nature::readFile(backup + "/profile-v2.json") == oldV2, "V2 profile backed up byte-identically");
        check(nature::readFile(root + "/profile-v2.json") == oldV2, "Original v2 profile retained");
        check(nature::readFile(root + "/mods-v2/modern/mod.json") == oldPack, "Original v2 pack retained");
        check(nature::readFile(root + "/mods/modern/mod.json") == oldPack, "Unified pack copied without source edits");
        const auto mergedBytes = nature::readFile(root + "/profile.json");
        nature::Library reopened(root);
        check(nature::readFile(root + "/profile.json") == mergedBytes, "Migration retry is idempotent");
        rejects([&] { reopened.setEnabled("legacy", false); });
        check(nature::readFile(root + "/profile.json") == mergedBytes, "Invalid cross-format activation preserves profile");
        reopened.moveMod("modern", -1);
        check(reopened.status().value("loaded").toArray() == QJsonArray({"legacy", "modern"}), "Dependency order beats requested order");
        reopened.setEnabled("modern", false);
        check(nature::ModcoreLibrary(root).nativeTexts().value("messages.selectAMemoryCardSlot") == QJsonValue("Legacy"), "Ordinary text mod uses modcore native pipeline");
        const auto saved = nature::readObject(root + "/profile.json").value("authoringSave").toObject();
        check(saved.value("flags").toObject().value("modern:kept").toBool(), "Missing-mod flags retained");
        check(saved.value("mod_data").toObject().value("modern").toObject().value("kept").toInt() == 42, "Missing-mod data retained");

        // A colliding ID never overwrites either original package or the old profile.
        const auto collision = tmp.path() + "/collision";
        put(collision + "/mods/legacy/mod.json", legacy);
        auto different = legacy;
        different.insert("name", "Different");
        put(collision + "/mods-v2/legacy/mod.json", different);
        put(collision + "/profile.json", {{"schema", 1}, {"enabled", QJsonArray{"legacy"}}});
        const auto before = nature::readFile(collision + "/profile.json");
        rejects([&] { nature::Library conflicting(collision); });
        check(nature::readFile(collision + "/profile.json") == before, "Collision leaves profile unchanged");
        check(nature::readObject(collision + "/mods/legacy/mod.json") == legacy, "Collision leaves installed mod unchanged");
        check(nature::readObject(collision + "/mods-v2/legacy/mod.json") == different, "Collision leaves old v2 mod unchanged");
        std::puts("unified migration tests passed");
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "unified migration tests failed: %s\n", e.what());
        return 1;
    }
}
