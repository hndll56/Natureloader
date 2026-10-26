#include "rom_catalog.h"
#include "core.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtEndian>
#include <archive.h>
#include <archive_entry.h>
#include <cstdio>
#include <stdexcept>

static void check(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
template<class F> static void rejects(F f) {
    bool rejected = false;
    try { f(); } catch (const std::exception &) { rejected = true; }
    check(rejected, "Expected rejection");
}
static void put32(QByteArray &bytes, int offset, quint32 value) { qToLittleEndian(value, bytes.data() + offset); }
static QByteArray image(int members = 33) {
    QByteArray iso((25 + members) * 2048, 0);
    iso.replace(32768, 7, QByteArray("\x01" "CD001\x01", 7));
    qToLittleEndian<quint16>(2048, iso.data() + 32768 + 128);
    put32(iso, 32768 + 158, 20); put32(iso, 32768 + 166, 2048);
    int cursor = 20 * 2048;
    for (const auto &record : {qMakePair(QByteArray("SLUS_011.15;1"), qMakePair(21, 4096)),
                              qMakePair(QByteArray("A_FILE.HDT;1"), qMakePair(23, (members + 1) * 4)),
                              qMakePair(QByteArray("A_FILE.BIN;1"), qMakePair(24, members * 2048))}) {
        int length = 33 + record.first.size();
        length += length % 2;
        iso[cursor] = char(length); iso[cursor + 32] = char(record.first.size());
        put32(iso, cursor + 2, record.second.first); put32(iso, cursor + 10, record.second.second);
        iso.replace(cursor + 33, record.first.size(), record.first); cursor += length;
    }
    const int exe = 21 * 2048;
    iso.replace(exe, 8, "PS-X EXE");
    put32(iso, exe + 0x10, 0x80010000); put32(iso, exe + 0x18, 0x80010000); put32(iso, exe + 0x1c, 2048);
    for (int i = 0; i <= members; ++i) put32(iso, 23 * 2048 + i * 4, i * 2048);
    return iso;
}
static QByteArray rawImage(const QByteArray &iso) {
    QByteArray raw;
    for (int i = 0; i < iso.size() / 2048; ++i) {
        QByteArray sector(2352, 0); sector[15] = 2;
        sector.replace(24, 2048, iso.mid(i * 2048, 2048)); raw += sector;
    }
    return raw;
}
static void zip(const QString &path, const QList<QPair<QString, QByteArray>> &files) {
    auto *a = archive_write_new(); archive_write_set_format_zip(a);
    const auto native = path.toStdWString();
    check(archive_write_open_filename_w(a, native.c_str()) == ARCHIVE_OK, "ZIP open");
    for (const auto &file : files) {
        auto *entry = archive_entry_new(); archive_entry_set_pathname(entry, file.first.toUtf8());
        archive_entry_set_filetype(entry, AE_IFREG); archive_entry_set_perm(entry, 0644); archive_entry_set_size(entry, file.second.size());
        check(archive_write_header(a, entry) == ARCHIVE_OK, "ZIP header");
        check(archive_write_data(a, file.second.constData(), file.second.size()) == file.second.size(), "ZIP data");
        archive_entry_free(entry);
    }
    check(archive_write_close(a) == ARCHIVE_OK, "ZIP close"); archive_write_free(a);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir tmp; check(tmp.isValid(), "Temporary directory");
        const auto root = tmp.path();
        nature::RomCatalog catalog(root + "/data");
        check(catalog.entries().isEmpty() && catalog.selected().isEmpty(), "Empty catalog");
        const auto original = image();
        nature::writeFile(root + "/example.iso", original);
        auto imported = catalog.importRom(root + "/example.iso");
        const auto id = imported.value("id").toString();
        check(imported.value("sectorSize").toInt() == 2048, "ISO sector detection");
        check(imported.value("archiveMemberCount").toInt() == 33, "Dynamic archive index");
        check(!imported.value("stockExecutable").toBool() && !imported.value("nativeReady").toBool(), "No fabricated compatibility");
        check(catalog.verify(id), "Imported image verification");
        check(nature::readFile(root + "/example.iso") == original, "Original image unchanged");
        check(catalog.importRom(root + "/example.iso").value("id").toString() == id && catalog.entries().size() == 1, "Deduplicated image");
        nature::writeFile(root + "/raw.iso", rawImage(original));
        nature::writeFile(root + "/raw.cue", "FILE \"raw.iso\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\n");
        const auto raw = catalog.importRom(root + "/raw.cue");
        const auto rawId = raw.value("id").toString();
        check(rawId != id && raw.value("sectorSize").toInt() == 2352, "Raw format detected despite ISO suffix");
        check(nature::readFile(raw.value("cuePath").toString()).contains("disc.bin"), "Normalized CUE");
        const auto goodCue = nature::readFile(raw.value("cuePath").toString());
        nature::writeFile(raw.value("cuePath").toString(), "FILE \"other.bin\" BINARY\n");
        check(!catalog.verify(rawId), "Changed CUE cannot redirect a verified image");
        nature::writeFile(raw.value("cuePath").toString(), goodCue);
        nature::RomCatalog reopened(root + "/data");
        check(reopened.selected() == rawId, "Persisted selection"); reopened.select(id);
        check(catalog.selected() == id && catalog.profilePath(id) != catalog.profilePath(rawId), "Independent profiles");
        zip(root + "/one.zip", {{"folder/disc.bin", rawImage(original)}, {"disc.cue", "ignored"}});
        check(catalog.importRom(root + "/one.zip").value("id").toString() == rawId, "ZIP import uses same identity");
        zip(root + "/two.zip", {{"first.bin", rawImage(original)}, {"second.iso", original}});
        rejects([&] { catalog.importRom(root + "/two.zip"); });
        nature::writeFile(root + "/bad.cue", "FILE \"../raw.iso\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\n");
        rejects([&] { catalog.importRom(root + "/bad.cue"); });
        nature::writeFile(root + "/mismatch.cue", "FILE \"example.iso\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\n");
        rejects([&] { catalog.importRom(root + "/mismatch.cue"); });
        auto malformed = original; put32(malformed, 23 * 2048 + 8, 1);
        nature::writeFile(root + "/bad-index.iso", malformed);
        rejects([&] { catalog.importRom(root + "/bad-index.iso"); });
        malformed = original; put32(malformed, 21 * 2048 + 0x10, 0x80020000);
        nature::writeFile(root + "/bad-entry.iso", malformed);
        rejects([&] { catalog.importRom(root + "/bad-entry.iso"); });
        check(catalog.entries().size() == 2, "Failed imports leave no catalog records");
        rejects([&] { catalog.metadata("../outside"); });
        auto changed = original; changed[0] = 1;
        nature::writeFile(imported.value("imagePath").toString(), changed);
        check(!catalog.verify(id), "Changed local copy fails digest verification");
        rejects([&] { catalog.importRom(root + "/example.iso"); });
        check(nature::readFile(root + "/example.iso") == original, "Corrupt cache does not modify source");
        if (argc > 1) {
            nature::RomCatalog real(root + "/real-roms");
            for (int i = 1; i < argc; ++i) {
                const auto source = QString::fromLocal8Bit(argv[i]);
                QFile before(source); check(before.open(QIODevice::ReadOnly), "Real ROM source");
                QCryptographicHash hashBefore(QCryptographicHash::Sha256); check(hashBefore.addData(&before), "Real ROM source hash");
                before.close();
                const auto info = real.importRom(source);
                check(real.verify(info.value("id").toString()), "Real ROM verification");
                check(!info.value("nativeReady").toBool(), "Imported ROM is not automatically playable");
                QFile after(source); check(after.open(QIODevice::ReadOnly), "Real ROM source after import");
                QCryptographicHash hashAfter(QCryptographicHash::Sha256); check(hashAfter.addData(&after), "Real ROM final hash");
                check(hashBefore.result() == hashAfter.result(), "Real ROM source remains unchanged");
                const auto report = QJsonDocument(QJsonObject{{"name", info.value("name")}, {"id", info.value("id")},
                    {"sectorSize", info.value("sectorSize")}, {"stockExecutable", info.value("stockExecutable")},
                    {"archiveMemberCount", info.value("archiveMemberCount")}}).toJson(QJsonDocument::Compact);
                std::puts(report.constData());
            }
        }
        std::puts("PASS: ROM catalog format sniffing, dynamic index, CUE/ZIP import, identity, isolated profiles, persistence, validation and immutable source.");
        return 0;
    } catch (const std::exception &e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
}
