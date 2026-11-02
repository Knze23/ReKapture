#include "RecordingController.h"

#include "../log/Logger.h"
#include "AudioFileWriter.h"

#include <obs.h>
#include <callback/calldata.h>
#include <callback/proc.h>
#include <callback/signal.h>
#include <graphics/vec2.h>
#include <media-io/audio-io.h>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QStorageInfo>
#include <QTimer>

#include <algorithm>
#include <cstring>

namespace {

constexpr qint64 kMinFreeBytes = 2LL * 1024 * 1024 * 1024; // 2 GB
constexpr int kStopTimeoutMs = 45000;

bool hasEncoder(const char *wanted)
{
    const char *id = nullptr;
    for (size_t i = 0; obs_enum_encoder_types(i, &id); ++i)
        if (std::strcmp(id, wanted) == 0)
            return true;
    return false;
}

bool hasOutput(const char *wanted)
{
    const char *id = nullptr;
    for (size_t i = 0; obs_enum_output_types(i, &id); ++i)
        if (std::strcmp(id, wanted) == 0)
            return true;
    return false;
}

QString outputErrorText(int code)
{
    switch (code) {
    case OBS_OUTPUT_SUCCESS:        return QStringLiteral("correcto");
    case OBS_OUTPUT_BAD_PATH:       return QStringLiteral("la ruta de destino no es válida");
    case OBS_OUTPUT_CONNECT_FAILED: return QStringLiteral("no se pudo abrir la salida");
    case OBS_OUTPUT_INVALID_STREAM: return QStringLiteral("flujo de salida no válido");
    case OBS_OUTPUT_ERROR:          return QStringLiteral("error durante la grabación");
    case OBS_OUTPUT_DISCONNECTED:   return QStringLiteral("la salida se desconectó");
    case OBS_OUTPUT_UNSUPPORTED:    return QStringLiteral("configuración no compatible con la salida");
    case OBS_OUTPUT_NO_SPACE:       return QStringLiteral("no hay espacio suficiente en el disco");
    case OBS_OUTPUT_ENCODE_ERROR:   return QStringLiteral("error del codificador");
    default:                        return QStringLiteral("error desconocido (código %1)").arg(code);
    }
}

QVector<MonitorInfo> listPropertyItems(const char *sourceType, const char *propertyName, bool skipEmptyIds)
{
    QVector<MonitorInfo> list;
    obs_properties_t *props = obs_get_source_properties(sourceType);
    if (!props)
        return list;
    obs_property_t *prop = obs_properties_get(props, propertyName);
    if (prop) {
        const size_t count = obs_property_list_item_count(prop);
        for (size_t i = 0; i < count; ++i) {
            const char *name = obs_property_list_item_name(prop, i);
            const char *id = obs_property_list_item_string(prop, i);
            if (!id || (skipEmptyIds && !*id))
                continue;
            list.append({QString::fromUtf8(id), QString::fromUtf8(name ? name : id)});
        }
    }
    obs_properties_destroy(props);
    return list;
}

// ---- Funciones que libobs llama desde sus propios hilos: solo reenvian al hilo de la interfaz ----

void onOutputStart(void *param, calldata_t *)
{
    auto *tag = static_cast<SignalTag *>(param);
    QMetaObject::invokeMethod(tag->self, "handleStarted", Qt::QueuedConnection, Q_ARG(int, tag->index));
}

void onOutputStop(void *param, calldata_t *cd)
{
    auto *tag = static_cast<SignalTag *>(param);
    const int code = static_cast<int>(calldata_int(cd, "code"));
    QMetaObject::invokeMethod(tag->self, "handleStopped", Qt::QueuedConnection, Q_ARG(int, tag->index),
                              Q_ARG(int, code));
}

void onGameHooked(void *param, calldata_t *cd)
{
    auto *self = static_cast<RecordingController *>(param);
    const char *title = calldata_string(cd, "title");
    const char *exe = calldata_string(cd, "executable");
    const QString t = QString::fromUtf8(title ? title : "");
    const QString e = QString::fromUtf8(exe ? exe : "");
    const QString text = QStringLiteral("Capturando juego: %1 (%2)").arg(t.isEmpty() ? QStringLiteral("?") : t,
                                                                          e.isEmpty() ? QStringLiteral("?") : e);
    QMetaObject::invokeMethod(self, "handleHooked", Qt::QueuedConnection, Q_ARG(QString, text), Q_ARG(QString, t),
                              Q_ARG(QString, e));
}

void onGameUnhooked(void *param, calldata_t *)
{
    auto *self = static_cast<RecordingController *>(param);
    QMetaObject::invokeMethod(self, "handleHooked", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("Se perdió la captura del juego.")), Q_ARG(QString, QString()),
                              Q_ARG(QString, QString()));
}

void onReplaySaved(void *param, calldata_t *)
{
    auto *self = static_cast<RecordingController *>(param);
    QMetaObject::invokeMethod(self, "handleClipSaved", Qt::QueuedConnection);
}

void onReplayStop(void *param, calldata_t *cd)
{
    auto *self = static_cast<RecordingController *>(param);
    const int code = static_cast<int>(calldata_int(cd, "code"));
    QMetaObject::invokeMethod(self, "handleReplayStopped", Qt::QueuedConnection, Q_ARG(int, code));
}

// Medidor de nivel: guarda el pico (0..1) del dispositivo. param = std::atomic<float> *
void onVolmeter(void *param, const float /*magnitude*/[MAX_AUDIO_CHANNELS], const float peak[MAX_AUDIO_CHANNELS],
                const float /*inputPeak*/[MAX_AUDIO_CHANNELS])
{
    float db = -100.0f;
    for (int ch = 0; ch < MAX_AUDIO_CHANNELS; ++ch)
        if (peak[ch] > db)
            db = peak[ch];
    const float level = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
    static_cast<std::atomic<float> *>(param)->store(level);
}

// Audio del microfono (mezcla 1) hacia el archivo WAV/MP3. Corre en el hilo de audio de libobs.
void onMicAudio(void *param, size_t /*mixIndex*/, struct audio_data *data)
{
    static_cast<AudioFileWriter *>(param)->push(data->data[0], data->frames);
}

void collectFilter(obs_source_t * /*parent*/, obs_source_t *child, void *param)
{
    static_cast<QVector<obs_source_t *> *>(param)->append(child);
}

QVector<obs_source_t *> filtersOf(obs_source_t *source)
{
    QVector<obs_source_t *> list;
    if (source)
        obs_source_enum_filters(source, collectFilter, &list);
    return list;
}

// ---- Rutas por juego y opciones derivadas ----
QString gameDir(const RecordingOptions &opt)
{
    QString dir = QDir::cleanPath(opt.folder);
    if (opt.organizeByGame && !opt.gameName.isEmpty())
        dir += QStringLiteral("/") + opt.gameName;
    return dir;
}

QString filePrefix(const RecordingOptions &opt)
{
    return (opt.organizeByGame && !opt.gameName.isEmpty()) ? opt.gameName : QStringLiteral("ReKapture");
}

QString clipDir(const RecordingOptions &opt)
{
    return gameDir(opt) + QStringLiteral("/Clips");
}

bool wantsMicTrack(const RecordingOptions &opt)
{
    return opt.recordMic && (opt.micExportMode == 1 || opt.micExportMode == 2);
}

bool wantsMicFile(const RecordingOptions &opt)
{
    return opt.recordMic && (opt.micExportMode == 0 || opt.micExportMode == 2);
}

QByteArray resolveVideoEncoderId(const RecordingOptions &opt)
{
    const QByteArray requested = opt.videoEncoderId.toUtf8();
    if (!requested.isEmpty() && hasEncoder(requested.constData()))
        return requested;
    return hasEncoder("obs_nvenc_h264_tex") ? QByteArray("obs_nvenc_h264_tex") : QByteArray("obs_x264");
}

QString encoderSignature(const RecordingOptions &opt)
{
    return QStringLiteral("%1|%2|%3x%4@%5|%6|%7|%8")
        .arg(QString::fromLatin1(resolveVideoEncoderId(opt)))
        .arg(opt.bitrateKbps)
        .arg(opt.width)
        .arg(opt.height)
        .arg(opt.fps)
        .arg(opt.recordDesktopAudio ? 1 : 0)
        .arg(wantsMicTrack(opt) ? 1 : 0)
        .arg(opt.audioBitrateKbps);
}

