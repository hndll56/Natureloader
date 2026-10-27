#include "core.h"
#include "native_mods.h"
#include "farm_banner.h"
#include "launcher_theme.h"
#include "rom_panel.h"
#include "asset_panel.h"
#include "modcore_library.h"
#include <QCheckBox>
#include <QApplication>
#include <QCommandLineParser>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <cstdio>
#include <memory>

class Launcher : public QWidget {
public:
    explicit Launcher(nature::Library &library) : library_(library) {
        setWindowTitle("NatureLoader — Native port workbench");
        resize(1120, 860);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(28, 24, 28, 24);
        layout->setSpacing(14);
        layout->addWidget(new FarmBanner(this));
        auto *notice = new QLabel("Koleksi ROM, peti mod, dan port PC. Boot native USA tersedia pada build riset; gameplay lengkap masih dikembangkan.");
        notice->setWordWrap(true);
        layout->addWidget(notice);
        auto *tabs = new QTabWidget;
        tabs->addTab(new RomPanel(library.root(), tabs), "Koleksi ROM");
        auto *workbench = new QWidget;
        auto *workLayout = new QVBoxLayout(workbench);
        tabs->addTab(workbench, "Peti mod");
        auto *contentPage = new QWidget;
        auto *contentLayout = new QVBoxLayout(contentPage);
        auto *contentNotice = new QLabel("Registry konten dari mod aktif. Data item, NPC, dan lokasi dapat diperiksa di sini; belum diterapkan ke gameplay.");
        contentNotice->setWordWrap(true);
        contentLayout->addWidget(contentNotice);
        auto *contentSearch = new QLineEdit;
        contentSearch->setPlaceholderText("Cari ID konten…");
        contentSearch->setAccessibleName("Cari konten mod aktif");
        contentLayout->addWidget(contentSearch);
        contentKeys_ = new QListWidget;
        contentKeys_->setAccessibleName("Registry konten mod aktif");
        contentLayout->addWidget(contentKeys_);
        contentPreview_ = new QTextEdit;
        contentPreview_->setReadOnly(true);
        contentPreview_->setAccessibleName("Data konten terpilih");
        contentLayout->addWidget(contentPreview_);
        tabs->addTab(contentPage, "Konten mod");
        connect(contentKeys_, &QListWidget::currentTextChanged, this, [this](const QString &id) {
            contentPreview_->setPlainText(contentRegistry_.contains(id)
                ? QString::fromUtf8(QJsonDocument(contentRegistry_.value(id).toObject()).toJson()) : QString());
        });
        connect(contentSearch, &QLineEdit::textChanged, this, [this](const QString &query) {
            filterContent(query);
        });
        connect(tabs, &QTabWidget::currentChanged, this, [this, contentSearch] {
            refreshContent(contentSearch->text());
        });
        tabs->addTab(new AssetPanel(library.root(), tabs), "Aset native USA");
        layout->addWidget(tabs, 1);
        status_ = new QLabel;
        status_->setTextFormat(Qt::PlainText);
        status_->setWordWrap(true);
        workLayout->addWidget(status_);
        auto *actions = new QHBoxLayout;
        auto *import = new QPushButton("Impor data USA asli…");
        auto *install = new QPushButton("Pasang mod ZIP…");
        auto *open = new QPushButton("Buka folder data");
        for (auto *button : {import, install, open}) actions->addWidget(button);
        workLayout->addLayout(actions);
        auto *columns = new QHBoxLayout;
        auto *left = new QVBoxLayout;
        left->addWidget(new QLabel("PETI MOD  ·  Prioritas mengikuti urutan dan dependensi"));
        mods_ = new QListWidget;
        mods_->setAccessibleName("Daftar mod");
        left->addWidget(mods_);
        auto *order = new QHBoxLayout;
        auto *toggle = new QPushButton("Aktif / nonaktif");
        auto *up = new QPushButton("Naik");
        auto *down = new QPushButton("Turun");
        order->addWidget(toggle); order->addWidget(up); order->addWidget(down);
        left->addLayout(order);
        auto *remove = new QPushButton("Lepas mod terpilih");
        remove->setToolTip("Menonaktifkan mod dan memindahkan filenya ke removed-mods agar bisa dipulihkan.");
        left->addWidget(remove);
        conflicts_ = new QTextEdit;
        conflicts_->setReadOnly(true);
        conflicts_->setMaximumHeight(110);
        conflicts_->setAccessibleName("Konflik mod");
        left->addWidget(conflicts_);
        auto *right = new QVBoxLayout;
        right->addWidget(new QLabel("BUKU CATATAN  ·  Teks dari ROM dan mod"));
        auto *search = new QLineEdit;
        search->setPlaceholderText("Cari ID teks…");
        search->setAccessibleName("Cari teks game");
        right->addWidget(search);
        keys_ = new QListWidget;
        keys_->setAccessibleName("ID teks game");
        for (const auto &value : nature::textLayout()) keys_->addItem(value.toObject().value("name").toString());
        right->addWidget(keys_);
        provider_ = new QLabel;
        provider_->setTextFormat(Qt::PlainText);
        right->addWidget(provider_);
        original_ = new QCheckBox("Bandingkan dengan teks asli (tanpa mengubah profil)");
        right->addWidget(original_);
        preview_ = new QTextEdit;
        preview_->setReadOnly(true);
        preview_->setAccessibleName("Hasil resolusi teks native");
        preview_->setFont(QFont("sans-serif", 18));
        preview_->setMinimumHeight(160);
        right->addWidget(preview_);
        columns->addLayout(left, 1); columns->addLayout(right, 2);
        workLayout->addLayout(columns, 1);
        auto *footer = new QLabel("ROM asli dibaca saja. Mod disimpan terpisah. {RAW:xxxx} menandai kontrol game yang belum dirender.");
        footer->setWordWrap(true);
        layout->addWidget(footer);
        setStyleSheet(launcherStyle());
        connect(import, &QPushButton::clicked, this, [this] {
            const auto path = QFileDialog::getOpenFileName(this, "Pilih ROM USA", {}, "Disc game (*.zip *.bin *.cue *.iso)");
            if (!path.isEmpty()) runImport(path);
        });
        connect(install, &QPushButton::clicked, this, [this] {
            const auto path = QFileDialog::getOpenFileName(this, "Pilih mod", {}, "NatureLoader mod (*.zip)");
            if (!path.isEmpty()) protect([&] { library_.installMod(path); refresh(); });
        });
        connect(open, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(library_.root())); });
        connect(toggle, &QPushButton::clicked, this, [this] {
            if (auto *item = mods_->currentItem()) protect([&] {
                library_.setEnabled(item->data(Qt::UserRole).toString(), !item->data(Qt::UserRole + 1).toBool()); refresh();
            });
        });
        connect(remove, &QPushButton::clicked, this, [this] {
            if (auto *item = mods_->currentItem()) protect([&] {
                const auto recovery = library_.removeMod(item->data(Qt::UserRole).toString());
                refresh(); status_->setText("Mod dilepas. Salinan pemulihan: " + recovery);
            });
        });
        connect(original_, &QCheckBox::toggled, this, [this] { updatePreview(); });
        for (auto pair : {qMakePair(up, -1), qMakePair(down, 1)})
            connect(pair.first, &QPushButton::clicked, this, [this, pair] {
                if (auto *item = mods_->currentItem()) protect([&] { library_.moveMod(item->data(Qt::UserRole).toString(), pair.second); refresh(); });
            });
        connect(keys_, &QListWidget::currentTextChanged, this, [this] { updatePreview(); });
        connect(search, &QLineEdit::textChanged, this, [this](const QString &query) {
            for (int i = 0; i < keys_->count(); ++i) keys_->item(i)->setHidden(!keys_->item(i)->text().contains(query, Qt::CaseInsensitive));
        });
        keys_->setCurrentRow(0);
        refresh();
    }
