#pragma once
#include <QString>
namespace nature {
// Merge the previous split workspace once, retaining original profiles and v2 packs.
void migrateModWorkspace(const QString &root);
}