obs_data_t *replaySettings(const RecordingOptions &opt)
{
    obs_data_t *s = obs_data_create();
    QString prefix = filePrefix(opt);
    prefix.replace(QLatin1Char('%'), QLatin1Char(' '));
    obs_data_set_string(s, "directory", clipDir(opt).toUtf8().constData());
    obs_data_set_string(s, "format",
                        (prefix + QStringLiteral("_Clip_%CCYY-%MM-%DD_%hh-%mm-%ss")).toUtf8().constData());
    obs_data_set_string(s, "extension", "mp4");
    obs_data_set_bool(s, "allow_spaces", true);
    obs_data_set_int(s, "max_time_sec", opt.clipSeconds);
    // Tope de memoria del buffer: bitrate x segundos con margen
    const double estimateMb = (opt.bitrateKbps + 400) * 1000.0 / 8.0 * opt.clipSeconds / 1e6 * 1.6 + 64.0;
    obs_data_set_int(s, "max_size_mb", static_cast<long long>(std::clamp(estimateMb, 128.0, 4096.0)));
    return s;
}

obs_video_info mixVideoInfo(int width, int height, uint32_t fpsNum, uint32_t fpsDen)
{
    obs_video_info ovi;
    std::memset(&ovi, 0, sizeof(ovi));
    ovi.graphics_module = "libobs-d3d11";
    ovi.fps_num = fpsNum;
    ovi.fps_den = fpsDen;
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

// FPS del lienzo principal. IMPORTANTE: toda mezcla de video adicional debe usar los mismos FPS;
// si no, su linea de tiempo avanza a otra velocidad y el archivo tarda en cerrarse.
void mainFps(uint32_t *num, uint32_t *den)
{
    obs_video_info mainVideo;
    std::memset(&mainVideo, 0, sizeof(mainVideo));
    if (obs_get_video_info(&mainVideo) && mainVideo.fps_num > 0 && mainVideo.fps_den > 0) {
        *num = mainVideo.fps_num;
        *den = mainVideo.fps_den;
    } else {
        *num = 60;
        *den = 1;
    }
}

} // namespace

RecordingController::RecordingController(QObject *parent)
    : QObject(parent)
{
    for (int i = 0; i < 3; ++i)
        m_tags[i] = {this, i};
    m_audio[0].channel = 1;
    m_audio[1].channel = 2;

    // Vigilante unico: si alguna salida no termina a tiempo tras pedir la parada, se fuerza.
    m_watchdog = new QTimer(this);
    m_watchdog->setSingleShot(true);
    m_watchdog->setInterval(kStopTimeoutMs);
    connect(m_watchdog, &QTimer::timeout, this, [this] {
        if (m_state != State::Stopping)
            return;
        for (Channel &c : m_ch)
            if (c.used && c.output && !c.stopped) {
                Logger::warn(QStringLiteral("La salida %1 no terminó a tiempo; se fuerza el cierre.").arg(c.label));
                obs_output_force_stop(c.output);
            }
    });

    m_perfTimer = new QTimer(this);
    m_perfTimer->setInterval(1000);
    connect(m_perfTimer, &QTimer::timeout, this, &RecordingController::perfTick);
    m_perfTimer->start();
}

RecordingController::~RecordingController()
{
    if (obs_initialized()) {
        for (Channel &c : m_ch)
            if (c.output && obs_output_active(c.output))
                obs_output_force_stop(c.output);
    }
    stopMicFile();
    cleanup();
    releaseSources();
}

void RecordingController::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged(static_cast<int>(s));
}

QVector<MonitorInfo> RecordingController::listMonitors() const
{
    return listPropertyItems("monitor_capture", "monitor_id", false);
}

QVector<MonitorInfo> RecordingController::listWindows() const
{
    return listPropertyItems("window_capture", "window", true);
}

QVector<DeviceInfo> RecordingController::listAudioDevices(bool input) const
{
    QVector<DeviceInfo> list;
    const QVector<MonitorInfo> items = listPropertyItems(input ? "wasapi_input_capture" : "wasapi_output_capture",
                                                         "device_id", false);
    for (const MonitorInfo &i : items)
        list.append({i.id, i.name});
    return list;
}

QVector<DeviceInfo> RecordingController::listWebcams() const
{
    QVector<DeviceInfo> list;
    const QVector<MonitorInfo> items = listPropertyItems("dshow_input", "video_device_id", true);
    for (const MonitorInfo &i : items)
        list.append({i.id, i.name});
    return list;
}

bool RecordingController::allUsed(bool Channel::*flag) const
{
    for (const Channel &c : m_ch)
        if (c.used && !(c.*flag))
            return false;
    return true;
}

// ---------------------------------------------------------------------------------------
// Audio persistente (medidores) y efectos
// ---------------------------------------------------------------------------------------

void RecordingController::configureAudio(AudioSlot &slot, const char *typeId, const char *name, bool enabled,
                                         const QString &device, int volumePct, uint32_t mixers, uint32_t channel)
{
    slot.channel = channel;
    const int slotIndex = (&slot == &m_audio[0]) ? 0 : 1;

    if (!enabled) {
        if (slot.source && slot.active) {
            obs_set_output_source(channel, nullptr);
            slot.active = false;
        }
        slot.level.store(0.0f);
        return;
    }

    if (!slot.source) {
        obs_data_t *s = obs_data_create();
        obs_data_set_string(s, "device_id", device.toUtf8().constData());
        slot.source = obs_source_create(typeId, name, s, nullptr);
        obs_data_release(s);
        if (!slot.source) {
            Logger::error(QStringLiteral("No se pudo crear la fuente de audio %1.").arg(QString::fromLatin1(name)));
            return;
        }
        slot.device = device;
        obs_source_set_audio_mixers(slot.source, mixers);

        slot.meter = obs_volmeter_create(OBS_FADER_LOG);
        if (slot.meter) {
            obs_volmeter_attach_source(slot.meter, slot.source);
            obs_volmeter_add_callback(slot.meter, onVolmeter, &slot.level);
        }
        applyEffectsToSlot(slotIndex); // efectos guardados (supresor de ruido, compresor, VST...)
    } else if (slot.device != device) {
        obs_data_t *s = obs_data_create();
        obs_data_set_string(s, "device_id", device.toUtf8().constData());
        obs_source_update(slot.source, s);
        obs_data_release(s);
        slot.device = device;
    }

    obs_source_set_volume(slot.source, static_cast<float>(std::clamp(volumePct, 0, 200)) / 100.0f);

    if (!slot.active) {
        obs_set_output_source(channel, slot.source);
        slot.active = true;
    }
}

void RecordingController::prepareAudio(const RecordingOptions &opt)
{
    configureAudio(m_audio[0], "wasapi_output_capture", "rk_desktop_audio", opt.recordDesktopAudio,
                   opt.desktopDeviceId, opt.desktopVolumePct, 1u, 1u); // pista 1
    configureAudio(m_audio[1], "wasapi_input_capture", "rk_mic_audio", opt.recordMic,
                   opt.micDeviceId, opt.micVolumePct, 2u, 2u);         // pista 2
}

void RecordingController::setVolumes(int desktopPct, int micPct)
{
    if (m_audio[0].source)
        obs_source_set_volume(m_audio[0].source, static_cast<float>(std::clamp(desktopPct, 0, 200)) / 100.0f);
    if (m_audio[1].source)
        obs_source_set_volume(m_audio[1].source, static_cast<float>(std::clamp(micPct, 0, 200)) / 100.0f);
}

QStringList RecordingController::availableFilters()
{
    QStringList ids;
    const char *id = nullptr;
    for (size_t i = 0; obs_enum_filter_types(i, &id); ++i)
        if (obs_get_source_output_flags(id) & OBS_SOURCE_AUDIO)
            ids << QString::fromUtf8(id);
    return ids;
}

bool RecordingController::audioSourceExists(int slot) const
{
    return slot >= 0 && slot < 2 && m_audio[slot].source != nullptr;
}

