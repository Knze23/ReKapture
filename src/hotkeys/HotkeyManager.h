#pragma once

#include <QAbstractNativeEventFilter>
#include <QKeySequence>
#include <QObject>
#include <QVector>

// Atajos globales de Windows (RegisterHotKey). Funcionan aunque la ventana no tenga el foco.
// Solo se registran las combinaciones configuradas y se liberan al cerrar.
class HotkeyManager : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    enum Id { Start = 1, Stop = 2, SaveClip = 3, ToggleReplay = 4 };

    explicit HotkeyManager(QObject *parent = nullptr);
    ~HotkeyManager() override;

    // Devuelve false y rellena errorOut si no se pudo registrar.
    bool registerHotkey(int id, const QKeySequence &sequence, QString *errorOut);
    void unregisterAll();

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    void triggered(int id);

private:
    QVector<int> m_registered;
};
