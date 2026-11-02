#pragma once

#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>

// Acceso a las funciones de video del Explorador de Windows: miniaturas, duracion, papelera.
// No necesita bibliotecas de video adicionales (usa los codecs que ya trae Windows).
namespace ShellMedia {

QImage thumbnail(const QString &path, const QSize &size); // imagen nula si Windows no puede generarla
qint64 durationMs(const QString &path);                   // -1 si no se conoce
bool moveToRecycleBin(const QStringList &paths, QString *errorOut);
void showInFolder(const QString &path);                   // abre el Explorador con el archivo seleccionado

} // namespace ShellMedia