QVector<EffectInfo> RecordingController::effects(int slot) const
{
    QVector<EffectInfo> list;
    if (!audioSourceExists(slot))
        return list;
    for (obs_source_t *f : filtersOf(m_audio[slot].source)) {
        EffectInfo e;
        e.filterId = QString::fromUtf8(obs_source_get_id(f));
        e.name = QString::fromUtf8(obs_source_get_name(f));
        e.enabled = obs_source_enabled(f);
        list.append(e);
    }
    return list;
}

obs_source *RecordingController::effectSource(int slot, int index) const
{
    if (!audioSourceExists(slot))
        return nullptr;
    const QVector<obs_source_t *> list = filtersOf(m_audio[slot].source);
    return (index >= 0 && index < list.size()) ? list[index] : nullptr;
}

bool RecordingController::addEffect(int slot, const QString &filterId, const QString &name, const QString &settingsJson)
{
    if (!audioSourceExists(slot))
        return false;
    obs_source_t *parent = m_audio[slot].source;

    obs_data_t *settings = settingsJson.isEmpty() ? nullptr : obs_data_create_from_json(settingsJson.toUtf8().constData());
    obs_source_t *filter = obs_source_create(filterId.toUtf8().constData(), name.toUtf8().constData(), settings, nullptr);
    if (settings)
        obs_data_release(settings);
    if (!filter) {
        Logger::warn(QStringLiteral("No se pudo crear el efecto '%1' (¿módulo no cargado?).").arg(filterId));
        return false;
    }

    obs_source_filter_add(parent, filter);
    // El orden de la lista es el orden de ejecucion: el efecto nuevo va al final.
    for (int guard = 0; guard < 64; ++guard) {
        const QVector<obs_source_t *> list = filtersOf(parent);
        if (list.isEmpty() || list.last() == filter)
            break;
        obs_source_filter_set_order(parent, filter, OBS_ORDER_MOVE_DOWN);
    }
    obs_source_release(filter); // el origen conserva su propia referencia
    return true;
}

void RecordingController::removeEffect(int slot, int index)
{
    obs_source_t *filter = effectSource(slot, index);
    if (filter)
        obs_source_filter_remove(m_audio[slot].source, filter);
}

void RecordingController::moveEffect(int slot, int index, int delta)
{
    obs_source_t *filter = effectSource(slot, index);
    if (filter && delta != 0)
        obs_source_filter_set_order(m_audio[slot].source, filter, delta < 0 ? OBS_ORDER_MOVE_UP : OBS_ORDER_MOVE_DOWN);
}

void RecordingController::setEffectEnabled(int slot, int index, bool enabled)
{
    if (obs_source_t *filter = effectSource(slot, index))
        obs_source_set_enabled(filter, enabled);
}

QString RecordingController::effectsJson(int slot) const
{
    if (slot < 0 || slot > 1)
        return QString();
    if (!audioSourceExists(slot))
        return m_effectsJson[slot];

    QJsonArray array;
    for (obs_source_t *f : filtersOf(m_audio[slot].source)) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), QString::fromUtf8(obs_source_get_id(f)));
        o.insert(QStringLiteral("name"), QString::fromUtf8(obs_source_get_name(f)));
        o.insert(QStringLiteral("enabled"), obs_source_enabled(f));
        obs_data_t *settings = obs_source_get_settings(f);
        o.insert(QStringLiteral("settings"), QString::fromUtf8(obs_data_get_json(settings)));
        obs_data_release(settings);
        array.append(o);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

void RecordingController::clearEffectsOfSlot(int slot)
{
    if (!audioSourceExists(slot))
        return;
    for (obs_source_t *f : filtersOf(m_audio[slot].source))
        obs_source_filter_remove(m_audio[slot].source, f);
}

void RecordingController::applyEffectsToSlot(int slot)
{
    if (!audioSourceExists(slot))
        return;
    clearEffectsOfSlot(slot);

    const QJsonDocument doc = QJsonDocument::fromJson(m_effectsJson[slot].toUtf8());
    if (!doc.isArray())
        return;
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        const QString id = o.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        if (addEffect(slot, id, o.value(QStringLiteral("name")).toString(id), o.value(QStringLiteral("settings")).toString())) {
            const QVector<obs_source_t *> list = filtersOf(m_audio[slot].source);
            if (!list.isEmpty())
                obs_source_set_enabled(list.last(), o.value(QStringLiteral("enabled")).toBool(true));
        }
    }
}

void RecordingController::setEffectsJson(int slot, const QString &json)
{
    if (slot < 0 || slot > 1)
        return;
    m_effectsJson[slot] = json;
    if (audioSourceExists(slot))
        applyEffectsToSlot(slot);
}

// ---------------------------------------------------------------------------------------
// Video persistente (previsualizacion y juego enganchado antes de grabar)
// ---------------------------------------------------------------------------------------

void RecordingController::disarmVideo()
{
    if (m_videoArmed && obs_initialized()) {
        obs_set_output_source(0, nullptr);
        m_videoArmed = false;
    }
}

bool RecordingController::prepareVideo(const RecordingOptions &opt, QString *errorOut)
{
    auto fail = [&](const QString &msg) {
        if (errorOut)
            *errorOut = msg;
        return false;
    };

    static const char *const kSourceIds[3] = {"monitor_capture", "window_capture", "game_capture"};
    static const char *const kSourceNames[3] = {"rk_monitor", "rk_window", "rk_game"};
    const int mode = static_cast<int>(opt.captureMode);
    m_mode = opt.captureMode;

    if (opt.captureMode == CaptureMode::Window && opt.windowId.isEmpty())
        return fail(QStringLiteral("Selecciona la ventana que quieres capturar."));

    obs_data_t *videoSource = obs_data_create();
    switch (opt.captureMode) {
    case CaptureMode::Monitor:
        if (!opt.monitorId.isEmpty())
            obs_data_set_string(videoSource, "monitor_id", opt.monitorId.toUtf8().constData());
        obs_data_set_bool(videoSource, "capture_cursor", true);
        break;
    case CaptureMode::Window:
        obs_data_set_string(videoSource, "window", opt.windowId.toUtf8().constData());
        obs_data_set_bool(videoSource, "cursor", true);
        break;
    case CaptureMode::Game:
        if (opt.windowId.isEmpty()) {
            obs_data_set_string(videoSource, "capture_mode", "any_fullscreen");
        } else {
            obs_data_set_string(videoSource, "capture_mode", "window");
            obs_data_set_string(videoSource, "window", opt.windowId.toUtf8().constData());
        }
        obs_data_set_bool(videoSource, "capture_cursor", true);
        break;
    }

    VideoSlot &slot = m_slots[mode];
    const bool firstTime = (slot.source == nullptr);
    const QString sig = opt.monitorId + QLatin1Char('|') + opt.windowId;
    if (firstTime) {
        slot.source = obs_source_create(kSourceIds[mode], kSourceNames[mode], videoSource, nullptr);
        m_videoSig[mode] = sig;
    } else if (m_videoSig[mode] != sig) {
        obs_source_update(slot.source, videoSource); // solo si cambio el objetivo: no reinicia la captura sin necesidad
        m_videoSig[mode] = sig;
    }
    obs_data_release(videoSource);
    if (!slot.source)
        return fail(QStringLiteral("No se pudo crear la fuente de captura (%1).").arg(QString::fromLatin1(kSourceIds[mode])));

    if (firstTime && opt.captureMode == CaptureMode::Game) {
        signal_handler_t *sh = obs_source_get_signal_handler(slot.source);
        signal_handler_connect(sh, "hooked", onGameHooked, this);
        signal_handler_connect(sh, "unhooked", onGameUnhooked, this);
    }

    if (!m_scene) {
        m_scene = obs_scene_create("rk_scene");
        if (!m_scene)
            return fail(QStringLiteral("No se pudo crear la escena."));
    }
    if (!slot.item) {
        slot.item = obs_scene_add(m_scene, slot.source);
        if (!slot.item)
            return fail(QStringLiteral("No se pudo añadir la fuente a la escena."));
    }

    // Solo el modo elegido esta visible (y por tanto capturando)
    for (int i = 0; i < 3; ++i)
        if (m_slots[i].item)
            obs_sceneitem_set_visible(m_slots[i].item, i == mode);

    // Escalado al lienzo actual (manteniendo proporciones)
    vec2 pos;
    vec2_zero(&pos);
    vec2 bounds;
    vec2_set(&bounds, static_cast<float>(opt.width), static_cast<float>(opt.height));
    obs_sceneitem_set_pos(slot.item, &pos);
    obs_sceneitem_set_alignment(slot.item, 5);        // izquierda | arriba
    obs_sceneitem_set_bounds_type(slot.item, OBS_BOUNDS_SCALE_INNER);
    obs_sceneitem_set_bounds_alignment(slot.item, 0); // centrado
    obs_sceneitem_set_bounds(slot.item, &bounds);

    if (!m_videoArmed) {
        obs_set_output_source(0, obs_scene_get_source(m_scene));
        m_videoArmed = true;
    }
    return true;
}

