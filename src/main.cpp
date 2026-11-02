#include "log/Logger.h"
#include "obs/ObsEngine.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QtGlobal>

namespace {

void qtMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    const char *level = nullptr;
    switch (type) {
    case QtWarningMsg:  level = "QT-WARN";  break;
    case QtCriticalMsg: level = "QT-ERROR"; break;
    case QtFatalMsg:    level = "QT-FATAL"; break;
    default:            return;
    }
    Logger::write(QString::fromLatin1(level), msg);
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ReKapture"));
    QApplication::setApplicationVersion(QStringLiteral("0.6.0"));

    QApplication::setStyle(QStringLiteral("Fusion"));

    Logger::init();
    qInstallMessageHandler(qtMessageHandler);

    // Prueba automatica sin ventana (la usa el empaquetado): comprueba que libobs y los modulos cargan.
    if (QCoreApplication::arguments().contains(QStringLiteral("--selftest"))) {
        ObsEngine engine;
        const ObsInitResult r = engine.initialize(1280, 720, 30);
        const bool ok = r.ok && r.modules.size() >= 6 && r.encoders.contains(QStringLiteral("obs_x264"))
                        && r.outputs.contains(QStringLiteral("mp4_output"));
        Logger::info(QStringLiteral("AUTOTEST: %1 (modulos=%2, codificadores=%3, mp4_output=%4, replay_buffer=%5)")
                         .arg(ok ? QStringLiteral("CORRECTO") : QStringLiteral("FALLO"))
                         .arg(r.modules.size())
                         .arg(r.encoders.size())
                         .arg(r.outputs.contains(QStringLiteral("mp4_output")) ? QStringLiteral("si") : QStringLiteral("no"))
                         .arg(r.outputs.contains(QStringLiteral("replay_buffer")) ? QStringLiteral("si") : QStringLiteral("no")));
        engine.shutdown();
        return ok ? 0 : 1;
    }

    Logger::info(QStringLiteral("=== ReKapture %1 iniciado ===").arg(QApplication::applicationVersion()));

    MainWindow window;
    window.show();

    const int code = app.exec();
    Logger::info(QStringLiteral("=== ReKapture terminado (codigo %1) ===").arg(code));
    return code;
}
