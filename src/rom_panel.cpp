#include "rom_panel.h"
#include "native_mods.h"
#include <QProcessEnvironment>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

namespace {
QString nativeRoot() {
#ifdef NATURE_NATIVE_RUNTIME_DIRECTORY
    return QString::fromUtf8(NATURE_NATIVE_RUNTIME_DIRECTORY);
#else
    return {};
#endif
}
QString nativeBinary() {
    if (nativeRoot().isEmpty()) return {};
    return nativeRoot() + "/build/Harvest_Moon__Back_to_Nature__Recompiled_"
#ifdef Q_OS_WIN
        ".exe"
#endif
        ;
}
bool canLaunch(const QJsonObject &metadata) {
    // This development build has only been audited against this complete disc.
    return metadata.value("discSha256").toString() == "41f81532a5fd70d6f79e7b5750513c35eee732d5e31ba144a1db7e90d84d97de"
        && QFileInfo(nativeBinary()).isExecutable() && QFileInfo::exists(nativeRoot() + "/game.toml");
}
}

RomPanel::RomPanel(const QString &dataRoot, QWidget *parent)
    : QWidget(parent), catalog_(dataRoot), dataRoot_(dataRoot) {
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel("Simpan ROM asli dan ROM mod dalam koleksi yang sama. Setiap versi memiliki identitas SHA-256 sendiri.");
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto *buttons = new QHBoxLayout;
    auto *add = new QPushButton("Tambah ROM…");
    add->setProperty("role", "primary");
    auto *select = new QPushButton("Pilih versi ini");
    auto *verify = new QPushButton("Periksa keutuhan");
    auto *play = play_ = new QPushButton("Jalankan native (uji)");
    play->setEnabled(false);
    play->setToolTip("ROM yang tersimpan belum berarti gameplay native sudah tersedia. Build native harus sesuai dengan kode dan overlay versi ROM tersebut.");
    for (auto *button : {add, select, verify, play}) buttons->addWidget(button);
    layout->addLayout(buttons);
    auto *content = new QHBoxLayout;
    list_ = new QListWidget;
    list_->setAccessibleName("Koleksi ROM");
    details_ = new QTextEdit;
    details_->setReadOnly(true);
    details_->setAccessibleName("Identitas dan kompatibilitas ROM");
    content->addWidget(list_, 1); content->addWidget(details_, 2);
    layout->addLayout(content, 1);
    status_ = new QLabel;
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    layout->addWidget(status_);
    auto *scope = new QLabel("Mod teks aktif diterapkan saat boot native USA. Mod konten dan ROM mod belum didukung runtime ini.");
    scope->setWordWrap(true);
    layout->addWidget(scope);
    connect(add, &QPushButton::clicked, this, [this] { importRom(); });
    connect(play_, &QPushButton::clicked, this, [this] { launchNative(); });
    connect(list_, &QListWidget::currentItemChanged, this, [this] { showSelection(); });
    connect(select, &QPushButton::clicked, this, [this] {
        if (!list_->currentItem()) return;
        try {
            catalog_.select(list_->currentItem()->data(Qt::UserRole).toString());
            refresh();
            status_->setText("Versi dipilih. Tombol native tersedia hanya untuk disc dan build yang sudah cocok.");
        } catch (const std::exception &e) { QMessageBox::critical(this, "Pilihan ROM", QString::fromUtf8(e.what())); }
    });
    connect(verify, &QPushButton::clicked, this, [this] {
        if (list_->currentItem()) runCommand({"--catalog-verify", list_->currentItem()->data(Qt::UserRole).toString()});
    });
    refresh();
}
void RomPanel::refresh() {
    list_->blockSignals(true);
    list_->clear();
    const auto selected = catalog_.selected();
    for (const auto &value : catalog_.entries()) {
        const auto entry = value.toObject();
        const auto id = entry.value("id").toString();
        auto *item = new QListWidgetItem(entry.value("name").toString() + (id == selected ? "  [dipilih]" : ""), list_);
        item->setData(Qt::UserRole, id);
        item->setToolTip(id);
        if (id == selected) list_->setCurrentItem(item);
    }
    if (!list_->currentItem() && list_->count()) list_->setCurrentRow(0);
    list_->blockSignals(false);
    showSelection();
}
void RomPanel::showSelection() {
    play_->setEnabled(false);
    if (!list_->currentItem()) {
        details_->setPlainText("Koleksi masih kosong. Tambahkan BIN, ISO, CUE, atau ZIP milikmu.\n\nROM sumber tidak ditimpa.");
        return;
    }
    try {
        const auto m = catalog_.metadata(list_->currentItem()->data(Qt::UserRole).toString());
        play_->setEnabled(!game_ && canLaunch(m));
        const auto sector = m.value("sectorSize").toInt();
        details_->setPlainText(QString("%1\n\n%2\nFormat aktual: %3\nUkuran: %4 MiB\nAnggota arsip: %5\n\nSHA-256 disc\n%6\n\nSHA-256 executable\n%7\n\nStatus: tersimpan; gameplay native belum tersedia.\nKode dan overlay yang berubah membutuhkan build native versi tersebut.")
            .arg(m.value("name").toString(),
                 m.value("stockExecutable").toBool() ? "Executable cocok dengan USA asli; aset disc tetap dapat berbeda." : "Executable modifikasi / versi berbeda.",
                 sector == 2352 ? "Mode 2 / 2352 byte (terlepas dari ekstensi file)" : "ISO / 2048 byte")
            .arg(m.value("discBytes").toDouble() / (1024 * 1024), 0, 'f', 1)
            .arg(m.value("archiveMemberCount").toInt())
            .arg(m.value("discSha256").toString(), m.value("executableSha256").toString()));
        if (canLaunch(m)) {
            details_->setPlainText(details_->toPlainText().replace("Status: tersimpan; gameplay native belum tersedia.",
                "Status: boot/title native teruji pada Linux; gameplay lengkap masih eksperimental."));
            play_->setToolTip("Menjalankan runtime native riset lokal. Mod teks dan empat field item retail yang didukung diterapkan saat boot.");
        }
    } catch (const std::exception &e) { details_->setPlainText(QString::fromUtf8(e.what())); }
}
void RomPanel::launchNative() {
    if (game_ || !list_->currentItem()) return;
    try {
        const auto id = list_->currentItem()->data(Qt::UserRole).toString();
        const auto metadata = catalog_.metadata(id);
        if (!canLaunch(metadata)) throw std::runtime_error("Build native yang cocok belum tersedia untuk ROM ini.");
        if (!catalog_.verify(id)) throw std::runtime_error("Verifikasi ROM gagal; jalankan impor ulang dari sumber asli.");
        const auto state = QDir(metadata.value("profilePath").toString()).filePath("native-saves");
        if (!QDir().mkpath(state)) throw std::runtime_error("Folder save native tidak dapat dibuat.");
        const auto modRoot = nature::exportNativeMods(nature::Library(dataRoot_), metadata.value("exePath").toString(), state + "/mods");
        game_ = new QProcess(this);
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("NATURE_MOD_DIRECTORY", modRoot);
        game_->setProcessEnvironment(environment);
        game_->setWorkingDirectory(nativeRoot() + "/build");
        game_->setStandardOutputFile(state + "/native.log");
        game_->setProcessChannelMode(QProcess::MergedChannels);
        connect(game_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, state](int code, QProcess::ExitStatus result) {
                status_->setText(QString("Runtime native berhenti (%1). Log: %2/native.log").arg(code).arg(state));
                if (code == 86) status_->setText("Port menemukan kode yang belum didukung. Detail: " + state + "/native.log");
                else if (result == QProcess::CrashExit) status_->setText("Runtime gagal. Detail: " + state + "/native.log");
                game_->deleteLater(); game_ = nullptr; showSelection();
            });
        connect(game_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                status_->setText(game_->errorString()); game_->deleteLater(); game_ = nullptr; showSelection();
            }
        });
        play_->setEnabled(false);
        status_->setText("Menjalankan port native eksperimental. Rencana mod aktif diterapkan saat boot.");
        game_->start(nativeBinary(), {"--no-launcher", "--game", nativeRoot() + "/game.toml",
            "--disc", metadata.value("cuePath").toString().isEmpty() ? metadata.value("imagePath").toString() : metadata.value("cuePath").toString(),
            "--memcard-dir", state});
    } catch (const std::exception &e) { QMessageBox::critical(this, "Runtime native", QString::fromUtf8(e.what())); }
}
void RomPanel::importRom() {
    const auto source = QFileDialog::getOpenFileName(this, "Tambahkan ROM asli atau ROM mod", {}, "Disc game (*.bin *.iso *.cue *.zip *.img)");
    if (!source.isEmpty()) runCommand({"--catalog-import", source});
}
void RomPanel::runCommand(const QStringList &arguments) {
    auto *process = new QProcess(this);
    setEnabled(false);
    status_->setText("Membaca dan memeriksa ROM…");
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this, process](int code, QProcess::ExitStatus exitStatus) {
            setEnabled(true);
            if (code || exitStatus != QProcess::NormalExit) {
                status_->setText("Pemeriksaan gagal.");
                QMessageBox::critical(this, "Koleksi ROM", QString::fromUtf8(process->readAllStandardError()));
            } else {
                status_->setText("Selesai. Salinan ROM terverifikasi; ROM sumber tidak ditulis ulang.");
                try { refresh(); }
                catch (const std::exception &e) { status_->setText(QString::fromUtf8(e.what())); }
            }
            process->deleteLater();
        });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            setEnabled(true); status_->setText(process->errorString()); process->deleteLater();
        }
    });
    QStringList args{"--data", dataRoot_}; args.append(arguments);
    process->start(QCoreApplication::applicationFilePath(), args);
}
