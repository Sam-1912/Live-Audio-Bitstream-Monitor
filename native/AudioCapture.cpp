#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <cmath>
#include <algorithm>
#include <condition_variable>
#include <array>
#include <complex>

using Microsoft::WRL::ComPtr;

extern "C" {
    typedef void(__stdcall* AudioMetricsCallback)(
        double rmsDb, double peakDb, unsigned int sampleRate,
        unsigned int channels, unsigned long long frames,
        double estimatedBitrateBps, double leftRmsDb, double rightRmsDb,
        double band0Db, double band1Db, double band2Db, double band3Db,
        double band4Db, double band5Db, double band6Db, double band7Db);

    static std::atomic<bool> g_running{ false };
    static std::thread g_worker;
    static std::mutex g_lock;
    static std::mutex g_startupMutex;
    static std::condition_variable g_startupCondition;
    static bool g_startupComplete = false;
    static HRESULT g_startupResult = E_PENDING;
    static AudioMetricsCallback g_callback = nullptr;

    static void SetStartupResult(HRESULT result) {
        {
            std::lock_guard<std::mutex> guard(g_startupMutex);
            if (g_startupComplete) return;
            g_startupResult = result;
            g_startupComplete = true;
        }
        g_startupCondition.notify_one();
    }

    static double ToDb(double x) {
        return 20.0 * std::log10(std::max(x, 1e-9));
    }

    class SpectrumAnalyzer {
        static constexpr size_t FftSize = 4096;
        static constexpr size_t HopSize = FftSize / 4;
        static constexpr size_t BandCount = 8;
        std::array<double, FftSize> m_samples{};
        std::array<double, FftSize> m_window{};
        std::array<double, BandCount> m_levels{};
        size_t m_writeIndex = 0;
        size_t m_samplesSinceAnalysis = 0;
        size_t m_totalSamples = 0;
        double m_windowSum = 0.0;
        unsigned int m_sampleRate;

        void Analyze() {
            std::array<std::complex<double>, FftSize> fft{};
            for (size_t i = 0; i < FftSize; ++i) {
                const size_t sampleIndex = (m_writeIndex + i) % FftSize;
                fft[i] = { m_samples[sampleIndex] * m_window[i], 0.0 };
            }

            for (size_t i = 1, j = 0; i < FftSize; ++i) {
                size_t bit = FftSize >> 1;
                for (; j & bit; bit >>= 1) j ^= bit;
                j ^= bit;
                if (i < j) std::swap(fft[i], fft[j]);
            }

            for (size_t length = 2; length <= FftSize; length <<= 1) {
                const double angle = -2.0 * 3.14159265358979323846 / length;
                const std::complex<double> step(std::cos(angle), std::sin(angle));
                for (size_t start = 0; start < FftSize; start += length) {
                    std::complex<double> phase(1.0, 0.0);
                    for (size_t offset = 0; offset < length / 2; ++offset) {
                        const auto even = fft[start + offset];
                        const auto odd = fft[start + offset + length / 2] * phase;
                        fft[start + offset] = even + odd;
                        fft[start + offset + length / 2] = even - odd;
                        phase *= step;
                    }
                }
            }

            static constexpr double edges[BandCount + 1] = {
                44.0, 88.0, 177.0, 354.0, 707.0, 1414.0, 2828.0, 5657.0, 11314.0
            };
            m_levels.fill(-120.0);
            for (size_t band = 0; band < BandCount; ++band) {
                double power = 0.0;
                size_t bins = 0;
                for (size_t bin = 1; bin < FftSize / 2; ++bin) {
                    const double frequency = static_cast<double>(bin) * m_sampleRate / FftSize;
                    if (frequency < edges[band] || frequency >= edges[band + 1]) continue;
                    const double amplitude = 2.0 * std::abs(fft[bin]) / m_windowSum;
                    power += amplitude * amplitude * 0.5;
                    ++bins;
                }
                if (bins > 0) m_levels[band] = ToDb(std::sqrt(power));
            }
        }

    public:
        explicit SpectrumAnalyzer(unsigned int sampleRate) : m_sampleRate(sampleRate) {
            m_levels.fill(-120.0);
            for (size_t i = 0; i < FftSize; ++i) {
                m_window[i] = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * i / (FftSize - 1));
                m_windowSum += m_window[i];
            }
        }

        void Add(double sample) {
            m_samples[m_writeIndex] = sample;
            m_writeIndex = (m_writeIndex + 1) % FftSize;
            ++m_samplesSinceAnalysis;
            ++m_totalSamples;
            if (m_totalSamples >= FftSize && m_samplesSinceAnalysis >= HopSize) {
                m_samplesSinceAnalysis = 0;
                Analyze();
            }
        }

        const std::array<double, BandCount>& Levels() const { return m_levels; }
    };

    static void CaptureLoop() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr)) {
            SetStartupResult(hr);
            return;
        }

        ComPtr<IMMDeviceEnumerator> enumerator;
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
            CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
        if (FAILED(hr)) {
            SetStartupResult(hr);
            CoUninitialize();
            return;
        }

        bool startupReported = false;
        while (g_running.load()) {
            ComPtr<IMMDevice> device;
            ComPtr<IAudioClient> client;
            ComPtr<IAudioCaptureClient> capture;
            LPWSTR activeDeviceId = nullptr;
            WAVEFORMATEX* mix = nullptr;
            HANDLE eventHandle = nullptr;
            bool deviceChanged = false;

            hr = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
            if (SUCCEEDED(hr)) hr = device->GetId(&activeDeviceId);
            if (SUCCEEDED(hr)) hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(client.GetAddressOf()));
            if (SUCCEEDED(hr)) hr = client->GetMixFormat(&mix);
            if (SUCCEEDED(hr)) {
                hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                    AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                    1000000, 0, mix, nullptr);
            }

            eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (SUCCEEDED(hr) && eventHandle) hr = client->SetEventHandle(eventHandle);
            else if (SUCCEEDED(hr)) hr = HRESULT_FROM_WIN32(GetLastError());
            if (SUCCEEDED(hr)) hr = client->GetService(IID_PPV_ARGS(&capture));
            if (SUCCEEDED(hr)) hr = client->Start();

            if (SUCCEEDED(hr)) {
                startupReported = true;
                SetStartupResult(S_OK);
                SpectrumAnalyzer spectrum(mix->nSamplesPerSec);

                while (g_running.load()) {
                    DWORD wait = WaitForSingleObject(eventHandle, 250);
                    if (wait != WAIT_OBJECT_0 && wait != WAIT_TIMEOUT) break;

                    ComPtr<IMMDevice> currentDefault;
                    LPWSTR currentDeviceId = nullptr;
                    HRESULT deviceHr = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &currentDefault);
                    if (SUCCEEDED(deviceHr)) deviceHr = currentDefault->GetId(&currentDeviceId);
                    if (SUCCEEDED(deviceHr) && _wcsicmp(activeDeviceId, currentDeviceId) != 0)
                        deviceChanged = true;
                    if (currentDeviceId) CoTaskMemFree(currentDeviceId);
                    if (deviceChanged || FAILED(deviceHr)) break;

                    UINT32 packetFrames = 0;
                    if (FAILED(capture->GetNextPacketSize(&packetFrames))) break;

                    while (packetFrames > 0) {
                        BYTE* data = nullptr;
                        UINT32 frames = 0;
                        DWORD flags = 0;
                        if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;

                        double sumSq = 0.0, peak = 0.0;
                        double leftSumSq = 0.0, rightSumSq = 0.0;
                        unsigned long long samples = 0;
                        unsigned long long leftSamples = 0, rightSamples = 0;
                        const unsigned int channels = mix->nChannels;
                        if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data && channels > 0) {
                            const unsigned int bits = mix->wBitsPerSample;
                            double frameSum = 0.0;
                            const bool isFloat = mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                                (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                                 reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);

                            auto accumulateSample = [&](unsigned long long i, double value) {
                                const double v = std::max(-1.0, std::min(value, 1.0));
                                sumSq += v * v;
                                peak = std::max(peak, std::abs(v));
                                ++samples;
                                frameSum += v;
                                if ((i % channels) == channels - 1) {
                                    spectrum.Add(frameSum / channels);
                                    frameSum = 0.0;
                                }
                                const unsigned int channel = static_cast<unsigned int>(i % channels);
                                if (channel == 0) {
                                    leftSumSq += v * v;
                                    ++leftSamples;
                                } else if (channel == 1) {
                                    rightSumSq += v * v;
                                    ++rightSamples;
                                }
                            };

                            const unsigned long long sampleCount = static_cast<unsigned long long>(frames) * channels;
                            if (isFloat && bits == 32) {
                                const float* p = reinterpret_cast<const float*>(data);
                                for (unsigned long long i = 0; i < sampleCount; ++i)
                                    accumulateSample(i, static_cast<double>(p[i]));
                            } else if (isFloat && bits == 64) {
                                const double* p = reinterpret_cast<const double*>(data);
                                for (unsigned long long i = 0; i < sampleCount; ++i)
                                    accumulateSample(i, p[i]);
                            } else if (bits == 16) {
                                const short* p = reinterpret_cast<const short*>(data);
                                for (unsigned long long i = 0; i < sampleCount; ++i)
                                    accumulateSample(i, static_cast<double>(p[i]) / 32768.0);
                            } else if (bits == 24) {
                                for (unsigned long long i = 0; i < sampleCount; ++i) {
                                    const BYTE* sample = data + i * 3;
                                    int value = static_cast<int>(sample[0]) |
                                        (static_cast<int>(sample[1]) << 8) |
                                        (static_cast<int>(sample[2]) << 16);
                                    if (value & 0x800000) value |= static_cast<int>(0xFF000000);
                                    accumulateSample(i, static_cast<double>(value) / 8388608.0);
                                }
                            } else if (!isFloat && bits == 32) {
                                const int* p = reinterpret_cast<const int*>(data);
                                for (unsigned long long i = 0; i < sampleCount; ++i)
                                    accumulateSample(i, static_cast<double>(p[i]) / 2147483648.0);
                            }
                        }

                        if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                            for (UINT32 frame = 0; frame < frames; ++frame) spectrum.Add(0.0);
                        }

                        double rmsDb = samples ? ToDb(std::sqrt(sumSq / samples)) : -120.0;
                        double peakDb = ToDb(peak);
                        double leftRmsDb = leftSamples ? ToDb(std::sqrt(leftSumSq / leftSamples)) : -120.0;
                        double rightRmsDb = rightSamples ? ToDb(std::sqrt(rightSumSq / rightSamples)) : leftRmsDb;
                        const double estimatedBitrateBps = static_cast<double>(mix->nSamplesPerSec)
                            * static_cast<double>(mix->nChannels)
                            * static_cast<double>(mix->wBitsPerSample);
                        const auto& bands = spectrum.Levels();
                        if (g_callback) g_callback(rmsDb, peakDb, mix->nSamplesPerSec,
                            mix->nChannels, frames, estimatedBitrateBps, leftRmsDb, rightRmsDb,
                            bands[0], bands[1], bands[2], bands[3],
                            bands[4], bands[5], bands[6], bands[7]);

                        capture->ReleaseBuffer(frames);
                        if (FAILED(capture->GetNextPacketSize(&packetFrames))) packetFrames = 0;
                    }
                }

                client->Stop();
            } else if (!startupReported) {
                SetStartupResult(hr);
                break;
            }

            if (mix) CoTaskMemFree(mix);
            if (eventHandle) CloseHandle(eventHandle);
            if (activeDeviceId) CoTaskMemFree(activeDeviceId);

            if (!g_running.load()) break;
            if (!deviceChanged) Sleep(500);
        }

        CoUninitialize();
    }

    __declspec(dllexport) int __stdcall AudioMonitor_Start(AudioMetricsCallback callback) {
        std::lock_guard<std::mutex> guard(g_lock);
        if (g_running.load()) return 1;
        if (!callback) return -1;

        {
            std::lock_guard<std::mutex> startupGuard(g_startupMutex);
            g_startupComplete = false;
            g_startupResult = E_PENDING;
        }
        g_callback = callback;
        g_running = true;
        try {
            g_worker = std::thread(CaptureLoop);
        } catch (...) {
            g_running = false;
            g_callback = nullptr;
            return -2;
        }

        HRESULT result;
        {
            std::unique_lock<std::mutex> startupGuard(g_startupMutex);
            g_startupCondition.wait(startupGuard, [] { return g_startupComplete; });
            result = g_startupResult;
        }
        if (FAILED(result)) {
            g_running = false;
            if (g_worker.joinable()) g_worker.join();
            g_callback = nullptr;
            return static_cast<int>(result);
        }
        return 0;
    }

    __declspec(dllexport) void __stdcall AudioMonitor_Stop() {
        {
            std::lock_guard<std::mutex> guard(g_lock);
            if (!g_running.exchange(false)) return;
        }
        if (g_worker.joinable()) g_worker.join();
        g_callback = nullptr;
    }
}
