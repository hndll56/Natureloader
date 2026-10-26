#include "rom_catalog.h"
#include "core.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtEndian>
#include <archive.h>
#include <archive_entry.h>
#include <memory>
#include <stdexcept>

namespace nature {
namespace {
constexpr qint64 MaxDisc = 800LL * 1024 * 1024;
[[noreturn]] void fail(const QString &message) { throw std::runtime_error(message.toStdString()); }
QString sha256(const QByteArray &data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}
QString hashFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaxDisc) fail("Cannot hash ROM: " + path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) fail("Incomplete ROM read: " + path);
    return QString::fromLatin1(hash.result().toHex());
}
quint32 word(const QByteArray &data, qsizetype offset) {
    if (offset < 0 || offset + 4 > data.size()) fail("Truncated ROM metadata.");
    return qFromLittleEndian<quint32>(data.constData() + offset);
}
QString cueImage(const QString &source) {
    const auto cue = QString::fromUtf8(readFile(source, 65536));
    const auto flags = QRegularExpression::CaseInsensitiveOption;
    QRegularExpression file("\\bFILE\\s+\"([^\"]+)\"\\s+BINARY", flags);
    auto matches = file.globalMatch(cue);
    if (!matches.hasNext()) fail("CUE must reference one quoted BINARY file.");
    const auto name = matches.next().captured(1);
    if (matches.hasNext() || cue.count(QRegularExpression("\\bFILE\\b", flags)) != 1 ||
        cue.count(QRegularExpression("\\bTRACK\\b", flags)) != 1 ||
        !cue.contains(QRegularExpression("\\bTRACK\\s+01\\s+MODE2/2352\\b", flags)) ||
        !cue.contains(QRegularExpression("\\bINDEX\\s+01\\s+00:00:00\\b", flags)))
        fail("Only one MODE2/2352 track starting at 00:00:00 is supported.");
    if (name.isEmpty() || name == "." || name == ".." || name.contains('/') || name.contains('\\') || name.contains(':'))
        fail("CUE image must be a file beside the CUE.");
    return QFileInfo(source).dir().filePath(name);
}
void copyImage(const QString &source, const QString &destination) {
    QFile input(source), output(destination);
    if (!input.open(QIODevice::ReadOnly) || input.size() < 32768 || input.size() > MaxDisc)
        fail("ROM is missing or outside the 32 KiB–800 MiB size range.");
    if (!output.open(QIODevice::WriteOnly)) fail("Cannot create local ROM copy.");
    qint64 total = 0;
    while (!input.atEnd()) {
        const auto bytes = input.read(1024 * 1024);
        total += bytes.size();
        if (bytes.isEmpty() || total > MaxDisc || output.write(bytes) != bytes.size()) fail("ROM copy failed.");
    }
    if (input.error() != QFile::NoError || total != input.size() || !output.flush()) fail("Incomplete ROM copy.");
}
void unpackImage(const QString &source, const QString &destination) {
    std::unique_ptr<archive, decltype(&archive_read_free)> archive(archive_read_new(), archive_read_free);
    archive_read_support_format_zip(archive.get());
    const auto path = source.toStdWString();
    if (archive_read_open_filename_w(archive.get(), path.c_str(), 65536) != ARCHIVE_OK) fail("Cannot open ROM ZIP.");
    archive_entry *entry = nullptr;
    int code = 0, count = 0, images = 0;
    while ((code = archive_read_next_header(archive.get(), &entry)) == ARCHIVE_OK) {
        if (++count > 100) fail("ROM ZIP has too many members.");
        const auto name = QString::fromUtf8(archive_entry_pathname(entry));
        if (!name.endsWith(".bin", Qt::CaseInsensitive) && !name.endsWith(".iso", Qt::CaseInsensitive) &&
            !name.endsWith(".img", Qt::CaseInsensitive)) continue;
        if (++images != 1) fail("ROM ZIP must contain exactly one BIN, ISO or IMG.");
        if (archive_entry_filetype(entry) != AE_IFREG || archive_entry_symlink(entry) || archive_entry_hardlink(entry))
            fail("ROM ZIP image must be a regular file.");
        const auto size = archive_entry_size(entry);
        if (size < 32768 || size > MaxDisc) fail("Unsupported ROM ZIP image size.");
        QFile output(destination);
        if (!output.open(QIODevice::WriteOnly)) fail("Cannot create local ROM copy.");
        char buffer[65536];
        qint64 total = 0;
        la_ssize_t n;
        while ((n = archive_read_data(archive.get(), buffer, sizeof buffer)) > 0) {
            total += n;
            if (total > MaxDisc || output.write(buffer, n) != n) fail("ROM ZIP exceeds limit or write failed.");
        }
        if (n < 0 || total != size || !output.flush()) fail("Corrupt ROM ZIP.");
    }
    if (code != ARCHIVE_EOF || images != 1) fail("Corrupt ROM ZIP or missing disc image.");
}
int sectorSize(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) fail("Cannot inspect disc format.");
    for (int sector : {2352, 2048}) {
        file.seek(16LL * sector + (sector == 2352 ? 24 : 0));
        if (file.read(7) == QByteArray("\x01" "CD001\x01", 7) && file.size() % sector == 0) return sector;
    }
    fail("Expected Mode 2/2352 image or 2048-byte ISO, regardless of filename extension.");
}
}