private:
    nature::Library &library_;
    QLabel *status_, *provider_;
    QListWidget *mods_, *keys_;
    QTextEdit *conflicts_, *preview_;
    QCheckBox *original_;
    QListWidget *contentKeys_;
    QTextEdit *contentPreview_;
    QJsonObject contentRegistry_;
    template<class F> void protect(F operation) {
        try { operation(); }
        catch (const std::exception &e) { QMessageBox::critical(this, "NatureLoader", QString::fromUtf8(e.what())); }
    }
    void filterContent(const QString &query) {
        QListWidgetItem *firstVisible = nullptr;
        for (int i = 0; i < contentKeys_->count(); ++i) {
            auto *item = contentKeys_->item(i);
            item->setHidden(!item->text().contains(query, Qt::CaseInsensitive));
            if (!item->isHidden() && !firstVisible) firstVisible = item;
        }
        if (!contentKeys_->currentItem() || contentKeys_->currentItem()->isHidden())
            contentKeys_->setCurrentItem(firstVisible);
        if (!firstVisible) contentPreview_->clear();
    }
    void refreshContent(const QString &query = {}) {
        const auto selected = contentKeys_->currentItem() ? contentKeys_->currentItem()->text() : QString();
        contentKeys_->clear();
        contentPreview_->clear();
        try {
            contentRegistry_ = library_.content();
            for (auto it = contentRegistry_.begin(); it != contentRegistry_.end(); ++it) {
                auto *item = new QListWidgetItem(it.key(), contentKeys_);
                item->setToolTip(it.value().toObject().value("name").toString());
                item->setHidden(!it.key().contains(query, Qt::CaseInsensitive));
                if (it.key() == selected) contentKeys_->setCurrentItem(item);
            }
            filterContent(query);
            if (contentRegistry_.isEmpty()) contentPreview_->setPlainText("Belum ada konten. Pasang dan aktifkan mod melalui tab Peti mod untuk melihat pratinjaunya.");
        } catch (const std::exception &e) {
            contentRegistry_ = {};
            contentPreview_->setPlainText(QString::fromUtf8(e.what()));
        }
    }
    void refresh() {
        const auto selected = mods_->currentItem() ? mods_->currentItem()->data(Qt::UserRole).toString() : QString();
        mods_->clear();
        for (const auto &mod : library_.mods()) {
            auto *item = new QListWidgetItem(QString("[%1] %2 — %3").arg(mod.enabled ? "ON" : "OFF", mod.name, mod.version), mods_);
            item->setData(Qt::UserRole, mod.id);
            item->setData(Qt::UserRole + 1, mod.enabled);
            item->setToolTip(mod.error.isEmpty() ? mod.id : mod.id + "\n" + mod.error);
            if (mod.id == selected) mods_->setCurrentItem(item);
        }
        const auto conflicts = library_.status().value("ok").toBool() ? library_.conflicts() : QStringList();
        const auto state = library_.status();
        QStringList loaded;
        for (const auto &id : state.value("loaded").toArray()) loaded.append(id.toString());
        QString report = "Urutan efektif: " + loaded.join(" → ") + "\n";
        report += conflicts.isEmpty() ? "Tidak ada konflik teks aktif." : conflicts.join('\n');
        if (!state.value("report").toString().isEmpty()) report += "\n" + state.value("report").toString();
        for (const auto &error : state.value("nativeErrors").toArray()) report += "\nNative: " + error.toString();
        conflicts_->setPlainText(report);
        status_->setText((library_.hasGame() ? "Executable USA terverifikasi • " : "Belum ada ROM • ") + library_.root()
            + QString(" • Mod native: %1 (%2 teks, %3 field item)")
                .arg(state.value("nativeReady").toBool() ? "didukung" : "ada hambatan")
                .arg(state.value("nativeTextCount").toInt()).arg(state.value("nativeItemPatchCount").toInt()));
        updatePreview();
        refreshContent();
    }
    void updatePreview() {
        try {
            if (!library_.hasGame()) { provider_->setText("Impor ROM untuk melihat teks asli."); preview_->clear(); return; }
            if (!keys_->currentItem()) return;
            QString provider;
            const auto key = keys_->currentItem()->text();
            if (original_->isChecked()) {
                preview_->setPlainText(nature::readObject(library_.root() + "/base/texts.json").value(key).toString());
                provider = "ROM (perbandingan; mod tetap pada profil)";
            } else preview_->setPlainText(library_.text(key, &provider));
            provider_->setText("Sumber aktif: " + provider);
        } catch (const std::exception &e) { provider_->setText(QString::fromUtf8(e.what())); preview_->clear(); }
    }
    void runImport(const QString &source) {
        auto *process = new QProcess(this);
        setEnabled(false);
        status_->setText("Mengimpor dan memverifikasi ROM…");
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process](int code, QProcess::ExitStatus status) {
                setEnabled(true);
                protect([&] { refresh(); });
                if (code != 0 || status != QProcess::NormalExit)
                    QMessageBox::critical(this, "Impor gagal", QString::fromUtf8(process->readAllStandardError()));
                process->deleteLater();
            });
        connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                setEnabled(true); protect([&] { refresh(); });
                QMessageBox::critical(this, "Impor gagal", process->errorString()); process->deleteLater();
            }
        });
        process->start(QCoreApplication::applicationFilePath(), {"--data", library_.root(), "--import", source});
    }
};

