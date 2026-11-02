#include "ActivityOverlay.h"

#include "../log/Logger.h"

#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QScreen>
#include <QShowEvent>
#include <QTimer>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace {
constexpr int kHeight = 34;
constexpr int kMargin = 18;
constexpr int kPad = 12;
constexpr int kGap = 12;
constexpr int kIcon = 16;
} // namespace

ActivityOverlay::ActivityOverlay(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput
                          | Qt::WindowDoesNotAcceptFocus)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);

    m_flashTimer = new QTimer(this);
    m_flashTimer->setSingleShot(true);
    connect(m_flashTimer, &QTimer::timeout, this, [this] {
        m_flash.clear();
        refresh();
    });
}

void ActivityOverlay::setOverlayEnabled(bool enabled)
{
    m_enabled = enabled;
    refresh();
}

void ActivityOverlay::setCorner(int corner)
{
    m_corner = qBound(0, corner, 3);
    refresh();
}

void ActivityOverlay::setState(bool recording, bool micOn, bool replayOn, int replaySeconds, bool camOn)
{
    m_cam = camOn;
    m_recording = recording;
    m_mic = micOn;
    m_replay = replayOn;
    m_replaySeconds = replaySeconds;
    refresh();
}

void ActivityOverlay::flash(const QString &text)
{
    m_flash = text;
    m_flashTimer->start(2500);
    refresh();
}

void ActivityOverlay::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // Excluye este icono de las capturas: no sale en las grabaciones ni en la previsualizacion.
    if (!m_affinityApplied) {
        if (SetWindowDisplayAffinity(reinterpret_cast<HWND>(winId()), WDA_EXCLUDEFROMCAPTURE))
            m_affinityApplied = true;
        else
            Logger::warn(QStringLiteral("No se pudo excluir el icono de actividad de las capturas (error %1).").arg(GetLastError()));
    }
}

void ActivityOverlay::refresh()
{
    const bool anything = m_recording || m_replay || !m_flash.isEmpty();
    if (!m_enabled || !anything) {
        hide();
        return;
    }

    // Ancho segun los elementos visibles
    QFont font(QStringLiteral("Segoe UI"), 9, QFont::Bold);
    const QFontMetrics fm(font);
    int width = kPad;
    if (m_recording)
        width += kIcon + 6 + fm.horizontalAdvance(QStringLiteral("REC")) + kGap;
    if (m_recording && m_mic)
        width += kIcon + kGap;
    if (m_recording && m_cam)
        width += kIcon + kGap;
    if (m_replay)
        width += kIcon + 6 + fm.horizontalAdvance(QStringLiteral("%1 s").arg(m_replaySeconds)) + kGap;
    if (!m_flash.isEmpty())
        width += fm.horizontalAdvance(m_flash) + kGap;
    width += kPad - kGap;

    setFixedSize(qMax(width, 60), kHeight);

    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect g = screen->geometry();
        const int x = (m_corner == 1 || m_corner == 3) ? g.right() - kMargin - this->width() + 1 : g.left() + kMargin;
        const int y = (m_corner == 2 || m_corner == 3) ? g.bottom() - kMargin - this->height() + 1 : g.top() + kMargin;
        move(x, y);
    }
    show();
    raise();
    update();
}

void ActivityOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Fondo
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(20, 20, 20, 215));
    p.drawRoundedRect(rect(), height() / 2.0, height() / 2.0);

    QFont font(QStringLiteral("Segoe UI"), 9, QFont::Bold);
    p.setFont(font);
    const QFontMetrics fm(font);
    const int cy = height() / 2;
    int x = kPad;

    if (m_recording) {
        // Punto rojo + REC
        p.setBrush(QColor(0xE5, 0x39, 0x45));
        p.drawEllipse(QPoint(x + kIcon / 2, cy), 6, 6);
        x += kIcon + 6;
        p.setPen(QColor(0xF4, 0xF4, 0xF4));
        p.drawText(QPoint(x, cy + fm.ascent() / 2 - 1), QStringLiteral("REC"));
        x += fm.horizontalAdvance(QStringLiteral("REC")) + kGap;

        if (m_mic) {
            // Icono de microfono
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0xF4, 0xF4, 0xF4));
            p.drawRoundedRect(QRectF(x + 5, cy - 7, 6, 10), 3, 3);
            QPen pen(QColor(0xF4, 0xF4, 0xF4), 1.6);
            pen.setCapStyle(Qt::RoundCap);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawArc(QRectF(x + 2.5, cy - 6, 11, 11), 200 * 16, 140 * 16);
            p.drawLine(QPointF(x + 8, cy + 5.5), QPointF(x + 8, cy + 8));
            p.drawLine(QPointF(x + 5, cy + 8), QPointF(x + 11, cy + 8));
            x += kIcon + kGap;
        }
    }

    if (m_recording && m_cam) {
        // Icono de webcam (camara con visor)
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xF4, 0xF4, 0xF4));
        p.drawRoundedRect(QRectF(x + 1, cy - 5, 10, 10), 2, 2);
        QPolygonF lens;
        lens << QPointF(x + 12, cy - 2.5) << QPointF(x + 16, cy - 5) << QPointF(x + 16, cy + 5) << QPointF(x + 12, cy + 2.5);
        p.drawPolygon(lens);
        x += kIcon + kGap;
    }

    if (m_replay) {
        // Icono de repeticion (flecha circular) + segundos
        QPen pen(QColor(0x3D, 0xDC, 0x84), 1.8);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(x + 2, cy - 6, 12, 12), 40 * 16, 280 * 16);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x3D, 0xDC, 0x84));
        QPainterPath arrow;
        arrow.moveTo(x + 12.5, cy - 8.5);
        arrow.lineTo(x + 15.5, cy - 3.5);
        arrow.lineTo(x + 9.5, cy - 3.5);
        arrow.closeSubpath();
        p.drawPath(arrow);
        x += kIcon + 6;
        p.setPen(QColor(0xF4, 0xF4, 0xF4));
        p.drawText(QPoint(x, cy + fm.ascent() / 2 - 1), QStringLiteral("%1 s").arg(m_replaySeconds));
        x += fm.horizontalAdvance(QStringLiteral("%1 s").arg(m_replaySeconds)) + kGap;
    }

    if (!m_flash.isEmpty()) {
        p.setPen(QColor(0x3D, 0xDC, 0x84));
        p.drawText(QPoint(x, cy + fm.ascent() / 2 - 1), m_flash);
    }
}
