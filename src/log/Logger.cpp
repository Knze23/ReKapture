#include "Logger.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QObject>
#include <QRegularExpression>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include "../config/AppPaths.h"

namespace {

QMutex g_mutex;
QFile g_file;
QString g_dir;
QString g_path;
QObject *g_sink = nullptr;
QElapsedTimer g_flushTimer;

} // namespace

namespace Logger {

QString logDirPath()
{
    return g_dir;
}

QString logFilePath()
{
    return g_path;
}

void init()
{
    QMutexLocker lock(&g_mutex);
    if (g_file.isOpen())
        return;

    g_dir = AppPaths::logDir();
    g_path = g_dir + QStringLiteral("/rekapture.log");

    // Rotacion sencilla: si pasa de 5 MB, se conserva una copia .old
    QFile existing(g_path);
    if (existing.exists() && existing.size() > 5 * 1024 * 1024) {
        QFile::remove(g_path + QStringLiteral(".old"));
        existing.rename(g_path + QStringLiteral(".old"));
    }

    g_file.setFileName(g_path);
    g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

void write(const QString &level, const QString &message)
{
    QMutexLocker lock(&g_mutex);
    if (!g_file.isOpen())
        return;

    const QString line = QStringLiteral("%1 [%2] %3\n")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
                                  level, message);
    g_file.write(line.toUtf8());

    // Los avisos y errores se guardan al instante; el resto, como mucho cada 250 ms (menos escrituras a disco)
    const bool urgent = level.contains(QStringLiteral("WARN")) || level.contains(QStringLiteral("ERROR"))
                        || level.contains(QStringLiteral("FATAL"));
    if (urgent || !g_flushTimer.isValid() || g_flushTimer.elapsed() > 250) {
        g_file.flush();
        g_flushTimer.restart();
    }

    if (g_sink)
        QMetaObject::invokeMethod(g_sink, "appendLogLine", Qt::QueuedConnection, Q_ARG(QString, line.trimmed()));
}

void setSink(QObject *receiver)
{
    QMutexLocker lock(&g_mutex);
    g_sink = receiver;
}

void flush()
{
    QMutexLocker lock(&g_mutex);
    if (g_file.isOpen())
        g_file.flush();
}

QStringList recentEntries(int maxEntries)
{
    flush();
    QStringList entries;
    QFile in(g_path);
    if (g_path.isEmpty() || !in.open(QIODevice::ReadOnly))
        return entries;

    // Solo se lee el final del archivo
    const qint64 tail = 300 * 1024;
    if (in.size() > tail)
        in.seek(in.size() - tail);
    const QString text = QString::fromUtf8(in.readAll());

    static const QRegularExpression start(QStringLiteral("^\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}\\.\\d{3} \\["));
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString l = raw.trimmed().isEmpty() ? QString() : raw;
        if (start.match(l).hasMatch())
            entries.append(l.trimmed());
        else if (!entries.isEmpty() && !l.isEmpty())
            entries.last() += QLatin1Char('\n') + l.trimmed(); // continuacion de la entrada anterior
    }
    while (entries.size() > maxEntries)
        entries.removeFirst();
    return entries;
}

} // namespace Logger
