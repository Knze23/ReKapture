#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <cstdint>
#include <memory>

class QTimer;

// Declaraciones adelantadas para no incluir obs.h en las cabeceras de la interfaz.
struct obs_source;
struct obs_scene;
struct obs_scene_item;
struct obs_encoder;
struct obs_output;
struct obs_view;
struct obs_volmeter;
struct calldata;

class AudioFileWriter;

struct MonitorInfo {
    QString id;    // identificador que entiende OBS (monitor o ventana)
    QString name;  // texto para mostrar al usuario
};

struct DeviceInfo {
    QString id;    // "default" o el identificador del dispositivo
    QString name;
};

enum class CaptureMode { Monitor = 0, Window = 1, Game = 2 };

struct RecordingOptions {
    CaptureMode captureMode = CaptureMode::Monitor;
    QString monitorId;
    QString windowId;             // ventana o juego (vacio = deteccion automatica en modo juego)
    QString folder;

    bool recordDesktopAudio = true;
    QString desktopDeviceId = QStringLiteral("default");
    int desktopVolumePct = 100;

    bool recordMic = true;
    QString micDeviceId = QStringLiteral("default");
    int micVolumePct = 100;
    int micExportMode = 0;        // 0 archivo aparte, 1 pista en el archivo principal, 2 ambos
    int micFileFormat = 0;        // archivo aparte: 0 WAV, 1 MP3, 2 MP4 con imagen negra

    // Webcam: se graba en un MP4 independiente (nunca se integra en el video de la pantalla)
    bool webcamEnabled = false;
    QString webcamDeviceId;
    int webcamWidth = 1280;
    int webcamHeight = 720;
    bool webcamWithAudio = false;

    QString videoEncoderId;       // id de OBS; vacio = elegir automaticamente
    int bitrateKbps = 12000;      // bitrate objetivo (CBR) del archivo principal
    int width = 1920;             // lienzo actual (ya aplicado en el motor)
    int height = 1080;
    int fps = 60;
    int audioBitrateKbps = 160;   // bitrate de cada pista de audio AAC
    bool webcamPreview = false;   // mantener la camara abierta para la vista previa

    // Organizacion por juego y clips
    QString gameName;             // nombre limpio del juego (vacio = sin juego detectado)
    bool organizeByGame = true;
    bool instantReplay = false;       // buffer siempre activo mientras no se graba
    bool clipsWhileRecording = true;  // el buffer arranca junto a la grabacion
    int clipSeconds = 30;
};

struct EffectInfo {
    QString filterId;
    QString name;
    bool enabled = true;
};

class RecordingController;

// Dato que se entrega a las funciones que libobs llama desde sus propios hilos.
struct SignalTag {
    RecordingController *self = nullptr;
    int index = 0;
};

// Una sesion de grabacion puede producir:
//  - Archivo principal: video (monitor, ventana o juego) + audio del escritorio (pista 1)
//    y, opcionalmente, el microfono como segunda pista.
//  - Microfono aparte: WAV, MP3 o MP4 con imagen negra (sincronizado con el reloj de libobs).
//  - Webcam: MP4 independiente.
// Las fuentes de captura y de audio se mantienen "armadas" mientras no se graba, para tener
// medidores de nivel, previsualizacion y el juego ya enganchado cuando se pulsa grabar.
class RecordingController : public QObject {
    Q_OBJECT

public:
    enum class State { Idle = 0, Starting = 1, Recording = 2, Stopping = 3 };

    explicit RecordingController(QObject *parent = nullptr);
    ~RecordingController() override;

    State state() const { return m_state; }

    // Solo llamar con el motor inicializado.
    QVector<MonitorInfo> listMonitors() const;
    QVector<MonitorInfo> listWindows() const; // ventanas abiertas (id = valor que entiende OBS)
    QVector<DeviceInfo> listAudioDevices(bool input) const;
    QVector<DeviceInfo> listWebcams() const;

    // Prepara (sin grabar) las fuentes de audio y, si captureVideo es true, la de video.
    void applyIdleState(const RecordingOptions &options, bool captureVideo);
    void setVolumes(int desktopPct, int micPct);

    // Nivel de pico 0..1 de los dispositivos de audio.
    float desktopLevel() const { return m_audio[0].level.load(); }
    float micLevel() const { return m_audio[1].level.load(); }
    bool gameHooked() const { return m_gameHooked; }
    obs_source *webcamSource() const { return m_camSource; } // para la vista previa de la webcam
    QString hookedTitle() const { return m_hookTitle; }
    QString hookedExe() const { return m_hookExe; }

    // Repeticion instantanea / clips (buffer de los ultimos N segundos)
    bool replayActive() const { return m_replay != nullptr; }
    bool saveClip(const RecordingOptions &options, QString *errorOut);

    // Devuelve false (y rellena errorOut) si no se pudo iniciar.
    bool start(const RecordingOptions &options, QString *errorOut);
    // Pide la parada; el resultado llega por recordingFinished().
    void stop();

    // Libera todas las fuentes persistentes (llamar antes de cerrar libobs).
    void releaseSources();

