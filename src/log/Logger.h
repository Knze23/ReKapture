#pragma once

#include <QString>
#include <QStringList>

class QObject;

// Registro de eventos en un archivo de texto (seguro entre hilos).
// El archivo queda en %LOCALAPPDATA%\ReKapture\logs\rekapture.log
namespace Logger {

void init();
void write(const QString &level, const QString &message);
QString logFilePath();
QString logDirPath();

// Cada linea nueva se envia (en cola) al metodo appendLogLine(QString) del objeto indicado.
// Pasar nullptr para dejar de recibirlas.
void setSink(QObject *receiver);

// Ultimas entradas del registro (una entrada puede ocupar varias lineas).
QStringList recentEntries(int maxEntries);
void flush();

inline void info(const QString &m)  { write(QStringLiteral("INFO"), m); }
inline void warn(const QString &m)  { write(QStringLiteral("WARN"), m); }
inline void error(const QString &m) { write(QStringLiteral("ERROR"), m); }

} // namespace Logger
