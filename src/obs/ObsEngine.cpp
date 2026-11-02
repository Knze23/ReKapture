#include "ObsEngine.h"

#include "../config/AppPaths.h"
#include "../log/Logger.h"

#include <obs.h>
#include <util/base.h>
#include <util/bmem.h>

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {

// Redirige los mensajes internos de libobs a nuestro archivo de log.
void obsLogHandler(int level, const char *format, va_list args, void * /*param*/)
{
    if (level != LOG_ERROR && level != LOG_WARNING && level != LOG_INFO)
        return; // los mensajes de depuracion no se formatean ni se guardan
    char buffer[4096];
    va_list copy;
    va_copy(copy, args);
    std::vsnprintf(buffer, sizeof(buffer), format, copy);
    va_end(copy);

    const char *name = nullptr;
    switch (level) {
    case LOG_ERROR:   name = "OBS-ERROR"; break;
    case LOG_WARNING: name = "OBS-WARN";  break;
    case LOG_INFO:    name = "OBS-INFO";  break;
    default:          return; // ignoramos DEBUG
    }
    Logger::write(QString::fromUtf8(name), QString::fromUtf8(buffer));
}

void collectModule(void *param, obs_module_t *module)
{
    auto *list = static_cast<QStringList *>(param);
    const char *file = obs_get_module_file_name(module);
    list->append(QString::fromUtf8(file ? file : "?"));
}

// Solo cargamos los modulos que ReKapture necesita. El resto (frontend-tools, obs-websocket,
// obs-vst, aja, decklink...) esperan la interfaz de OBS y pueden fallar sin ella.
const QStringList kAllowedModules = {
    QStringLiteral("obs-ffmpeg"),   // muxers, AAC y codificadores AMF
    QStringLiteral("obs-nvenc"),
    QStringLiteral("obs-outputs"),  // incluye la salida MP4 (mp4_output)
    QStringLiteral("obs-qsv11"),
    QStringLiteral("obs-x264"),
    QStringLiteral("win-capture"),  // monitor, ventana y juego
    QStringLiteral("win-wasapi"),   // audio de escritorio y microfono
    QStringLiteral("image-source"),
    QStringLiteral("obs-filters"),
    QStringLiteral("win-dshow"),    // webcam (opcional, protegido)
};

// Modulos opcionales: si uno cierra la aplicacion mientras se carga, queda una marca y la proxima
// vez no se vuelve a cargar (asi la aplicacion siempre puede arrancar).
const QStringList kOptionalModules = {QStringLiteral("win-dshow"), QStringLiteral("obs-vst")};

QString guardFile(const QString &module)
{
    return AppPaths::configDir() + QStringLiteral("/module-guard-") + module + QStringLiteral(".flag");
}

obs_video_info makeVideoInfo(int width, int height, int fps)
{
    obs_video_info ovi;
    std::memset(&ovi, 0, sizeof(ovi));
    ovi.graphics_module = "libobs-d3d11";
    ovi.fps_num = static_cast<uint32_t>(fps);
    ovi.fps_den = 1;
    ovi.base_width = static_cast<uint32_t>(width);
    ovi.base_height = static_cast<uint32_t>(height);
    ovi.output_width = static_cast<uint32_t>(width);
    ovi.output_height = static_cast<uint32_t>(height);
    ovi.output_format = VIDEO_FORMAT_NV12;
    ovi.adapter = 0;
    ovi.gpu_conversion = true;
    ovi.colorspace = VIDEO_CS_709;
    ovi.range = VIDEO_RANGE_PARTIAL;
    ovi.scale_type = OBS_SCALE_BICUBIC;
    return ovi;
}

bool logRemainingSource(void *, obs_source_t *source)
{
    Logger::warn(QStringLiteral("Fuente sin liberar al cerrar: nombre=%1 tipo=%2")
                     .arg(QString::fromUtf8(obs_source_get_name(source)),
                          QString::fromUtf8(obs_source_get_id(source))));
    return true;
}

QByteArray utf8Path(const QString &p)
{
    return QDir::cleanPath(p).toUtf8();
}

} // namespace

void ObsEngine::clearModuleGuards()
{
    for (const QString &m : kOptionalModules)
        QFile::remove(guardFile(m));
}