// ---------------------------------------------------------------------------------------
// Webcam (se mantiene abierta mientras esta habilitada; se graba en su propio MP4)
// ---------------------------------------------------------------------------------------

bool RecordingController::armWebcam(const RecordingOptions &opt)
{
    if ((!opt.webcamEnabled && !opt.webcamPreview) || opt.webcamDeviceId.isEmpty()) {
        disarmWebcam();
        return false;
    }

    obs_data_t *s = obs_data_create();
    obs_data_set_string(s, "video_device_id", opt.webcamDeviceId.toUtf8().constData());
    obs_data_set_int(s, "res_type", 1); // resolucion personalizada
    obs_data_set_string(s, "resolution", QStringLiteral("%1x%2").arg(opt.webcamWidth).arg(opt.webcamHeight).toUtf8().constData());
    obs_data_set_int(s, "frame_interval", 333333); // 30 FPS de camara (unidades de 100 ns)
    obs_data_set_bool(s, "deactivate_when_not_showing", true);
    obs_data_set_bool(s, "activate", true);

    if (!m_camSource) {
        m_camSource = obs_source_create("dshow_input", "rk_webcam", s, nullptr);
    } else if (m_camDevice != opt.webcamDeviceId || !m_camArmed) {
        obs_source_update(m_camSource, s);
    }
    obs_data_release(s);
    if (!m_camSource) {
        Logger::warn(QStringLiteral("No se pudo crear la fuente de webcam (¿módulo win-dshow no cargado?)."));
        return false;
    }
    m_camDevice = opt.webcamDeviceId;

    if (!m_camScene) {
        m_camScene = obs_scene_create("rk_cam_scene");
        if (!m_camScene)
            return false;
        m_camItem = obs_scene_add(m_camScene, m_camSource);
        if (!m_camItem)
            return false;
    }
    vec2 pos;
    vec2_zero(&pos);
    vec2 bounds;
    vec2_set(&bounds, static_cast<float>(opt.webcamWidth), static_cast<float>(opt.webcamHeight));
    obs_sceneitem_set_pos(m_camItem, &pos);
    obs_sceneitem_set_alignment(m_camItem, 5);
    obs_sceneitem_set_bounds_type(m_camItem, OBS_BOUNDS_SCALE_INNER);
    obs_sceneitem_set_bounds_alignment(m_camItem, 0);
    obs_sceneitem_set_bounds(m_camItem, &bounds);

    // Una vista sin mezcla de video mantiene la camara abierta (activa) sin dibujar nada
    if (!m_camView)
        m_camView = obs_view_create();
    if (m_camView && !m_camArmed) {
        obs_view_set_source(m_camView, 0, obs_scene_get_source(m_camScene));
        m_camArmed = true;
    }
    return m_camView != nullptr;
}

void RecordingController::disarmWebcam()
{
    if (!obs_initialized())
        return;
    if (m_camView) {
        if (m_camMixAdded) {
            obs_view_remove(m_camView);
            m_camMixAdded = false;
        }
        obs_view_set_source(m_camView, 0, nullptr);
        obs_view_destroy(m_camView);
        m_camView = nullptr;
    }
    m_camArmed = false;
}

void RecordingController::applyIdleState(const RecordingOptions &opt, bool captureVideo)
{
    if (m_state != State::Idle || !obs_initialized())
        return;

    prepareAudio(opt);
    if (opt.webcamEnabled || opt.webcamPreview)
        armWebcam(opt);
    else
        disarmWebcam();

    if (captureVideo) {
        QString error;
        if (!prepareVideo(opt, &error)) {
            Logger::info(QStringLiteral("Captura en espera: %1").arg(error));
            disarmVideo();
        }
    } else {
        disarmVideo();
    }

    // Repeticion instantanea (buffer siempre activo mientras no se graba)
    if (opt.instantReplay && captureVideo) {
        if (m_replay && m_encSig != encoderSignature(opt))
            disarmReplay(); // cambiaron los ajustes de video: se vuelve a crear
        if (!m_replay) {
            QString error;
            if (!armReplay(opt, &error))
                Logger::warn(QStringLiteral("Repetición instantánea no disponible: %1").arg(error));
        } else {
            obs_data_t *s = replaySettings(opt); // actualiza duracion y carpeta
            obs_output_update(m_replay, s);
            obs_data_release(s);
        }
    } else if (m_replay) {
        disarmReplay();
    }
    releaseSharedEncodersIfUnused();
}

// ---------------------------------------------------------------------------------------
// Codificadores (propios de la grabacion o compartidos con la repeticion)
// ---------------------------------------------------------------------------------------

bool RecordingController::createEncoderSet(const RecordingOptions &opt, EncoderSet *out, const QString &tag, QString *errorOut)
{
    auto fail = [&](const QString &msg) {
        releaseEncoderSet(out);
        if (errorOut)
            *errorOut = msg;
        return false;
    };

    const QByteArray encoderId = resolveVideoEncoderId(opt);
    obs_data_t *vs = obs_encoder_defaults(encoderId.constData());
    if (!vs)
        vs = obs_data_create();
    Logger::info(QStringLiteral("Ajustes por defecto del codificador: %1").arg(QString::fromUtf8(obs_data_get_json(vs))));
    obs_data_set_string(vs, "rate_control", "CBR");
    obs_data_set_int(vs, "bitrate", opt.bitrateKbps);
    Logger::info(QStringLiteral("Codificador (%1): %2 kbps, lienzo %3x%4 a %5 FPS")
                     .arg(tag).arg(opt.bitrateKbps).arg(opt.width).arg(opt.height).arg(opt.fps));
    out->video = obs_video_encoder_create(encoderId.constData(), ("rk_video_encoder_" + tag).toUtf8().constData(), vs, nullptr);
    obs_data_release(vs);
    if (!out->video)
        return fail(QStringLiteral("No se pudo crear el codificador de video."));
    obs_encoder_set_video(out->video, obs_get_video());

    if (opt.recordDesktopAudio) {
        obs_data_t *as = obs_data_create();
        obs_data_set_int(as, "bitrate", opt.audioBitrateKbps);
        out->audio = obs_audio_encoder_create("ffmpeg_aac", ("rk_audio_encoder_" + tag).toUtf8().constData(), as, 0, nullptr);
        obs_data_release(as);
        if (!out->audio)
            return fail(QStringLiteral("No se pudo crear el codificador de audio del escritorio."));
        obs_encoder_set_audio(out->audio, obs_get_audio());
    }

    if (wantsMicTrack(opt)) {
        obs_data_t *ms = obs_data_create();
        obs_data_set_int(ms, "bitrate", opt.audioBitrateKbps);
        out->mic = obs_audio_encoder_create("ffmpeg_aac", ("rk_mic_track_encoder_" + tag).toUtf8().constData(), ms, 1, nullptr); // mezcla 1
        obs_data_release(ms);
        if (!out->mic)
            return fail(QStringLiteral("No se pudo crear el codificador de audio del micrófono."));
        obs_encoder_set_audio(out->mic, obs_get_audio());
    }
    return true;
}

void RecordingController::releaseEncoderSet(EncoderSet *set)
{
    if (!obs_initialized())
        return;
    for (obs_encoder **e : {&set->video, &set->audio, &set->mic}) {
        if (*e) {
            obs_encoder_release(*e);
            *e = nullptr;
        }
    }
}

