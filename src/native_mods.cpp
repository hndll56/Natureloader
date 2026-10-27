#include "native_mods.h"
#include "modcore_library.h"
#include <QCryptographicHash>
#include <QDir>
#include <QTemporaryDir>
#include <QtEndian>
#include <stdexcept>

namespace nature {
QString exportNativeTextMods(const Library &library, const QString &exePath, const QString &outputRoot) {
    const auto exe = readFile(exePath, 1024 * 1024);
    if (QCryptographicHash::hash(exe, QCryptographicHash::Sha256).toHex() != ExeHash)
        throw std::runtime_error("Native text mods require the verified USA executable.");
    QJsonObject resolved;
    for (const auto &mod : library.mods()) if (mod.enabled) {
        if (!mod.content.isEmpty()) throw std::runtime_error("Nonaktifkan mod konten: belum terhubung ke gameplay native.");
        for (auto it = mod.texts.begin(); it != mod.texts.end(); ++it) resolved.insert(it.key(), it.value());
    }
    const auto v2Texts = ModcoreLibrary(library.root()).nativeTexts();
    for (auto it = v2Texts.begin(); it != v2Texts.end(); ++it) {
        if (resolved.contains(it.key()))
            throw std::runtime_error(("Teks v1 dan v2 memakai key yang sama: " + it.key()
                + ". Nonaktifkan salah satu mod sebelum menjalankan native.").toStdString());
        resolved.insert(it.key(), it.value());
    }
    QString manifest = "format_version = 1\nid = \"nature.texts\"\nversion = \"1.0.0\"\nname = \"NatureLoader text overrides\"\nresolver = \"declarative\"\n"
        "[[target]]\ngame_id = \"SLUS-01115\"\nexe_sha256 = \"" + QString(ExeHash) + "\"\n";
    QMap<QChar, quint16> glyphs;
    const QStringList rows{"ABCDEFGHIJKLMNOP", "QRSTUVWXYZ", "abcdefghijklmnop", "qrstuvwxyz", "0123456789"};
    for (int row = 0; row < rows.size(); ++row)
        for (int col = 0; col < rows[row].size(); ++col) glyphs.insert(rows[row][col], quint16(row * 16 + col));
    const QMap<quint16, QChar> extras{{0x62,'?'},{0x63,'!'},{0x66,':'},{0x67,';'},{0x68,'+'},{0x69,'-'},
        {0x6a,'('},{0x6b,')'},{0x6c,'.'},{0x74,'_'},{0x75,'~'},{0x76,'&'},{0x8c,','},{0x8d,'\''},
        {0x8e,'"'},{0x8f,'%'},{0xffd,' '},{0xfffe,'\n'}};
    for (auto it = extras.begin(); it != extras.end(); ++it) glyphs.insert(it.value(), it.key());
    for (const auto &value : textLayout()) {
        const auto entry = value.toObject(); const auto key = entry.value("name").toString();
        if (!resolved.contains(key)) continue;
        const auto text = resolved.value(key).toString();
        const int capacity = entry.value("capacity").toInt();
        if (text.size() + 1 > capacity)
            throw std::runtime_error(("Teks native terlalu panjang: " + key + " (maksimal " + QString::number(capacity - 1) + " glyph).").toStdString());
        QByteArray replacement(capacity * 2, '\xff');
        for (int i = 0; i < text.size(); ++i) {
            if (!glyphs.contains(text[i])) throw std::runtime_error(("Glyph belum didukung pada teks native: " + key).toStdString());
            qToLittleEndian<quint16>(glyphs.value(text[i]), replacement.data() + i * 2);
        }
        const quint32 address = 0x8005175c + entry.value("offset").toInt();
        const int offset = int(address - 0x80010000 + 0x800);
        if (offset < 0 || offset + replacement.size() > exe.size()) throw std::runtime_error("Native text range outside executable.");
        manifest += "[[patch]]\ntarget = \"main_exe\"\naddress = " + QString::number(address)
            + "\nexpected = \"" + QString::fromLatin1(exe.mid(offset, replacement.size()).toHex(' '))
            + "\"\nreplace = \"" + QString::fromLatin1(replacement.toHex(' ')) + "\"\n";
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