ObsEngine::ObsEngine()
{
    base_set_log_handler(obsLogHandler, nullptr);
}

ObsEngine::~ObsEngine()
{
    shutdown();
}

ObsInitResult ObsEngine::initialize(int width, int height, int fps, bool enableVst)
{
    ObsInitResult result;

    if (m_initialized) {
        result.ok = true;
        result.version = QString::fromUtf8(obs_get_version_string());
        return result;
    }

    // exe en <deploy>/bin/64bit  ->  raiz = <deploy>
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString root = QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../..")));
    const QString dataLibobs = root + QStringLiteral("/data/libobs");
    const QString coreDir = root + QStringLiteral("/core");

    Logger::info(QStringLiteral("Raiz del runtime: %1").arg(root));

    if (!QFileInfo::exists(dataLibobs)) {
        result.errorMessage = QStringLiteral("No se encuentra la carpeta de datos de libobs: %1").arg(dataLibobs);
        Logger::error(result.errorMessage);
        return result;
    }
    if (!QFileInfo::exists(coreDir)) {
        Logger::warn(QStringLiteral("No existe la carpeta de modulos: %1").arg(coreDir));
    }

    // 1) Arranque del nucleo
    if (!obs_startup("en-US", nullptr, nullptr)) {
        result.errorMessage = QStringLiteral("obs_startup devolvio error.");
        Logger::error(result.errorMessage);
        return result;
    }
    m_initialized = true; // a partir de aqui hay que llamar a obs_shutdown

    obs_add_data_path((utf8Path(dataLibobs) + "/").constData());

    // 2) Video
    obs_video_info ovi = makeVideoInfo(width, height, fps);
    const int videoCode = obs_reset_video(&ovi);
    if (videoCode != OBS_VIDEO_SUCCESS) {
        result.errorMessage = QStringLiteral("obs_reset_video fallo (codigo %1).").arg(videoCode);
        Logger::error(result.errorMessage);
        shutdown();
        return result;
    }

    // 3) Audio
    obs_audio_info avi;
    std::memset(&avi, 0, sizeof(avi));
    avi.samples_per_sec = 48000;
    avi.speakers = SPEAKERS_STEREO;
    if (!obs_reset_audio(&avi)) {
        result.errorMessage = QStringLiteral("obs_reset_audio fallo.");
        Logger::error(result.errorMessage);
        shutdown();
        return result;
    }

    // 4) Modulos. OBS 33 coloca los modulos oficiales como core/<modulo>/<modulo>.dll.
    //    Buscamos los .dll bajo core/ (ignorando las carpetas "data") y cargamos solo los permitidos.
    QDirIterator it(coreDir, QStringList() << QStringLiteral("*.dll"), QDir::Files,
                    QDirIterator::Subdirectories);
    int found = 0;
    int skipped = 0;
    while (it.hasNext()) {
        const QString dllPath = it.next();
        const QFileInfo info(dllPath);
        const QString relative = QDir(coreDir).relativeFilePath(dllPath);

        // Saltar cualquier DLL que viva dentro de una carpeta "data"
        if (relative.split('/').contains(QStringLiteral("data"), Qt::CaseInsensitive))
            continue;

        const QString name = info.completeBaseName();
        const bool allowed = kAllowedModules.contains(name, Qt::CaseInsensitive)
                             || (enableVst && name.compare(QStringLiteral("obs-vst"), Qt::CaseInsensitive) == 0);
        if (!allowed) {
            ++skipped;
            continue;
        }
        ++found;

        // Proteccion de modulos opcionales
        const bool optional = kOptionalModules.contains(name, Qt::CaseInsensitive);
        if (optional && QFileInfo::exists(guardFile(name))) {
            Logger::warn(QStringLiteral("Modulo %1 omitido: cerro la aplicacion la ultima vez que se cargo.").arg(name));
            result.skippedModules.append(name);
            continue;
        }
        if (optional) {
            QFile marker(guardFile(name));
            marker.open(QIODevice::WriteOnly);
        }

        // Datos del modulo: core/<modulo>/data si existe, o <carpeta del dll>/data
        QString dataDir = coreDir + QStringLiteral("/") + name + QStringLiteral("/data");
        if (!QFileInfo::exists(dataDir))
            dataDir = info.absolutePath() + QStringLiteral("/data");

        obs_module_t *module = nullptr;
        const int code = obs_open_module(&module, utf8Path(dllPath).constData(),
                                         QFileInfo::exists(dataDir) ? utf8Path(dataDir).constData() : nullptr);
        if (code != MODULE_SUCCESS || !module) {
            Logger::warn(QStringLiteral("Modulo no cargado: %1 (codigo %2)").arg(relative).arg(code));
            continue;
        }
        if (!obs_init_module(module)) {
            Logger::warn(QStringLiteral("Modulo no inicializado: %1").arg(relative));
            if (optional)
                QFile::remove(guardFile(name));
            continue;
        }
        if (optional)
            QFile::remove(guardFile(name)); // se cargo sin cerrar la aplicacion
        Logger::info(QStringLiteral("Modulo cargado: %1").arg(relative));
    }
    Logger::info(QStringLiteral("Modulos permitidos encontrados: %1 (omitidos: %2)").arg(found).arg(skipped));

    obs_post_load_modules();
    obs_log_loaded_modules();

    // 5) Inventario (solo informativo)
    obs_enum_modules(collectModule, &result.modules);

    const char *id = nullptr;
    for (size_t i = 0; obs_enum_encoder_types(i, &id); ++i)
        result.encoders.append(QString::fromUtf8(id));
    for (size_t i = 0; obs_enum_source_types(i, &id); ++i)
        result.sources.append(QString::fromUtf8(id));
    for (size_t i = 0; obs_enum_output_types(i, &id); ++i)
        result.outputs.append(QString::fromUtf8(id));

    result.version = QString::fromUtf8(obs_get_version_string());
    result.ok = true;

    Logger::info(QStringLiteral("Motor listo. OBS %1, %2 modulos, %3 codificadores, %4 tipos de fuente.")
                     .arg(result.version)
                     .arg(result.modules.size())
                     .arg(result.encoders.size())
                     .arg(result.sources.size()));
    Logger::info(QStringLiteral("Codificadores: %1").arg(result.encoders.join(QStringLiteral(", "))));
    Logger::info(QStringLiteral("Fuentes: %1").arg(result.sources.join(QStringLiteral(", "))));
    Logger::info(QStringLiteral("Salidas: %1").arg(result.outputs.join(QStringLiteral(", "))));
    return result;
}

