#include "HotkeyManager.h"

#include <QCoreApplication>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

// Convierte una tecla de Qt en un codigo de tecla virtual de Windows (0 = no admitida).
UINT virtualKeyFor(int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return static_cast<UINT>('A' + (key - Qt::Key_A));
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return static_cast<UINT>('0' + (key - Qt::Key_0));
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        return static_cast<UINT>(VK_F1 + (key - Qt::Key_F1));
    switch (key) {
    case Qt::Key_Space:      return VK_SPACE;
    case Qt::Key_Home:       return VK_HOME;
    case Qt::Key_End:        return VK_END;
    case Qt::Key_Insert:     return VK_INSERT;
    case Qt::Key_Delete:     return VK_DELETE;
    case Qt::Key_PageUp:     return VK_PRIOR;
    case Qt::Key_PageDown:   return VK_NEXT;
    case Qt::Key_Pause:      return VK_PAUSE;
    case Qt::Key_Print:      return VK_SNAPSHOT;
    case Qt::Key_ScrollLock: return VK_SCROLL;
    default:                 return 0;
    }
}

} // namespace

HotkeyManager::HotkeyManager(QObject *parent)
    : QObject(parent)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

HotkeyManager::~HotkeyManager()
{
    unregisterAll();
    if (QCoreApplication::instance())
        QCoreApplication::instance()->removeNativeEventFilter(this);
}

bool HotkeyManager::registerHotkey(int id, const QKeySequence &sequence, QString *errorOut)
{
    auto fail = [&](const QString &msg) {
        if (errorOut)
            *errorOut = msg;
        return false;
    };

    // Si el id ya estaba registrado, se libera primero (cambio de combinacion).
    if (m_registered.contains(id)) {
        UnregisterHotKey(nullptr, id);
        m_registered.removeAll(id);
    }

    if (sequence.isEmpty())
        return fail(QStringLiteral("No hay ninguna combinación configurada."));

    const QKeyCombination combo = sequence[0];
    const int key = static_cast<int>(combo.key());
    const Qt::KeyboardModifiers mods = combo.keyboardModifiers();

    const UINT vk = virtualKeyFor(key);
    if (vk == 0)
        return fail(QStringLiteral("La tecla de la combinación %1 no es compatible con atajos globales.")
                        .arg(sequence.toString(QKeySequence::NativeText)));

    UINT winMods = MOD_NOREPEAT;
    if (mods & Qt::ControlModifier) winMods |= MOD_CONTROL;
    if (mods & Qt::ShiftModifier)   winMods |= MOD_SHIFT;
    if (mods & Qt::AltModifier)     winMods |= MOD_ALT;

    const bool isFunctionKey = key >= Qt::Key_F1 && key <= Qt::Key_F24;
    if ((winMods & ~static_cast<UINT>(MOD_NOREPEAT)) == 0 && !isFunctionKey)
        return fail(QStringLiteral("Usa al menos Ctrl, Alt o Mayús junto con la tecla (o una tecla F)."));

    if (!RegisterHotKey(nullptr, id, winMods, vk)) {
        const DWORD error = GetLastError();
        if (error == ERROR_HOTKEY_ALREADY_REGISTERED)
            return fail(QStringLiteral("La combinación %1 ya está en uso por otra aplicación.")
                            .arg(sequence.toString(QKeySequence::NativeText)));
        return fail(QStringLiteral("Windows no permitió registrar %1 (error %2).")
                        .arg(sequence.toString(QKeySequence::NativeText))
                        .arg(error));
    }

    m_registered.append(id);
    return true;
}

void HotkeyManager::unregisterAll()
{
    for (int id : std::as_const(m_registered))
        UnregisterHotKey(nullptr, id);
    m_registered.clear();
}

bool HotkeyManager::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *)
{
    if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
        const MSG *msg = static_cast<const MSG *>(message);
        if (msg->message == WM_HOTKEY) {
            const int id = static_cast<int>(msg->wParam);
            if (m_registered.contains(id)) {
                emit triggered(id);
                return true;
            }
        }
    }
    return false;
}
