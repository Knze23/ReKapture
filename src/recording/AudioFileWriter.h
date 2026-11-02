#pragma once

#include <QString>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

// Escribe el microfono en un archivo de audio independiente (WAV o MP3) en un hilo propio,
// para no bloquear nunca el hilo de audio de libobs. El audio llega como PCM de 16 bits
// entrelazado. MP3 usa el codificador que ya trae Windows (Media Foundation).
class AudioFileWriter {
public:
    enum class Format { Wav, Mp3 };

    AudioFileWriter() = default;
    ~AudioFileWriter();
    AudioFileWriter(const AudioFileWriter &) = delete;
    AudioFileWriter &operator=(const AudioFileWriter &) = delete;

    // Abre el archivo y arranca el hilo. Devuelve false (y el motivo) si no se pudo.
    bool start(const QString &path, Format format, int sampleRate, int channels, QString *errorOut);

    // Seguro desde el hilo de audio: solo copia los datos a una cola.
    void push(const uint8_t *data, size_t frames);

    // Vacia la cola, finaliza el archivo y detiene el hilo. Devuelve false si hubo errores.
    bool finish(QString *errorOut);

    QString path() const { return m_path; }
    bool isRunning() const { return m_running; }

private:
    void run();

    QString m_path;
    Format m_format = Format::Wav;
    int m_rate = 48000;
    int m_channels = 2;

    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::vector<uint8_t>> m_queue;
    bool m_stop = false;
    bool m_running = false;

    // Resultado de la apertura (lo rellena el hilo)
    bool m_openDone = false;
    bool m_openOk = false;
    QString m_error;
};
