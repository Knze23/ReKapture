#include "EffectsDialog.h"

#include "../log/Logger.h"
#include "../recording/RecordingController.h"
#include "PropertiesPanel.h"

#include <obs.h>

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace {

struct FilterName {
    const char *id;
    const char *name;
    const char *hint;
};

// Efectos de audio de OBS con nombre en espanol (en el orden en que se ofrecen)
const FilterName kFilters[] = {
    {"noise_suppress_filter_v2", "Supresión de ruido", "Reduce el ruido de fondo constante de forma automática."},
    {"noise_gate_filter", "Puerta de ruido (gate)", "Silencia el micrófono cuando no hablas."},
    {"compressor_filter", "Compresor", "Iguala el volumen: sube lo bajo y contiene lo alto."},
    {"limiter_filter", "Limitador", "Evita que la voz sature si gritas."},
    {"expander_filter", "Expansor", "Reduce el ruido bajo sin cortarlo del todo."},
    {"basic_eq_filter", "Ecualizador (3 bandas)", "Graves, medios y agudos."},
    {"gain_filter", "Ganancia", "Sube o baja el volumen."},
    {"upward_compressor_filter", "Compresor ascendente", "Sube los pasajes más bajos."},
    {"invert_polarity_filter", "Invertir polaridad", "Corrige micrófonos en fase invertida."},
    {"vst_filter", "Plugin VST 2.x externo", "Carga un plugin VST 2.x de 64 bits instalado en tu equipo."},
};

QString friendlyName(const QString &id)
{
    for (const FilterName &f : kFilters)
        if (id == QLatin1String(f.id))
            return QString::fromUtf8(f.name);
    return id;
}

} // namespace

EffectsDialog::EffectsDialog(RecordingController *controller, int slot, QWidget *parent)
    : QDialog(parent)
    , m_controller(controller)
    , m_slot(slot)
{
    setWindowTitle(slot == 1 ? QStringLiteral("Efectos del micrófono") : QStringLiteral("Efectos del audio del escritorio"));
    resize(820, 520);

    auto *root = new QVBoxLayout(this);
    auto *hint = new QLabel(QStringLiteral("Los efectos se aplican de arriba abajo, en directo, y también a lo que se graba. "
                                           "El medidor de la ventana principal muestra el resultado."), this);
    hint->setObjectName(QStringLiteral("muted"));
    hint->setWordWrap(true);
    root->addWidget(hint);

    auto *body = new QHBoxLayout();
    body->setSpacing(14);

    // ---- Lista de efectos ----
    auto *left = new QVBoxLayout();
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("fxList"));
    m_list->setFixedWidth(280);
    left->addWidget(m_list, 1);

    auto *row1 = new QHBoxLayout();
    m_addButton = new QPushButton(QStringLiteral("＋ Añadir efecto"), this);
    m_removeButton = new QPushButton(QStringLiteral("Quitar"), this);
    row1->addWidget(m_addButton, 1);
    row1->addWidget(m_removeButton);
    left->addLayout(row1);
    auto *row2 = new QHBoxLayout();
    m_upButton = new QPushButton(QStringLiteral("▲ Subir"), this);
    m_downButton = new QPushButton(QStringLiteral("▼ Bajar"), this);
    row2->addWidget(m_upButton);
    row2->addWidget(m_downButton);
    left->addLayout(row2);
    body->addLayout(left);

    // ---- Parametros del efecto elegido ----
    m_panel = new PropertiesPanel(this);
    body->addWidget(m_panel, 1);
    root->addLayout(body, 1);

    auto *close = new QPushButton(QStringLiteral("Cerrar"), this);
    close->setDefault(true);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    auto *footer = new QHBoxLayout();
    footer->addStretch(1);
    footer->addWidget(close);
    root->addLayout(footer);

    // ---- Menu "Añadir efecto" ----
    auto *menu = new QMenu(this);
    const QStringList available = RecordingController::availableFilters();
    for (const FilterName &f : kFilters) {
        const QString id = QString::fromLatin1(f.id);
        const bool isVst = id == QLatin1String("vst_filter");
        if (!available.contains(id) && !isVst)
            continue;
        QAction *a = menu->addAction(QString::fromUtf8(f.name));
        a->setToolTip(QString::fromUtf8(f.hint));
        if (isVst && !available.contains(id)) {
            a->setEnabled(false);
            a->setText(a->text() + QStringLiteral(" (actívalo en Ajustes > Audio y webcam)"));
        } else {
            connect(a, &QAction::triggered, this, [this, id] { addFilter(id); });
        }
    }
    menu->addSeparator();
    QAction *chain = menu->addAction(QStringLiteral("Cadena recomendada para voz"));
    chain->setToolTip(QStringLiteral("Supresión de ruido + puerta + compresor + limitador"));
    connect(chain, &QAction::triggered, this, [this] { addVoiceChain(); });
    m_addButton->setMenu(menu);

    connect(m_removeButton, &QPushButton::clicked, this, [this] {
        const int row = m_list->currentRow();
        if (row < 0)
            return;
        m_panel->setSource(nullptr); // el efecto va a desaparecer
        m_controller->removeEffect(m_slot, row);
        refreshList(qMin(row, m_list->count() - 2));
        emit changed();
    });
    connect(m_upButton, &QPushButton::clicked, this, [this] {
        const int row = m_list->currentRow();
        if (row <= 0)
            return;
        m_controller->moveEffect(m_slot, row, -1);
        refreshList(row - 1);
        emit changed();
    });
    connect(m_downButton, &QPushButton::clicked, this, [this] {
        const int row = m_list->currentRow();
        if (row < 0 || row >= m_list->count() - 1)
            return;
        m_controller->moveEffect(m_slot, row, +1);
        refreshList(row + 1);
        emit changed();
    });
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        m_panel->setSource(row >= 0 ? m_controller->effectSource(m_slot, row) : nullptr);
        m_removeButton->setEnabled(row >= 0);
        m_upButton->setEnabled(row > 0);
        m_downButton->setEnabled(row >= 0 && row < m_list->count() - 1);
    });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        const int row = m_list->row(item);
        m_controller->setEffectEnabled(m_slot, row, item->checkState() == Qt::Checked);
        emit changed();
    });
    connect(m_panel, &PropertiesPanel::changed, this, &EffectsDialog::changed);

    refreshList(0);
}

