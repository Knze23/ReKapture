#include "PropertiesPanel.h"

#include "../log/Logger.h"

#include <obs.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace {

QLabel *makeLabel(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("muted"));
    l->setWordWrap(true);
    return l;
}

} // namespace

PropertiesPanel::PropertiesPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_content = new QWidget(scroll);
    m_layout = new QVBoxLayout(m_content);
    m_layout->setContentsMargins(4, 4, 12, 4);
    m_layout->setSpacing(10);
    scroll->setWidget(m_content);
    outer->addWidget(scroll);
}

PropertiesPanel::~PropertiesPanel()
{
    clearUi();
}

void PropertiesPanel::setSource(obs_source *source)
{
    m_source = source;
    rebuild();
}

void PropertiesPanel::clearUi()
{
    while (QLayoutItem *item = m_layout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    if (m_props) {
        obs_properties_destroy(m_props);
        m_props = nullptr;
    }
    if (m_settings) {
        obs_data_release(m_settings);
        m_settings = nullptr;
    }
}

void PropertiesPanel::commit(const QString &name)
{
    if (!m_source || !m_settings)
        return;
    obs_source_update(m_source, m_settings);
    if (m_props) {
        obs_property_t *prop = obs_properties_get(m_props, name.toUtf8().constData());
        if (prop && obs_property_modified(prop, m_settings))
            QTimer::singleShot(0, this, [this] { rebuild(); }); // las propiedades cambian: se reconstruye fuera de la señal
    }
    emit changed();
}

void PropertiesPanel::rebuild()
{
    clearUi();
    if (!m_source) {
        m_layout->addWidget(makeLabel(QStringLiteral("Selecciona un efecto para ajustar sus parámetros."), m_content));
        m_layout->addStretch(1);
        return;
    }

    m_settings = obs_source_get_settings(m_source);
    m_props = obs_source_properties(m_source);
    if (!m_props || !m_settings) {
        m_layout->addWidget(makeLabel(QStringLiteral("Este efecto no tiene parámetros."), m_content));
        m_layout->addStretch(1);
        return;
    }

    int shown = 0;
    for (obs_property_t *p = obs_properties_first(m_props); p; obs_property_next(&p)) {
        if (!obs_property_visible(p))
            continue;

        const QString name = QString::fromUtf8(obs_property_name(p));
        const QString desc = QString::fromUtf8(obs_property_description(p));
        const std::string nameUtf8 = name.toStdString();
        const bool enabled = obs_property_enabled(p);
        QWidget *control = nullptr;

        switch (obs_property_get_type(p)) {
        case OBS_PROPERTY_BOOL: {
            auto *cb = new QCheckBox(desc, m_content);
            cb->setChecked(obs_data_get_bool(m_settings, nameUtf8.c_str()));
            connect(cb, &QCheckBox::toggled, this, [this, name](bool v) {
                obs_data_set_bool(m_settings, name.toUtf8().constData(), v);
                commit(name);
            });
            control = cb;
            break;
        }
        case OBS_PROPERTY_INT: {
            const int minV = obs_property_int_min(p);
            const int maxV = obs_property_int_max(p);
            const int step = std::max(1, obs_property_int_step(p));
            const QString suffix = QString::fromUtf8(obs_property_int_suffix(p));
            const int value = static_cast<int>(obs_data_get_int(m_settings, nameUtf8.c_str()));
            auto *box = new QWidget(m_content);
            auto *vl = new QVBoxLayout(box);
            vl->setContentsMargins(0, 0, 0, 0);
            vl->setSpacing(4);
            vl->addWidget(makeLabel(desc, box));
            if (obs_property_int_type(p) == OBS_NUMBER_SLIDER) {
                auto *row = new QHBoxLayout();
                auto *slider = new QSlider(Qt::Horizontal, box);
                slider->setRange(minV, maxV);
                slider->setSingleStep(step);
                slider->setValue(value);
                auto *val = new QLabel(QStringLiteral("%1%2").arg(value).arg(suffix), box);
                val->setMinimumWidth(64);
                val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                row->addWidget(slider, 1);
                row->addWidget(val);
                vl->addLayout(row);
                connect(slider, &QSlider::valueChanged, this, [this, name, val, suffix](int v) {
                    val->setText(QStringLiteral("%1%2").arg(v).arg(suffix));
                    obs_data_set_int(m_settings, name.toUtf8().constData(), v);
                    commit(name);
                });
            } else {
                auto *spin = new QSpinBox(box);
                spin->setRange(minV, maxV);
                spin->setSingleStep(step);
                spin->setSuffix(suffix);
                spin->setValue(value);
                vl->addWidget(spin);
                connect(spin, &QSpinBox::valueChanged, this, [this, name](int v) {
                    obs_data_set_int(m_settings, name.toUtf8().constData(), v);
                    commit(name);
                });
            }
            control = box;
            break;
        }
        case OBS_PROPERTY_FLOAT: {
            const double minV = obs_property_float_min(p);
            const double maxV = obs_property_float_max(p);
            const double step = obs_property_float_step(p) > 0.0 ? obs_property_float_step(p) : 0.1;
            const QString suffix = QString::fromUtf8(obs_property_float_suffix(p));
            const double value = obs_data_get_double(m_settings, nameUtf8.c_str());
            auto *box = new QWidget(m_content);
            auto *vl = new QVBoxLayout(box);
            vl->setContentsMargins(0, 0, 0, 0);
            vl->setSpacing(4);
            vl->addWidget(makeLabel(desc, box));
            const int decimals = step >= 1.0 ? 0 : static_cast<int>(std::ceil(-std::log10(step)));
            if (obs_property_float_type(p) == OBS_NUMBER_SLIDER) {
                const double scale = std::pow(10.0, decimals);
                auto *row = new QHBoxLayout();
                auto *slider = new QSlider(Qt::Horizontal, box);
                slider->setRange(static_cast<int>(std::round(minV * scale)), static_cast<int>(std::round(maxV * scale)));
                slider->setValue(static_cast<int>(std::round(value * scale)));
                auto *val = new QLabel(QStringLiteral("%1%2").arg(value, 0, 'f', decimals).arg(suffix), box);
                val->setMinimumWidth(72);
                val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                row->addWidget(slider, 1);
                row->addWidget(val);
                vl->addLayout(row);
                connect(slider, &QSlider::valueChanged, this, [this, name, val, suffix, scale, decimals](int v) {
                    const double d = v / scale;
                    val->setText(QStringLiteral("%1%2").arg(d, 0, 'f', decimals).arg(suffix));
                    obs_data_set_double(m_settings, name.toUtf8().constData(), d);
                    commit(name);
                });
            } else {
                auto *spin = new QDoubleSpinBox(box);
                spin->setRange(minV, maxV);
                spin->setSingleStep(step);
                spin->setDecimals(decimals);
                spin->setSuffix(suffix);
                spin->setValue(value);
                vl->addWidget(spin);
                connect(spin, &QDoubleSpinBox::valueChanged, this, [this, name](double v) {
                    obs_data_set_double(m_settings, name.toUtf8().constData(), v);
                    commit(name);
                });
            }
            control = box;
            break;
        }
        case OBS_PROPERTY_LIST: {
            const obs_combo_format format = obs_property_list_format(p);
            auto *box = new QWidget(m_content);
            auto *vl = new QVBoxLayout(box);
            vl->setContentsMargins(0, 0, 0, 0);
            vl->setSpacing(4);
            vl->addWidget(makeLabel(desc, box));
            auto *combo = new QComboBox(box);
            const size_t count = obs_property_list_item_count(p);
            int currentIndex = -1;
            for (size_t i = 0; i < count; ++i) {
                if (obs_property_list_item_disabled(p, i))
                    continue;
                const QString itemName = QString::fromUtf8(obs_property_list_item_name(p, i));
                QVariant value;
                bool isCurrent = false;
                if (format == OBS_COMBO_FORMAT_INT) {
                    value = static_cast<qlonglong>(obs_property_list_item_int(p, i));
                    isCurrent = obs_data_get_int(m_settings, nameUtf8.c_str()) == value.toLongLong();
                } else if (format == OBS_COMBO_FORMAT_FLOAT) {
                    value = obs_property_list_item_float(p, i);
                    isCurrent = std::abs(obs_data_get_double(m_settings, nameUtf8.c_str()) - value.toDouble()) < 1e-9;
                } else {
                    const char *s = obs_property_list_item_string(p, i);
                    value = QString::fromUtf8(s ? s : "");
                    const char *cur = obs_data_get_string(m_settings, nameUtf8.c_str());
                    isCurrent = value.toString() == QString::fromUtf8(cur ? cur : "");
                }
                combo->addItem(itemName, value);
                if (isCurrent)
                    currentIndex = combo->count() - 1;
            }
            if (currentIndex >= 0)
                combo->setCurrentIndex(currentIndex);
            else if (combo->count() > 0)
                combo->setCurrentIndex(-1);
            vl->addWidget(combo);
            connect(combo, &QComboBox::currentIndexChanged, this, [this, name, combo, format](int index) {
                if (index < 0)
                    return;
                const QVariant v = combo->itemData(index);
                const QByteArray key = name.toUtf8();
                if (format == OBS_COMBO_FORMAT_INT)
                    obs_data_set_int(m_settings, key.constData(), v.toLongLong());
                else if (format == OBS_COMBO_FORMAT_FLOAT)
                    obs_data_set_double(m_settings, key.constData(), v.toDouble());
                else
                    obs_data_set_string(m_settings, key.constData(), v.toString().toUtf8().constData());
                commit(name);
            });
            control = box;
            break;
        }
        case OBS_PROPERTY_TEXT: {
            auto *box = new QWidget(m_content);
            auto *vl = new QVBoxLayout(box);
            vl->setContentsMargins(0, 0, 0, 0);
            vl->setSpacing(4);
            vl->addWidget(makeLabel(desc, box));
            auto *edit = new QLineEdit(QString::fromUtf8(obs_data_get_string(m_settings, nameUtf8.c_str())), box);
            if (obs_property_text_type(p) == OBS_TEXT_PASSWORD)
                edit->setEchoMode(QLineEdit::Password);
            vl->addWidget(edit);
            connect(edit, &QLineEdit::editingFinished, this, [this, name, edit] {
                obs_data_set_string(m_settings, name.toUtf8().constData(), edit->text().toUtf8().constData());
                commit(name);
            });
            control = box;
            break;
        }
        case OBS_PROPERTY_PATH: {
            auto *box = new QWidget(m_content);
            auto *vl = new QVBoxLayout(box);
            vl->setContentsMargins(0, 0, 0, 0);
            vl->setSpacing(4);
            vl->addWidget(makeLabel(desc, box));
            auto *row = new QHBoxLayout();
            auto *edit = new QLineEdit(QString::fromUtf8(obs_data_get_string(m_settings, nameUtf8.c_str())), box);
            auto *browse = new QPushButton(QStringLiteral("…"), box);
            browse->setFixedWidth(40);
            row->addWidget(edit, 1);
            row->addWidget(browse);
            vl->addLayout(row);
            const QString filter = QString::fromUtf8(obs_property_path_filter(p));
            connect(browse, &QPushButton::clicked, this, [this, name, edit, filter] {
                const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Elegir archivo"), edit->text(), filter);
                if (file.isEmpty())
                    return;
                edit->setText(file);
                obs_data_set_string(m_settings, name.toUtf8().constData(), file.toUtf8().constData());
                commit(name);
            });
            connect(edit, &QLineEdit::editingFinished, this, [this, name, edit] {
                obs_data_set_string(m_settings, name.toUtf8().constData(), edit->text().toUtf8().constData());
                commit(name);
            });
            control = box;
            break;
        }
        case OBS_PROPERTY_BUTTON: {
            auto *button = new QPushButton(desc, m_content);
            connect(button, &QPushButton::clicked, this, [this, name] {
                if (!m_props || !m_source)
                    return;
                obs_property_t *prop = obs_properties_get(m_props, name.toUtf8().constData());
                if (prop && obs_property_button_clicked(prop, m_source))
                    QTimer::singleShot(0, this, [this] { rebuild(); });
                emit changed();
            });
            control = button;
            break;
        }
        default:
            Logger::info(QStringLiteral("Propiedad de efecto no compatible con el panel: %1").arg(name));
            break;
        }

        if (control) {
            control->setEnabled(enabled);
            m_layout->addWidget(control);
            ++shown;
        }
    }

    if (shown == 0)
        m_layout->addWidget(makeLabel(QStringLiteral("Este efecto no tiene parámetros que ajustar."), m_content));
    m_layout->addStretch(1);
}
