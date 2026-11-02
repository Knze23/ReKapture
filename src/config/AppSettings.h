#pragma once

#include <QString>

struct AppSettingsData {
    QString folder;                                   // carpeta de grabaciones
    int captureMode = 0;                              // 0 monitor, 1 ventana, 2 juego
    QString monitorId;                                // vacio = primero de la lista
    QString windowId;                                 // ventana o juego elegido (vacio = automatico en modo juego)
    QString desktopDeviceId = QStringLiteral("default"); // "" = sin audio del escritorio
    QString micDeviceId = QStringLiteral("default");     // "" = sin microfono
    QString encoderKey = QStringLiteral("auto");      // auto | nvenc | amf | qsv | x264
    bool bitrateCustom = false;                       // true: se usa bitrateKbps en vez del perfil de calidad
    int bitrateKbps = 12000;                          // bitrate de video personalizado (CBR)
    int audioBitrateKbps = 160;                       // bitrate de audio AAC por pista
    int quality = 2;                                  // 0 baja, 1 media, 2 alta, 3 muy alta
    int fps = 60;                                     // 30 o 60
    int width = 1920;
    int height = 1080;
    bool previewEnabled = true;                       // previsualizacion activada
    int desktopVolume = 100;                          // % del audio del escritorio
    int micVolume = 100;                              // % del microfono
    QString windowGeometry;                           // geometria de la ventana (hex)
    // Microfono y webcam
    int micExportMode = 0;                            // 0 archivo aparte, 1 pista en el archivo principal, 2 ambos
    int micFileFormat = 0;                            // archivo aparte: 0 WAV, 1 MP3, 2 MP4 con imagen negra
    bool webcamEnabled = false;                       // grabar la webcam en un MP4 independiente
    QString webcamDeviceId;
    int webcamResolution = 0;                         // 0 = 720p, 1 = 1080p (a 30 FPS de camara)
    bool webcamWithAudio = false;                     // incluir el microfono en el MP4 de la webcam
    bool webcamPreview = false;                       // mostrar la vista previa de la webcam (abre la camara)
    bool hideAppFromCapture = false;                  // ocultar la ventana de ReKapture en grabaciones y capturas
    bool vstEnabled = false;                          // cargar el modulo de plugins VST 2.x externos
    QString desktopEffects;                           // cadena de efectos del audio del escritorio (JSON)
    QString micEffects;                               // cadena de efectos del microfono (JSON)

    QString theme = QStringLiteral("dark");           // dark | light | skin:<archivo>
    bool organizeByGame = true;                       // carpeta y nombre por juego
    bool overlayEnabled = true;                       // icono de actividad en pantalla
    int overlayCorner = 1;                            // 0 arriba-izq, 1 arriba-der, 2 abajo-izq, 3 abajo-der
    bool instantReplay = false;                       // repeticion instantanea (buffer siempre activo)
    bool clipsWhileRecording = true;                  // permitir clips mientras se graba
    int clipSeconds = 30;                             // duracion del buffer / clip
    bool autoDisableReplay = true;                    // apagar la repeticion si se pierden fotogramas
    QString clipHotkey = QStringLiteral("Ctrl+Shift+F11");
    QString replayHotkey = QStringLiteral("Ctrl+Shift+F8");
    bool soundCues = true;                            // avisos sonoros al iniciar/detener
    QString startHotkey = QStringLiteral("Ctrl+Shift+F9");
    QString stopHotkey = QStringLiteral("Ctrl+Shift+F10");
};

// Preferencias en %APPDATA%\ReKapture\settings.ini. Los valores no validos se corrigen al cargar.
namespace AppSettings {

AppSettingsData load();
void save(const AppSettingsData &data);
QString filePath();

} // namespace AppSettings
