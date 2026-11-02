#pragma once

#include <QString>
#include <QStringList>

// Resultado de la inicializacion del motor (se puede copiar entre hilos).
struct ObsInitResult {
    bool ok = false;
    QString version;
    QString errorMessage;      // vacio si ok
    QStringList modules;       // modulos cargados (nombre de archivo)
    QStringList encoders;      // ids de codificadores registrados
    QStringList sources;       // ids de tipos de fuente registrados
    QStringList outputs;       // ids de tipos de salida registrados
    QStringList skippedModules; // modulos opcionales desactivados porque cerraron la app la vez anterior
};

// Envuelve el ciclo de vida de libobs. Esta clase NO incluye cabeceras de OBS
// para que el resto de la aplicacion no dependa de ellas.
class ObsEngine {
public:
    ObsEngine();
    ~ObsEngine();

    ObsEngine(const ObsEngine &) = delete;
    ObsEngine &operator=(const ObsEngine &) = delete;

    // Puede llamarse desde un hilo de trabajo. Nunca lanza excepciones.
    ObsInitResult initialize(int width = 1920, int height = 1080, int fps = 60, bool enableVst = false);

    // Borra las marcas de proteccion de modulos opcionales para volver a intentar cargarlos.
    static void clearModuleGuards();

    // Cambia resolucion/FPS del lienzo. Solo funciona sin grabaciones activas.
    bool applyVideo(int width, int height, int fps, QString *errorOut);

    // Libera todo lo creado por initialize(). Seguro de llamar varias veces.
    void shutdown();

    bool isInitialized() const { return m_initialized; }

private:
    bool m_initialized = false;
};