bool RecordingController::ensureSharedEncoders(const RecordingOptions &opt, QString *errorOut)
{
    const QString sig = encoderSignature(opt);
    if (m_shared.video) {
        if (sig == m_encSig)
            return true;
        if (m_replay || m_ch[0].output) {
            if (errorOut)
                *errorOut = QStringLiteral("No se pueden cambiar los ajustes de video mientras hay una grabación o repetición activa.");
            return false;
        }
        releaseSharedEncoders();
    }
    if (!createEncoderSet(opt, &m_shared, QStringLiteral("shared"), errorOut))
        return false;
    m_encSig = sig;
    return true;
}

void RecordingController::releaseSharedEncoders()
{
    releaseEncoderSet(&m_shared);
    m_encSig.clear();
}

void RecordingController::releaseSharedEncodersIfUnused()
{
    if (!m_replay && !m_ch[0].output)
        releaseSharedEncoders();
}

// ---------------------------------------------------------------------------------------
// Repeticion y clips
// ---------------------------------------------------------------------------------------

bool RecordingController::armReplay(const RecordingOptions &opt, QString *errorOut)
{
    auto fail = [&](const QString &msg) {
        if (errorOut)
            *errorOut = msg;
        return false;
    };

    if (m_replay)
        return true;
    if (!obs_initialized())
        return fail(QStringLiteral("El motor no está inicializado."));
    if (!hasOutput("replay_buffer"))
        return fail(QStringLiteral("Esta versión de OBS no incluye la salida de repetición (replay_buffer)."));

    QString encoderError;
    if (!ensureSharedEncoders(opt, &encoderError))
        return fail(encoderError);

    QDir().mkpath(clipDir(opt));
    obs_data_t *s = replaySettings(opt);
    m_replay = obs_output_create("replay_buffer", "rk_replay", s, nullptr);
    obs_data_release(s);
    if (!m_replay) {
        releaseSharedEncodersIfUnused();
        return fail(QStringLiteral("No se pudo crear la salida de repetición."));
    }

    obs_output_set_video_encoder(m_replay, m_shared.video);
    int track = 0;
    if (m_shared.audio)
        obs_output_set_audio_encoder(m_replay, m_shared.audio, track++);
    if (m_shared.mic)
        obs_output_set_audio_encoder(m_replay, m_shared.mic, track++);

    signal_handler_t *handler = obs_output_get_signal_handler(m_replay);
    signal_handler_connect(handler, "saved", onReplaySaved, this);
    signal_handler_connect(handler, "stop", onReplayStop, this);

    if (!obs_output_start(m_replay)) {
        const char *last = obs_output_get_last_error(m_replay);
        const QString msg = QStringLiteral("No se pudo iniciar la repetición: %1")
                                .arg(last ? QString::fromUtf8(last) : QStringLiteral("error desconocido"));
        signal_handler_disconnect(handler, "saved", onReplaySaved, this);
        signal_handler_disconnect(handler, "stop", onReplayStop, this);
        obs_output_release(m_replay);
        m_replay = nullptr;
        releaseSharedEncodersIfUnused();
        return fail(msg);
    }

    m_lastTotal = 0;
    m_perfTotal = m_perfLagged = 0;
    m_perfSeconds = 0;
    Logger::info(QStringLiteral("Repetición activa: últimos %1 s en %2").arg(opt.clipSeconds).arg(clipDir(opt)));
    emit replayStateChanged(true);
    return true;
}

void RecordingController::disarmReplay()
{
    if (!m_replay || !obs_initialized())
        return;

    signal_handler_t *handler = obs_output_get_signal_handler(m_replay);
    signal_handler_disconnect(handler, "saved", onReplaySaved, this);
    signal_handler_disconnect(handler, "stop", onReplayStop, this);
    if (obs_output_active(m_replay))
        obs_output_force_stop(m_replay);
    obs_output_release(m_replay);
    m_replay = nullptr;
    Logger::info(QStringLiteral("Repetición detenida."));
    releaseSharedEncodersIfUnused();
    emit replayStateChanged(false);
}

bool RecordingController::saveClip(const RecordingOptions &opt, QString *errorOut)
{
    auto fail = [&](const QString &msg) {
        if (errorOut)
            *errorOut = msg;
        return false;
    };

    if (!m_replay || !obs_output_active(m_replay))
        return fail(QStringLiteral("No hay repetición activa: activa la repetición instantánea o los clips durante la grabación."));

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastClipMs < 1500)
        return fail(QStringLiteral("Espera un momento antes de guardar otro clip."));
    m_lastClipMs = now;

    // Carpeta y nombre segun el juego actual
    QDir().mkpath(clipDir(opt));
    obs_data_t *s = replaySettings(opt);
    obs_output_update(m_replay, s);
    obs_data_release(s);

    calldata_t cd;
    calldata_init(&cd);
    proc_handler_t *ph = obs_output_get_proc_handler(m_replay);
    const bool ok = ph && proc_handler_call(ph, "save", &cd);
    calldata_free(&cd);
    if (!ok)
        return fail(QStringLiteral("No se pudo guardar el clip."));

    Logger::info(QStringLiteral("Clip solicitado (últimos %1 s).").arg(opt.clipSeconds));
    return true;
}

void RecordingController::handleClipSaved()
{
    QString path;
    if (m_replay) {
        calldata_t cd;
        calldata_init(&cd);
        proc_handler_t *ph = obs_output_get_proc_handler(m_replay);
        if (ph && proc_handler_call(ph, "get_last_replay", &cd)) {
            const char *p = calldata_string(&cd, "path");
            if (p)
                path = QString::fromUtf8(p);
        }
        calldata_free(&cd);
    }
    Logger::info(QStringLiteral("Clip guardado: %1").arg(path.isEmpty() ? QStringLiteral("(ruta desconocida)") : path));
    emit clipSaved(path);
}

void RecordingController::handleReplayStopped(int code)
{
    if (!m_replay)
        return;
    Logger::warn(QStringLiteral("La repetición se detuvo (código %1): %2").arg(code).arg(outputErrorText(code)));

    signal_handler_t *handler = obs_output_get_signal_handler(m_replay);
    signal_handler_disconnect(handler, "saved", onReplaySaved, this);
    signal_handler_disconnect(handler, "stop", onReplayStop, this);
    obs_output_release(m_replay);
    m_replay = nullptr;
    releaseSharedEncodersIfUnused();
    emit replayStateChanged(false);
    emit captureStatus(QStringLiteral("La repetición se detuvo: %1").arg(outputErrorText(code)));
}

void RecordingController::perfTick()
{
    if (!m_replay || m_state != State::Idle || !obs_initialized()) {
        m_lastTotal = m_lastLagged = 0;
        m_perfTotal = m_perfLagged = 0;
        m_perfSeconds = 0;
        return;
    }

    const uint32_t total = obs_get_total_frames();
    const uint32_t lagged = obs_get_lagged_frames();
    if (m_lastTotal == 0 || total < m_lastTotal) {
        m_lastTotal = total;
        m_lastLagged = lagged;
        return;
    }
    m_perfTotal += total - m_lastTotal;
    m_perfLagged += lagged - m_lastLagged;
    m_lastTotal = total;
    m_lastLagged = lagged;

    if (++m_perfSeconds >= 10) {
        // Mas del 5 % de fotogramas con retraso de renderizado en 10 s: el juego va justo de GPU
        if (m_perfTotal >= 200 && m_perfLagged * 100 >= m_perfTotal * 5)
            emit performanceWarning(QStringLiteral("Se perdieron %1 de %2 fotogramas en 10 s (%3 %).")
                                        .arg(m_perfLagged).arg(m_perfTotal)
                                        .arg(static_cast<double>(m_perfLagged) * 100.0 / static_cast<double>(m_perfTotal), 0, 'f', 1));
        m_perfTotal = m_perfLagged = 0;
        m_perfSeconds = 0;
    }
}

// ---------------------------------------------------------------------------------------
// Microfono en archivo de audio (WAV / MP3)
// ---------------------------------------------------------------------------------------

