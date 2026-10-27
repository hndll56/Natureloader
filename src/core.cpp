#include "core.h"
#include "modcore_library.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <QUuid>
#include <QtEndian>
#include <archive.h>
#include <archive_entry.h>
#include <memory>
#include <stdexcept>

static void initializeResources() { Q_INIT_RESOURCE(assets); }

namespace nature {
namespace {
[[noreturn]] void fail(const QString &message) { throw std::runtime_error(message.toStdString()); }
quint32 u32(const QByteArray &b, qsizetype offset) {
    if (offset < 0 || offset + 4 > b.size()) fail("Truncated 32-bit field.");
    return qFromLittleEndian<quint32>(b.constData() + offset);
}
QString digest(const QByteArray &b) {
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex());
}
void writeObject(const QString &path, const QJsonObject &object) {
    writeFile(path, QJsonDocument(object).toJson());
}
QJsonObject objectFrom(const QByteArray &bytes) {
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) fail("Invalid JSON object.");
    return doc.object();
}
void checkId(const QString &id) {
    static const QRegularExpression pattern("\\A[a-z][a-z0-9-]{0,63}\\z");
    if (!pattern.match(id).hasMatch()) fail("Mod ID must contain lowercase letters, digits or hyphens.");
    safePath(id);
}
Mod validateMod(const QJsonObject &o, const QString &directory) {
    const QSet<QString> allowed{"schema", "id", "name", "version", "game", "texts", "content"};
    for (auto i = o.begin(); i != o.end(); ++i)
        if (!allowed.contains(i.key())) fail("Unsupported mod field: " + i.key());
    if (o.value("schema") != QJsonValue(1) || o.value("game").toString() != GameId)
        fail("Mod must use schema 1 and game SLUS_011.15.");
    Mod m{o.value("id").toString(), o.value("name").toString(),
          o.value("version").toString(), directory, o.value("texts").toObject(), o.value("content").toObject(), false};
    checkId(m.id);
    if (m.name.trimmed().isEmpty() || m.name.size() > 120 || m.version.isEmpty() || m.version.size() > 40)
        fail("Mod name or version is missing or too long.");
    if ((o.contains("texts") && !o.value("texts").isObject()) ||
        (o.contains("content") && !o.value("content").isObject()))
        fail("Mod texts and content must be JSON objects.");
    if (m.texts.isEmpty() && m.content.isEmpty()) fail("Mod must contain texts or content.");
    if (m.content.size() > 1024) fail("Mod content exceeds 1024 records.");
    static const QSet<QString> types{"item", "tool", "npc", "location", "dialog", "schedule",
                                    "story", "building", "sprite", "ui_theme", "rules"};
    static const QRegularExpression localId("\\A[a-z][a-z0-9-]{0,63}\\z");
    for (auto i = m.content.begin(); i != m.content.end(); ++i) {
        const auto prefix = m.id + ":";
        if (!i.key().startsWith(prefix) || !localId.match(i.key().mid(prefix.size())).hasMatch())
            fail("Content ID must belong to its mod namespace: " + i.key());
        if (!i.value().isObject()) fail("Content record must be an object: " + i.key());
        const auto record = i.value().toObject();
        for (auto field = record.begin(); field != record.end(); ++field)
            if (field.key() != "type" && field.key() != "name" && field.key() != "data")
                fail("Unsupported content field: " + field.key());
        if (!types.contains(record.value("type").toString())) fail("Unknown content type: " + i.key());
        const auto name = record.value("name").toString();
        if (name.trimmed().isEmpty() || name.size() > 120) fail("Content name is missing or too long: " + i.key());
        if (record.contains("data") && !record.value("data").isObject())
            fail("Content data must be an object: " + i.key());
        // Data is preserved for authoring previews, not interpreted as gameplay or executable code.
        if (QJsonDocument(record).toJson(QJsonDocument::Compact).size() > 65536)
            fail("Content record exceeds 64 KiB: " + i.key());
    }
    QSet<QString> keys;
    for (const auto &entry : textLayout()) keys.insert(entry.toObject().value("name").toString());
    for (auto i = m.texts.begin(); i != m.texts.end(); ++i) {
        if (!keys.contains(i.key())) fail("Unknown text key: " + i.key());
        if (!i.value().isString() || i.value().toString().size() > 16384)
            fail("Text overrides must be strings of at most 16384 characters.");
    }
    return m;
}

