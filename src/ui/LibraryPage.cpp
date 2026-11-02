#include "LibraryPage.h"

#include "../library/ShellMedia.h"
#include "../log/Logger.h"

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr int kIconW = 240;
constexpr int kIconH = 135;

QString key(const VideoEntry &e)
{
    return e.path + QLatin1Char('|') + QString::number(e.modified.toMSecsSinceEpoch());
}

QString formatDuration(qint64 ms)
{
    if (ms < 0)
        return QStringLiteral("--:--");
    const qint64 total = ms / 1000;
    const qint64 h = total / 3600;
    const qint64 m = (total / 60) % 60;
    const qint64 s = total % 60;
    return h > 0 ? QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'))
                 : QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

} // namespace

LibraryPage::LibraryPage(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(12);

    // ---------------- Cabecera: tipo, juego, acciones ----------------
    auto *header = new QHBoxLayout();
    header->setSpacing(10);

    m_kindGroup = new QButtonGroup(this);
    auto *seg = new QHBoxLayout();
    seg->setSpacing(0);
    const char *names[2] = {"Vídeos", "Clips"};
    const char *pos[2] = {"left", "right"};
    for (int i = 0; i < 2; ++i) {
        auto *b = new QPushButton(QString::fromUtf8(names[i]), this);
        b->setObjectName(QStringLiteral("seg"));
        b->setProperty("pos", QString::fromLatin1(pos[i]));
        b->setCheckable(true);
        b->setMinimumWidth(100);
        m_kindGroup->addButton(b, i);
        seg->addWidget(b);
    }
    m_kindGroup->button(0)->setChecked(true);
    connect(m_kindGroup, &QButtonGroup::idClicked, this, [this] { populateGames(); rebuildList(); });

    m_gameCombo = new QComboBox(this);
    m_gameCombo->setMinimumWidth(220);
    connect(m_gameCombo, &QComboBox::currentIndexChanged, this, [this] { rebuildList(); });

    m_count = new QLabel(this);
    m_count->setObjectName(QStringLiteral("muted"));

    auto *refreshButton = new QPushButton(QStringLiteral("↻  Actualizar"), this);
    connect(refreshButton, &QPushButton::clicked, this, [this] { refresh(); });
    auto *openButton = new QPushButton(QStringLiteral("Abrir carpeta"), this);
    connect(openButton, &QPushButton::clicked, this, [this] {
        QDir().mkpath(m_folder);
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_folder));
    });

    header->addLayout(seg);
    header->addWidget(m_gameCombo);
    header->addWidget(m_count, 1);
    header->addWidget(refreshButton);
    header->addWidget(openButton);
    root->addLayout(header);

    // ---------------- Cuerpo: lista + detalle ----------------
    auto *body = new QHBoxLayout();
    body->setSpacing(14);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("libList"));
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(kIconW, kIconH));
    m_list->setGridSize(QSize(kIconW + 28, kIconH + 76));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setWordWrap(true);
    m_list->setSpacing(4);
    m_list->setUniformItemSizes(true);
    m_list->setTextElideMode(Qt::ElideRight);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setFrameShape(QFrame::NoFrame);
    connect(m_list, &QListWidget::itemSelectionChanged, this, &LibraryPage::onSelectionChanged);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this] { playSelected(); });

    m_empty = new QLabel(QStringLiteral("Todavía no hay nada aquí."), this);
    m_empty->setObjectName(QStringLiteral("placeholder"));
    m_empty->setAlignment(Qt::AlignCenter);

    auto *listLayout = new QVBoxLayout();
    listLayout->addWidget(m_list, 1);
    listLayout->addWidget(m_empty, 1);
    body->addLayout(listLayout, 1);

    // Panel de detalle
    auto *panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("card"));
    panel->setFixedWidth(360);
    auto *pl = new QVBoxLayout(panel);
    pl->setContentsMargins(16, 16, 16, 16);
    pl->setSpacing(10);

    m_thumb = new QLabel(panel);
    m_thumb->setObjectName(QStringLiteral("thumb"));
    m_thumb->setFixedSize(328, 185);
    m_thumb->setAlignment(Qt::AlignCenter);
    pl->addWidget(m_thumb);

    m_title = new QLabel(panel);
    m_title->setWordWrap(true);
    m_title->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 700;"));
    pl->addWidget(m_title);

    m_details = new QLabel(panel);
    m_details->setObjectName(QStringLiteral("muted"));
    m_details->setWordWrap(true);
    m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pl->addWidget(m_details);
    pl->addStretch(1);

    m_playButton = new QPushButton(QStringLiteral("▶  Reproducir"), panel);
    m_playButton->setObjectName(QStringLiteral("record"));
    m_playButton->setMinimumHeight(44);
    m_folderButton = new QPushButton(QStringLiteral("Mostrar en la carpeta"), panel);
    m_micButton = new QPushButton(QStringLiteral("Abrir el audio del micrófono"), panel);
    m_camButton = new QPushButton(QStringLiteral("Reproducir la webcam"), panel);
    m_copyButton = new QPushButton(QStringLiteral("Copiar la ruta"), panel);
    m_deleteButton = new QPushButton(QStringLiteral("Eliminar (a la Papelera)"), panel);
    m_deleteButton->setObjectName(QStringLiteral("danger"));
    for (QPushButton *b : {m_playButton, m_folderButton, m_micButton, m_camButton, m_copyButton, m_deleteButton})
        pl->addWidget(b);

    connect(m_playButton, &QPushButton::clicked, this, &LibraryPage::playSelected);
    connect(m_folderButton, &QPushButton::clicked, this, &LibraryPage::showSelectedInFolder);
    connect(m_micButton, &QPushButton::clicked, this, &LibraryPage::playMic);
    connect(m_camButton, &QPushButton::clicked, this, &LibraryPage::playCam);
    connect(m_copyButton, &QPushButton::clicked, this, &LibraryPage::copyPath);
    connect(m_deleteButton, &QPushButton::clicked, this, &LibraryPage::deleteSelected);

    body->addWidget(panel);
    root->addLayout(body, 1);

    m_loader = new QTimer(this);
    m_loader->setInterval(12);
    connect(m_loader, &QTimer::timeout, this, &LibraryPage::loadNextThumbnail);

    showDetails(nullptr);
}