bool RecordingController::startMicFile(const QString &path, int format, QString *errorOut)
{
    auto writer = std::make_unique<AudioFileWriter>();
    QString error;
    QString finalPath = path;
    bool ok = writer->start(finalPath, format == 1 ? AudioFileWriter::Format::Mp3 : AudioFileWriter::Format::Wav, 48000, 2, &error);

    if (!ok && format == 1) {
        // Windows sin el codificador MP3 (por ejemplo ediciones N): se usa WAV
        Logger::warn(QStringLiteral("MP3 no disponible (%1); se guarda el micrófono en WAV.").arg(error));
        finalPath = QFileInfo(path).absolutePath() + QStringLiteral("/") + QFileInfo(path).completeBaseName() + QStringLiteral(".wav");
        ok = writer->start(finalPath, AudioFileWriter::Format::Wav, 48000, 2, &error);
        if (ok)
            emit captureStatus(QStringLiteral("MP3 no está disponible en este Windows: el micrófono se guarda en WAV."));
    }
    if (!ok) {
        if (errorOut)
            *errorOut = error;
        return false;
    }

    // Mezcla 1 = microfono (el mismo reloj de audio que el resto de archivos)
    audio_convert_info conversion;
    std::memset(&conversion, 0, sizeof(conversion));
    conversion.samples_per_sec = 48000;
    conversion.format = AUDIO_FORMAT_16BIT;
    conversion.speakers = SPEAKERS_STEREO;
    audio_output_connect(obs_get_audio(), 1, &conversion, onMicAudio, writer.get());

    m_micFilePath = finalPath;
    m_micWriter = std::move(writer);
    m_micFileActive = true;
    m_micFileDone = false;
    Logger::info(QStringLiteral("Micrófono en archivo de audio: %1").arg(finalPath));
    return true;
}

void RecordingController::stopMicFile()
{
    if (!m_micWriter)
        return;
    if (obs_initialized())
        audio_output_disconnect(obs_get_audio(), 1, onMicAudio, m_micWriter.get()); // deja de recibir audio ya
    QString error;
    m_micFileOk = m_micWriter->finish(&error);
    m_micFileError = error;
    m_micFileDone = true;
    m_micFileActive = false;
    m_micWriter.reset();
}

// ---------------------------------------------------------------------------------------
// Grabacion
// ---------------------------------------------------------------------------------------

