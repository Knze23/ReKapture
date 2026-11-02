#pragma once

#include <QWidget>

#include <atomic>
#include <mutex>

struct obs_display;
struct obs_source;
struct obs_weak_source;

// Vista previa en vivo dentro de la ventana. Dos modos:
//  - MainCanvas: el lienzo principal de libobs (lo que se graba).
//  - Source: una sola fuente (por ejemplo la webcam), dibujada ajustada al widget.
class PreviewWidget : public QWidget {
    Q_OBJECT

public:
    enum class Mode { MainCanvas, Source };

    explicit PreviewWidget(Mode mode = Mode::MainCanvas, QWidget *parent = nullptr);
    ~PreviewWidget() override;

    // Crea la pantalla de libobs (solo con el motor inicializado). Seguro de llamar varias veces.
    bool ensureDisplay();
    void destroyDisplay();
    void setPreviewEnabled(bool enabled); // false: no se dibuja nada (no gasta GPU)

    // Solo modo Source: fuente a mostrar (nullptr para ninguna). Se guarda una referencia debil,
    // asi que la fuente puede liberarse sin riesgo mientras se dibuja.
    void setSource(obs_source *source);

    QPaintEngine *paintEngine() const override { return nullptr; }
    QSize sizeHint() const override { return {640, 360}; }

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    static void drawThunk(void *param, uint32_t cx, uint32_t cy);
    void draw(uint32_t cx, uint32_t cy);
    void updateShowing();

    Mode m_mode;
    obs_display *m_display = nullptr;
    bool m_enabled = true;

    std::mutex m_mutex;                 // protege m_weak y m_showing (el dibujo corre en otro hilo)
    obs_weak_source *m_weak = nullptr;
    bool m_showing = false;
};
