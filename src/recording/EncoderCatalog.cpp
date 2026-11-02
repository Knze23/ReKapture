#include "EncoderCatalog.h"

#include <algorithm>
#include <functional>

namespace EncoderCatalog {

QVector<EncoderChoice> detect(const QStringList &ids)
{
    auto findId = [&ids](const std::function<bool(const QString &)> &match) {
        for (const QString &id : ids)
            if (match(id))
                return id;
        return QString();
    };

    EncoderChoice nvenc;
    nvenc.key = QStringLiteral("nvenc");
    nvenc.label = QStringLiteral("NVIDIA NVENC (H.264)");
    nvenc.obsId = ids.contains(QStringLiteral("obs_nvenc_h264_tex")) ? QStringLiteral("obs_nvenc_h264_tex") : QString();
    nvenc.reason = QStringLiteral("No se detectó NVENC (requiere una GPU NVIDIA compatible y su controlador).");

    EncoderChoice amf;
    amf.key = QStringLiteral("amf");
    amf.label = QStringLiteral("AMD AMF (H.264)");
    amf.obsId = findId([](const QString &id) {
        return id.contains(QStringLiteral("amf"), Qt::CaseInsensitive) && id.contains(QStringLiteral("h264"), Qt::CaseInsensitive);
    });
    amf.reason = QStringLiteral("No se detectó AMF (requiere una GPU AMD compatible).");

    EncoderChoice qsv;
    qsv.key = QStringLiteral("qsv");
    qsv.label = QStringLiteral("Intel Quick Sync (H.264)");
    qsv.obsId = findId([](const QString &id) {
        return id.contains(QStringLiteral("qsv"), Qt::CaseInsensitive) && !id.contains(QStringLiteral("av1"), Qt::CaseInsensitive)
               && !id.contains(QStringLiteral("hevc"), Qt::CaseInsensitive);
    });
    qsv.reason = QStringLiteral("No se detectó Quick Sync (requiere una GPU o CPU Intel compatible).");

    EncoderChoice x264;
    x264.key = QStringLiteral("x264");
    x264.label = QStringLiteral("x264 (CPU)");
    x264.obsId = ids.contains(QStringLiteral("obs_x264")) ? QStringLiteral("obs_x264") : QString();
    x264.reason = QStringLiteral("No se detectó el codificador x264.");

    QVector<EncoderChoice> list = {nvenc, amf, qsv, x264};
    for (EncoderChoice &c : list)
        c.available = !c.obsId.isEmpty();
    return list;
}

QString resolve(const QVector<EncoderChoice> &choices, const QString &key, QString *usedLabel)
{
    if (key != QStringLiteral("auto")) {
        for (const EncoderChoice &c : choices)
            if (c.key == key && c.available) {
                if (usedLabel)
                    *usedLabel = c.label;
                return c.obsId;
            }
        return QString();
    }
    for (const EncoderChoice &c : choices)
        if (c.available) {
            if (usedLabel)
                *usedLabel = c.label;
            return c.obsId;
        }
    return QString();
}

int bitrateKbps(int quality, int width, int height, int fps)
{
    static const int base[] = {6000, 12000, 20000, 35000}; // a 1920x1080 y 60 FPS
    const int q = std::clamp(quality, 0, 3);
    const double scale = (static_cast<double>(width) * height / (1920.0 * 1080.0)) * (static_cast<double>(fps) / 60.0);
    return std::max(2500, static_cast<int>(base[q] * scale));
}

QStringList qualityLabels()
{
    return {QStringLiteral("Baja"), QStringLiteral("Media"), QStringLiteral("Alta"), QStringLiteral("Muy alta")};
}

} // namespace EncoderCatalog
