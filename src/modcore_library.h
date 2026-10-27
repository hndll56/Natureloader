#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

namespace nature {

struct ModcoreMod {
    QString id, name, version, directory;
    bool enabled = false;
    QJsonObject manifest;
    QString error; // A broken installed package remains visible and removable.
};

// Schema 2 authoring workspace; profiles and ID state are separate from BTN saves.
class ModcoreLibrary {
public:
    explicit ModcoreLibrary(QString root);
    QString root() const { return root_; }
    ModcoreMod installZip(const QString &zip);
    QList<ModcoreMod> mods() const;
    void setEnabled(const QString &id, bool enabled);
    void moveMod(const QString &id, int delta);
    QString removeMod(const QString &id);
    // {ok, contentGameplayReady:false, enabled:[], loaded:[], errors:[],
    //  report:string, records:{id:{type,name,data,handle,owner}}}.
    QJsonObject inspect() const;
    // Merge active text-only packs in resolved order. Unsupported gameplay data fails.
    QJsonObject nativeTexts() const;

private:
    QString root_;
    QJsonObject profile() const;
    void saveProfile(const QJsonObject &proposed) const;
};

} // namespace nature