// libarchive parses ZIP; entry names never become filesystem paths.
// Copy only the requested members into a private staging directory.
QString unpackRom(const QString &zip, const QString &directory) {
    std::unique_ptr<archive, decltype(&archive_read_free)> a(archive_read_new(), archive_read_free);
    archive_read_support_format_zip(a.get());
    const auto native = zip.toStdWString();
    if (archive_read_open_filename_w(a.get(), native.c_str(), 65536) != ARCHIVE_OK)
        fail("Cannot open ROM ZIP.");
    archive_entry *entry = nullptr;
    QString result;
    int entries = 0, code;
    while ((code = archive_read_next_header(a.get(), &entry)) == ARCHIVE_OK) {
        if (++entries > 100) fail("ROM ZIP has too many members.");
        const QString name = QString::fromUtf8(archive_entry_pathname(entry));
        if (!name.endsWith(".bin", Qt::CaseInsensitive) && !name.endsWith(".iso", Qt::CaseInsensitive)) continue;
        if (!result.isEmpty()) fail("Expected exactly one BIN or ISO inside ROM ZIP.");
        if (archive_entry_filetype(entry) != AE_IFREG || archive_entry_symlink(entry) || archive_entry_hardlink(entry))
            fail("ROM ZIP member must be a regular file.");
        if (archive_entry_size(entry) < 32768 || archive_entry_size(entry) > 800LL * 1024 * 1024)
            fail("Unsupported ROM size.");
        result = directory + "/disc.img";
        QFile out(result);
        if (!out.open(QIODevice::WriteOnly)) fail(out.errorString());
        char buffer[65536];
        qint64 total = 0;
        la_ssize_t count;
        while ((count = archive_read_data(a.get(), buffer, sizeof buffer)) > 0) {
            total += count;
            if (total > 800LL * 1024 * 1024 || out.write(buffer, count) != count) fail("ROM ZIP exceeds limit or write failed.");
        }
        if (count < 0 || total != archive_entry_size(entry)) fail("Corrupt ROM ZIP.");
    }
    if (code != ARCHIVE_EOF || result.isEmpty()) fail("Invalid ROM ZIP or missing BIN/ISO.");
    return result;
}

}

Mod validateLegacyMod(const QJsonObject &manifest, const QString &directory) { return validateMod(manifest, directory); }

