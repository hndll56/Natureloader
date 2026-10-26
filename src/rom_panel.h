#pragma once
#include "rom_catalog.h"
#include <QWidget>
class QLabel;
class QListWidget;
class QTextEdit;
class QPushButton;
class QProcess;

class RomPanel : public QWidget {
public:
    explicit RomPanel(const QString &dataRoot, QWidget *parent = nullptr);
private:
    nature::RomCatalog catalog_;
    QString dataRoot_;
    QListWidget *list_;
    QTextEdit *details_;
    QLabel *status_;
    QPushButton *play_;
    QProcess *game_ = nullptr;
    void launchNative();
    void refresh();
    void showSelection();
    void importRom();
    void runCommand(const QStringList &arguments);
};
