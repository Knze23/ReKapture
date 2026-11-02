#pragma once

#include "../library/VideoLibrary.h"

#include <QHash>
#include <QImage>
#include <QQueue>
#include <QWidget>

class QButtonGroup;
class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPixmap;
class QPushButton;
class QTimer;

// Biblioteca: grabaciones y clips separados, con miniaturas, filtro por juego y acciones.
class LibraryPage : public QWidget {
    Q_OBJECT

public:
    explicit LibraryPage(QWidget *parent = nullptr);

    void setFolder(const QString &folder);
    void refresh();
    void markDirty(); // refresca ahora si esta visible, o al mostrarse

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void rebuildList();
    void onSelectionChanged();
    void loadNextThumbnail();
    void playSelected();
    void showSelectedInFolder();
    void playMic();
    void playCam();
    void copyPath();
    void deleteSelected();

private:
    const VideoEntry *selectedEntry() const;
    void showDetails(const VideoEntry *entry);
    void populateGames();
    QString infoLine(const VideoEntry &e, qint64 durationMs) const;
    QPixmap placeholder() const;

    QString m_folder;
    bool m_dirty = true;
    QVector<VideoEntry> m_all;

    QButtonGroup *m_kindGroup = nullptr;
    QComboBox *m_gameCombo = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_count = nullptr;
    QLabel *m_empty = nullptr;

    // Detalle
    QLabel *m_thumb = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_details = nullptr;
    QPushButton *m_playButton = nullptr;
    QPushButton *m_folderButton = nullptr;
    QPushButton *m_micButton = nullptr;
    QPushButton *m_camButton = nullptr;
    QPushButton *m_copyButton = nullptr;
    QPushButton *m_deleteButton = nullptr;

    // Carga perezosa de miniaturas y duraciones
    QTimer *m_loader = nullptr;
    QQueue<QString> m_queue;
    QHash<QString, QImage> m_thumbs;   // clave: ruta|fecha
    QHash<QString, qint64> m_durations;
};
