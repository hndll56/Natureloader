#pragma once

#include "modcore_library.h"
#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTextEdit;

class ModcorePanel : public QWidget {
public:
    explicit ModcorePanel(QString dataRoot, QWidget *parent = nullptr);

private:
    nature::ModcoreLibrary library_;
    QList<nature::ModcoreMod> mods_;
    QJsonObject records_;
    QListWidget *modsList_, *recordsList_;
    QLineEdit *search_;
    QTextEdit *report_, *preview_;
    QLabel *status_, *selection_, *recordCount_;
    QPushButton *enable_, *up_, *down_, *remove_;

    QString selectedModId() const;
    void refresh(const QString &preferredMod = {});
    void showSelection();
    void filterRecords();
    void showRecord();
    void installMod();
    void toggleMod();
    void moveMod(int delta);
    void removeMod();
};
