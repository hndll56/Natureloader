#include "modcore_panel.h"

#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextEdit>
#include <QVBoxLayout>
#include <exception>
#include <utility>

namespace {
QStringList ids(const QJsonValue &value) {
    QStringList result;
    for (const auto &entry : value.toArray()) result.append(entry.toString());
    return result;
}
QString orderText(const QStringList &order) {
    return order.isEmpty() ? QStringLiteral("(kosong)") : order.join(" → ");
}
}

ModcorePanel::ModcorePanel(QString dataRoot, QWidget *parent)
    : QWidget(parent), library_(std::move(dataRoot)) {
    auto *layout = new QVBoxLayout(this);
    auto *scope = new QLabel("Mod teks v2 diterapkan saat native USA dijalankan. NPC, item, area, sprite HD, dan script masih pratinjau pengembangan.");
    scope->setWordWrap(true);
    scope->setTextFormat(Qt::PlainText);
    layout->addWidget(scope);

    auto *actions = new QHBoxLayout;
    auto *install = new QPushButton("Pasang mod ZIP…");
    install->setProperty("role", "primary");
    auto *refreshButton = new QPushButton("Periksa / muat ulang");
    actions->addWidget(install);
    actions->addWidget(refreshButton);
    actions->addStretch();
    layout->addLayout(actions);

    auto *splitter = new QSplitter(Qt::Horizontal);
    auto *modColumn = new QWidget;
    auto *modLayout = new QVBoxLayout(modColumn);
    modLayout->setContentsMargins(0, 0, 0, 0);
    auto *modHeading = new QLabel("Paket terpasang · urutan profil");
    modHeading->setProperty("role", "section");
    modLayout->addWidget(modHeading);
    modsList_ = new QListWidget;
    modsList_->setAccessibleName("Mod schema 2 terpasang dalam urutan profil");
    modLayout->addWidget(modsList_, 1);
    selection_ = new QLabel;
    selection_->setWordWrap(true);
    selection_->setTextFormat(Qt::PlainText);
    modLayout->addWidget(selection_);
    auto *modActions = new QGridLayout;
    enable_ = new QPushButton("Aktifkan");
    remove_ = new QPushButton("Lepas mod");
    up_ = new QPushButton("Naik");
    down_ = new QPushButton("Turun");
    up_->setToolTip("Ubah urutan profil. Resolver tetap mengikuti dependensi dan aturan urutan mod.");
    down_->setToolTip(up_->toolTip());
    remove_->setToolTip("Pindahkan mod ke penyimpanan mod yang dilepas.");
    modActions->addWidget(enable_, 0, 0);
    modActions->addWidget(remove_, 0, 1);
    modActions->addWidget(up_, 1, 0);
    modActions->addWidget(down_, 1, 1);
    modLayout->addLayout(modActions);
    splitter->addWidget(modColumn);

    auto *inspection = new QWidget;
    auto *inspectionLayout = new QVBoxLayout(inspection);
    inspectionLayout->setContentsMargins(0, 0, 0, 0);
    auto *reportHeading = new QLabel("Hasil validasi dan urutan efektif");
    reportHeading->setProperty("role", "section");
    inspectionLayout->addWidget(reportHeading);
    report_ = new QTextEdit;
    report_->setReadOnly(true);
    report_->setAccessibleName("Laporan resolver mod schema 2");
    report_->setMinimumHeight(110);
    report_->setMaximumHeight(180);
    inspectionLayout->addWidget(report_);
    search_ = new QLineEdit;
    search_->setPlaceholderText("Cari ID, tipe, atau nama konten…");
    search_->setAccessibleName("Cari konten mod schema 2");
    inspectionLayout->addWidget(search_);
    recordCount_ = new QLabel;
    recordCount_->setTextFormat(Qt::PlainText);
    inspectionLayout->addWidget(recordCount_);
    auto *recordsSplitter = new QSplitter(Qt::Horizontal);
    recordsList_ = new QListWidget;
    recordsList_->setAccessibleName("Record registry mod schema 2 yang telah dimuat");
    preview_ = new QTextEdit;
    preview_->setReadOnly(true);
    preview_->setAccessibleName("Pratinjau JSON record mod schema 2");
    recordsSplitter->addWidget(recordsList_);
    recordsSplitter->addWidget(preview_);
    recordsSplitter->setStretchFactor(0, 1);
    recordsSplitter->setStretchFactor(1, 2);
    inspectionLayout->addWidget(recordsSplitter, 1);
    splitter->addWidget(inspection);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    layout->addWidget(splitter, 1);
    status_ = new QLabel;
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    layout->addWidget(status_);

    connect(install, &QPushButton::clicked, this, [this] { installMod(); });
    connect(refreshButton, &QPushButton::clicked, this, [this] { refresh(); });
    connect(enable_, &QPushButton::clicked, this, [this] { toggleMod(); });
    connect(remove_, &QPushButton::clicked, this, [this] { removeMod(); });
    connect(up_, &QPushButton::clicked, this, [this] { moveMod(-1); });
    connect(down_, &QPushButton::clicked, this, [this] { moveMod(1); });
    connect(modsList_, &QListWidget::currentItemChanged, this, [this] { showSelection(); });
    connect(search_, &QLineEdit::textChanged, this, [this] { filterRecords(); });
    connect(recordsList_, &QListWidget::currentItemChanged, this, [this] { showRecord(); });
    refresh();
}

