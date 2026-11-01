#include "audio_v2/WasapiExclusiveSink.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace ammod::audio_v2;

struct Silence final : IIntegerPcmSource {
    std::atomic<std::uint64_t> sourceFrames{};
    std::atomic<bool> ready{true};
    bool CanProvide(std::uint32_t frames) const noexcept override {
        return frames != 0 && ready.load(std::memory_order_acquire);
    }
    HRESULT Fill(std::int32_t* data, std::uint32_t capacity,
                 std::uint32_t& frames, bool& end) noexcept override {
        std::memset(data, 0, capacity * 2 * sizeof(*data));
        frames = capacity;
        end = false;
        sourceFrames.fetch_add(capacity, std::memory_order_relaxed);
        return S_OK;
    }
};

double Measure(WasapiExclusiveSink& sink, const char* label) {
    Sleep(500);
    LARGE_INTEGER frequency{}, begin{}, end{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&begin);
    const auto before = sink.Stats();
    Sleep(3000);
    const auto after = sink.Stats();
    QueryPerformanceCounter(&end);
    const double seconds = static_cast<double>(end.QuadPart - begin.QuadPart) / frequency.QuadPart;
    const double rate = (after.submittedFrames - before.submittedFrames) / seconds;
    std::printf("%s seconds=%.3f framesPerSecond=%.1f nominal=%u ratio=%.4f "
                "clockDelta=%llu clockHz=%llu starts=%llu underruns=%llu lastError=%08lx\n",
                label, seconds, rate, sink.Format().sampleRate, rate / sink.Format().sampleRate,
                after.clockPosition - before.clockPosition, after.clockFrequency,
                after.clientStarts, after.underruns, after.lastError);
    std::fflush(stdout);
    return SUCCEEDED(after.lastError) && sink.State() == WasapiSinkState::Running &&
        after.underruns == before.underruns ? rate : 0.0;
}

int main(int argc, char** argv) {
    const unsigned rate = argc >= 3 ? static_cast<unsigned>(std::strtoul(argv[2], nullptr, 10)) : 44100u;
    const bool legacyWait = argc == 4 && std::strcmp(argv[3], "--legacy-wait") == 0;
    if (argc < 2 || argc > 4 || (argc == 4 && !legacyWait) ||
        std::strcmp(argv[1], "--run-on-default-device") ||
        (rate != 44100 && rate != 96000 && rate != 192000)) {
        std::puts("Explicit opt-in: --run-on-default-device [44100|96000|192000] [--legacy-wait]");
        return 2;
    }
    Silence source;
    WasapiExclusiveSink sink;
    WasapiSinkConfig config{};
    config.format = {rate, 2, 16, 16};
    config.periodFrames = rate / 50;
    config.alignmentRetries = 3;
    config.keepRunningOnPause = true;
    config.keepRunningOnStarvation = !legacyWait;
    config.sourceReadyEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    auto hr = sink.Open(config);
    if (FAILED(hr)) {
        std::printf("Open failed: %08lx\n", hr);
        if (config.sourceReadyEvent) CloseHandle(config.sourceReadyEvent);
        return 1;
    }
    hr = sink.Start(source);
    if (FAILED(hr)) {
        std::printf("Start failed: %08lx\n", hr);
        sink.Close();
        if (config.sourceReadyEvent) CloseHandle(config.sourceReadyEvent);
        return 1;
    }
    const auto baseline = Measure(sink, "baseline");
    bool passed = std::abs(baseline / config.format.sampleRate - 1.0) < 0.05;
    for (unsigned round = 1; round <= 2; ++round) {
        sink.Pause();
        sink.Pause(); // Repeated pause must not stop the kept-alive device.
        const auto sourceBefore = source.sourceFrames.load();
        const auto deviceBefore = sink.Stats();
        Sleep(1000);
        const auto consumed = source.sourceFrames.load() - sourceBefore;
        const auto deviceAfter = sink.Stats();
        // One in-flight media packet can cross the control boundary, but the
        // paused worker must not continue reading the source behind the user.
        passed = passed && consumed <= sink.BufferFrames() &&
            deviceAfter.controlledSilenceFrames > deviceBefore.controlledSilenceFrames &&
            deviceAfter.clientStarts == deviceBefore.clientStarts;
        std::printf("pause%u sourceFramesConsumed=%llu silentDeviceFrames=%llu starts=%llu\n",
                    round, consumed, deviceAfter.controlledSilenceFrames - deviceBefore.controlledSilenceFrames,
                    deviceAfter.clientStarts);
        hr = sink.Start(source);
        if (FAILED(hr)) { passed = false; break; }
        const auto resumed = Measure(sink, round == 1 ? "resume1" : "resume2");
        passed = passed && std::abs(resumed / config.format.sampleRate - 1.0) < 0.05;
    }
    // Simulate a brief delivery gap at display/Modern Standby wake. No seek,
    // track restart, source discard or device format change is permitted.
    source.ready.store(false, std::memory_order_release);
    const auto beforeWait = sink.Stats();
    const auto sourceBeforeWait = source.sourceFrames.load();
    Sleep(1000);
    const auto afterWait = sink.Stats();
    const auto consumedWhileWaiting = source.sourceFrames.load() - sourceBeforeWait;
    std::printf("source-wait consumed=%llu silence=%llu starts=%llu waits=%llu legacy=%d\n",
                consumedWhileWaiting, afterWait.controlledSilenceFrames - beforeWait.controlledSilenceFrames,
                afterWait.clientStarts, afterWait.sourceWaits, legacyWait);
    if (!legacyWait) {
        passed = passed && sink.WaitingForSource() &&
            consumedWhileWaiting <= sink.BufferFrames() &&
            afterWait.controlledSilenceFrames > beforeWait.controlledSilenceFrames &&
            afterWait.clientStarts == beforeWait.clientStarts;
    }
    sink.SetResumeBlocked(true);
    source.ready.store(true, std::memory_order_release);
    if (config.sourceReadyEvent) SetEvent(config.sourceReadyEvent);
    const auto blockedSourceBefore = source.sourceFrames.load();
    Sleep(100);
    const auto blockedConsumption = source.sourceFrames.load() - blockedSourceBefore;
    passed = passed && blockedConsumption <= sink.BufferFrames();
    std::printf("resume-blocked sourceFramesConsumed=%llu\n", blockedConsumption);
    sink.SetResumeBlocked(false);
    if (config.sourceReadyEvent) SetEvent(config.sourceReadyEvent);
    const auto afterSourceWait = Measure(sink, "after-source-wait");
    passed = passed && std::abs(afterSourceWait / config.format.sampleRate - 1.0) < 0.05;
    if (!legacyWait) passed = passed && sink.Stats().clientStarts == beforeWait.clientStarts;
    sink.Close();
    if (config.sourceReadyEvent) CloseHandle(config.sourceReadyEvent);
    std::printf("Pause/starvation cadence: %s\n", passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}
