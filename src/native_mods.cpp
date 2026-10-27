#include "native_mods.h"
#include "modcore_library.h"
#include <QCryptographicHash>
#include <QDir>
#include <QTemporaryDir>
#include <QtEndian>
#include <cmath>
#include <stdexcept>

namespace nature {
namespace {
QString guardedPatch(const QByteArray &exe, quint32 address, const QByteArray &replacement) {
    if (exe.size() < 0x800 || exe.left(8) != "PS-X EXE")
        throw std::runtime_error("Invalid native executable header.");
    const quint32 loadAddress = qFromLittleEndian<quint32>(exe.constData() + 0x18);
    const quint32 loadSize = qFromLittleEndian<quint32>(exe.constData() + 0x1c);
    if (address < loadAddress || replacement.isEmpty())
        throw std::runtime_error("Native patch is outside executable data.");
    const quint64 relative = quint64(address) - loadAddress;
    const quint64 size = quint64(replacement.size());
    if (relative > loadSize || size > loadSize - relative ||
        relative + 0x800 > quint64(exe.size()) || size > quint64(exe.size()) - relative - 0x800)
        throw std::runtime_error("Native patch range outside executable.");
    return "[[patch]]\ntarget = \"main_exe\"\naddress = " + QString::number(address)
        + "\nexpected = \"" + QString::fromLatin1(exe.mid(qsizetype(relative + 0x800), replacement.size()).toHex(' '))
        + "\"\nreplace = \"" + QString::fromLatin1(replacement.toHex(' ')) + "\"\n";
}

// USA ItemInfo: 380 rows of 40 bytes at D_8004B1A4. Offsets follow the
// reconstructed ItemInfo layout and are checked against the exact executable hash.
constexpr quint32 ItemTableAddress = 0x8004B1A4;
constexpr int ItemCount = 380, ItemRowBytes = 40;
struct ItemField { int offset, size, minimum, maximum; };
const QMap<QString, ItemField> ItemFields{
    {"buy_price", {16, 2, -32768, 32767}}, {"sell_price", {18, 2, -32768, 32767}},
    {"stamina", {32, 1, -128, 127}}, {"fatigue", {33, 1, -128, 127}}
};
}

QByteArray encodeNativeText(const QString &text, int capacity, const QString &key) {
    if (capacity < 1 || text.size() + 1 > capacity)
        throw std::runtime_error(("Teks native terlalu panjang: " + key + " (maksimal " + QString::number(capacity - 1) + " glyph).").toStdString());
    static const auto glyphs = [] {
        QMap<QChar, quint16> result;
        const QStringList rows{"ABCDEFGHIJKLMNOP", "QRSTUVWXYZ", "abcdefghijklmnop", "qrstuvwxyz", "0123456789"};
        for (int row = 0; row < rows.size(); ++row)
            for (int col = 0; col < rows[row].size(); ++col) result.insert(rows[row][col], quint16(row * 16 + col));
        const QMap<quint16, QChar> extras{{0x62,'?'},{0x63,'!'},{0x66,':'},{0x67,';'},{0x68,'+'},{0x69,'-'},
            {0x6a,'('},{0x6b,')'},{0x6c,'.'},{0x74,'_'},{0x75,'~'},{0x76,'&'},{0x8c,','},{0x8d,'\''},
            {0x8e,'"'},{0x8f,'%'},{0xffd,' '},{0xfffe,'\n'}};
        for (auto it = extras.begin(); it != extras.end(); ++it) result.insert(it.value(), it.key());
        return result;
    }();
    QByteArray replacement(capacity * 2, '\xff');
    for (int i = 0; i < text.size(); ++i) {
        if (!glyphs.contains(text[i])) throw std::runtime_error(("Glyph belum didukung pada teks native: " + key).toStdString());
        qToLittleEndian<quint16>(glyphs.value(text[i]), replacement.data() + i * 2);
    }
    return replacement;
}

QString exportNativeMods(const Library &library, const QString &exePath, const QString &outputRoot) {
    const auto exe = readFile(exePath, 1024 * 1024);
    if (QCryptographicHash::hash(exe, QCryptographicHash::Sha256).toHex() != ExeHash)
        throw std::runtime_error("Native mods require the verified USA executable.");
    const auto v2Plan = ModcoreLibrary(library.root()).nativePlan();
    const auto resolved = v2Plan.value("texts").toObject();
    QString manifest = "format_version = 1\nid = \"nature.texts\"\nversion = \"1.0.0\"\nname = \"NatureLoader native overrides\"\nresolver = \"declarative\"\n"
        "[[target]]\ngame_id = \"SLUS-01115\"\nexe_sha256 = \"" + QString(ExeHash) + "\"\n";
    for (const auto &value : textLayout()) {
        const auto entry = value.toObject(); const auto key = entry.value("name").toString();
        if (!resolved.contains(key)) continue;
        const auto text = resolved.value(key).toString();
        const int capacity = entry.value("capacity").toInt();
        const auto replacement = encodeNativeText(text, capacity, key);
        const quint32 address = 0x8005175c + entry.value("offset").toInt();
        manifest += guardedPatch(exe, address, replacement);
    }
    for (const auto &value : v2Plan.value("items").toArray()) {
        const auto patch = value.toObject();
        const auto fieldName = patch.value("field").toString();
        const int handle = patch.value("handle").toInt(-1);
        if (!ItemFields.contains(fieldName) || handle < 0 || handle >= ItemCount ||
            !patch.value("handle").isDouble() || patch.value("handle").toDouble() != handle)
            throw std::runtime_error("Unsupported native item field or table index.");
        const auto field = ItemFields.value(fieldName);
        const auto number = patch.value("value");
        const double numeric = number.toDouble();
        if (!number.isDouble() || !std::isfinite(numeric) || std::floor(numeric) != numeric ||
            numeric < field.minimum || numeric > field.maximum)
            throw std::runtime_error("Native item value exceeds its signed field width.");
        const int integer = int(numeric);
        QByteArray replacement(field.size, '\0');
        if (field.size == 2) qToLittleEndian<quint16>(quint16(qint16(integer)), replacement.data());
        else replacement[0] = char(quint8(qint8(integer)));
        manifest += guardedPatch(exe, ItemTableAddress + quint32(handle * ItemRowBytes + field.offset), replacement);
    }
    if (!QDir().mkpath(outputRoot)) throw std::runtime_error("Cannot create native mod directory.");
    QTemporaryDir staging(outputRoot + "/run-XXXXXX");
    if (!staging.isValid()) throw std::runtime_error("Cannot create native mod snapshot.");
    if (!QDir().mkpath(staging.path() + "/packages/nature.texts/1.0.0"))
        throw std::runtime_error("Cannot create native package directory.");
    writeFile(staging.path() + "/packages/nature.texts/1.0.0/manifest.toml", manifest.toUtf8());
    writeFile(staging.path() + "/state.toml", "format_version = 1\n[[package]]\nid = \"nature.texts\"\nversion = \"1.0.0\"\nenabled = true\n");
    staging.setAutoRemove(false);
    return staging.path();
}
}