bool ObsEngine::applyVideo(int width, int height, int fps, QString *errorOut)
{
    if (!m_initialized) {
        if (errorOut)
            *errorOut = QStringLiteral("El motor no está inicializado.");
        return false;
    }

    obs_video_info current;
    if (obs_get_video_info(&current) && current.base_width == static_cast<uint32_t>(width)
        && current.base_height == static_cast<uint32_t>(height)
        && current.output_width == static_cast<uint32_t>(width)
        && current.output_height == static_cast<uint32_t>(height)
        && current.fps_num == static_cast<uint32_t>(fps) && current.fps_den == 1)
        return true; // ya esta aplicado

    obs_video_info ovi = makeVideoInfo(width, height, fps);
    const int code = obs_reset_video(&ovi);
    if (code != OBS_VIDEO_SUCCESS) {
        const QString msg = QStringLiteral("No se pudo aplicar %1x%2 a %3 FPS (código %4).")
                                .arg(width).arg(height).arg(fps).arg(code);
        Logger::error(msg);
        if (errorOut)
            *errorOut = msg;
        return false;
    }
    Logger::info(QStringLiteral("Video reconfigurado: %1x%2 a %3 FPS").arg(width).arg(height).arg(fps));
    return true;
}

void ObsEngine::shutdown()
{
    if (!m_initialized)
        return;

    Logger::info(QStringLiteral("Cerrando libobs..."));
    obs_enum_sources(logRemainingSource, nullptr);
    obs_enum_scenes(logRemainingSource, nullptr);
    obs_shutdown();
    m_initialized = false;
    Logger::info(QStringLiteral("libobs cerrado. Asignaciones sin liberar: %1")
                     .arg(static_cast<long long>(bnum_allocs())));
}
