#pragma once

#include <QString>

// Rutas de datos de usuario. En modo portable (existe "portable.flag" en la carpeta raiz del
// programa) todo se guarda junto al programa, en UserData\; si no, en las carpetas de Windows.
namespace AppPaths {

QString rootDir();     // carpeta raiz del runtime (el .exe esta en <raiz>\bin\64bit)
bool isPortable();
QString configDir();   // settings.ini
QString logDir();      // rekapture.log
QString skinsDir();    // skins personalizadas (.json)

} // namespace AppPaths