    // ---- Efectos de audio (filtros de OBS: supresor de ruido, compresor, VST...) ----
    // slot: 0 = audio del escritorio, 1 = microfono
    static QStringList availableFilters();
    bool audioSourceExists(int slot) const;
    QVector<EffectInfo> effects(int slot) const;
    obs_source *effectSource(int slot, int index) const;
    bool addEffect(int slot, const QString &filterId, const QString &name, const QString &settingsJson = QString());
    void removeEffect(int slot, int index);
    void moveEffect(int slot, int index, int delta); // -1 sube, +1 baja
    void setEffectEnabled(int slot, int index, bool enabled);
    QString effectsJson(int slot) const;               // para guardar en los ajustes
    void setEffectsJson(int slot, const QString &json); // se aplica al crear (o ya) la fuente

signals:
    void stateChanged(int state);
    void recordingStarted(const QStringList &files);
    void recordingFinished(const QStringList &files, bool ok, const QString &message);
    void captureStatus(const QString &text); // avisos de la captura de juego y de dispositivos
    void clipSaved(const QString &path);
    void replayStateChanged(bool active);
    void performanceWarning(const QString &text);

private slots:
    void handleStarted(int index);
    void handleStopped(int index, int code);
    void handleHooked(const QString &text, const QString &title, const QString &exe);
    void handleClipSaved();
    void handleReplayStopped(int code);
    void perfTick();

private:
    struct EncoderSet {
        obs_encoder *video = nullptr;
        obs_encoder *audio = nullptr;  // escritorio (mezcla 0)
        obs_encoder *mic = nullptr;    // microfono como pista extra (mezcla 1)
    };

    struct Channel {
        bool used = false;
        bool started = false;
        bool stopped = false;
        int stopCode = 0;
        QString failMessage;
        QString lastError;
        QString label;
        QString path;
        obs_output *output = nullptr;
        EncoderSet enc;
        bool sharedEncoders = false; // los codificadores son compartidos con la repeticion
    };

    struct VideoSlot {
        obs_source *source = nullptr;
        obs_scene_item *item = nullptr;
    };

    struct AudioSlot {
        obs_source *source = nullptr;
        obs_volmeter *meter = nullptr;
        QString device;
        uint32_t channel = 0;
        bool active = false;
        std::atomic<float> level{0.0f};
    };

    void setState(State s);
    void cleanup();
    void requestStopAll();
    void finishIfDone();
    bool allUsed(bool Channel::*flag) const;

    bool prepareVideo(const RecordingOptions &opt, QString *errorOut);
    void disarmVideo();
    void prepareAudio(const RecordingOptions &opt);
    void configureAudio(AudioSlot &slot, const char *typeId, const char *name, bool enabled,
                        const QString &device, int volumePct, uint32_t mixers, uint32_t channel);
    void applyEffectsToSlot(int slot);
    void clearEffectsOfSlot(int slot);

    // Codificadores
    bool createEncoderSet(const RecordingOptions &opt, EncoderSet *out, const QString &tag, QString *errorOut);
    void releaseEncoderSet(EncoderSet *set);
    bool ensureSharedEncoders(const RecordingOptions &opt, QString *errorOut);
    void releaseSharedEncoders();
    void releaseSharedEncodersIfUnused();
    bool armReplay(const RecordingOptions &opt, QString *errorOut);
    void disarmReplay();

    // Webcam (independiente)
    bool armWebcam(const RecordingOptions &opt);
    void disarmWebcam();

    // Microfono en archivo de audio (WAV/MP3)
    bool startMicFile(const QString &path, int format, QString *errorOut);
    void stopMicFile();

    State m_state = State::Idle;
    Channel m_ch[3];      // 0 = principal, 1 = micrófono (MP4 con imagen negra), 2 = webcam
    SignalTag m_tags[3];

    VideoSlot m_slots[3]; // una fuente persistente por modo de captura
    QString m_videoSig[3]; // ajustes aplicados a cada fuente (evita reiniciar la captura sin necesidad)
    AudioSlot m_audio[2]; // 0 = escritorio, 1 = micrófono
    QString m_effectsJson[2];
    obs_scene *m_scene = nullptr;
    bool m_videoArmed = false;
    CaptureMode m_mode = CaptureMode::Monitor;
    bool m_gameHooked = false;

    obs_source *m_blackSource = nullptr;
    obs_view *m_micView = nullptr;
    QTimer *m_watchdog = nullptr;

    QString m_hookTitle;
    QString m_hookExe;

    // Webcam persistente (activa mientras esta habilitada) y su vista de video por grabacion
    obs_source *m_camSource = nullptr;
    obs_scene *m_camScene = nullptr;
    obs_scene_item *m_camItem = nullptr;
    obs_view *m_camView = nullptr;
    bool m_camMixAdded = false;
    bool m_camArmed = false;
    QString m_camDevice;

    // Microfono en WAV/MP3
    std::unique_ptr<AudioFileWriter> m_micWriter;
    QString m_micFilePath;
    int m_micFileFormat = 0;
    bool m_micFileActive = false;
    bool m_micFileDone = false;
    bool m_micFileOk = true;
    QString m_micFileError;

    // Codificadores compartidos entre la grabacion principal y la repeticion
    EncoderSet m_shared;
    QString m_encSig;
    obs_output *m_replay = nullptr;
    qint64 m_lastClipMs = 0;

    // Vigilancia de rendimiento (fotogramas perdidos) mientras la repeticion esta activa
    QTimer *m_perfTimer = nullptr;
    uint32_t m_lastTotal = 0;
    uint32_t m_lastLagged = 0;
    uint64_t m_perfTotal = 0;
    uint64_t m_perfLagged = 0;
    int m_perfSeconds = 0;
};
