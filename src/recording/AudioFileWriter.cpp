#include "AudioFileWriter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <cstring>
#include <string>

namespace {

void put16(QByteArray &b, uint16_t v)
{
    b.append(static_cast<char>(v & 0xFF));
    b.append(static_cast<char>((v >> 8) & 0xFF));
}

void put32(QByteArray &b, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        b.append(static_cast<char>((v >> (8 * i)) & 0xFF));
}

QByteArray wavHeader(uint32_t dataBytes, int rate, int channels)
{
    QByteArray h;
    h.append("RIFF");
    put32(h, 36 + dataBytes);
    h.append("WAVE");
    h.append("fmt ");
    put32(h, 16);
    put16(h, 1); // PCM
    put16(h, static_cast<uint16_t>(channels));
    put32(h, static_cast<uint32_t>(rate));
    put32(h, static_cast<uint32_t>(rate * channels * 2));
    put16(h, static_cast<uint16_t>(channels * 2));
    put16(h, 16);
    h.append("data");
    put32(h, dataBytes);
    return h;
}

QString hresultText(const char *what, HRESULT hr)
{
    return QStringLiteral("%1 (error 0x%2)").arg(QString::fromLatin1(what)).arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0'));
}

template <typename T>
void safeRelease(T *&p)
{
    if (p) {
        p->Release();
        p = nullptr;
    }
}

} // namespace

AudioFileWriter::~AudioFileWriter()
{
    QString ignored;
    finish(&ignored);
}

bool AudioFileWriter::start(const QString &path, Format format, int sampleRate, int channels, QString *errorOut)
{
    if (m_running) {
        if (errorOut)
            *errorOut = QStringLiteral("El escritor de audio ya está en marcha.");
        return false;
    }
    m_path = path;
    m_format = format;
    m_rate = sampleRate;
    m_channels = channels;
    m_stop = false;
    m_openDone = false;
    m_openOk = false;
    m_error.clear();
    m_queue.clear();

    m_thread = std::thread([this] { run(); });

    // Esperar al resultado de la apertura del archivo
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this] { return m_openDone; });
    if (!m_openOk) {
        const QString err = m_error;
        lock.unlock();
        if (m_thread.joinable())
            m_thread.join();
        if (errorOut)
            *errorOut = err;
        return false;
    }
    m_running = true;
    return true;
}

void AudioFileWriter::push(const uint8_t *data, size_t frames)
{
    if (!m_running || !data || frames == 0)
        return;
    const size_t bytes = frames * static_cast<size_t>(m_channels) * 2;
    std::vector<uint8_t> chunk(data, data + bytes);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back(std::move(chunk));
    }
    m_cv.notify_one();
}

