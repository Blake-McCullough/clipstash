#include "core/AudioSources.h"

#include "core/Recorder.h"

#include <QCoreApplication>
#include <QProcess>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmreg.h>
#include <audioclient.h>
#include <mmdeviceapi.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <thread>
#include <vector>
#endif

namespace {
// The tests (and Linux when the ffmpeg engine is forced) use tones instead
// of real devices.
bool testMode() { return qEnvironmentVariableIsSet("CLIPSTASH_TEST_VIDEO"); }
}

#ifdef Q_OS_WIN
namespace {
constexpr int kRate = 48000;
constexpr int kChannels = 2;
// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, spelled out so no extra library is needed.
const GUID kFloatSubtype = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

template <typename T> void release(T *&p)
{
    if (p) p->Release();
    p = nullptr;
}

// One audio device being captured into a queue of 48 kHz stereo floats.
class DeviceCapture {
public:
    explicit DeviceCapture(QString source) : m_source(std::move(source)) {}
    ~DeviceCapture() { shutdown(); }

    // Called on the capture thread (COM initialised there).
    void init()
    {
        shutdown();
        IMMDeviceEnumerator *en = nullptr;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                    reinterpret_cast<void **>(&en))))
            return;
        const bool output = m_source == "default_output";
        HRESULT hr = E_FAIL;
        if (m_source.startsWith("device:")) {
            const std::wstring id = m_source.mid(7).toStdWString();
            hr = en->GetDevice(id.c_str(), &m_device);
        } else {
            hr = en->GetDefaultAudioEndpoint(output ? eRender : eCapture, eConsole, &m_device);
        }
        if (SUCCEEDED(hr) && m_device) {
            LPWSTR id = nullptr;
            if (SUCCEEDED(m_device->GetId(&id))) {
                m_deviceId = QString::fromWCharArray(id);
                CoTaskMemFree(id);
            }
            EDataFlow flow = eCapture;
            IMMEndpoint *ep = nullptr;
            if (SUCCEEDED(m_device->QueryInterface(__uuidof(IMMEndpoint), reinterpret_cast<void **>(&ep)))) {
                ep->GetDataFlow(&flow);
                ep->Release();
            }
            m_loopback = flow == eRender;
            openClient();
        }
        en->Release();
    }

    void shutdown()
    {
        if (m_client) m_client->Stop();
        release(m_capture);
        release(m_client);
        release(m_device);
        m_native = false;
    }

    // Has the default device changed (headphones plugged in, etc.)?
    bool defaultChanged()
    {
        if (m_source.startsWith("device:")) return false;
        IMMDeviceEnumerator *en = nullptr;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                    reinterpret_cast<void **>(&en))))
            return false;
        IMMDevice *dev = nullptr;
        bool changed = false;
        if (SUCCEEDED(en->GetDefaultAudioEndpoint(m_source == "default_output" ? eRender : eCapture, eConsole, &dev))) {
            LPWSTR id = nullptr;
            if (SUCCEEDED(dev->GetId(&id))) {
                changed = QString::fromWCharArray(id) != m_deviceId;
                CoTaskMemFree(id);
            }
            dev->Release();
        } else {
            changed = m_client != nullptr;
        }
        en->Release();
        return changed;
    }

    // Moves whatever the device has into the queue.
    void drain()
    {
        if (!m_capture) return;
        UINT32 packet = 0;
        while (SUCCEEDED(m_capture->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE *data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(m_capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) return;
            const bool silent = flags & AUDCLNT_BUFFERFLAGS_SILENT;
            if (m_native) {
                appendConverted(silent ? nullptr : data, frames);
            } else {
                const float *f = reinterpret_cast<const float *>(data);
                for (UINT32 i = 0; i < frames * kChannels; ++i) queue.push_back(silent ? 0.f : f[i]);
            }
            m_capture->ReleaseBuffer(frames);
        }
    }

    std::deque<float> queue;  // interleaved stereo

private:
    void openClient()
    {
        if (FAILED(m_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(&m_client))))
            return;
        // Ask Windows to hand us 48 kHz stereo float whatever the device runs at.
        WAVEFORMATEXTENSIBLE fmt{};
        fmt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        fmt.Format.nChannels = kChannels;
        fmt.Format.nSamplesPerSec = kRate;
        fmt.Format.wBitsPerSample = 32;
        fmt.Format.nBlockAlign = kChannels * 4;
        fmt.Format.nAvgBytesPerSec = kRate * kChannels * 4;
        fmt.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        fmt.Samples.wValidBitsPerSample = 32;
        fmt.dwChannelMask = 0x1 | 0x2;  // front left, front right
        fmt.SubFormat = kFloatSubtype;
        DWORD flags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        if (m_loopback) flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
        const REFERENCE_TIME buffer = 2000000;  // 200 ms
        HRESULT hr = m_client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, buffer, 0,
                                          reinterpret_cast<WAVEFORMATEX *>(&fmt), nullptr);
        if (FAILED(hr)) {
            // Fall back to the device's own format and convert here.
            release(m_client);
            if (FAILED(m_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                          reinterpret_cast<void **>(&m_client))))
                return;
            WAVEFORMATEX *mix = nullptr;
            if (FAILED(m_client->GetMixFormat(&mix))) return;
            m_mixRate = int(mix->nSamplesPerSec);
            m_mixChannels = mix->nChannels;
            m_mixFloat = mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT
                || (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE
                    && reinterpret_cast<WAVEFORMATEXTENSIBLE *>(mix)->SubFormat == kFloatSubtype);
            m_mixBits = mix->wBitsPerSample;
            hr = m_client->Initialize(AUDCLNT_SHAREMODE_SHARED, m_loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0, buffer, 0,
                                      mix, nullptr);
            CoTaskMemFree(mix);
            if (FAILED(hr)) {
                release(m_client);
                return;
            }
            m_native = true;
        }
        if (FAILED(m_client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void **>(&m_capture)))) {
            release(m_client);
            return;
        }
        m_client->Start();
    }

    // Device format to 48 kHz stereo float (fallback path only).
    void appendConverted(const BYTE *data, UINT32 frames)
    {
        const double step = double(m_mixRate) / kRate;
        for (UINT32 i = 0; i < frames; ++i) {
            float ch[2] = {0.f, 0.f};
            if (data) {
                for (int c = 0; c < 2; ++c) {
                    const int src = qMin(c, m_mixChannels - 1);
                    const BYTE *p = data + (size_t(i) * m_mixChannels + src) * (m_mixBits / 8);
                    if (m_mixFloat) ch[c] = *reinterpret_cast<const float *>(p);
                    else if (m_mixBits == 16) ch[c] = *reinterpret_cast<const qint16 *>(p) / 32768.f;
                    else if (m_mixBits == 32) ch[c] = *reinterpret_cast<const qint32 *>(p) / 2147483648.f;
                }
            }
            // Nearest-sample rate conversion: rarely used, good enough for voice.
            m_phase += 1.0;
            while (m_phase >= step) {
                m_phase -= step;
                queue.push_back(ch[0]);
                queue.push_back(ch[1]);
            }
        }
    }

    QString m_source;
    QString m_deviceId;
    IMMDevice *m_device = nullptr;
    IAudioClient *m_client = nullptr;
    IAudioCaptureClient *m_capture = nullptr;
    bool m_loopback = false;
    bool m_native = false;
    int m_mixRate = kRate, m_mixChannels = 2, m_mixBits = 32;
    bool m_mixFloat = true;
    double m_phase = 0;
};

