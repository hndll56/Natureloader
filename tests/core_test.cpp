#include "core.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtEndian>
#include <archive.h>
#include <archive_entry.h>
#include <cstdio>
#include <stdexcept>

void check(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
template<class F> void rejects(F f) {
    bool rejected = false;
    try { f(); } catch (const std::exception &) { rejected = true; }
    check(rejected, "Expected rejection");
}
void zip(const QString &path, const QList<QPair<QString, QByteArray>> &files, bool symlink = false) {
    auto *a = archive_write_new();
    archive_write_set_format_zip(a);
    check(archive_write_open_filename(a, QFile::encodeName(path)) == ARCHIVE_OK, "ZIP open");
    for (const auto &file : files) {
        auto *entry = archive_entry_new();
        archive_entry_set_pathname(entry, file.first.toUtf8());
        archive_entry_set_filetype(entry, symlink ? AE_IFLNK : AE_IFREG);
        archive_entry_set_perm(entry, 0644);
        if (symlink) archive_entry_set_symlink(entry, "../outside");
        archive_entry_set_size(entry, symlink ? 0 : file.second.size());
        check(archive_write_header(a, entry) == ARCHIVE_OK, "ZIP header");
        if (!symlink) check(archive_write_data(a, file.second.constData(), file.second.size()) == file.second.size(), "ZIP data");
        archive_entry_free(entry);
    }
    check(archive_write_close(a) == ARCHIVE_OK, "ZIP close");
    archive_write_free(a);
}
QByteArray manifest(const QString &id, const QString &text) {
    return QJsonDocument(QJsonObject{{"schema", 1}, {"id", id}, {"name", id}, {"version", "1"},
        {"game", nature::GameId}, {"texts", QJsonObject{{"messages.promptYesNo", text}}}}).toJson();
}
void put32(QByteArray &b, int offset, quint32 value) { qToLittleEndian(value, b.data() + offset); }
QByteArray syntheticIso() {
    QByteArray iso(24 * 2048, 0);
    iso.replace(16 * 2048, 7, QByteArray("\x01" "CD001\x01", 7));
    qToLittleEndian<quint16>(2048, iso.data() + 16 * 2048 + 128);
    put32(iso, 16 * 2048 + 158, 20); put32(iso, 16 * 2048 + 166, 2048);
    const int dir = 20 * 2048;
    const QByteArray name = "TEST.BIN;1";
    iso[dir] = char(44); iso[dir + 32] = char(name.size());
    put32(iso, dir + 2, 21); put32(iso, dir + 10, 4096);
    iso.replace(dir + 33, name.size(), name);
    iso.replace(21 * 2048, 4096, QByteArray(2048, 'A') + QByteArray(2048, 'B'));
    return iso;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir tmp;
        check(tmp.isValid(), "Temporary directory");
        const QString root = tmp.path();
        for (const QString &bad : {"../escape", "/absolute", "C:/file", "a\\b", "a//b", "CON", "nul.txt", "a/..", "a.", "COM1", "LPT9.txt", "mod.json\n"})
            rejects([&] { nature::safePath(bad); });
        check(nature::safePath("mod.json") == "mod.json", "Safe path");
        check(!nature::textLayout().isEmpty(), "Embedded layout");
        check(nature::decodeText(QByteArray::fromHex("00000100fd0f2000feffffffffff")) == "AB a\n", "Glyph decoding");
        check(nature::decodeText(QByteArray::fromHex("7a40ffff")) == "{RAW:407A}", "Unknown control is preserved");
        rejects([] { nature::decodeText("x"); });
        auto iso = syntheticIso();
        nature::writeFile(root + "/disc.iso", iso);
        nature::Disc disc(root + "/disc.iso");
        check(disc.files().value("TEST.BIN") == qMakePair(quint32(21), quint32(4096)), "ISO directory");
        check(disc.read(22 * 2048 - 2, 4) == "AABB", "Cross-sector ISO read");
        rejects([&] { disc.read(24 * 2048, 1); });
        rejects([&] { disc.read(-1, 1); });
        QByteArray raw;
        for (int i = 0; i < iso.size() / 2048; ++i) {
            QByteArray sector(2352, 0); sector[15] = 2;
            sector.replace(24, 2048, iso.mid(i * 2048, 2048)); raw += sector;
        }
        nature::writeFile(root + "/disc.bin", raw);
        nature::Disc rawDisc(root + "/disc.bin");
        check(rawDisc.read(22 * 2048 - 2, 4) == "AABB", "Cross-sector BIN read");
        raw[21 * 2352 + 18] = 0x20;
        nature::writeFile(root + "/form2.bin", raw);
        rejects([&] { nature::Disc(root + "/form2.bin").read(21 * 2048, 1); });
        iso[20 * 2048] = 10;
        nature::writeFile(root + "/bad.iso", iso);
        rejects([&] { nature::Disc(root + "/bad.iso").files(); });

        nature::Library lib(root + "/library");
        check(!lib.hasGame(), "No fabricated game readiness");
        rejects([&] { lib.importGame(root + "/disc.iso"); });
        check(!QFile::exists(lib.root() + "/base"), "Failed import is atomic");
        rejects([&] { lib.text("messages.promptYesNo"); });
        zip(root + "/good.zip", {{"mod.json", manifest("alpha", "First")}});
        check(lib.installMod(root + "/good.zip").id == "alpha", "Install valid mod");
        check(!lib.mods()[0].enabled, "Install disabled by default");
        rejects([&] { lib.installMod(root + "/good.zip"); });
        lib.setEnabled("alpha", true);
        zip(root + "/beta.zip", {{"mod.json", manifest("beta", "Second")}});
        lib.installMod(root + "/beta.zip"); lib.setEnabled("beta", true);
        check(lib.conflicts().size() == 1 && lib.conflicts()[0].endsWith("winner: beta"), "Conflict and priority");
        lib.moveMod("beta", -1);
        check(lib.conflicts()[0].endsWith("winner: alpha"), "Reordered winner");
        nature::Library reopened(lib.root());
        check(reopened.mods()[0].id == "beta", "Persisted order");
        reopened.setEnabled("alpha", false);
        check(reopened.conflicts().isEmpty(), "Disabling removes conflict");
        rejects([&] { lib.setEnabled("missing", true); });
        zip(root + "/traversal.zip", {{"../mod.json", manifest("escape", "x")}});
        rejects([&] { lib.installMod(root + "/traversal.zip"); });
        zip(root + "/symlink.zip", {{"mod.json", {}}}, true);
        rejects([&] { lib.installMod(root + "/symlink.zip"); });
        zip(root + "/duplicate.zip", {{"mod.json", manifest("other", "x")}, {"mod.json", manifest("other", "y")}});
        rejects([&] { lib.installMod(root + "/duplicate.zip"); });
        zip(root + "/bad-json.zip", {{"mod.json", "[]"}});
        rejects([&] { lib.installMod(root + "/bad-json.zip"); });
        auto invalid = QJsonDocument::fromJson(manifest("other", "x")).object();
        invalid.insert("scripts", "evil.dll");
        zip(root + "/scripts.zip", {{"mod.json", QJsonDocument(invalid).toJson()}});
        rejects([&] { lib.installMod(root + "/scripts.zip"); });
        invalid.remove("scripts"); invalid.insert("texts", QJsonObject{{"invented.key", "x"}});
        zip(root + "/key.zip", {{"mod.json", QJsonDocument(invalid).toJson()}});
        rejects([&] { lib.installMod(root + "/key.zip"); });
        zip(root + "/large.zip", {{"mod.json", QByteArray(1024 * 1024 + 1, ' ')}});
        rejects([&] { lib.installMod(root + "/large.zip"); });
        check(lib.mods().size() == 2, "Rejected installs leave no mod directories");
        const auto recovery = lib.removeMod("beta");
        check(QFile::exists(recovery + "/mod.json"), "Removed mod remains recoverable");
        check(lib.mods().size() == 1 && lib.status().value("enabled").toArray().isEmpty(), "Active mod removed from profile");
        rejects([&] { lib.removeMod("../alpha"); });
        rejects([&] { lib.removeMod("missing"); });
        // Content-only packages share installation/profile rules with existing text mods.
        nature::Library contentLib(root + "/content-library");
        const QJsonObject turnip{{"type", "item"}, {"name", "Blue Turnip"},
                                 {"data", QJsonObject{{"sellPrice", 120}}}};
        QJsonObject contentManifest{{"schema", 1}, {"id", "hello-crops"}, {"name", "Crops"},
            {"version", "1"}, {"game", nature::GameId},
            {"content", QJsonObject{{"hello-crops:blue-turnip", turnip}}}};
        auto installContent = [&](const QJsonObject &object) {
            zip(root + "/content.zip", {{"mod.json", QJsonDocument(object).toJson()}});
            return contentLib.installMod(root + "/content.zip");
        };
        check(installContent(contentManifest).content.size() == 1, "Content-only package installs");
        check(contentLib.content().isEmpty(), "Content starts disabled");
        contentLib.setEnabled("hello-crops", true);
        check(contentLib.content().value("hello-crops:blue-turnip") == turnip, "Enabled data preserved exactly");
        check(contentLib.status().value("contentCount").toInt() == 1 &&
              !contentLib.status().value("contentGameplayReady").toBool(), "Content count without gameplay claim");
        nature::Library reopenedContent(contentLib.root());
        check(reopenedContent.content() == contentLib.content(), "Content profile persists");
        reopenedContent.setEnabled("hello-crops", false);
        check(contentLib.content().isEmpty(), "Disabled content disappears");
        contentLib.setEnabled("hello-crops", true);
        auto otherContent = contentManifest;
        otherContent.insert("id", "other-crops");
        rejects([&] { installContent(otherContent); }); // Foreign namespace cannot replace existing records.
        otherContent.insert("content", QJsonObject{{"other-crops:blue-turnip", turnip}});
        installContent(otherContent);
        contentLib.setEnabled("other-crops", true);
        check(contentLib.content().size() == 2 && contentLib.conflicts().isEmpty(), "Namespaces isolate equal local IDs");
        const auto beforeMove = contentLib.content();
        contentLib.moveMod("other-crops", -1);
        check(contentLib.content() == beforeMove, "Registry deterministic across independent mod ordering");
        auto rejected = contentManifest;
        rejected.insert("id", "invalid-crops");
        auto rejectRecord = [&](const QString &id, const QJsonValue &value) {
            rejected.insert("content", QJsonObject{{id, value}});
            rejects([&] { installContent(rejected); });
        };
        rejectRecord("invalid-crops:../escape", turnip);
        rejectRecord("invalid-crops:blue:turnip", turnip);
        rejectRecord("invalid-crops:blue-turnip\n", turnip);
        rejectRecord("invalid-crops:blue-turnip", "not-an-object");
        auto record = turnip; record.insert("type", "executable");
        rejectRecord("invalid-crops:blue-turnip", record);
        record = turnip; record.insert("name", 123);
        rejectRecord("invalid-crops:blue-turnip", record);
        record = turnip; record.insert("data", QJsonArray{1, 2});
        rejectRecord("invalid-crops:blue-turnip", record);
        record = turnip; record.insert("script", "run.lua");
        rejectRecord("invalid-crops:blue-turnip", record);
        record = turnip; record.insert("data", QJsonObject{{"description", QString(65536, 'x')}});
        rejectRecord("invalid-crops:blue-turnip", record);
        rejected.insert("content", QJsonArray{});
        rejects([&] { installContent(rejected); });
        rejected.insert("content", QJsonObject{});
        rejects([&] { installContent(rejected); });
        QJsonObject many;
        for (int i = 0; i < 1025; ++i) many.insert("invalid-crops:item-" + QString::number(i), turnip);
        rejected.insert("content", many);
        rejects([&] { installContent(rejected); });
        check(contentLib.mods().size() == 2, "Invalid content leaves installed data untouched");
        contentLib.removeMod("hello-crops");
        check(contentLib.content().size() == 1 && !contentLib.content().contains("hello-crops:blue-turnip"),
              "Removing content mod updates registry");
        std::puts("PASS: disc parsing, bounds, text codec, atomic import failure, ZIP validation, mod lifecycle, persisted priority, conflicts, content namespaces and validation.");
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1;
    }
}
