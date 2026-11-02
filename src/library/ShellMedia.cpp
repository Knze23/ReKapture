#include "ShellMedia.h"

#include <QDir>

#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace {

// System.Media.Duration (unidades de 100 ns)
const PROPERTYKEY kMediaDuration = {{0x64440490, 0x4C8B, 0x11D1, {0x8B, 0x70, 0x08, 0x00, 0x36, 0xB1, 0x1A, 0x03}}, 3};

std::wstring nativePath(const QString &path)
{
    return QDir::toNativeSeparators(path).toStdWString();
}

QImage imageFromHBitmap(HBITMAP hbmp)
{
    BITMAP bm = {};
    if (!GetObjectW(hbmp, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0)
        return QImage();

    QImage img(bm.bmWidth, bm.bmHeight, QImage::Format_ARGB32_Premultiplied);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = bm.bmWidth;
    bi.bmiHeader.biHeight = -bm.bmHeight; // de arriba hacia abajo
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC dc = GetDC(nullptr);
    const int lines = GetDIBits(dc, hbmp, 0, static_cast<UINT>(bm.bmHeight), img.bits(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (lines == 0)
        return QImage();

    // Algunas miniaturas llegan sin canal alfa: se marcan como opacas.
    bool hasAlpha = false;
    const uchar *p = img.constBits();
    const qsizetype pixels = static_cast<qsizetype>(img.width()) * img.height();
    for (qsizetype i = 0; i < pixels; ++i)
        if (p[i * 4 + 3] != 0) {
            hasAlpha = true;
            break;
        }
    if (!hasAlpha) {
        uchar *w = img.bits();
        for (qsizetype i = 0; i < pixels; ++i)
            w[i * 4 + 3] = 255;
    }
    return img;
}

} // namespace

namespace ShellMedia {

QImage thumbnail(const QString &path, const QSize &size)
{
    const std::wstring wpath = nativePath(path);
    IShellItem *item = nullptr;
    if (FAILED(SHCreateItemFromParsingName(wpath.c_str(), nullptr, IID_PPV_ARGS(&item))) || !item)
        return QImage();

    QImage result;
    IShellItemImageFactory *factory = nullptr;
    if (SUCCEEDED(item->QueryInterface(IID_PPV_ARGS(&factory))) && factory) {
        SIZE sz = {size.width(), size.height()};
        HBITMAP hbmp = nullptr;
        // THUMBNAILONLY: si no hay miniatura real no devuelve un icono generico
        if (SUCCEEDED(factory->GetImage(sz, SIIGBF_BIGGERSIZEOK | SIIGBF_THUMBNAILONLY, &hbmp)) && hbmp) {
            result = imageFromHBitmap(hbmp);
            DeleteObject(hbmp);
        }
        factory->Release();
    }
    item->Release();
    return result;
}

qint64 durationMs(const QString &path)
{
    const std::wstring wpath = nativePath(path);
    IShellItem2 *item = nullptr;
    if (FAILED(SHCreateItemFromParsingName(wpath.c_str(), nullptr, IID_PPV_ARGS(&item))) || !item)
        return -1;

    qint64 ms = -1;
    ULONGLONG value = 0;
    if (SUCCEEDED(item->GetUInt64(kMediaDuration, &value)))
        ms = static_cast<qint64>(value / 10000ULL); // 100 ns -> ms
    item->Release();
    return ms;
}

bool moveToRecycleBin(const QStringList &paths, QString *errorOut)
{
    std::wstring list;
    for (const QString &p : paths) {
        list += nativePath(p);
        list.push_back(L'\0');
    }
    list.push_back(L'\0'); // doble terminador nulo

    SHFILEOPSTRUCTW op = {};
    op.wFunc = FO_DELETE;
    op.pFrom = list.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    const int result = SHFileOperationW(&op);
    if (result != 0 || op.fAnyOperationsAborted) {
        if (errorOut)
            *errorOut = QStringLiteral("Windows no pudo mover los archivos a la Papelera (código %1).").arg(result);
        return false;
    }
    return true;
}

void showInFolder(const QString &path)
{
    const std::wstring wpath = nativePath(path);
    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(wpath.c_str());
    if (pidl) {
        SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
        ILFree(pidl);
    }
}

} // namespace ShellMedia