// One track: its sources mixed together and written to a named pipe at a
// steady real-time pace (silence when nothing plays), which is what ffmpeg
// reads as a raw audio input.
class PipeTrack {
public:
    PipeTrack(const QString &pipeName, QStringList sources) : m_name(pipeName), m_sources(std::move(sources)) {}
    ~PipeTrack() { stop(); }

    bool start()
    {
        const std::wstring name = m_name.toStdWString();
        m_pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_OUTBOUND, PIPE_TYPE_BYTE | PIPE_WAIT, 1, 1 << 20, 0, 0,
                                  nullptr);
        if (m_pipe == INVALID_HANDLE_VALUE) return false;
        m_thread = std::thread([this] { run(); });
        return true;
    }

    void stop()
    {
        m_stop = true;
        if (m_thread.joinable()) {
            // The thread may be blocked waiting for ffmpeg or in a write:
            // keep cancelling its pipe calls until it notices m_stop.
            while (!m_finished) {
                if (HANDLE h = m_handle.load()) CancelSynchronousIo(h);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            m_thread.join();
        }
        if (HANDLE h = m_handle.exchange(nullptr)) CloseHandle(h);
        if (m_pipe != INVALID_HANDLE_VALUE) {
            CloseHandle(m_pipe);
            m_pipe = INVALID_HANDLE_VALUE;
        }
    }

private:
    void run()
    {
        m_handle = OpenThread(THREAD_TERMINATE, FALSE, GetCurrentThreadId());
        struct Done {
            std::atomic<bool> &flag;
            ~Done() { flag = true; }
        } done{m_finished};
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (!ConnectNamedPipe(m_pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) {
            CoUninitialize();
            return;
        }
        std::vector<std::unique_ptr<DeviceCapture>> devices;
        for (const QString &s : m_sources) {
            devices.push_back(std::make_unique<DeviceCapture>(s));
            devices.back()->init();
        }
        const auto t0 = std::chrono::steady_clock::now();
        auto lastCheck = t0;
        qint64 written = 0;  // frames
        std::vector<float> out;
        while (!m_stop) {
            Sleep(10);
            for (auto &d : devices) d->drain();
            const auto now = std::chrono::steady_clock::now();
            if (now - lastCheck > std::chrono::seconds(2)) {
                lastCheck = now;
                for (auto &d : devices)
                    if (d->defaultChanged()) d->init();
            }
            const double elapsed = std::chrono::duration<double>(now - t0).count();
            const qint64 target = qint64(elapsed * kRate);
            const qint64 need = target - written;
            if (need <= 0) continue;
            out.assign(size_t(need) * kChannels, 0.f);
            for (auto &d : devices) {
                auto &q = d->queue;
                // More than a quarter second queued means the device runs
                // fast compared to the clock: drop the excess to stay in sync.
                const size_t maxQueued = size_t(kRate / 4) * kChannels;
                while (q.size() > maxQueued + out.size()) q.pop_front();
                const size_t take = qMin(q.size(), out.size());
                for (size_t i = 0; i < take; ++i) {
                    out[i] += q.front();
                    q.pop_front();
                }
            }
            DWORD done = 0;
            if (!WriteFile(m_pipe, out.data(), DWORD(out.size() * sizeof(float)), &done, nullptr)) break;
            written += need;
        }
        devices.clear();
        CoUninitialize();
    }

    QString m_name;
    QStringList m_sources;
    HANDLE m_pipe = INVALID_HANDLE_VALUE;
    std::thread m_thread;
    std::atomic<HANDLE> m_handle{nullptr};
    std::atomic<bool> m_finished{false};
    std::atomic<bool> m_stop{false};
};
}
#endif