int main(int argc, char **argv) {
    bool cli = false;
    for (int i = 1; i < argc; ++i)
        if (QStringList{"--import", "--status", "--native-export", "--content", "--install", "--enable", "--disable", "--remove", "--text", "--catalog-import", "--catalog-list", "--catalog-select", "--catalog-verify", "--v2-install", "--v2-enable", "--v2-disable", "--v2-remove", "--v2-status", "--help", "-h"}.contains(QString::fromLocal8Bit(argv[i]))) cli = true;
    std::unique_ptr<QCoreApplication> app;
    if (cli) app = std::make_unique<QCoreApplication>(argc, argv);
    else app = std::make_unique<QApplication>(argc, argv);
    QCoreApplication::setApplicationName("NatureLoader");
    QCoreApplication::setOrganizationName("NatureLoader");
    QCommandLineParser parser;
    parser.setApplicationDescription("NatureLoader 0.3 — ROM catalog, experimental native launch, and mod workbench.");
    parser.addHelpOption();
    for (const auto &name : {"data", "native-export", "import", "install", "enable", "disable", "remove", "text", "screenshot", "catalog-import", "catalog-select", "catalog-verify", "v2-install", "v2-enable", "v2-disable", "v2-remove"})
        parser.addOption({name, QString("%1 value").arg(name), "value"});
    parser.addOption({"status", "Print library status as JSON."});
    parser.addOption({"content", "Print enabled mod content registry; not bound to gameplay."});
    parser.addOption({"v2-status", "Compatibility alias for shared mod/profile inspection as JSON."});
    parser.addOption({"catalog-list", "Print detected ROM variants, without claiming native compatibility."});
    parser.addOption({"smoke-test", "Construct the native launcher and exit."});
    parser.process(*app);
    try {
        QTemporaryDir smokeData;
        auto root = parser.value("data");
        if (root.isEmpty()) root = parser.isSet("smoke-test") ? smokeData.path() : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        nature::Library library(root);
        nature::RomCatalog catalog(root);
        QJsonObject result;
        if (parser.isSet("v2-install")) result = {{"installed", nature::ModcoreLibrary(root).installZip(parser.value("v2-install")).id}};
        else if (parser.isSet("v2-enable")) { nature::ModcoreLibrary v2(root); v2.setEnabled(parser.value("v2-enable"), true); result = v2.inspect(); }
        else if (parser.isSet("v2-disable")) { nature::ModcoreLibrary v2(root); v2.setEnabled(parser.value("v2-disable"), false); result = v2.inspect(); }
        else if (parser.isSet("v2-remove")) result = {{"recoveryDirectory", nature::ModcoreLibrary(root).removeMod(parser.value("v2-remove"))}};
        else if (parser.isSet("v2-status")) result = nature::ModcoreLibrary(root).inspect();
        else if (parser.isSet("catalog-import")) result = catalog.importRom(parser.value("catalog-import"));
        else if (parser.isSet("catalog-list")) result = {{"selected", catalog.selected()}, {"roms", catalog.entries()}};
        else if (parser.isSet("catalog-select")) { catalog.select(parser.value("catalog-select")); result = catalog.metadata(catalog.selected()); }
        else if (parser.isSet("catalog-verify")) {
            if (!catalog.verify(parser.value("catalog-verify"))) throw std::runtime_error("ROM verification failed; the stored image or metadata changed.");
            result = {{"verified", true}, {"id", parser.value("catalog-verify")}};
        }
        else if (parser.isSet("import")) result = library.importGame(parser.value("import"));
        else if (parser.isSet("install")) result = {{"installed", library.installMod(parser.value("install")).id}};
        else if (parser.isSet("enable")) { library.setEnabled(parser.value("enable"), true); result = library.status(); }
        else if (parser.isSet("disable")) { library.setEnabled(parser.value("disable"), false); result = library.status(); }
        else if (parser.isSet("remove")) result = {{"recoveryDirectory", library.removeMod(parser.value("remove"))}};
        else if (parser.isSet("status")) result = library.status();
        else if (parser.isSet("content")) result = library.content();
        else if (parser.isSet("native-export")) result = {{"directory", nature::exportNativeMods(library, parser.value("native-export"), library.root() + "/native-mods")}};
        else if (parser.isSet("text")) {
            QString provider;
            auto text = library.text(parser.value("text"), &provider);
            result = {{"key", parser.value("text")}, {"text", text}, {"provider", provider}};
        } else {
            Launcher window(library);
            window.show();
            if (parser.isSet("smoke-test") || parser.isSet("screenshot")) {
                QTimer::singleShot(100, &window, [&] {
                    if (parser.isSet("screenshot") && !window.grab().save(parser.value("screenshot"))) app->exit(1);
                    else app->exit(0);
                });
            }
            return app->exec();
        }
        const auto output = QJsonDocument(result).toJson();
        std::fwrite(output.constData(), 1, size_t(output.size()), stdout);
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "NatureLoader: %s\n", e.what());
        return 1;
    }
}