bool AudioFileWriter::finish(QString *errorOut)
{
    if (!m_thread.joinable()) {
        m_running = false;
        return true;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_one();
    m_thread.join();
    m_running = false;

    if (!m_error.isEmpty()) {
        if (errorOut)
            *errorOut = m_error;
        return false;
    }
    return true;
}

void AudioFileWriter::run()
{
    const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));

    QFile wav;
    uint64_t wavBytes = 0;
    IMFSinkWriter *writer = nullptr;
    bool mfStarted = false;
    uint64_t framesWritten = 0;

    auto signalOpen = [this](bool ok, const QString &error) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_openOk = ok;
        m_openDone = true;
        if (!ok)
            m_error = error;
        m_cv.notify_all();
    };

    // ---------- Apertura ----------
    if (m_format == Format::Wav) {
        QDir().mkpath(QFileInfo(m_path).absolutePath());
        wav.setFileName(m_path);
        if (!wav.open(QIODevice::WriteOnly)) {
            signalOpen(false, QStringLiteral("No se pudo crear %1: %2").arg(m_path, wav.errorString()));
            if (com)
                CoUninitialize();
            return;
        }
        wav.write(wavHeader(0, m_rate, m_channels)); // se corrige al terminar
    } else {
        HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
        if (FAILED(hr)) {
            signalOpen(false, hresultText("No se pudo iniciar Media Foundation", hr));
            if (com)
                CoUninitialize();
            return;
        }
        mfStarted = true;

        std::wstring wpath = QDir::toNativeSeparators(m_path).toStdWString();
        hr = MFCreateSinkWriterFromURL(wpath.c_str(), nullptr, nullptr, &writer);
        IMFMediaType *outType = nullptr;
        IMFMediaType *inType = nullptr;
        DWORD streamIndex = 0;
        bool ok = SUCCEEDED(hr);
        QString error = ok ? QString() : hresultText("Windows no puede crear archivos MP3 (¿falta el codificador?)", hr);

        if (ok) {
            hr = MFCreateMediaType(&outType);
            if (SUCCEEDED(hr)) {
                outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
                outType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_MP3);
                outType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, static_cast<UINT32>(m_rate));
                outType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, static_cast<UINT32>(m_channels));
                outType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000); // 192 kbps
                outType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 1);
                hr = writer->AddStream(outType, &streamIndex);
            }
            ok = SUCCEEDED(hr);
            if (!ok)
                error = hresultText("No se pudo configurar la salida MP3", hr);
        }
        if (ok) {
            hr = MFCreateMediaType(&inType);
            if (SUCCEEDED(hr)) {
                inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
                inType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
                inType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
                inType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, static_cast<UINT32>(m_rate));
                inType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, static_cast<UINT32>(m_channels));
                inType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, static_cast<UINT32>(m_channels * 2));
                inType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, static_cast<UINT32>(m_rate * m_channels * 2));
                hr = writer->SetInputMediaType(streamIndex, inType, nullptr);
            }
            ok = SUCCEEDED(hr);
            if (!ok)
                error = hresultText("No se pudo configurar la entrada del codificador MP3", hr);
        }
        if (ok) {
            hr = writer->BeginWriting();
            ok = SUCCEEDED(hr);
            if (!ok)
                error = hresultText("No se pudo iniciar la escritura MP3", hr);
        }
        safeRelease(outType);
        safeRelease(inType);

        if (!ok) {
            safeRelease(writer);
            MFShutdown();
            signalOpen(false, error);
            if (com)
                CoUninitialize();
            return;
        }
    }

    signalOpen(true, QString());

    // ---------- Bucle de escritura ----------
    const size_t bytesPerFrame = static_cast<size_t>(m_channels) * 2;
    for (;;) {
        std::vector<uint8_t> chunk;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return m_stop || !m_queue.empty(); });
            if (m_queue.empty()) {
                if (m_stop)
                    break;
                continue;
            }
            chunk = std::move(m_queue.front());
            m_queue.pop_front();
        }

        if (m_format == Format::Wav) {
            if (wav.write(reinterpret_cast<const char *>(chunk.data()), static_cast<qint64>(chunk.size())) < 0) {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_error.isEmpty())
                    m_error = QStringLiteral("Error al escribir el WAV: %1").arg(wav.errorString());
            }
            wavBytes += chunk.size();
        } else if (writer) {
            IMFMediaBuffer *buffer = nullptr;
            IMFSample *sample = nullptr;
            HRESULT hr = MFCreateMemoryBuffer(static_cast<DWORD>(chunk.size()), &buffer);
            if (SUCCEEDED(hr)) {
                BYTE *dst = nullptr;
                hr = buffer->Lock(&dst, nullptr, nullptr);
                if (SUCCEEDED(hr)) {
                    std::memcpy(dst, chunk.data(), chunk.size());
                    buffer->Unlock();
                    buffer->SetCurrentLength(static_cast<DWORD>(chunk.size()));
                    hr = MFCreateSample(&sample);
                }
            }
            if (SUCCEEDED(hr)) {
                const uint64_t frames = chunk.size() / bytesPerFrame;
                sample->AddBuffer(buffer);
                sample->SetSampleTime(static_cast<LONGLONG>(framesWritten * 10000000ULL / static_cast<uint64_t>(m_rate)));
                sample->SetSampleDuration(static_cast<LONGLONG>(frames * 10000000ULL / static_cast<uint64_t>(m_rate)));
                hr = writer->WriteSample(0, sample);
                framesWritten += frames;
            }
            if (FAILED(hr)) {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_error.isEmpty())
                    m_error = hresultText("Error al codificar el MP3", hr);
            }
            safeRelease(sample);
            safeRelease(buffer);
        }
    }

    // ---------- Cierre ----------
    if (m_format == Format::Wav) {
        // Corrige los tamanos de la cabecera (el limite del formato WAV es 4 GB)
        const uint32_t size = wavBytes > 0xFFFFFFF0ULL ? 0xFFFFFFF0u : static_cast<uint32_t>(wavBytes);
        wav.seek(0);
        wav.write(wavHeader(size, m_rate, m_channels));
        wav.close();
    } else if (writer) {
        HRESULT hr = writer->Finalize();
        if (FAILED(hr)) {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_error.isEmpty())
                m_error = hresultText("No se pudo finalizar el MP3", hr);
        }
        safeRelease(writer);
    }
    if (mfStarted)
        MFShutdown();
    if (com)
        CoUninitialize();
}
