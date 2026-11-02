#pragma once

#include "../config/AppSettings.h"
#include "../recording/EncoderCatalog.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QSpinBox;

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    SettingsDialog(const AppSettingsData &current, const QVector<EncoderChoice> &encoders, QWidget *parent = nullptr);

    AppSettingsData result() const { return m_result; }

protected:
    void accept() override;

private:
    QWidget *buildVideoTab();
    QWidget *buildRecordingTab();
    QWidget *buildClipsTab();
    QWidget *buildAudioTab();
    QWidget *buildHotkeysTab();
    QWidget *buildAppearanceTab();
    void updateNotes();

    AppSettingsData m_result;
    QVector<EncoderChoice> m_encoders;

    // Video
    QComboBox *m_resolution = nullptr;
    QComboBox *m_fps = nullptr;
    QComboBox *m_encoder = nullptr;
    QComboBox *m_quality = nullptr;
    QComboBox *m_bitrateMode = nullptr;
    QSpinBox *m_bitrateSpin = nullptr;
    QComboBox *m_audioBitrate = nullptr;
    QLabel *m_encoderNote = nullptr;
    QLabel *m_bitrateNote = nullptr;

    // Grabacion
    QCheckBox *m_organizeByGame = nullptr;
    QCheckBox *m_soundCues = nullptr;
    QCheckBox *m_overlayEnabled = nullptr;
    QComboBox *m_overlayCorner = nullptr;
    QCheckBox *m_hideApp = nullptr;

    // Audio y webcam
    QComboBox *m_micExport = nullptr;
    QComboBox *m_micFormat = nullptr;
    QComboBox *m_webcamRes = nullptr;
    QCheckBox *m_webcamAudio = nullptr;
    QCheckBox *m_vstEnabled = nullptr;

    // Clips
    QCheckBox *m_clipsWhileRecording = nullptr;
    QCheckBox *m_instantReplay = nullptr;
    QSpinBox *m_clipSeconds = nullptr;
    QCheckBox *m_autoDisableReplay = nullptr;
    QLabel *m_memoryNote = nullptr;

    // Atajos
    QKeySequenceEdit *m_startKey = nullptr;
    QKeySequenceEdit *m_stopKey = nullptr;
    QKeySequenceEdit *m_clipKey = nullptr;
    QKeySequenceEdit *m_replayKey = nullptr;

    // Apariencia
    QComboBox *m_theme = nullptr;
};
