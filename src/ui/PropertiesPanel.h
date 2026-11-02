#pragma once

#include <QWidget>

struct obs_source;
struct obs_properties;
struct obs_data;

class QVBoxLayout;

// Genera automaticamente los controles de un efecto de audio (o de cualquier fuente de OBS) a
// partir de sus propiedades: interruptores, deslizadores, listas, textos y botones. Asi sirve
// tanto para los efectos de OBS (supresor de ruido, compresor...) como para plugins VST.
class PropertiesPanel : public QWidget {
    Q_OBJECT

public:
    explicit PropertiesPanel(QWidget *parent = nullptr);
    ~PropertiesPanel() override;

    // Pasar nullptr antes de eliminar el efecto que se estaba mostrando.
    void setSource(obs_source *source);

signals:
    void changed();

private:
    void rebuild();
    void clearUi();
    void commit(const QString &name);

    obs_source *m_source = nullptr;
    obs_properties *m_props = nullptr;
    obs_data *m_settings = nullptr;
    QVBoxLayout *m_layout = nullptr;
    QWidget *m_content = nullptr;
};
