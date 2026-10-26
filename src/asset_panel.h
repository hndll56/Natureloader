#pragma once
#include "texture_decoder.h"
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QPixmap>
#include <QSpinBox>
#include <QSaveFile>
#include <QVBoxLayout>
#include <QWidget>
#include <stdexcept>

class AssetPanel : public QWidget {
public:
    explicit AssetPanel(QString dataRoot, QWidget *parent = nullptr) : QWidget(parent), root_(std::move(dataRoot)) {
        auto *layout = new QVBoxLayout(this);
        auto *scope = new QLabel("Pembacaan dan rendering tekstur asli oleh kode PC. Pilih blok tekstur dan CLUT secara eksplisit.\n"
            "Menggunakan data USA asli yang sudah diimpor; belum terhubung ke rendering gameplay.");
        scope->setWordWrap(true); layout->addWidget(scope);
        auto *controls = new QHBoxLayout;
        offset_ = new QSpinBox; offset_->setRange(0, 200 * 1024 * 1024); offset_->setPrefix("Offset: ");
        offset_->setAccessibleName("Offset kontainer dalam A_FILE.BIN");
        auto *load = new QPushButton("Baca kontainer 0D");
        controls->addWidget(offset_); controls->addWidget(load); layout->addLayout(controls);
        auto *selection = new QHBoxLayout;
        imageBlock_ = new QComboBox; paletteBlock_ = new QComboBox; depth_ = new QComboBox;
        depth_->addItems({"4 bpp + CLUT", "8 bpp + CLUT", "Word BGR555 (diagnostik)"});
        imageBlock_->setAccessibleName("Blok tekstur"); paletteBlock_->setAccessibleName("Blok palette");
        selection->addWidget(new QLabel("Tekstur")); selection->addWidget(imageBlock_, 1);
        selection->addWidget(new QLabel("Palette")); selection->addWidget(paletteBlock_, 1); selection->addWidget(depth_);
        layout->addLayout(selection);
        preview_ = new QLabel("Impor data USA pada tab mod, lalu baca kontainer.");
        preview_->setAlignment(Qt::AlignCenter); preview_->setMinimumHeight(220);
        preview_->setStyleSheet("background: #c5bb97; border: 2px solid #9b8154; border-radius: 6px;");
        layout->addWidget(preview_, 1);
        info_ = new QLabel; info_->setWordWrap(true); info_->setTextFormat(Qt::PlainText); layout->addWidget(info_);
        export_ = new QPushButton("Ekspor gambar PNG…");
        export_->setEnabled(false);
        export_->setToolTip("Menyimpan piksel hasil decode pada ukuran asli, tanpa pembesaran preview.");
        layout->addWidget(export_);
        auto *note = new QLabel("Contoh offset pada arsip USA: 0, 11548, 156636. Palette dipakai dari baris pertama; blending semitransparan PS1 belum disimulasikan.");
        note->setWordWrap(true); layout->addWidget(note);
        connect(load, &QPushButton::clicked, this, [this] { loadContainer(); });
        connect(export_, &QPushButton::clicked, this, [this] { exportImage(); });
        for (auto *box : {imageBlock_, paletteBlock_, depth_})
            connect(box, &QComboBox::currentIndexChanged, this, [this] { draw(); });
    }
private:
    QString root_;
    nature::TextureContainer container_;
    QImage decodedImage_;
    int loadedOffset_ = 0;
    QSpinBox *offset_;
    QComboBox *imageBlock_, *paletteBlock_, *depth_;
    QLabel *preview_, *info_;
    QPushButton *export_;
    void loadContainer() {
        try {
            QFile archive(root_ + "/base/A_FILE.BIN");
            if (!archive.open(QIODevice::ReadOnly) || !archive.seek(offset_->value())) throw std::runtime_error("Impor data USA asli terlebih dahulu.");
            auto decoded = nature::decodeContainer0D(archive.read(8 * 1024 * 1024));
            container_ = std::move(decoded);
            loadedOffset_ = offset_->value();
            imageBlock_->blockSignals(true); paletteBlock_->blockSignals(true);
            imageBlock_->clear(); paletteBlock_->clear();
            for (int i = 0; i < container_.blocks.size(); ++i) {
                const auto &block = container_.blocks[i];
                if (block.isPalette()) paletteBlock_->addItem(QString("%1 (%2 warna)").arg(i).arg(block.widthWords), i);
                else imageBlock_->addItem(QString("%1 (%2 word × %3)").arg(i).arg(block.widthWords).arg(block.height), i);
            }
            imageBlock_->blockSignals(false); paletteBlock_->blockSignals(false);
            draw();
        } catch (const std::exception &e) {
            container_ = {}; decodedImage_ = {}; export_->setEnabled(false);
            imageBlock_->clear(); paletteBlock_->clear();
            preview_->clear(); info_->setText(QString::fromUtf8(e.what()));
        }
    }
    void draw() {
        decodedImage_ = {}; export_->setEnabled(false);
        paletteBlock_->setEnabled(depth_->currentIndex() != 2);
        try {
            const int selected = imageBlock_->currentData().toInt();
            if (imageBlock_->currentIndex() < 0 || selected < 0 || selected >= container_.blocks.size()) {
                preview_->clear(); info_->setText("Belum ada blok tekstur yang dapat ditampilkan."); return;
            }
            const auto &block = container_.blocks.at(selected);
            QImage image;
            if (depth_->currentIndex() == 2) image = nature::previewBgr555(block);
            else {
                const int palette = paletteBlock_->currentData().toInt();
                if (paletteBlock_->currentIndex() < 0 || palette < 0 || palette >= container_.blocks.size())
                    throw std::runtime_error("Kontainer ini tidak memiliki blok palette yang ditandai.");
                image = nature::previewIndexed(block, container_.blocks.at(palette), depth_->currentIndex() == 0 ? 4 : 8);
            }
            if (image.isNull()) throw std::runtime_error("Gambar hasil decode kosong.");
            const int zoom = qMax(1, qMin(8, qMin(qMax(1, preview_->width() - 24) / image.width(), qMax(1, preview_->height() - 24) / image.height())));
            preview_->setPixmap(QPixmap::fromImage(image.scaled(image.size() * zoom, Qt::KeepAspectRatio, Qt::FastTransformation)));
            decodedImage_ = image; export_->setEnabled(true);
            info_->setText(QString("Offset %1 • Blok %2 • %3 • %4 blok • %5 byte kontainer\nHasil native: %6 × %7 piksel. Metadata ini belum mengidentifikasi nama karakter atau item.")
                .arg(loadedOffset_).arg(selected).arg(depth_->currentText()).arg(container_.blocks.size())
                .arg(container_.bytesConsumed).arg(image.width()).arg(image.height()));
        } catch (const std::exception &e) { preview_->clear(); info_->setText(QString::fromUtf8(e.what())); }
    }
    void exportImage() {
        if (decodedImage_.isNull()) return;
        const auto suggested = QString("btn-%1-block-%2.png").arg(loadedOffset_).arg(imageBlock_->currentData().toInt());
        const auto path = QFileDialog::getSaveFileName(this, "Ekspor tekstur pada ukuran asli", suggested, "PNG (*.png)");
        if (path.isEmpty()) return;
        QSaveFile output(path);
        if (!output.open(QIODevice::WriteOnly) || !decodedImage_.save(&output, "PNG") || !output.commit()) {
            QMessageBox::critical(this, "Ekspor gagal", "Gambar tidak dapat disimpan: " + output.errorString());
            return;
        }
        info_->setText(info_->text() + "\nPNG tersimpan: " + path);
    }
};
