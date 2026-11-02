#include "GameNaming.h"

#include <QFileInfo>
#include <QStringList>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {

bool isShellProcess(const QString &exe)
{
    static const QStringList shell = {
        QStringLiteral("explorer.exe"), QStringLiteral("searchhost.exe"), QStringLiteral("shellexperiencehost.exe"),
        QStringLiteral("startmenuexperiencehost.exe"), QStringLiteral("applicationframehost.exe"),
        QStringLiteral("textinputhost.exe"), QStringLiteral("lockapp.exe"), QStringLiteral("taskmgr.exe")};
    return shell.contains(exe.toLower());
}

} // namespace

namespace GameNaming {

WindowInfo foreground()
{
    WindowInfo info;
    HWND hwnd = GetForegroundWindow();
    if (!hwnd)
        return info;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == GetCurrentProcessId())
        return info;

    wchar_t title[256] = {};
    GetWindowTextW(hwnd, title, 255);

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process) {
        wchar_t path[MAX_PATH] = {};
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(process, 0, path, &size))
            info.exeName = QFileInfo(QString::fromWCharArray(path)).fileName();
        CloseHandle(process);
    }
    info.title = QString::fromWCharArray(title).trimmed();

    if (isShellProcess(info.exeName))
        return WindowInfo();
    return info;
}

WindowInfo fromObsWindowId(const QString &id)
{
    // Formato de OBS: "titulo:clase:ejecutable" (los ':' del titulo se escapan como "#3A")
    WindowInfo info;
    const QStringList parts = id.split(QLatin1Char(':'));
    if (parts.size() >= 3) {
        info.title = parts.first();
        info.title.replace(QStringLiteral("#3A"), QStringLiteral(":"));
        info.exeName = parts.last();
    } else if (!parts.isEmpty()) {
        info.title = parts.first();
    }
    return info;
}

QString sanitize(const QString &name)
{
    QString s = name;
    static const QString invalid = QStringLiteral("\\/:*?\"<>|%");
    for (QChar &c : s)
        if (invalid.contains(c) || c.unicode() < 32)
            c = QLatin1Char(' ');
    s = s.simplified();
    while (s.endsWith(QLatin1Char('.')) || s.endsWith(QLatin1Char(' ')))
        s.chop(1);
    if (s.size() > 60)
        s = s.left(60).trimmed();
    return s;
}

QString displayName(const WindowInfo &info)
{
    QString exeBase = info.exeName;
    if (exeBase.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
        exeBase.chop(4);

    // Titulo si es razonable; si no, el nombre del ejecutable
    QString name = sanitize(info.title);
    if (name.isEmpty() || name.size() < 2)
        name = sanitize(exeBase);

    // Nombres reservados de Windows
    static const QStringList reserved = {QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"), QStringLiteral("NUL")};
    if (reserved.contains(name.toUpper()))
        name += QStringLiteral("_");

    return name.isEmpty() ? QStringLiteral("Escritorio") : name;
}

} // namespace GameNaming
