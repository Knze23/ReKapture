#pragma once

#include <QStringList>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QTextEdit;

// Pestaña "Registro": muestra en vivo lo que ReKapture y el motor de captura van registrando,
// con filtros, para consultarlo cuando haga falta sin que ocupe sitio en la ventana principal.
class LogPage : public QWidget {
    Q_OBJECT

public:
    explicit LogPage(QWidget *parent = nullptr);
    ~LogPage() override;

public slots:
    void appendLogLine(const QString &entry); // lo invoca el registro (en cola)

private:
    int levelOf(const QString &entry) const; // 0 info, 1 aviso, 2 error
    bool isVisibleEntry(const QString &entry) const;
    QString toHtml(const QString &entry) const;
    void render();
    void updateCount();

    QStringList m_entries;
    QTextEdit *m_view = nullptr;
    QComboBox *m_filter = nullptr;
    QCheckBox *m_hideObs = nullptr;
    QLabel *m_count = nullptr;
};