void EffectsDialog::refreshList(int selectRow)
{
    m_panel->setSource(nullptr);
    {
        QSignalBlocker blocker(m_list);
        m_list->clear();
        const QVector<EffectInfo> effects = m_controller->effects(m_slot);
        for (const EffectInfo &e : effects) {
            auto *item = new QListWidgetItem(m_list);
            item->setText(friendlyName(e.filterId) == e.name ? e.name : QStringLiteral("%1 (%2)").arg(e.name, friendlyName(e.filterId)));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(e.enabled ? Qt::Checked : Qt::Unchecked);
        }
    }
    if (m_list->count() > 0)
        m_list->setCurrentRow(qBound(0, selectRow, m_list->count() - 1));
    else
        m_list->setCurrentRow(-1);

    // currentRowChanged no se emite si la fila no cambia: se actualiza el panel a mano
    const int row = m_list->currentRow();
    m_panel->setSource(row >= 0 ? m_controller->effectSource(m_slot, row) : nullptr);
    m_removeButton->setEnabled(row >= 0);
    m_upButton->setEnabled(row > 0);
    m_downButton->setEnabled(row >= 0 && row < m_list->count() - 1);
}

void EffectsDialog::addFilter(const QString &filterId)
{
    // Nombre unico dentro de la cadena
    const QString base = friendlyName(filterId);
    QString name = base;
    const QVector<EffectInfo> existing = m_controller->effects(m_slot);
    for (int n = 2;; ++n) {
        bool taken = false;
        for (const EffectInfo &e : existing)
            if (e.name == name)
                taken = true;
        if (!taken)
            break;
        name = QStringLiteral("%1 %2").arg(base).arg(n);
    }

    if (!m_controller->addEffect(m_slot, filterId, name)) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("No se pudo añadir el efecto «%1».").arg(base));
        return;
    }
    refreshList(m_controller->effects(m_slot).size() - 1);
    emit changed();
}

void EffectsDialog::addVoiceChain()
{
    // Valores pensados para voz hablada; todos se pueden ajustar despues
    struct Step { const char *id; const char *settings; };
    const Step steps[] = {
        {"noise_suppress_filter_v2", "{\"method\":\"rnnoise\"}"},
        {"noise_gate_filter", "{}"},
        {"compressor_filter", "{\"ratio\":4.0,\"threshold\":-18.0,\"attack_time\":6,\"release_time\":60}"},
        {"limiter_filter", "{\"threshold\":-3.0}"},
    };
    const QStringList available = RecordingController::availableFilters();
    int added = 0;
    for (const Step &s : steps) {
        const QString id = QString::fromLatin1(s.id);
        if (!available.contains(id))
            continue;
        if (m_controller->addEffect(m_slot, id, friendlyName(id) + QStringLiteral(" (voz)"), QString::fromLatin1(s.settings)))
            ++added;
    }
    if (added == 0)
        QMessageBox::warning(this, windowTitle(), QStringLiteral("No se pudo crear la cadena de efectos."));
    refreshList(m_controller->effects(m_slot).size() - 1);
    emit changed();
}
