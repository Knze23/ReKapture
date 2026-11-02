#include "VideoLibrary.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#include <algorithm>

namespace VideoLibrary {

QVector<VideoEntry> scan(const QString &rootFolder)
{
    QVector<VideoEntry> list;
    const QDir root(rootFolder);
    if (!root.exists())
        return list;

    QDirIterator it(rootFolder, {QStringLiteral("*.mp4"), QStringLiteral("*.mkv"), QStringLiteral("*.mov")},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo fi(it.next());
        if (fi.size() <= 0)
            continue;
        const QString base = fi.completeBaseName();
        if (base.endsWith(QStringLiteral("_MIC"), Qt::CaseInsensitive) || base.endsWith(QStringLiteral("_CAM"), Qt::CaseInsensitive))
            continue; // es el archivo del micrófono o de la webcam de otra grabación

        VideoEntry e;
        e.path = fi.absoluteFilePath();
        e.name = base;
        e.modified = fi.lastModified();
        e.size = fi.size();

        // Estructura: <raiz>/<Juego>/[Clips/]archivo
        const QStringList parts = root.relativeFilePath(e.path).split(QLatin1Char('/'));
        QStringList folders = parts;
        folders.removeLast(); // quita el nombre del archivo
        for (const QString &f : std::as_const(folders))
            if (f.compare(QStringLiteral("Clips"), Qt::CaseInsensitive) == 0)
                e.isClip = true;
        const bool firstIsClips = !folders.isEmpty() && folders.first().compare(QStringLiteral("Clips"), Qt::CaseInsensitive) == 0;
        e.game = (folders.isEmpty() || firstIsClips) ? QStringLiteral("(Sin juego)") : folders.first();

        const QString prefix = fi.absolutePath() + QStringLiteral("/") + base;
        for (const QString &ext : {fi.suffix(), QStringLiteral("wav"), QStringLiteral("mp3"), QStringLiteral("mp4"), QStringLiteral("mkv")}) {
            const QString mic = prefix + QStringLiteral("_MIC.") + ext;
            if (QFileInfo::exists(mic)) {
                e.micPath = mic;
                break;
            }
        }
        const QString cam = prefix + QStringLiteral("_CAM.") + fi.suffix();
        if (QFileInfo::exists(cam))
            e.camPath = cam;

        list.append(e);
    }

    std::sort(list.begin(), list.end(), [](const VideoEntry &a, const VideoEntry &b) { return a.modified > b.modified; });
    return list;
}

} // namespace VideoLibrary
