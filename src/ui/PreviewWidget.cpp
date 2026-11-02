#include "PreviewWidget.h"

#include "../log/Logger.h"

#include <obs.h>
#include <graphics/graphics.h>

#include <QResizeEvent>

#include <algorithm>
#include <cstring>

namespace {

struct Fit { int x, y, w, h; };

Fit fitInto(uint32_t boxW, uint32_t boxH, uint32_t baseW, uint32_t baseH)
{
    const float scale = std::min(static_cast<float>(boxW) / static_cast<float>(baseW),
                                 static_cast<float>(boxH) / static_cast<float>(baseH));
    Fit f;
    f.w = static_cast<int>(static_cast<float>(baseW) * scale);
    f.h = static_cast<int>(static_cast<float>(baseH) * scale);
    f.x = (static_cast<int>(boxW) - f.w) / 2;
    f.y = (static_cast<int>(boxH) - f.h) / 2;
    return f;
}

} // namespace

PreviewWidget::PreviewWidget(Mode mode, QWidget *parent)
    : QWidget(parent)
    , m_mode(mode)
{
    // Ventana nativa propia para que Direct3D pueda dibujar en ella.
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_NativeWindow);
    setMinimumSize(mode == Mode::Source ? 200 : 320, mode == Mode::Source ? 112 : 180);
}

PreviewWidget::~PreviewWidget()
{
    destroyDisplay();
}

bool PreviewWidget::ensureDisplay()
{
    if (m_display)
        return true;
    if (!obs_initialized())
        return false;

    const qreal dpr = devicePixelRatioF();
    gs_init_data info;
    std::memset(&info, 0, sizeof(info));
    info.cx = static_cast<uint32_t>(std::max(1, static_cast<int>(width() * dpr)));
    info.cy = static_cast<uint32_t>(std::max(1, static_cast<int>(height() * dpr)));
    info.format = GS_BGRA;
    info.zsformat = GS_ZS_NONE;
    info.window.hwnd = reinterpret_cast<decltype(info.window.hwnd)>(winId());

    m_display = obs_display_create(&info, 0xFF151515);
    if (!m_display) {
        Logger::error(QStringLiteral("No se pudo crear la previsualización."));
        return false;
    }
    obs_display_add_draw_callback(m_display, &PreviewWidget::drawThunk, this);
    obs_display_set_enabled(m_display, m_enabled);
    Logger::info(QStringLiteral("Previsualización creada (%1x%2, %3).")
                     .arg(info.cx).arg(info.cy).arg(m_mode == Mode::Source ? QStringLiteral("fuente") : QStringLiteral("lienzo")));
    return true;
}

void PreviewWidget::destroyDisplay()
{
    setSource(nullptr); // libera la referencia y deja de marcar la fuente como visible
    if (!m_display)
        return;
    obs_display_remove_draw_callback(m_display, &PreviewWidget::drawThunk, this);
    obs_display_destroy(m_display);
    m_display = nullptr;
}

void PreviewWidget::setPreviewEnabled(bool enabled)
{
    m_enabled = enabled;
    if (m_display)
        obs_display_set_enabled(m_display, enabled);
    updateShowing();
}

void PreviewWidget::setSource(obs_source *source)
{
    // Las llamadas a libobs que pueden usar el contexto grafico se hacen SIN tener el cierre, para
    // no bloquearse con el hilo de dibujo.
    obs_weak_source *oldWeak = nullptr;
    bool oldShowing = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        oldWeak = m_weak;
        oldShowing = m_showing;
        m_weak = source ? obs_source_get_weak_source(source) : nullptr;
        m_showing = false;
    }
    if (oldWeak) {
        if (obs_source_t *old = obs_weak_source_get_source(oldWeak)) {
            if (oldShowing)
                obs_source_dec_showing(old);
            obs_source_release(old);
        }
        obs_weak_source_release(oldWeak);
    }
    updateShowing();
}

// Una fuente solo genera imagen si "se esta mostrando": se marca mientras la vista previa esta activa.
void PreviewWidget::updateShowing()
{
    if (m_mode != Mode::Source)
        return;

    obs_source_t *src = nullptr;
    bool inc = false;
    bool dec = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_weak)
            return;
        src = obs_weak_source_get_source(m_weak);
        if (!src)
            return;
        if (m_enabled && !m_showing) {
            m_showing = true;
            inc = true;
        } else if (!m_enabled && m_showing) {
            m_showing = false;
            dec = true;
        }
    }
    if (inc)
        obs_source_inc_showing(src);
    if (dec)
        obs_source_dec_showing(src);
    obs_source_release(src);
}

void PreviewWidget::drawThunk(void *param, uint32_t cx, uint32_t cy)
{
    static_cast<PreviewWidget *>(param)->draw(cx, cy);
}

// Se ejecuta en el hilo grafico de libobs.
void PreviewWidget::draw(uint32_t cx, uint32_t cy)
{
    if (cx == 0 || cy == 0)
        return;

    if (m_mode == Mode::MainCanvas) {
        obs_video_info ovi;
        if (!obs_get_video_info(&ovi) || ovi.base_width == 0 || ovi.base_height == 0)
            return;
        const Fit f = fitInto(cx, cy, ovi.base_width, ovi.base_height);
        gs_viewport_push();
        gs_projection_push();
        gs_ortho(0.0f, static_cast<float>(ovi.base_width), 0.0f, static_cast<float>(ovi.base_height), -100.0f, 100.0f);
        gs_set_viewport(f.x, f.y, f.w, f.h);
        obs_render_main_texture();
        gs_projection_pop();
        gs_viewport_pop();
        return;
    }

    // Modo fuente: referencia fuerte solo durante este dibujo
    obs_source_t *src = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_weak && m_showing)
            src = obs_weak_source_get_source(m_weak);
    }
    if (!src)
        return;
    const uint32_t w = obs_source_get_width(src);
    const uint32_t h = obs_source_get_height(src);
    if (w > 0 && h > 0) {
        const Fit f = fitInto(cx, cy, w, h);
        gs_viewport_push();
        gs_projection_push();
        gs_ortho(0.0f, static_cast<float>(w), 0.0f, static_cast<float>(h), -100.0f, 100.0f);
        gs_set_viewport(f.x, f.y, f.w, f.h);
        obs_source_video_render(src);
        gs_projection_pop();
        gs_viewport_pop();
    }
    obs_source_release(src);
}

void PreviewWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_display) {
        const qreal dpr = devicePixelRatioF();
        obs_display_resize(m_display, static_cast<uint32_t>(std::max(1, static_cast<int>(width() * dpr))),
                           static_cast<uint32_t>(std::max(1, static_cast<int>(height() * dpr))));
    }
}
