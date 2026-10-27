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

// Shared package/profile backend for the launcher; modcore stays an internal engine.
// Authoring ID state is separate from BTN gameplay saves.
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
    //  nativeReady,nativeErrors,nativeTextCount,nativeItemPatchCount,
    //  report:string, records:{id:{type,name,data,handle,owner}}}.
    QJsonObject inspect() const;
    // {texts:{key:string},items:[{id,handle,field,value,provider}]} after load-order resolution.
    // Only guarded retail item properties and text overrides are supported by BTN.
    QJsonObject nativePlan() const;
    // Compatibility accessor for the text portion; unsupported native data still fails.
    QJsonObject nativeTexts() const;

private:
    QString root_;
    QJsonObject profile() const;
    void saveProfile(const QJsonObject &proposed) const;
};

} // namespace nature