bool RecordingController::start(const RecordingOptions &opt, QString *errorOut)
{
    auto fail = [&](const QString &msg) {
        Logger::error(msg);
        if (errorOut)
            *errorOut = msg;
        stopMicFile();
        cleanup();
        setState(State::Idle);
        return false;
    };

    if (m_state != State::Idle) {
        if (errorOut)
            *errorOut = QStringLiteral("Ya hay una grabación en curso.");
        return false;
    }
    setState(State::Starting);
    cleanup(); // por si quedaba algo de un intento anterior
    for (Channel &c : m_ch)
        c = Channel();
    m_micFileActive = false;
    m_micFileDone = false;
    m_micFileOk = true;
    m_micFileError.clear();
    m_micFilePath.clear();

    // --- Comprobaciones previas: carpeta (por juego) y espacio ---
    const QString outDir = gameDir(opt);
    if (!QDir().mkpath(outDir))
        return fail(QStringLiteral("No se pudo crear o abrir la carpeta de destino: %1").arg(outDir));

    const QStorageInfo storage(outDir);
    if (storage.isValid() && storage.bytesAvailable() < kMinFreeBytes)
        return fail(QStringLiteral("Queda menos de 2 GB libres en el disco de destino."));

    // --- Formato de salida segun lo disponible ---
    const bool useMp4 = hasOutput("mp4_output");
    const char *outputId = useMp4 ? "mp4_output" : "ffmpeg_muxer";
    const QString extension = useMp4 ? QStringLiteral("mp4") : QStringLiteral("mkv");
    if (!hasOutput(outputId))
        return fail(QStringLiteral("No hay ninguna salida de archivo disponible (%1).").arg(QString::fromLatin1(outputId)));

    Logger::info(QStringLiteral("Salida: %1, codificador de video principal: %2")
                     .arg(QString::fromLatin1(outputId), QString::fromLatin1(resolveVideoEncoderId(opt))));

    const int modeIndex = static_cast<int>(opt.captureMode);
    Logger::info(QStringLiteral("Modo de captura: %1, objetivo: %2")
                     .arg(modeIndex == 0 ? QStringLiteral("monitor") : modeIndex == 1 ? QStringLiteral("ventana") : QStringLiteral("juego"),
                          modeIndex == 0 ? opt.monitorId : (opt.windowId.isEmpty() ? QStringLiteral("(automático)") : opt.windowId)));

    const QString base = outDir + QStringLiteral("/") + filePrefix(opt) + QStringLiteral("_")
                         + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    Logger::info(QStringLiteral("Juego/carpeta: %1").arg(opt.organizeByGame && !opt.gameName.isEmpty() ? opt.gameName : QStringLiteral("(sin organizar)")));

    const bool micTrack = wantsMicTrack(opt);
    const bool micFile = wantsMicFile(opt);
    const bool micAsMp4 = micFile && opt.micFileFormat == 2;
    const bool micAsAudioFile = micFile && !micAsMp4;
    Logger::info(QStringLiteral("Micrófono: %1")
                     .arg(!opt.recordMic ? QStringLiteral("no se graba")
                                         : QStringLiteral("%1%2%3")
                                               .arg(micTrack ? QStringLiteral("pista en el archivo principal") : QString(),
                                                    (micTrack && micFile) ? QStringLiteral(" + ") : QString(),
                                                    micAsMp4 ? QStringLiteral("MP4 aparte")
                                                             : micAsAudioFile ? (opt.micFileFormat == 1 ? QStringLiteral("MP3 aparte") : QStringLiteral("WAV aparte"))
                                                                              : QString())));

    m_ch[0].used = true;
    m_ch[0].label = QStringLiteral("principal");
    m_ch[0].path = base + QStringLiteral(".") + extension;
    if (micAsMp4) {
        m_ch[1].used = true;
        m_ch[1].label = QStringLiteral("micrófono");
        m_ch[1].path = base + QStringLiteral("_MIC.") + extension;
    }
    if (micAsAudioFile)
        m_micFilePath = base + QStringLiteral("_MIC.") + (opt.micFileFormat == 1 ? QStringLiteral("mp3") : QStringLiteral("wav"));
    if (opt.webcamEnabled) {
        m_ch[2].used = true;
        m_ch[2].label = QStringLiteral("webcam");
        m_ch[2].path = base + QStringLiteral("_CAM.") + extension;
    }

    // --- Fuentes (normalmente ya estan armadas desde antes de pulsar grabar) ---
    QString videoError;
    if (!prepareVideo(opt, &videoError))
        return fail(videoError);
    prepareAudio(opt);
    if (opt.webcamEnabled && !armWebcam(opt)) {
        m_ch[2] = Channel(); // sin webcam: se sigue grabando lo demas
        Logger::warn(QStringLiteral("Webcam no disponible: se graba sin ella."));
        emit captureStatus(QStringLiteral("No se pudo abrir la webcam: se graba sin ella."));
    }

    // ===================== Canal 0: archivo principal =====================
    {
        Channel &c = m_ch[0];

        if (m_replay && m_encSig != encoderSignature(opt))
            disarmReplay(); // ajustes de video distintos: se rearma despues con los nuevos

        QString encoderError;
        if (m_replay) {
            // La repeticion instantanea ya esta codificando: la grabacion usa codificadores propios para
            // empezar exactamente en su primer fotograma (si compartiera, esperaria al siguiente fotograma
            // clave y los archivos quedarian desfasados).
            if (!createEncoderSet(opt, &c.enc, QStringLiteral("main"), &encoderError))
                return fail(encoderError);
            c.sharedEncoders = false;
        } else {
            if (!ensureSharedEncoders(opt, &encoderError))
                return fail(encoderError);
            c.enc = m_shared;
            c.sharedEncoders = true;
        }

        obs_data_t *os = obs_data_create();
        obs_data_set_string(os, "path", c.path.toUtf8().constData());
        c.output = obs_output_create(outputId, "rk_output_main", os, nullptr);
        obs_data_release(os);
        if (!c.output)
            return fail(QStringLiteral("No se pudo crear la salida del archivo principal."));
        obs_output_set_video_encoder(c.output, c.enc.video);
        int track = 0;
        if (c.enc.audio)
            obs_output_set_audio_encoder(c.output, c.enc.audio, track++);
        if (c.enc.mic)
            obs_output_set_audio_encoder(c.output, c.enc.mic, track++); // microfono como pista extra
    }

    // ===================== Canal 1: microfono en MP4 con imagen negra =====================
    if (m_ch[1].used) {
        Channel &c = m_ch[1];

        // Video minimo: una mezcla de video propia de 320x180 con una fuente negra.
        obs_data_t *bs = obs_data_create();
        obs_data_set_int(bs, "color", 0xFF000000); // negro opaco (ABGR)
        obs_data_set_int(bs, "width", 320);
        obs_data_set_int(bs, "height", 180);
        m_blackSource = obs_source_create("color_source_v3", "rk_black", bs, nullptr);
        obs_data_release(bs);
        if (!m_blackSource)
            return fail(QStringLiteral("No se pudo crear la imagen negra del archivo del micrófono."));

        m_micView = obs_view_create();
        if (!m_micView)
            return fail(QStringLiteral("No se pudo crear la vista de video del micrófono."));
        obs_view_set_source(m_micView, 0, m_blackSource);

        uint32_t fpsNum = 60, fpsDen = 1;
        mainFps(&fpsNum, &fpsDen);
        Logger::info(QStringLiteral("Mezcla de video del archivo MIC a %1/%2 FPS").arg(fpsNum).arg(fpsDen));
        obs_video_info ovi = mixVideoInfo(320, 180, fpsNum, fpsDen);
        video_t *micVideo = obs_view_add2(m_micView, &ovi);
        if (!micVideo)
            return fail(QStringLiteral("No se pudo crear la mezcla de video del archivo del micrófono."));

        obs_data_t *vs = obs_encoder_defaults("obs_x264");
        if (!vs)
            vs = obs_data_create();
        obs_data_set_string(vs, "rate_control", "CRF");
        obs_data_set_int(vs, "crf", 35);
        obs_data_set_string(vs, "preset", "ultrafast");
        c.enc.video = obs_video_encoder_create("obs_x264", "rk_mic_video_encoder", vs, nullptr);
        obs_data_release(vs);
        if (!c.enc.video)
            return fail(QStringLiteral("No se pudo crear el codificador de video del archivo del micrófono."));
        obs_encoder_set_video(c.enc.video, micVideo);

        obs_data_t *as = obs_data_create();
        obs_data_set_int(as, "bitrate", opt.audioBitrateKbps);
        c.enc.audio = obs_audio_encoder_create("ffmpeg_aac", "rk_mic_audio_encoder", as, 1, nullptr); // mezcla 1
        obs_data_release(as);
        if (!c.enc.audio)
            return fail(QStringLiteral("No se pudo crear el codificador de audio del micrófono."));
        obs_encoder_set_audio(c.enc.audio, obs_get_audio());

        obs_data_t *os = obs_data_create();
        obs_data_set_string(os, "path", c.path.toUtf8().constData());
        c.output = obs_output_create(outputId, "rk_output_mic", os, nullptr);
        obs_data_release(os);
        if (!c.output)
            return fail(QStringLiteral("No se pudo crear la salida del archivo del micrófono."));
        obs_output_set_video_encoder(c.output, c.enc.video);
        obs_output_set_audio_encoder(c.output, c.enc.audio, 0);
    }

    // ===================== Canal 2: webcam (MP4 independiente) =====================
    if (m_ch[2].used) {
        Channel &c = m_ch[2];

        uint32_t fpsNum = 60, fpsDen = 1;
        mainFps(&fpsNum, &fpsDen);
        obs_video_info ovi = mixVideoInfo(opt.webcamWidth, opt.webcamHeight, fpsNum, fpsDen);
        video_t *camVideo = obs_view_add2(m_camView, &ovi);
        if (!camVideo)
            return fail(QStringLiteral("No se pudo crear la mezcla de video de la webcam."));
        m_camMixAdded = true;

        const QByteArray encoderId = resolveVideoEncoderId(opt);
        obs_data_t *vs = obs_encoder_defaults(encoderId.constData());
        if (!vs)
            vs = obs_data_create();
        obs_data_set_string(vs, "rate_control", "CBR");
        obs_data_set_int(vs, "bitrate", opt.webcamHeight >= 1080 ? 8000 : 4500);
        c.enc.video = obs_video_encoder_create(encoderId.constData(), "rk_cam_video_encoder", vs, nullptr);
        obs_data_release(vs);
        if (!c.enc.video)
            return fail(QStringLiteral("No se pudo crear el codificador de video de la webcam."));
        obs_encoder_set_video(c.enc.video, camVideo);

        if (opt.webcamWithAudio && opt.recordMic) {
            obs_data_t *as = obs_data_create();
            obs_data_set_int(as, "bitrate", opt.audioBitrateKbps);
            c.enc.audio = obs_audio_encoder_create("ffmpeg_aac", "rk_cam_audio_encoder", as, 1, nullptr); // microfono (mezcla 1)
            obs_data_release(as);
            if (!c.enc.audio)
                return fail(QStringLiteral("No se pudo crear el codificador de audio de la webcam."));
            obs_encoder_set_audio(c.enc.audio, obs_get_audio());
        }

        obs_data_t *os = obs_data_create();
        obs_data_set_string(os, "path", c.path.toUtf8().constData());
        c.output = obs_output_create(outputId, "rk_output_cam", os, nullptr);
        obs_data_release(os);
        if (!c.output)
            return fail(QStringLiteral("No se pudo crear la salida del archivo de la webcam."));
        obs_output_set_video_encoder(c.output, c.enc.video);
        if (c.enc.audio)
            obs_output_set_audio_encoder(c.output, c.enc.audio, 0);
    }

    // --- Clips durante la grabacion: el buffer arranca junto a la grabacion y comparte codificadores ---
    if (opt.clipsWhileRecording && !m_replay) {
        QString replayError;
        if (!armReplay(opt, &replayError))
            Logger::warn(QStringLiteral("Clips no disponibles en esta grabación: %1").arg(replayError));
    }

    // --- Conectar senales e iniciar las salidas lo mas juntas posible ---
    for (int i = 0; i < 3; ++i) {
        if (!m_ch[i].used)
            continue;
        signal_handler_t *handler = obs_output_get_signal_handler(m_ch[i].output);
        signal_handler_connect(handler, "start", onOutputStart, &m_tags[i]);
        signal_handler_connect(handler, "stop", onOutputStop, &m_tags[i]);
    }

    for (int i = 0; i < 3; ++i) {
        if (!m_ch[i].used)
            continue;
        if (!obs_output_start(m_ch[i].output)) {
            const char *last = obs_output_get_last_error(m_ch[i].output);
            const QString msg = QStringLiteral("No se pudo iniciar el archivo %1: %2")
                                    .arg(m_ch[i].label, last ? QString::fromUtf8(last) : QStringLiteral("error desconocido"));
            Logger::error(msg);

            // Si algun archivo ya arranco, se detiene todo y se informa al terminar.
            m_ch[i].stopped = true;
            m_ch[i].failMessage = msg;
            bool anyRunning = false;
            for (int j = 0; j < 3; ++j)
                if (m_ch[j].used && j != i && !m_ch[j].stopped)
                    anyRunning = true;
            if (!anyRunning)
                return fail(msg);
            if (errorOut)
                *errorOut = msg;
            requestStopAll();
            return false; // la limpieza ocurre cuando terminen las otras salidas
        }
    }

    // Microfono en WAV/MP3: se conecta al audio de libobs justo despues de arrancar las salidas
    if (micAsAudioFile) {
        QString micError;
        if (!startMicFile(m_micFilePath, opt.micFileFormat, &micError)) {
            Logger::error(QStringLiteral("No se pudo iniciar el archivo de audio del micrófono: %1").arg(micError));
            m_micFilePath.clear();
            m_micFileOk = false;
            m_micFileError = micError;
            m_micFileDone = true;
            emit captureStatus(QStringLiteral("No se pudo guardar el micrófono aparte: %1").arg(micError));
        }
    }

    Logger::info(QStringLiteral("Iniciando grabación: %1").arg(m_ch[0].path));
    return true; // el paso a "Recording" ocurre en handleStarted()
}

