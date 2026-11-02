#include "LogPage.h"

#include "../log/Logger.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr int kMaxEntries = 6000;
}

LogPage::LogPage(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(12);

    auto *header = new QHBoxLayout();
    header->setSpacing(10);

    m_filter = new QComboBox(this);
    m_filter->addItem(QStringLiteral("Todo"), 0);
    m_filter->addItem(QStringLiteral("Avisos y errores"), 1);
    m_filter->addItem(QStringLiteral("Solo errores"), 2);
    m_filter->setToolTip(QStringLiteral("Qué nivel de mensajes se muestra"));

    m_hideObs = new QCheckBox(QStringLiteral("Ocultar mensajes informativos del motor"), this);
    m_hideObs->setChecked(true);
    m_hideObs->setToolTip(QStringLiteral("Los avisos y errores del motor siempre se muestran; solo se ocultan sus mensajes informativos."));

    m_count = new QLabel(this);
    m_count->setObjectName(QStringLiteral("muted"));

    auto *openButton = new QPushButton(QStringLiteral("Abrir carpeta"), this);
    auto *copyButton = new QPushButton(QStringLiteral("Copiar"), this);
    auto *clearButton = new QPushButton(QStringLiteral("Limpiar vista"), this);
    clearButton->setToolTip(QStringLiteral("Vacía solo esta lista; el archivo del registro no se borra."));

    header->addWidget(m_filter);
    header->addWidget(m_hideObs);
    header->addStretch(1);
    header->addWidget(m_count);
    header->addWidget(openButton);
    header->addWidget(copyButton);
    header->addWidget(clearButton);
    root->addLayout(header);

    m_view = new QTextEdit(this);
    m_view->setObjectName(QStringLiteral("logView"));
    m_view->setReadOnly(true);
    m_view->setLineWrapMode(QTextEdit::WidgetWidth);
    m_view->document()->setMaximumBlockCount(0);
    QFont mono(QStringLiteral("Consolas"), 10);
    mono.setStyleHint(QFont::Monospace);
    m_view->setFont(mono);
    root->addWidget(m_view, 1);

    connect(m_filter, &QComboBox::currentIndexChanged, this, [this] { render(); });
    connect(m_hideObs, &QCheckBox::toggled, this, [this] { render(); });
    connect(openButton, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(Logger::logDirPath()));
    });
    connect(copyButton, &QPushButton::clicked, this, [this] {
        QStringList visible;
        for (const QString &e : std::as_const(m_entries))
            if (isVisibleEntry(e))
                visible << e;
        QApplication::clipboard()->setText(visible.join(QStringLiteral("\n")));
    });
    connect(clearButton, &QPushButton::clicked, this, [this] {
        m_entries.clear();
        render();
    });

    m_entries = Logger::recentEntries(2500);
    render();
    Logger::setSink(this); // a partir de ahora, cada linea nueva llega en directo
}

LogPage::~LogPage()
{
    Logger::setSink(nullptr);
}

int LogPage::levelOf(const QString &entry) const
{
    const int a = entry.indexOf(QLatin1Char('['));
    const int b = a >= 0 ? entry.indexOf(QLatin1Char(']'), a) : -1;
    if (a < 0 || b < 0)
        return 0;
    const QString tag = entry.mid(a + 1, b - a - 1).toUpper();
    if (tag.contains(QStringLiteral("ERROR")) || tag.contains(QStringLiteral("FATAL")))
        return 2;
    if (tag.contains(QStringLiteral("WARN")))
        return 1;
    return 0;
}

bool LogPage::isVisibleEntry(const QString &entry) const
{
    const int level = levelOf(entry);
    if (level < m_filter->currentData().toInt())
        return false;
    if (m_hideObs->isChecked() && level == 0 && entry.contains(QStringLiteral("[OBS-INFO]")))
        return false;
    return true;
}

QString LogPage::toHtml(const QString &entry) const
{
    QString body = entry.toHtmlEscaped();
    body.replace(QStringLiteral("\n"), QStringLiteral("<br>"));
    body.replace(QStringLiteral("\t"), QStringLiteral("&nbsp;&nbsp;&nbsp;&nbsp;"));
    const int level = levelOf(entry);
    const char *color = level == 2 ? "#FF6B76" : level == 1 ? "#F5C542" : nullptr;
    if (!color)
        return body;
    return QStringLiteral("<span style=\"color:%1\">%2</span>").arg(QString::fromLatin1(color), body);
}

void LogPage::render()
{
    QString html;
    int shown = 0;
    for (const QString &e : std::as_const(m_entries)) {
        if (!isVisibleEntry(e))
            continue;
        html += toHtml(e) + QStringLiteral("<br>");
        ++shown;
    }
    m_view->setHtml(html);
    m_view->verticalScrollBar()->setValue(m_view->verticalScrollBar()->maximum());
    m_count->setText(QStringLiteral("%1 mensajes").arg(shown));
}

void LogPage::updateCount()
{
    int shown = 0;
    for (const QString &e : std::as_const(m_entries))
        if (isVisibleEntry(e))
            ++shown;
    m_count->setText(QStringLiteral("%1 mensajes").arg(shown));
}

void LogPage::appendLogLine(const QString &entry)
{
    if (entry.isEmpty())
        return;
    m_entries.append(entry);
    while (m_entries.size() > kMaxEntries)
        m_entries.removeFirst();

    if (!isVisibleEntry(entry))
        return;

    QScrollBar *bar = m_view->verticalScrollBar();
    const bool atBottom = bar->value() >= bar->maximum() - 4;
    m_view->append(toHtml(entry));
    if (atBottom)
        bar->setValue(bar->maximum());
    // Al pasar de cierto tamano se recorta la parte antigua de la vista
    if (m_view->document()->blockCount() > kMaxEntries * 3)
        render();
    else
        updateCount();
}