void LibraryPage::setFolder(const QString &folder)
{
    if (m_folder == folder)
        return;
    m_folder = folder;
    m_dirty = true;
    if (isVisible())
        refresh();
}

void LibraryPage::markDirty()
{
    m_dirty = true;
    if (isVisible())
        refresh();
}

void LibraryPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_dirty)
        refresh();
}

void LibraryPage::refresh()
{
    m_dirty = false;
    m_all = VideoLibrary::scan(m_folder);
    populateGames();
    rebuildList();
}

void LibraryPage::populateGames()
{
    const bool clips = m_kindGroup->checkedId() == 1;
    QStringList games;
    for (const VideoEntry &e : std::as_const(m_all))
        if (e.isClip == clips && !games.contains(e.game))
            games << e.game;
    games.sort(Qt::CaseInsensitive);

    const QString previous = m_gameCombo->currentData().toString();
    QSignalBlocker blocker(m_gameCombo);
    m_gameCombo->clear();
    m_gameCombo->addItem(QStringLiteral("Todos los juegos"), QString());
    for (const QString &g : std::as_const(games))
        m_gameCombo->addItem(g, g);
    const int index = m_gameCombo->findData(previous);
    m_gameCombo->setCurrentIndex(index >= 0 ? index : 0);
}

QPixmap LibraryPage::placeholder() const
{
    QPixmap pm(kIconW, kIconH);
    pm.fill(QColor(0x1C, 0x1C, 0x1F));
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 60));
    QPolygonF triangle;
    triangle << QPointF(kIconW / 2 - 12, kIconH / 2 - 18) << QPointF(kIconW / 2 - 12, kIconH / 2 + 18)
             << QPointF(kIconW / 2 + 20, kIconH / 2);
    p.drawPolygon(triangle);
    return pm;
}