void RecordingController::requestStopAll()
{
    setState(State::Stopping);
    for (Channel &c : m_ch)
        if (c.used && c.output && !c.stopped) {
            obs_output_stop(c.output);
            Logger::info(QStringLiteral("obs_output_stop(%1) llamado").arg(c.label));
        }
    stopMicFile(); // el audio del microfono termina en el mismo instante
    m_watchdog->start();
}

void RecordingController::stop()
{
    if (m_state != State::Recording)
        return;
    Logger::info(QStringLiteral("Deteniendo grabación..."));
    requestStopAll();
}

void RecordingController::handleStarted(int index)
{
    if (index < 0 || index > 2 || !m_ch[index].used)
        return;
    m_ch[index].started = true;
    Logger::info(QStringLiteral("Salida %1 iniciada.").arg(m_ch[index].label));

    if (m_state == State::Starting && allUsed(&Channel::started)) {
        setState(State::Recording);
        if (m_mode == CaptureMode::Game) {
            QTimer::singleShot(8000, this, [this] {
                if (m_state == State::Recording && !m_gameHooked)
                    emit captureStatus(QStringLiteral("Todavía no se detecta ningún juego (la imagen saldrá en negro). "
                                                      "Si el juego tiene anticheat, la captura de juego puede quedar bloqueada: "
                                                      "detén la grabación y usa el modo Ventana o Monitor."));
            });
        }
        QStringList files;
        for (const Channel &c : m_ch)
            if (c.used)
                files << c.path;
        if (m_micFileActive && !m_micFilePath.isEmpty())
            files << m_micFilePath;
        emit recordingStarted(files);
    }
}

void RecordingController::handleHooked(const QString &text, const QString &title, const QString &exe)
{
    m_gameHooked = !text.startsWith(QStringLiteral("Se perdió"));
    m_hookTitle = title;
    m_hookExe = exe;
    Logger::info(text);
    emit captureStatus(text);
}

void RecordingController::handleStopped(int index, int code)
{
    if (index < 0 || index > 2 || !m_ch[index].used)
        return;

    Channel &c = m_ch[index];
    c.stopped = true;
    c.stopCode = code;
    if (c.output) {
        const char *last = obs_output_get_last_error(c.output);
        if (last && *last)
            c.lastError = QString::fromUtf8(last);
    }
    Logger::info(QStringLiteral("Salida %1 detenida (código %2).").arg(c.label).arg(code));

    // Si una salida cae por su cuenta, se detienen las demas y se informa al final.
    if (m_state != State::Stopping) {
        if (code != OBS_OUTPUT_SUCCESS)
            Logger::error(QStringLiteral("La salida %1 falló durante la grabación: %2")
                              .arg(c.label, outputErrorText(code)));
        requestStopAll();
    }

    finishIfDone();
}

void RecordingController::finishIfDone()
{
    for (const Channel &c : m_ch)
        if (c.used && !c.stopped)
            return;

    QStringList files;
    QStringList problems;
    for (const Channel &c : m_ch) {
        if (!c.used)
            continue;
        const QFileInfo info(c.path);
        const bool fileOk = info.exists() && info.size() > 0;
        if (fileOk)
            files << c.path;

        if (!c.failMessage.isEmpty()) {
            problems << c.failMessage;
        } else if (c.stopCode != OBS_OUTPUT_SUCCESS) {
            QString p = QStringLiteral("Archivo %1: %2").arg(c.label, outputErrorText(c.stopCode));
            if (!c.lastError.isEmpty())
                p += QStringLiteral(" (%1)").arg(c.lastError);
            problems << p;
        } else if (!fileOk) {
            problems << QStringLiteral("Archivo %1: no existe o está vacío.").arg(c.label);
        }
        Logger::info(QStringLiteral("Resultado %1: archivo=%2 tamaño=%3 bytes código=%4")
                         .arg(c.label, c.path)
                         .arg(info.exists() ? info.size() : 0)
                         .arg(c.stopCode));
    }

    // Microfono en WAV/MP3
    if (m_micFileDone && !m_micFilePath.isEmpty()) {
        const QFileInfo info(m_micFilePath);
        if (info.exists() && info.size() > 44)
            files << m_micFilePath;
        else
            problems << QStringLiteral("Archivo de audio del micrófono: no existe o está vacío.");
        if (!m_micFileOk)
            problems << QStringLiteral("Micrófono aparte: %1").arg(m_micFileError);
        Logger::info(QStringLiteral("Resultado micrófono (audio): archivo=%1 tamaño=%2 bytes")
                         .arg(m_micFilePath).arg(info.exists() ? info.size() : 0));
    } else if (m_micFileDone && !m_micFileOk) {
        problems << QStringLiteral("Micrófono aparte: %1").arg(m_micFileError);
    }

    m_watchdog->stop();
    cleanup();
    releaseSharedEncodersIfUnused();
    setState(State::Idle);

    const bool ok = problems.isEmpty();
    const QString message = ok ? QStringLiteral("Grabación finalizada correctamente.")
                               : problems.join(QStringLiteral("\n"));
    emit recordingFinished(files, ok, message);
}

void RecordingController::cleanup()
{
    if (!obs_initialized())
        return;

    if (m_micWriter)
        stopMicFile();

    for (int i = 0; i < 3; ++i) {
        Channel &c = m_ch[i];
        if (c.output) {
            signal_handler_t *handler = obs_output_get_signal_handler(c.output);
            signal_handler_disconnect(handler, "start", onOutputStart, &m_tags[i]);
            signal_handler_disconnect(handler, "stop", onOutputStop, &m_tags[i]);
            obs_output_release(c.output);
            c.output = nullptr;
        }
        if (c.sharedEncoders) {
            c.enc = EncoderSet(); // son del controlador (compartidos); se liberan en releaseSharedEncoders()
        } else {
            releaseEncoderSet(&c.enc);
        }
    }

    // Las fuentes de captura y de audio NO se tocan aqui: siguen armadas para el siguiente uso.
    if (m_micView) {
        obs_view_set_source(m_micView, 0, nullptr);
        obs_view_remove(m_micView);
        obs_view_destroy(m_micView);
        m_micView = nullptr;
    }
    if (m_blackSource) {
        obs_source_release(m_blackSource);
        m_blackSource = nullptr;
    }
    if (m_camView && m_camMixAdded) { // la webcam sigue abierta; solo se quita la mezcla de video
        obs_view_remove(m_camView);
        m_camMixAdded = false;
    }
}

void RecordingController::releaseSources()
{
    if (!obs_initialized())
        return;

    stopMicFile();
    disarmReplay();
    releaseSharedEncoders();
    disarmWebcam();
    disarmVideo();

    for (AudioSlot &slot : m_audio) {
        if (slot.meter) {
            obs_volmeter_remove_callback(slot.meter, onVolmeter, &slot.level);
            obs_volmeter_detach_source(slot.meter);
            obs_volmeter_destroy(slot.meter);
            slot.meter = nullptr;
        }
        if (slot.source) {
            if (slot.active)
                obs_set_output_source(slot.channel, nullptr);
            slot.active = false;
            obs_source_release(slot.source);
            slot.source = nullptr;
        }
        slot.level.store(0.0f);
    }

    if (m_camScene) {
        obs_scene_release(m_camScene);
        m_camScene = nullptr;
        m_camItem = nullptr;
    }
    if (m_camSource) {
        obs_source_release(m_camSource);
        m_camSource = nullptr;
    }

    if (m_scene) {
        obs_scene_release(m_scene);
        m_scene = nullptr;
    }
    for (int i = 0; i < 3; ++i) {
        VideoSlot &slot = m_slots[i];
        slot.item = nullptr;
        if (!slot.source)
            continue;
        if (i == 2) {
            signal_handler_t *sh = obs_source_get_signal_handler(slot.source);
            signal_handler_disconnect(sh, "hooked", onGameHooked, this);
            signal_handler_disconnect(sh, "unhooked", onGameUnhooked, this);
        }
        obs_source_release(slot.source);
        slot.source = nullptr;
    }
}
