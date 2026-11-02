#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

struct VideoEntry {
    QString path;
    QString name;        // nombre del archivo sin extension
    QString game;        // carpeta del juego ("(Sin juego)" si va suelto)
    QDateTime modified;
    qint64 size = 0;
    bool isClip = false;
    QString micPath;     // archivo _MIC asociado: MP4, WAV o MP3 (vacio si no existe)
    QString camPath;     // archivo _CAM (webcam) asociado (vacio si no existe)
};

namespace VideoLibrary {

// Busca los videos grabados bajo la carpeta de grabaciones. Los archivos _MIC no salen como
// entradas propias: se asocian a su video principal. Ordenado del mas reciente al mas antiguo.
QVector<VideoEntry> scan(const QString &rootFolder);

} // namespace VideoLibrary