QByteArray readFile(const QString &path, qint64 limit) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) fail("Cannot read " + path + ": " + file.errorString());
    if (file.size() > limit) fail("File exceeds size limit: " + path);
    const auto bytes = file.readAll();
    if (bytes.size() != file.size()) fail("Incomplete file read: " + path);
    return bytes;
}
void writeFile(const QString &path, const QByteArray &bytes) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        fail("Cannot save " + path + ": " + file.errorString());
}
QJsonObject readObject(const QString &path) { return objectFrom(readFile(path, 2 * 1024 * 1024)); }
QString safePath(const QString &name) {
    if (name.isEmpty() || name.size() > 240 || name.contains('\\') || name.contains(':') || name.startsWith('/'))
        fail("Unsafe path: " + name);
    static const QRegularExpression device("^(con|prn|aux|nul|com[0-9]|lpt[0-9])(?:\\..*)?$", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression chars("\\A[A-Za-z0-9_.-]+\\z");
    for (const auto &part : name.split('/')) {
        if (part.isEmpty() || part == "." || part == ".." || part.endsWith('.') ||
            !chars.match(part).hasMatch() || device.match(part).hasMatch()) fail("Unsafe path: " + name);
    }
    return name;
}

Disc::Disc(QString path) : path_(std::move(path)) {
    QFile f(path_);
    if (!f.open(QIODevice::ReadOnly)) fail("Cannot open disc.");
    bytes_ = f.size();
    if (bytes_ > 800LL * 1024 * 1024) fail("Disc exceeds 800 MiB limit.");
    for (const auto sector : {2352, 2048}) {
        const int payload = sector == 2352 ? 24 : 0;
        f.seek(16LL * sector + payload);
        if (f.read(7) == QByteArray("\x01" "CD001\x01", 7)) {
            sector_ = sector; payload_ = payload; break;
        }
    }
    if (!sector_ || bytes_ % sector_) fail("Expected single-track Mode 2/2352 BIN or 2048-byte ISO.");
}
QByteArray Disc::read(qint64 offset, qint64 size) const {
    const qint64 logicalSize = bytes_ / sector_ * 2048;
    if (offset < 0 || size < 0 || size > 256LL * 1024 * 1024 || offset > logicalSize || size > logicalSize - offset)
        fail("Disc extent is outside image bounds.");
    QFile f(path_);
    if (!f.open(QIODevice::ReadOnly)) fail("Disc disappeared.");
    QByteArray result;
    result.reserve(size);
    while (size) {
        const qint64 lba = offset / 2048, within = offset % 2048;
        const auto take = qMin(size, 2048 - within);
        if (sector_ == 2352) {
            f.seek(lba * sector_ + 15);
            const auto header = f.read(9);
            if (header.size() != 9 || header[0] != 2 || (static_cast<unsigned char>(header[3]) & 0x20))
                fail("Required file uses unsupported sector mode.");
        }
        if (!f.seek(lba * sector_ + payload_ + within)) fail("Disc seek failed.");
        const auto part = f.read(take);
        if (part.size() != take) fail("Truncated disc.");
        result += part; offset += take; size -= take;
    }
    return result;
}
QMap<QString, QPair<quint32, quint32>> Disc::files() const {
    const auto pvd = read(16 * 2048, 2048);
    if (qFromLittleEndian<quint16>(pvd.constData() + 128) != 2048) fail("Unsupported ISO block size.");
    const auto rootLba = u32(pvd, 158), rootSize = u32(pvd, 166);
    if (rootSize > 1024 * 1024) fail("ISO root directory too large.");
    const auto dir = read(qint64(rootLba) * 2048, rootSize);
    QMap<QString, QPair<quint32, quint32>> result;
    for (qsizetype pos = 0; pos < dir.size();) {
        const int length = static_cast<unsigned char>(dir[pos]);
        if (!length) { pos = (pos / 2048 + 1) * 2048; continue; }
        if (length < 34 || pos + length > dir.size() || pos % 2048 + length > 2048) fail("Invalid ISO directory record.");
        const int nameLength = static_cast<unsigned char>(dir[pos + 32]);
        if (33 + nameLength > length) fail("Invalid ISO filename length.");
        const auto flags = static_cast<unsigned char>(dir[pos + 25]);
        const auto name = QString::fromLatin1(dir.mid(pos + 33, nameLength)).section(';', 0, 0);
        if (!(flags & 2)) {
            if (flags & 0x80) fail("Multi-extent ISO files are unsupported.");
            if (result.contains(name)) fail("Duplicate ISO filename.");
            result.insert(name, {u32(dir, pos + 2), u32(dir, pos + 10)});
        }
        pos += length;
    }
    return result;
}

QJsonArray textLayout() {
    static const QJsonArray layout = [] {
        initializeResources();
        return readObject(":/text_bank.layout.json").value("entries").toArray();
    }();
    return layout;
}
QString decodeText(const QByteArray &bytes) {
    if (bytes.size() % 2) fail("Text data must contain 16-bit glyphs.");
    // Glyph mapping follows the supplied decomp's tools/text_codec.py.
    const QStringList rows{"ABCDEFGHIJKLMNOP", "QRSTUVWXYZ", "abcdefghijklmnop", "qrstuvwxyz", "0123456789"};
    const QMap<quint16, QString> extra{{0x50,"'t"},{0x51,"'s"},{0x52,"'l"},{0x53,"'d"},{0x54,"'m"},{0x55,"'r"},{0x56,"'v"},
        {0x62,"?"},{0x63,"!"},{0x66,":"},{0x67,";"},{0x68,"+"},{0x69,"-"},{0x6a,"("},{0x6b,")"},{0x6c,"."},
        {0x74,"_"},{0x75,"~"},{0x76,"&"},{0x8c,","},{0x8d,"'"},{0x8e,"\""},{0x8f,"%"},{0xffd," "},{0xfffe,"\n"}};
    QString result;
    for (qsizetype i = 0; i < bytes.size(); i += 2) {
        auto code = qFromLittleEndian<quint16>(bytes.constData() + i);
        if (code == 0xffff) break;
        const int row = code >> 4, col = code & 15;
        if (row < rows.size() && col < rows[row].size()) result += rows[row][col];
        else if (extra.contains(code)) result += extra[code];
        else result += QString("{RAW:%1}").arg(code, 4, 16, QChar('0')).toUpper();
    }
    return result;
}

Library::Library(QString root) : root_(QDir(root).absolutePath()) {
    ModcoreLibrary backend(root_);
}
bool Library::hasGame() const {
    const auto path = root_ + "/base/SLUS_011.15";
    return QFileInfo::exists(path) && digest(readFile(path, 1024 * 1024)) == ExeHash;
}
QJsonObject Library::importGame(const QString &source) {
    if (QFileInfo::exists(root_ + "/base")) fail("A game is already imported. Use another data directory to import again.");
    QTemporaryDir staging(root_ + "/.import-XXXXXX");
    if (!staging.isValid()) fail("Cannot create import staging directory.");
    QString path = source;
    if (source.endsWith(".zip", Qt::CaseInsensitive)) path = unpackRom(source, staging.path());
    if (source.endsWith(".cue", Qt::CaseInsensitive)) {
        const QString cue = QString::fromUtf8(readFile(source, 65536));
        const QRegularExpression filePattern("FILE\\s+\"([^\"]+)\"\\s+BINARY", QRegularExpression::CaseInsensitiveOption);
        auto matches = filePattern.globalMatch(cue);
        if (!matches.hasNext()) fail("CUE must reference a single BINARY file.");
        const auto name = matches.next().captured(1);
        if (matches.hasNext() || cue.count(QRegularExpression("\\bTRACK\\b", QRegularExpression::CaseInsensitiveOption)) != 1 ||
            !cue.contains(QRegularExpression("TRACK\\s+01\\s+MODE2/2352", QRegularExpression::CaseInsensitiveOption)) ||
            !cue.contains(QRegularExpression("INDEX\\s+01\\s+00:00:00", QRegularExpression::CaseInsensitiveOption)))
            fail("Only single-track MODE2/2352 CUE starting at 00:00:00 is supported.");
        if (name.contains('/') || name.contains('\\') || name.contains(':') || name == "..") fail("CUE file must be beside its BIN.");
        path = QFileInfo(source).dir().filePath(name);
    }
    const Disc disc(path);
    const auto files = disc.files();
    QJsonObject hashes;
    QByteArray exe, hdt, archiveBytes;
    for (const QString &name : {QString(GameId), QString("A_FILE.HDT"), QString("A_FILE.BIN")}) {
        if (!files.contains(name)) fail("Missing disc file: " + name);
        const auto [lba, size] = files[name];
        const auto data = disc.read(qint64(lba) * 2048, size);
        hashes.insert(name, digest(data));
        if (name == GameId) exe = data;
        if (name == "A_FILE.HDT") hdt = data;
        if (name == "A_FILE.BIN") archiveBytes = data;
        writeFile(staging.path() + "/" + name, data);
    }
    if (digest(exe) != ExeHash) fail("ROM executable does not match unmodified USA SLUS_011.15.");
    if (hdt.size() < 33 * 4) fail("Archive index is truncated.");
    QJsonArray members;
    // afile_init copies 33 offsets; adjacent offsets delimit 32 members.
    for (int i = 0; i < 32; ++i) {
        const auto start = u32(hdt, i * 4), end = u32(hdt, (i + 1) * 4);
        if (start > end || end > static_cast<quint32>(archiveBytes.size())) fail("Invalid A_FILE.HDT offsets.");
        members.append(QJsonObject{{"index", i}, {"offset", double(start)}, {"size", double(end - start)}});
    }
    const qint64 textOffset = 0x8005175cLL - u32(exe, 0x18) + 0x800;
    if (textOffset < 0 || textOffset + 13406 > exe.size()) fail("Text bank is outside executable.");
    const auto bank = exe.mid(textOffset, 13406);
    if (digest(bank) != "e4e0c6a6346e7f8d9d61382d72b0198ef4b24470e0ed74094c907f8e8075a4a1") fail("Text bank hash mismatch.");
    QJsonObject texts;
    for (const auto &value : textLayout()) {
        const auto entry = value.toObject();
        const int offset = entry.value("offset").toInt(), length = entry.value("capacity").toInt() * 2;
        if (offset < 0 || length < 0 || offset + length > bank.size()) fail("Invalid text layout.");
        texts.insert(entry.value("name").toString(), decodeText(bank.mid(offset, length)));
    }
    QJsonObject report{{"schema", 1}, {"game", GameId}, {"hashes", hashes}, {"members", members}, {"textCount", texts.size()}};
    writeObject(staging.path() + "/texts.json", texts);
    writeObject(staging.path() + "/import.json", report);
    QFile::remove(staging.path() + "/disc.img");
    if (!QDir().rename(staging.path(), root_ + "/base")) fail("Cannot commit game import.");
    return report;
}
namespace {
Mod publicMod(const ModcoreMod &m) {
    Mod result{m.id, m.name, m.version, m.directory, m.manifest.value("texts").toObject(),
        m.manifest.value("schema") == QJsonValue(1) ? m.manifest.value("content").toObject() : QJsonObject(), m.enabled};
    result.schema = m.manifest.value("schema").toInt();
    result.error = m.error;
    return result;
}
}
Mod Library::installMod(const QString &zip) { return publicMod(ModcoreLibrary(root_).installZip(zip)); }
QList<Mod> Library::mods() const {
    QList<Mod> result;
    for (const auto &m : ModcoreLibrary(root_).mods()) result.append(publicMod(m));
    return result;
}
QList<Mod> Library::resolvedMods() const {
    const auto report = ModcoreLibrary(root_).inspect();
    if (!report.value("ok").toBool()) fail("Mod profile failed validation: " + report.value("report").toString());
    QMap<QString, Mod> available;
    for (const auto &mod : mods()) available.insert(mod.id, mod);
    QList<Mod> result;
    for (const auto &id : report.value("loaded").toArray()) result.append(available.value(id.toString()));
    return result;
}
void Library::setEnabled(const QString &id, bool enabled) { ModcoreLibrary(root_).setEnabled(id, enabled); }
void Library::moveMod(const QString &id, int delta) { ModcoreLibrary(root_).moveMod(id, delta); }
QString Library::removeMod(const QString &id) { return ModcoreLibrary(root_).removeMod(id); }
QString Library::text(const QString &key, QString *provider) const {
    if (!hasGame()) fail("Import a verified USA ROM first.");
    const auto texts = readObject(root_ + "/base/texts.json");
    if (!texts.contains(key)) fail("Unknown text key: " + key);
    auto result = texts.value(key).toString();
    if (provider) *provider = "ROM";
    for (const auto &mod : resolvedMods()) {
        if (mod.texts.contains(key)) {
            result = mod.texts.value(key).toString();
            if (provider) *provider = mod.id;
        }
    }
    return result;
}
QJsonObject Library::content() const { return ModcoreLibrary(root_).inspect().value("records").toObject(); }
QStringList Library::conflicts() const {
    QMap<QString, QStringList> owners;
    for (const auto &mod : resolvedMods())
        for (auto it = mod.texts.begin(); it != mod.texts.end(); ++it) owners[it.key()].append(mod.id);
    QStringList result;
    for (auto it = owners.begin(); it != owners.end(); ++it)
        if (it.value().size() > 1) result.append(it.key() + ": " + it.value().join(", ") + " | winner: " + it.value().last());
    return result;
}
QJsonObject Library::status() const {
    auto report = ModcoreLibrary(root_).inspect();
    const int count = report.value("records").toObject().size();
    report.remove("records");
    report.insert("gameImported", hasGame());
    report.insert("gameplayReady", false);
    report.insert("dataDirectory", root_);
    report.insert("contentCount", count);
    report.insert("installedMods", mods().size());
    report.insert("conflicts", QJsonArray::fromStringList(report.value("ok").toBool() ? conflicts() : QStringList()));
    return report;

}
}
