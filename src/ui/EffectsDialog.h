#pragma once

#include <QDialog>

class PropertiesPanel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class RecordingController;

// Cadena de efectos de un dispositivo de audio: supresor de ruido, puerta, compresor, limitador,
// ecualizador y plugins VST 2.x externos.
class EffectsDialog : public QDialog {
    Q_OBJECT

public:
    // slot: 0 = audio del escritorio, 1 = microfono
    EffectsDialog(RecordingController *controller, int slot, QWidget *parent = nullptr);

signals:
    void changed(); // la cadena cambio: hay que guardarla

private:
    void refreshList(int selectRow);
    void addFilter(const QString &filterId);
    void addVoiceChain();

    RecordingController *m_controller;
    int m_slot;
    QListWidget *m_list = nullptr;
    PropertiesPanel *m_panel = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_removeButton = nullptr;
    QPushButton *m_upButton = nullptr;
    QPushButton *m_downButton = nullptr;
};