QString LibraryPage::infoLine(const VideoEntry &e, qint64 durationMs) const
{
    return QStringLiteral("%1 · %2 · %3")
        .arg(formatDuration(durationMs), QLocale().formattedDataSize(e.size, 1),
             QLocale().toString(e.modified, QStringLiteral("dd/MM/yy HH:mm")));
}

void LibraryPage::rebuildList()
{
    const bool clips = m_kindGroup->checkedId() == 1;
    const QString game = m_gameCombo->currentData().toString();

    m_loader->stop();
    m_queue.clear();
    m_list->clear();

    qint64 totalSize = 0;
    int count = 0;
    const QPixmap holder = placeholder();
    for (int i = 0; i < m_all.size(); ++i) {
        const VideoEntry &e = m_all[i];
        if (e.isClip != clips || (!game.isEmpty() && e.game != game))
            continue;

        auto *item = new QListWidgetItem(m_list);
        item->setData(Qt::UserRole, i);
        item->setToolTip(QDir::toNativeSeparators(e.path));
        const qint64 duration = m_durations.value(key(e), -1);
        item->setText(e.name + QStringLiteral("\n") + infoLine(e, duration));
        item->setSizeHint(QSize(kIconW + 24, kIconH + 70));

        if (m_thumbs.contains(key(e))) {
            item->setIcon(QIcon(QPixmap::fromImage(m_thumbs.value(key(e)))));
        } else {
            item->setIcon(QIcon(holder));
            m_queue.enqueue(e.path);
        }
        totalSize += e.size;
        ++count;
    }

    m_count->setText(count == 0 ? QString()
                                : QStringLiteral("%1 %2 · %3").arg(count).arg(clips ? QStringLiteral("clips") : QStringLiteral("vídeos"),
                                                                           QLocale().formattedDataSize(totalSize, 1)));
    m_empty->setVisible(count == 0);
    m_list->setVisible(count > 0);
    m_empty->setText(clips ? QStringLiteral("Todavía no hay clips.\nGuarda uno con su atajo mientras juegas.")
                           : QStringLiteral("Todavía no hay grabaciones.\nAparecerán aquí automáticamente."));

    showDetails(nullptr);
    if (!m_queue.isEmpty())
        m_loader->start();
}

void LibraryPage::loadNextThumbnail()
{
    if (m_queue.isEmpty()) {
        m_loader->stop();
        return;
    }
    const QString path = m_queue.dequeue();

    // Localizar la entrada y su elemento en la lista
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem *item = m_list->item(row);
        const int idx = item->data(Qt::UserRole).toInt();
        if (idx < 0 || idx >= m_all.size() || m_all[idx].path != path)
            continue;
        const VideoEntry &e = m_all[idx];

        QImage thumb = ShellMedia::thumbnail(e.path, QSize(kIconW * 2, kIconH * 2));
        const qint64 duration = ShellMedia::durationMs(e.path);
        m_durations.insert(key(e), duration);
        if (!thumb.isNull()) {
            thumb = thumb.scaled(kIconW, kIconH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            m_thumbs.insert(key(e), thumb);
            item->setIcon(QIcon(QPixmap::fromImage(thumb)));
        }
        item->setText(e.name + QStringLiteral("\n") + infoLine(e, duration));
        break;
    }
}

const VideoEntry *LibraryPage::selectedEntry() const
{
    const QList<QListWidgetItem *> sel = m_list->selectedItems();
    if (sel.isEmpty())
        return nullptr;
    const int idx = sel.first()->data(Qt::UserRole).toInt();
    return (idx >= 0 && idx < m_all.size()) ? &m_all[idx] : nullptr;
}

void LibraryPage::onSelectionChanged()
{
    showDetails(selectedEntry());
}

