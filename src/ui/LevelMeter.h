#pragma once

#include <QColor>
#include <QWidget>

// Medidor de nivel de audio horizontal (verde -> amarillo -> rojo) con marca de pico.
class LevelMeter : public QWidget {
    Q_OBJECT

public:
    explicit LevelMeter(QWidget *parent = nullptr);

    // Nivel instantaneo 0..1; el medidor aplica su propia caida suave.
    void setLevel(float level);
    void setTrackColor(const QColor &color) { m_track = color; update(); }

    QSize sizeHint() const override { return {200, 10}; }
    QSize minimumSizeHint() const override { return {60, 10}; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    float m_level = 0.0f;
    float m_peak = 0.0f;
    QColor m_track = QColor(0x2A, 0x2A, 0x2A);
};