struct AudioSources::Impl {
#ifdef Q_OS_WIN
    std::vector<std::unique_ptr<PipeTrack>> tracks;
#endif
};

AudioSources::AudioSources(QObject *parent) : QObject(parent), d(std::make_unique<Impl>()) {}

AudioSources::~AudioSources() { close(); }

bool AudioSources::supportsPerApp()
{
#ifdef Q_OS_WIN
    return false;  // per-app capture on Windows is not wired up yet
#else
    return true;
#endif
}

QList<QPair<QString, QString>> AudioSources::choices()
{
    return {{"default_output", "Desktop audio (everything you hear)"}, {"default_input", "Microphone (default)"}};
}

QList<QStringList> AudioSources::open(const Settings &s, QStringList *names)
{
    close();
    QList<QStringList> inputs;
    const QList<AudioTrackCfg> tracks = s.effectiveTracks();
    int n = 0;
    for (const AudioTrackCfg &t : tracks) {
        QStringList sources = t.sources.split('|', Qt::SkipEmptyParts);
        for (QString &src : sources) src = src.trimmed();
        sources.removeAll(QString());
        if (sources.isEmpty()) continue;

        if (testMode()) {
            inputs << QStringList{"-re", "-f", "lavfi", "-i", QString("sine=frequency=%1:sample_rate=48000").arg(300 + 200 * n)};
        } else {
#ifdef Q_OS_WIN
            QStringList usable;
            for (const QString &src : sources) {
                if (src.startsWith("app:") || src.startsWith("app-inverse:")) {
                    Recorder::log("audio: " + src + " is not supported on Windows yet, skipped");
                    continue;
                }
                usable << src;
            }
            if (usable.isEmpty()) continue;
            const QString pipe = QString("\\\\.\\pipe\\clipstash-%1-%2").arg(QCoreApplication::applicationPid()).arg(n);
            auto track = std::make_unique<PipeTrack>(pipe, usable);
            if (!track->start()) {
                Recorder::log("audio: could not create pipe for " + t.name);
                continue;
            }
            d->tracks.push_back(std::move(track));
            inputs << QStringList{"-thread_queue_size", "4096", "-f", "f32le", "-ar", "48000", "-ac", "2", "-i", pipe};
#else
            // Linux with the ffmpeg engine: PulseAudio/PipeWire devices.
            const QString src = sources.first();
            const QString dev = src == "default_output" ? "@DEFAULT_MONITOR@"
                                : src == "default_input" ? "default" : src.section(':', 1);
            inputs << QStringList{"-thread_queue_size", "4096", "-f", "pulse", "-i", dev};
#endif
        }
        if (names) *names << t.name;
        ++n;
    }
    return inputs;
}

void AudioSources::close()
{
#ifdef Q_OS_WIN
    for (auto &t : d->tracks) t->stop();
    d->tracks.clear();
#endif
}