RomCatalog::RomCatalog(QString root) : root_(QDir(root).absolutePath()) {
    if (!QDir().mkpath(root_ + "/roms")) fail("Cannot create ROM catalog.");
}
QString RomCatalog::directory(const QString &id) const {
    static const QRegularExpression digest("^[0-9a-f]{64}$");
    if (!digest.match(id).hasMatch()) fail("Invalid ROM ID.");
    return root_ + "/roms/" + id;
}
QJsonObject RomCatalog::importRom(const QString &source) {
    QTemporaryDir staging(root_ + "/roms/.import-XXXXXX");
    if (!staging.isValid()) fail("Cannot create ROM import staging directory.");
    QString image = staging.path() + "/disc.image";
    if (source.endsWith(".zip", Qt::CaseInsensitive)) unpackImage(source, image);
    else copyImage(source.endsWith(".cue", Qt::CaseInsensitive) ? cueImage(source) : source, image);
    const int sector = sectorSize(image);
    if (source.endsWith(".cue", Qt::CaseInsensitive) && sector != 2352) fail("CUE declares MODE2/2352 but image uses another format.");
    const QString id = hashFile(image), destination = directory(id);
    if (QFileInfo::exists(destination)) {
        if (!verify(id)) fail("Existing catalog copy failed verification.");
        select(id);
        return metadata(id);
    }
    const Disc disc(image);
    const auto files = disc.files();
    for (const auto &name : {QString(GameId), QString("A_FILE.HDT"), QString("A_FILE.BIN")})
        if (!files.contains(name)) fail("Not a supported BTN-family disc: missing " + name);
    const auto [exeLba, exeSize] = files.value(GameId);
    if (exeSize < 2048 || exeSize > 2 * 1024 * 1024 + 2048) fail("Unsupported PS-X executable size.");
    const auto exe = disc.read(qint64(exeLba) * 2048, exeSize);
    if (exe.left(8) != "PS-X EXE") fail("Invalid PS-X executable signature.");
    const auto entry = word(exe, 0x10), load = word(exe, 0x18), size = word(exe, 0x1c);
    const quint64 physicalLoad = load & 0x1fffffff, physicalEntry = entry & 0x1fffffff;
    if (size == 0 || quint64(size) + 2048 > quint64(exe.size()) || physicalLoad + size > 2 * 1024 * 1024 ||
        physicalEntry < physicalLoad || physicalEntry >= physicalLoad + size || (entry & 3)) fail("Invalid PS-X executable address range.");
    const auto [indexLba, indexSize] = files.value("A_FILE.HDT");
    const auto [archiveLba, archiveSize] = files.value("A_FILE.BIN");
    if (indexSize < 8 || indexSize > 65536 || indexSize % 4) fail("Invalid archive index size.");
    const auto index = disc.read(qint64(indexLba) * 2048, indexSize);
    if (word(index, 0) != 0 || word(index, index.size() - 4) != archiveSize) fail("Archive index does not span A_FILE.BIN.");
    QJsonArray members;
    for (qsizetype i = 0; i + 4 < index.size(); i += 4) {
        const auto start = word(index, i), end = word(index, i + 4);
        if (start > end || end > archiveSize || start % 2048 || end % 2048) fail("Invalid archive member range.");
        members.append(QJsonObject{{"index", double(i / 4)}, {"offset", double(start)}, {"size", double(end - start)}});
    }
    // Verify the extent itself even though archive assets are read lazily later.
    if (archiveSize) disc.read(qint64(archiveLba) * 2048 + archiveSize - 1, 1);
    const auto exeHash = sha256(exe);
    QJsonObject result{{"schema", 1}, {"id", id}, {"name", QFileInfo(source).completeBaseName()},
        {"game", GameId}, {"discSha256", id}, {"discBytes", double(QFileInfo(image).size())},
        {"sectorSize", sector}, {"imageFile", sector == 2352 ? "disc.bin" : "disc.iso"},
        {"executableSha256", exeHash}, {"executableBytes", double(exe.size())}, {"entryPc", double(entry)},
        {"loadAddress", double(load)}, {"payloadBytes", double(size)}, {"stockExecutable", exeHash == ExeHash},
        {"archiveIndexSha256", sha256(index)}, {"archiveBytes", double(archiveSize)},
        {"archiveMemberCount", members.size()}, {"members", members}, {"nativeReady", false},
        {"compatibility", "Requires a verified native runtime for this exact ROM revision."}};
    writeFile(staging.path() + "/SLUS_011.15", exe);
    writeFile(staging.path() + "/rom.json", QJsonDocument(result).toJson());
    if (!QFile::rename(image, staging.path() + "/" + result.value("imageFile").toString())) fail("Cannot finalize ROM filename.");
    if (sector == 2352) writeFile(staging.path() + "/disc.cue", "FILE \"disc.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n");
    if (!QDir().rename(staging.path(), destination)) fail("Cannot commit ROM import.");
    select(id);
    return metadata(id);
}
QJsonObject RomCatalog::metadata(const QString &id) const {
    const auto path = directory(id);
    auto result = readObject(path + "/rom.json");
    if (result.value("schema") != QJsonValue(1) || result.value("id").toString() != id || result.value("discSha256").toString() != id)
        fail("Invalid ROM catalog metadata.");
    const auto image = result.value("imageFile").toString();
    const auto sector = result.value("sectorSize").toInt();
    if ((sector != 2352 && sector != 2048) || image != (sector == 2352 ? "disc.bin" : "disc.iso")) fail("Invalid catalog image filename.");
    result.insert("imagePath", path + "/" + image);
    result.insert("cuePath", sector == 2352 ? path + "/disc.cue" : QString());
    result.insert("exePath", path + "/SLUS_011.15");
    result.insert("profilePath", profilePath(id));
    // Disk metadata never grants execution permission.
    result.insert("nativeReady", false);
    return result;
}
QJsonArray RomCatalog::entries() const {
    QJsonArray entries;
    const auto ids = QDir(root_ + "/roms").entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto &id : ids) entries.append(metadata(id));
    return entries;
}
void RomCatalog::select(const QString &id) {
    metadata(id);
    writeFile(root_ + "/rom-selection.json", QJsonDocument(QJsonObject{{"schema", 1}, {"selected", id}}).toJson());
}
QString RomCatalog::selected() const {
    if (!QFileInfo::exists(root_ + "/rom-selection.json")) return {};
    const auto selection = readObject(root_ + "/rom-selection.json");
    if (selection.value("schema") != QJsonValue(1)) fail("Invalid ROM selection.");
    const auto id = selection.value("selected").toString();
    metadata(id);
    return id;
}
bool RomCatalog::verify(const QString &id) const {
    const auto info = metadata(id);
    const auto image = info.value("imagePath").toString();
    const bool filesMatch = QFileInfo::exists(image) && QFileInfo(image).size() == info.value("discBytes").toDouble() &&
        hashFile(image) == id && QFileInfo::exists(info.value("exePath").toString()) &&
        sha256(readFile(info.value("exePath").toString(), 2 * 1024 * 1024 + 2048)) == info.value("executableSha256").toString();
    if (!filesMatch) return false;
    if (info.value("sectorSize").toInt() == 2352) {
        const auto cue = info.value("cuePath").toString();
        if (!QFileInfo::exists(cue) || readFile(cue, 1024) != "FILE \"disc.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n") return false;
    }
    return true;
}
QString RomCatalog::profilePath(const QString &id) const { return directory(id) + "/profile-data"; }
}
