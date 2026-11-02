#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct EncoderChoice {
    QString key;       // nvenc | amf | qsv | x264
    QString label;     // texto para la interfaz
    QString obsId;     // id real registrado en libobs (vacio si no esta disponible)
    bool available = false;
    QString reason;    // explicacion cuando no esta disponible
};

// Detecta los codificadores H.264 realmente registrados por libobs. No incluye cabeceras de OBS.
namespace EncoderCatalog {

QVector<EncoderChoice> detect(const QStringList &registeredEncoders);

// "auto" elige el primero disponible (NVENC, AMF, Quick Sync, x264).
// Devuelve el id de OBS o una cadena vacia si no hay ninguno; usedLabel recibe el nombre elegido.
QString resolve(const QVector<EncoderChoice> &choices, const QString &key, QString *usedLabel);

// Bitrate objetivo (CBR) segun el perfil de calidad, la resolucion y los FPS.
int bitrateKbps(int quality, int width, int height, int fps);
QStringList qualityLabels();

} // namespace EncoderCatalog
