#pragma once

#include <QString>

// Deteccion del nombre del juego para organizar las grabaciones por carpeta.
namespace GameNaming {

struct WindowInfo {
    QString title;     // titulo de la ventana
    QString exeName;   // nombre del ejecutable (sin ruta)
    bool isEmpty() const { return title.isEmpty() && exeName.isEmpty(); }
};

// Ventana en primer plano, ignorando ReKapture y el escritorio/Explorador.
WindowInfo foreground();

// Convierte "titulo:clase:ejecutable" (formato de OBS) en una ventana.
WindowInfo fromObsWindowId(const QString &id);

// Nombre limpio para usar como carpeta/archivo (sin caracteres no validos).
QString displayName(const WindowInfo &info);
QString sanitize(const QString &name);

} // namespace GameNaming