void LibraryPage::showDetails(const VideoEntry *e)
{
    const bool has = e != nullptr;
    for (QPushButton *b : {m_playButton, m_folderButton, m_copyButton, m_deleteButton})
        b->setEnabled(has);
    m_micButton->setEnabled(has && !e->micPath.isEmpty());
    m_micButton->setVisible(!has || !e->isClip);
    m_camButton->setEnabled(has && !e->camPath.isEmpty());
    m_camButton->setVisible(!has || !e->isClip);

    if (!has) {
        m_thumb->setPixmap(QPixmap());
        m_thumb->setText(QStringLiteral("Selecciona un vídeo\npara ver su vista previa"));
        m_title->setText(QString());
        m_details->setText(QString());
        return;
    }

    // Vista previa grande (el Explorador la genera con los codecs de Windows)
    QImage big = ShellMedia::thumbnail(e->path, QSize(656, 370));
    if (big.isNull() && m_thumbs.contains(key(*e)))
        big = m_thumbs.value(key(*e));
    if (big.isNull()) {
        m_thumb->setPixmap(QPixmap());
        m_thumb->setText(QStringLiteral("Sin vista previa"));
    } else {
        m_thumb->setPixmap(QPixmap::fromImage(big.scaled(m_thumb->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    }

    const qint64 duration = m_durations.contains(key(*e)) ? m_durations.value(key(*e)) : ShellMedia::durationMs(e->path);
    m_durations.insert(key(*e), duration);

    m_title->setText(e->name);
    QString text = QStringLiteral("Juego: %1\nFecha: %2\nDuración: %3\nTamaño: %4")
                       .arg(e->game, QLocale().toString(e->modified, QLocale::ShortFormat), formatDuration(duration),
                            QLocale().formattedDataSize(e->size, 1));
    if (!e->isClip)
        text += QStringLiteral("\nMicrófono aparte: %1\nWebcam aparte: %2")
                    .arg(e->micPath.isEmpty() ? QStringLiteral("no") : QFileInfo(e->micPath).suffix().toUpper(),
                         e->camPath.isEmpty() ? QStringLiteral("no") : QStringLiteral("sí (_CAM)"));
    text += QStringLiteral("\n\n") + QDir::toNativeSeparators(e->path);
    m_details->setText(text);
}

void LibraryPage::playSelected()
{
    if (const VideoEntry *e = selectedEntry())
        QDesktopServices::openUrl(QUrl::fromLocalFile(e->path));
}

void LibraryPage::showSelectedInFolder()
{
    if (const VideoEntry *e = selectedEntry())
        ShellMedia::showInFolder(e->path);
}

void LibraryPage::playMic()
{
    if (const VideoEntry *e = selectedEntry())
        if (!e->micPath.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(e->micPath));
}

void LibraryPage::playCam()
{
    if (const VideoEntry *e = selectedEntry())
        if (!e->camPath.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(e->camPath));
}

void LibraryPage::copyPath()
{
    if (const VideoEntry *e = selectedEntry())
        QApplication::clipboard()->setText(QDir::toNativeSeparators(e->path));
}

void LibraryPage::deleteSelected()
{
    const VideoEntry *e = selectedEntry();
    if (!e)
        return;

    QStringList files = {e->path};
    QString text = QStringLiteral("Se moverá a la Papelera:\n%1").arg(QDir::toNativeSeparators(e->path));
    if (!e->micPath.isEmpty()) {
        files << e->micPath;
        text += QStringLiteral("\n\ny también el archivo del micrófono:\n%1").arg(QDir::toNativeSeparators(e->micPath));
    }
    if (!e->camPath.isEmpty()) {
        files << e->camPath;
        text += QStringLiteral("\n\ny también el archivo de la webcam:\n%1").arg(QDir::toNativeSeparators(e->camPath));
    }
    const auto answer = QMessageBox::question(this, QStringLiteral("Eliminar"), text, QMessageBox::Yes | QMessageBox::No,
                                              QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    QString error;
    if (!ShellMedia::moveToRecycleBin(files, &error)) {
        Logger::warn(error);
        QMessageBox::warning(this, QStringLiteral("Eliminar"), error);
        return;
    }
    Logger::info(QStringLiteral("Enviado a la Papelera: %1").arg(files.join(QStringLiteral(", "))));
    refresh();
}
