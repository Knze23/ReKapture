#include "AppSettings.h"

#include "AppPaths.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>

namespace {

struct Resolution { int w; int h; };
const Resolution kResolutions[] = {{1280, 720}, {1920, 1080}, {2560, 1440}, {3840, 2160}};

} // namespace

namespace AppSettings {

QString filePath()
{
    return AppPaths::configDir() + QStringLiteral("/settings.ini");
}

AppSettingsData load()
{
    AppSettingsData d;
    d.folder = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + QStringLiteral("/ReKapture");

    QSettings s(filePath(), QSettings::IniFormat);
    d.folder = s.value(QStringLiteral("folder"), d.folder).toString();
    d.micExportMode = s.value(QStringLiteral("micExportMode"), d.micExportMode).toInt();
    d.micFileFormat = s.value(QStringLiteral("micFileFormat"), d.micFileFormat).toInt();
    d.webcamEnabled = s.value(QStringLiteral("webcamEnabled"), d.webcamEnabled).toBool();
    d.webcamDeviceId = s.value(QStringLiteral("webcamDeviceId"), d.webcamDeviceId).toString();
    d.webcamResolution = s.value(QStringLiteral("webcamResolution"), d.webcamResolution).toInt();
    d.webcamWithAudio = s.value(QStringLiteral("webcamWithAudio"), d.webcamWithAudio).toBool();
    d.hideAppFromCapture = s.value(QStringLiteral("hideAppFromCapture"), d.hideAppFromCapture).toBool();
    d.vstEnabled = s.value(QStringLiteral("vstEnabled"), d.vstEnabled).toBool();
    d.desktopEffects = s.value(QStringLiteral("desktopEffects"), d.desktopEffects).toString();
    d.micEffects = s.value(QStringLiteral("micEffects"), d.micEffects).toString();
    d.theme = s.value(QStringLiteral("theme"), d.theme).toString();
    d.organizeByGame = s.value(QStringLiteral("organizeByGame"), d.organizeByGame).toBool();
    d.overlayEnabled = s.value(QStringLiteral("overlayEnabled"), d.overlayEnabled).toBool();
    d.overlayCorner = s.value(QStringLiteral("overlayCorner"), d.overlayCorner).toInt();
    d.instantReplay = s.value(QStringLiteral("instantReplay"), d.instantReplay).toBool();
    d.clipsWhileRecording = s.value(QStringLiteral("clipsWhileRecording"), d.clipsWhileRecording).toBool();
    d.clipSeconds = s.value(QStringLiteral("clipSeconds"), d.clipSeconds).toInt();
    d.autoDisableReplay = s.value(QStringLiteral("autoDisableReplay"), d.autoDisableReplay).toBool();
    d.clipHotkey = s.value(QStringLiteral("clipHotkey"), d.clipHotkey).toString();
    d.replayHotkey = s.value(QStringLiteral("replayHotkey"), d.replayHotkey).toString();
    d.captureMode = s.value(QStringLiteral("captureMode"), d.captureMode).toInt();
    d.monitorId = s.value(QStringLiteral("monitorId"), d.monitorId).toString();
    d.windowId = s.value(QStringLiteral("windowId"), d.windowId).toString();
    d.desktopDeviceId = s.value(QStringLiteral("desktopDeviceId"), d.desktopDeviceId).toString();
    d.micDeviceId = s.value(QStringLiteral("micDeviceId"), d.micDeviceId).toString();
    d.encoderKey = s.value(QStringLiteral("encoderKey"), d.encoderKey).toString();
    d.quality = s.value(QStringLiteral("quality"), d.quality).toInt();
    d.bitrateCustom = s.value(QStringLiteral("bitrateCustom"), d.bitrateCustom).toBool();
    d.bitrateKbps = s.value(QStringLiteral("bitrateKbps"), d.bitrateKbps).toInt();
    d.audioBitrateKbps = s.value(QStringLiteral("audioBitrateKbps"), d.audioBitrateKbps).toInt();
    d.webcamPreview = s.value(QStringLiteral("webcamPreview"), d.webcamPreview).toBool();
    d.fps = s.value(QStringLiteral("fps"), d.fps).toInt();
    d.width = s.value(QStringLiteral("width"), d.width).toInt();
    d.height = s.value(QStringLiteral("height"), d.height).toInt();
    d.previewEnabled = s.value(QStringLiteral("previewEnabled"), d.previewEnabled).toBool();
    d.desktopVolume = s.value(QStringLiteral("desktopVolume"), d.desktopVolume).toInt();
    d.micVolume = s.value(QStringLiteral("micVolume"), d.micVolume).toInt();
    d.windowGeometry = s.value(QStringLiteral("windowGeometry"), d.windowGeometry).toString();
    d.soundCues = s.value(QStringLiteral("soundCues"), d.soundCues).toBool();
    d.startHotkey = s.value(QStringLiteral("startHotkey"), d.startHotkey).toString();
    d.stopHotkey = s.value(QStringLiteral("stopHotkey"), d.stopHotkey).toString();

    // Validacion: cualquier valor fuera de lo permitido vuelve al valor por defecto.
    if (d.folder.trimmed().isEmpty())
        d.folder = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + QStringLiteral("/ReKapture");
    d.desktopVolume = qBound(0, d.desktopVolume, 150);
    d.micVolume = qBound(0, d.micVolume, 150);
    d.clipSeconds = qBound(5, d.clipSeconds, 600);
    d.bitrateKbps = qBound(1000, d.bitrateKbps, 150000);
    if (d.audioBitrateKbps != 96 && d.audioBitrateKbps != 128 && d.audioBitrateKbps != 160 && d.audioBitrateKbps != 192
        && d.audioBitrateKbps != 256 && d.audioBitrateKbps != 320)
        d.audioBitrateKbps = 160;
    d.micExportMode = qBound(0, d.micExportMode, 2);
    d.micFileFormat = qBound(0, d.micFileFormat, 2);
    d.webcamResolution = qBound(0, d.webcamResolution, 1);
    if (d.overlayCorner < 0 || d.overlayCorner > 3)
        d.overlayCorner = 1;
    if (d.theme.isEmpty())
        d.theme = QStringLiteral("dark");
    if (d.captureMode < 0 || d.captureMode > 2)
        d.captureMode = 0;
    if (d.quality < 0 || d.quality > 3)
        d.quality = 2;
    if (d.fps != 30 && d.fps != 60)
        d.fps = 60;
    bool resOk = false;
    for (const Resolution &r : kResolutions)
        if (r.w == d.width && r.h == d.height)
            resOk = true;
    if (!resOk) {
        d.width = 1920;
        d.height = 1080;
    }
    const QStringList encoders = {QStringLiteral("auto"), QStringLiteral("nvenc"), QStringLiteral("amf"),
                                  QStringLiteral("qsv"), QStringLiteral("x264")};
    if (!encoders.contains(d.encoderKey))
        d.encoderKey = QStringLiteral("auto");
    return d;
}

void save(const AppSettingsData &d)
{
    QSettings s(filePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("folder"), d.folder);
    s.setValue(QStringLiteral("micExportMode"), d.micExportMode);
    s.setValue(QStringLiteral("micFileFormat"), d.micFileFormat);
    s.setValue(QStringLiteral("webcamEnabled"), d.webcamEnabled);
    s.setValue(QStringLiteral("webcamDeviceId"), d.webcamDeviceId);
    s.setValue(QStringLiteral("webcamResolution"), d.webcamResolution);
    s.setValue(QStringLiteral("webcamWithAudio"), d.webcamWithAudio);
    s.setValue(QStringLiteral("hideAppFromCapture"), d.hideAppFromCapture);
    s.setValue(QStringLiteral("vstEnabled"), d.vstEnabled);
    s.setValue(QStringLiteral("desktopEffects"), d.desktopEffects);
    s.setValue(QStringLiteral("micEffects"), d.micEffects);
    s.setValue(QStringLiteral("theme"), d.theme);
    s.setValue(QStringLiteral("organizeByGame"), d.organizeByGame);
    s.setValue(QStringLiteral("overlayEnabled"), d.overlayEnabled);
    s.setValue(QStringLiteral("overlayCorner"), d.overlayCorner);
    s.setValue(QStringLiteral("instantReplay"), d.instantReplay);
    s.setValue(QStringLiteral("clipsWhileRecording"), d.clipsWhileRecording);
    s.setValue(QStringLiteral("clipSeconds"), d.clipSeconds);
    s.setValue(QStringLiteral("autoDisableReplay"), d.autoDisableReplay);
    s.setValue(QStringLiteral("clipHotkey"), d.clipHotkey);
    s.setValue(QStringLiteral("replayHotkey"), d.replayHotkey);
    s.setValue(QStringLiteral("captureMode"), d.captureMode);
    s.setValue(QStringLiteral("monitorId"), d.monitorId);
    s.setValue(QStringLiteral("windowId"), d.windowId);
    s.setValue(QStringLiteral("desktopDeviceId"), d.desktopDeviceId);
    s.setValue(QStringLiteral("micDeviceId"), d.micDeviceId);
    s.setValue(QStringLiteral("encoderKey"), d.encoderKey);
    s.setValue(QStringLiteral("quality"), d.quality);
    s.setValue(QStringLiteral("bitrateCustom"), d.bitrateCustom);
    s.setValue(QStringLiteral("bitrateKbps"), d.bitrateKbps);
    s.setValue(QStringLiteral("audioBitrateKbps"), d.audioBitrateKbps);
    s.setValue(QStringLiteral("webcamPreview"), d.webcamPreview);
    s.setValue(QStringLiteral("fps"), d.fps);
    s.setValue(QStringLiteral("width"), d.width);
    s.setValue(QStringLiteral("height"), d.height);
    s.setValue(QStringLiteral("previewEnabled"), d.previewEnabled);
    s.setValue(QStringLiteral("desktopVolume"), d.desktopVolume);
    s.setValue(QStringLiteral("micVolume"), d.micVolume);
    s.setValue(QStringLiteral("windowGeometry"), d.windowGeometry);
    s.setValue(QStringLiteral("soundCues"), d.soundCues);
    s.setValue(QStringLiteral("startHotkey"), d.startHotkey);
    s.setValue(QStringLiteral("stopHotkey"), d.stopHotkey);
    s.sync();
}

} // namespace AppSettings
