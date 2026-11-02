#pragma once

#include <QString>
#include <QWidget>

class QTimer;

// Icono de actividad en pantalla (al estilo ShadowPlay): indica si se esta grabando, si el
// microfono esta incluido y si la repeticion instantanea esta activa. Es transparente a los
// clics, siempre visible y NO aparece en las grabaciones.
class ActivityOverlay : public QWidget {
    Q_OBJECT

public:
    explicit ActivityOverlay(QWidget *parent = nullptr);

    void setOverlayEnabled(bool enabled);
    void setCorner(int corner); // 0 arriba-izq, 1 arriba-der, 2 abajo-izq, 3 abajo-der
    void setState(bool recording, bool micOn, bool replayOn, int replaySeconds, bool camOn = false);
    void flash(const QString &text); // aviso breve (por ejemplo "CLIP GUARDADO")

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void refresh();

    bool m_enabled = true;
    int m_corner = 1;
    bool m_recording = false;
    bool m_mic = false;
    bool m_cam = false;
    bool m_replay = false;
    int m_replaySeconds = 30;
    QString m_flash;
    QTimer *m_flashTimer = nullptr;
    bool m_affinityApplied = false;
};
