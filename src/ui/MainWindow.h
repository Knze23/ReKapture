#pragma once

#include "../capture/GameNaming.h"
#include "../config/AppSettings.h"
#include "../obs/ObsEngine.h"
#include "../recording/EncoderCatalog.h"
#include "../recording/RecordingController.h"

#include <QElapsedTimer>
#include <QMainWindow>

class ActivityOverlay;
class HotkeyManager;
class LevelMeter;
class LogPage;
class LibraryPage;
class PreviewWidget;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QStackedWidget;
class QThread;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void onRecordClicked();
    void onModeClicked(int mode);
    void onTargetChanged();
    void onRefreshTargets();
    void onDeviceChanged();
    void onVolumeChanged();
    void onVolumeReleased();
    void onPreviewToggled(bool on);
    void onNavClicked(int page);
    void onWebcamToggled(bool on);
    void onWebcamPreviewToggled(bool on);
    void onWebcamChanged();
    void onRefreshWebcams();
    void openEffects(int slot);
    void onReplayToggled(bool on);
    void onClipSaved(const QString &path);
    void onReplayStateChanged(bool active);
    void onPerformanceWarning(const QString &text);
    void onChooseFolder();
    void onOpenFolder();
    void onOpenSettings();
    void onHotkey(int id);
    void onStateChanged(int state);
    void onRecordingStarted(const QStringList &files);
    void onRecordingFinished(const QStringList &files, bool ok, const QString &message);
    void onCaptureStatus(const QString &text);
    void updateLive();

private:
    void buildUi();
    void startEngineInit();
    void showInitResult(const ObsInitResult &result);
    void setStatus(const QString &text, bool error = false);
    void updateControls();
    void updateSummary();
    void updateDisk();
    void updateCaptureChip();
    void populateTargets();
    void rememberTarget(int mode);
    void selectById(QComboBox *combo, const QString &id);
    void refreshIdleState();
    void applyHotkeys();
    void applyTheme();
    void updateOverlay();
    void refreshForeground();
    QString currentGameName() const;
    void saveClip();
    void toggleReplay();
    void applyCaptureVisibility();
    void updatePreviewActivity();
    void syncCameraPreview();
    int videoBitrateKbps() const;
    void populateWebcams();
    void saveEffects(int slot);
    void startRecording(bool interactive);
    void stopRecording();
    RecordingOptions buildOptions() const;

    ObsEngine m_engine;
    RecordingController *m_recorder = nullptr;
    HotkeyManager *m_hotkeys = nullptr;
    QThread *m_initThread = nullptr;
    bool m_engineReady = false;
    bool m_closing = false;
    bool m_closeAfterStop = false;
    bool m_affinitySet = false;

    AppSettingsData m_settings;
    QVector<EncoderChoice> m_encoders;
    QString m_hotkeyWarning;
    QString m_hookText;
    QString m_filesText;
    QElapsedTimer m_elapsed;
    QTimer *m_liveTimer = nullptr;
    int m_liveTick = 0;

    // Interfaz
    PreviewWidget *m_preview = nullptr;
    QStackedWidget *m_pages = nullptr;
    LibraryPage *m_library = nullptr;
    QButtonGroup *m_navGroup = nullptr;
    QLabel *m_logo = nullptr;
    QString m_emblem;
    QStackedWidget *m_previewStack = nullptr;
    QCheckBox *m_previewToggle = nullptr;
    QCheckBox *m_replayToggle = nullptr;
    ActivityOverlay *m_overlay = nullptr;
    GameNaming::WindowInfo m_lastForeground;
    bool m_recordingMic = false;
    bool m_recordingCam = false;
    QPushButton *m_desktopFx = nullptr;
    QPushButton *m_micFx = nullptr;
    QCheckBox *m_webcamToggle = nullptr;
    QCheckBox *m_webcamPreviewToggle = nullptr;
    QStackedWidget *m_camStack = nullptr;
    PreviewWidget *m_camPreview = nullptr;
    QLabel *m_camPlaceholder = nullptr;
    QLabel *m_previewPlaceholder = nullptr;
    LogPage *m_logPage = nullptr;
    QComboBox *m_webcamCombo = nullptr;
    QPushButton *m_webcamRefresh = nullptr;
    QPushButton *m_recButton = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_pill = nullptr;
    QLabel *m_captureChip = nullptr;
    QLabel *m_status = nullptr;

    QButtonGroup *m_modeGroup = nullptr;
    QLabel *m_targetLabel = nullptr;
    QComboBox *m_targetCombo = nullptr;
    QPushButton *m_refreshButton = nullptr;

    QComboBox *m_desktopCombo = nullptr;
    QComboBox *m_micCombo = nullptr;
    LevelMeter *m_desktopMeter = nullptr;
    LevelMeter *m_micMeter = nullptr;
    QSlider *m_desktopSlider = nullptr;
    QSlider *m_micSlider = nullptr;
    QLabel *m_desktopVolLabel = nullptr;
    QLabel *m_micVolLabel = nullptr;

    QLabel *m_folderLabel = nullptr;
    QPushButton *m_folderButton = nullptr;
    QPushButton *m_openFolderButton = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QLabel *m_diskLabel = nullptr;
    QPushButton *m_settingsButton = nullptr;
};