QString ModcorePanel::selectedModId() const {
    const auto *item = modsList_->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void ModcorePanel::refresh(const QString &preferredMod) {
    const QString selected = preferredMod.isEmpty() ? selectedModId() : preferredMod;
    try {
        mods_ = library_.mods();
        {
            const QSignalBlocker blocker(modsList_);
            modsList_->clear();
            for (const auto &mod : mods_) {
                auto *item = new QListWidgetItem(QString("[%1] %2%3 · %4\n%5")
                    .arg(mod.enabled ? "aktif" : "nonaktif", mod.error.isEmpty() ? "" : "[error] ", mod.name, mod.version, mod.id), modsList_);
                item->setData(Qt::UserRole, mod.id);
                item->setToolTip(mod.directory + (mod.error.isEmpty() ? QString() : "\n\n" + mod.error));
                if (mod.id == selected) modsList_->setCurrentItem(item);
            }
            if (!modsList_->currentItem() && modsList_->count()) modsList_->setCurrentRow(0);
        }
        showSelection();
        const auto result = library_.inspect();
        records_ = result.value("records").toObject();
        const auto requested = ids(result.value("enabled"));
        const auto loaded = ids(result.value("loaded"));
        QString report = "Urutan profil (diminta):\n" + orderText(requested)
            + "\n\nUrutan efektif (hasil resolver):\n" + orderText(loaded);
        if (requested != loaded) report += "\n\nUrutan efektif berbeda dari profil; lihat laporan resolver.";
        const QString resolver = result.value("report").toString().trimmed();
        if (!resolver.isEmpty()) report += "\n\n" + resolver;
        for (const auto &error : result.value("errors").toArray()) {
            const QString message = error.toString();
            if (!message.isEmpty() && !report.contains(message)) report += "\n\n" + message;
        }
        report_->setPlainText(report);
        status_->setText(QString("Validasi paket: %1. %2 dari %3 mod aktif dimuat; %4 record. Konten gameplay baru belum tersedia.")
            .arg(result.value("ok").toBool() ? "lolos" : "ada kesalahan")
            .arg(loaded.size()).arg(requested.size()).arg(records_.size()));
    } catch (const std::exception &e) {
        records_ = {};
        report_->setPlainText(QString::fromUtf8(e.what()));
        status_->setText("Pemeriksaan mod gagal. Periksa laporan, lalu muat ulang.");
        showSelection();
    }
    filterRecords();
}

void ModcorePanel::showSelection() {
    const int row = modsList_->currentRow();
    const bool selected = row >= 0 && row < mods_.size();
    enable_->setEnabled(selected);
    remove_->setEnabled(selected);
    up_->setEnabled(selected && row > 0);
    down_->setEnabled(selected && row + 1 < mods_.size());
    if (!selected) {
        enable_->setText("Aktifkan");
        selection_->setText("Belum ada paket terpilih. Pasang ZIP mod schema 2 untuk mulai.");
        showRecord();
        return;
    }
    const auto &mod = mods_.at(row);
    enable_->setEnabled(mod.enabled || mod.error.isEmpty());
    enable_->setText(mod.enabled ? "Nonaktifkan" : "Aktifkan");
    enable_->setToolTip(!mod.error.isEmpty() && !mod.enabled
        ? "Paket bermasalah tidak bisa diaktifkan. Lepas lalu pasang kembali paket yang benar."
        : "Aktifkan atau nonaktifkan paket dalam profil.");
    selection_->setText(mod.id + " · " + mod.version
        + (mod.error.isEmpty() ? QString() : "\nPaket bermasalah; lihat detail pada tooltip paket."));
    showRecord();
}

void ModcorePanel::filterRecords() {
    const QString selected = recordsList_->currentItem() ? recordsList_->currentItem()->data(Qt::UserRole).toString() : QString();
    const QString query = search_->text().trimmed();
    {
        const QSignalBlocker blocker(recordsList_);
        recordsList_->clear();
        for (auto it = records_.begin(); it != records_.end(); ++it) {
            const auto record = it.value().toObject();
            const QString type = record.value("type").toString(), name = record.value("name").toString();
            if (!query.isEmpty() && !it.key().contains(query, Qt::CaseInsensitive)
                && !type.contains(query, Qt::CaseInsensitive) && !name.contains(query, Qt::CaseInsensitive)) continue;
            auto *item = new QListWidgetItem(it.key() + "\n" + type + (name.isEmpty() ? QString() : " · " + name), recordsList_);
            item->setData(Qt::UserRole, it.key());
            if (it.key() == selected) recordsList_->setCurrentItem(item);
        }
        if (!recordsList_->currentItem() && recordsList_->count()) recordsList_->setCurrentRow(0);
    }
    recordCount_->setText(QString("%1 dari %2 record · pratinjau registry").arg(recordsList_->count()).arg(records_.size()));
    showRecord();
}

void ModcorePanel::showRecord() {
    if (!recordsList_->currentItem()) {
        if (!search_->text().trimmed().isEmpty()) {
            preview_->setPlainText("Tidak ada record yang cocok. Ubah pencarian untuk melihat record lain.");
            return;
        }
        const int row = modsList_->currentRow();
        if (row >= 0 && row < mods_.size()) {
            const auto &mod = mods_.at(row);
            preview_->setPlainText(mod.error.isEmpty()
                ? QString::fromUtf8(QJsonDocument(mod.manifest).toJson(QJsonDocument::Indented))
                : "Paket bermasalah: " + mod.id + "\n\n" + mod.error);
        } else {
            preview_->setPlainText("Pilih paket untuk melihat manifest. Paket pengganti teks dapat dipakai tanpa record konten.");
        }
        return;
    }
    const QString id = recordsList_->currentItem()->data(Qt::UserRole).toString();
    auto record = records_.value(id).toObject();
    record.insert("id", id);
    preview_->setPlainText(QString::fromUtf8(QJsonDocument(record).toJson(QJsonDocument::Indented)));
}

void ModcorePanel::installMod() {
    const QString path = QFileDialog::getOpenFileName(this, "Pasang mod schema 2", {}, "Paket mod ZIP (*.zip)");
    if (path.isEmpty()) return;
    try {
        const auto mod = library_.installZip(path);
        refresh(mod.id);
        status_->setText("Mod " + mod.id + " terpasang dalam keadaan nonaktif. Pilih Aktifkan untuk memuatnya.");
    } catch (const std::exception &e) { QMessageBox::critical(this, "Pemasangan mod gagal", QString::fromUtf8(e.what())); }
}

void ModcorePanel::toggleMod() {
    const int row = modsList_->currentRow();
    if (row < 0 || row >= mods_.size()) return;
    const auto mod = mods_.at(row);
    try { library_.setEnabled(mod.id, !mod.enabled); refresh(mod.id); }
    catch (const std::exception &e) { QMessageBox::critical(this, "Aktivasi mod gagal", QString::fromUtf8(e.what())); }
}

void ModcorePanel::moveMod(int delta) {
    const QString id = selectedModId();
    if (id.isEmpty()) return;
    try { library_.moveMod(id, delta); refresh(id); }
    catch (const std::exception &e) { QMessageBox::critical(this, "Urutan mod gagal diubah", QString::fromUtf8(e.what())); }
}

void ModcorePanel::removeMod() {
    const QString id = selectedModId();
    if (id.isEmpty()) return;
    try {
        const QString removed = library_.removeMod(id);
        refresh();
        status_->setText("Mod " + id + " dilepas. File disimpan di: " + removed);
    } catch (const std::exception &e) { QMessageBox::critical(this, "Pelepasan mod gagal", QString::fromUtf8(e.what())); }
}
