#include "LevelMeter.h"

#include <QLinearGradient>
#include <QPainter>

#include <algorithm>

LevelMeter::LevelMeter(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(10);
}

void LevelMeter::setLevel(float level)
{
    level = std::clamp(level, 0.0f, 1.0f);
    if (level > m_level)
        m_level = level;
    else
        m_level = std::max(level, m_level - 0.05f); // caida suave

    if (level > m_peak)
        m_peak = level;
    else
        m_peak = std::max(m_level, m_peak - 0.012f); // el pico baja mas despacio
    update();
}

void LevelMeter::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF full(0.5, 0.5, width() - 1.0, height() - 1.0);
    const qreal radius = full.height() / 2.0;

    p.setPen(Qt::NoPen);
    p.setBrush(m_track);
    p.drawRoundedRect(full, radius, radius);

    if (m_level > 0.001f) {
        QLinearGradient gradient(full.left(), 0, full.right(), 0);
        gradient.setColorAt(0.00, QColor(0x3D, 0xDC, 0x84));
        gradient.setColorAt(0.70, QColor(0x3D, 0xDC, 0x84));
        gradient.setColorAt(0.85, QColor(0xF5, 0xC5, 0x42));
        gradient.setColorAt(1.00, QColor(0xE5, 0x39, 0x45));
        p.setBrush(gradient);
        QRectF bar = full;
        bar.setWidth(std::max<qreal>(full.height(), full.width() * m_level));
        p.drawRoundedRect(bar, radius, radius);
    }

    if (m_peak > 0.02f) {
        const qreal x = full.left() + full.width() * m_peak;
        p.setBrush(QColor(0xF4, 0xF4, 0xF4));
        p.drawRect(QRectF(std::min(x, full.right() - 2.0), full.top(), 2.0, full.height()));
    }
}
