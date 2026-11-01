#include "audio_v2/AudioCoreRuntime.h"

#include <iostream>
#include <stdexcept>
#include <vector>

using namespace ammod::audio_v2;

void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

ApplePcmFormatView Pcm() {
    ApplePcmFormatView view{};
    view.sampleRate = 44100;
    view.formatId = kAppleLinearPcm;
    view.formatFlags = 9; // float, packed, interleaved
    view.bytesPerPacket = view.bytesPerFrame = 8;
    view.framesPerPacket = 1;
    view.channelsPerFrame = 2;
    view.bitsPerChannel = 32;
    return view;
}

void Bind(AudioCoreRuntime& runtime, void* converter, void* unit, std::uint64_t observation) {
    auto source = Pcm();
    source.formatId = 0x616C6163; // alac
    source.bitsPerChannel = 16;
    runtime.OnConverterCreated(converter, source, Pcm(), observation);
    runtime.OnAudioUnitInitialized(unit, converter, 44100, 2, 16, observation);
    runtime.OnAudioUnitStarted(unit);
    Require(runtime.BoundConverter() == converter, "test source failed to bind");
}

void TestLegacySelectionAfterObserverLoss() {
    AudioCoreRuntime runtime;
    runtime.OnTransportPlaybackState(10, false, 123);
    runtime.SetUiEnabled(true);
    runtime.OnTransportPlaybackUnavailable(11);
    Require(runtime.Stats().userPaused, "lost observer should not itself synthesize Play");
    runtime.OnTransportSelectionArm(1);
    int converter{}, unit{};
    Bind(runtime, &converter, &unit, 1);
    Require(!runtime.Stats().userPaused && !runtime.Stats().playbackStateAvailable,
            "selected replacement must not inherit a stale legacy pause");
    runtime.OnConverterDisposed(&converter);
    Require(!runtime.IsConverterRegistered(&converter) && !runtime.IsConverterBound(&converter),
            "disposed converter must not survive recovery");
}

void TestCanonicalPauseWinsOverSelection() {
    AudioCoreRuntime runtime;
    runtime.OnTransportPlaybackState(10, false, 123);
    runtime.SetUiEnabled(true);
    runtime.OnTransportSelectionArm(1);
    int converter{}, unit{};
    Bind(runtime, &converter, &unit, 1);
    Require(runtime.Stats().userPaused && runtime.Stats().playbackStateAvailable,
            "binding must preserve a known canonical pause");
    runtime.OnTransportPlayPause(9);
    runtime.OnTransportPlaybackState(9, true, 999);
    runtime.OnTransportPlaybackUnavailable(9);
    Require(runtime.Stats().userPaused && runtime.Stats().playbackStateAvailable,
            "stale clicks and snapshots must not override the player");
    runtime.OnTransportPlaybackState(12, true, 124);
    Require(!runtime.Stats().userPaused, "fresh canonical Play must resume a selected item");
}

void TestLegacyRebindWithoutSelectionStaysPaused() {
    AudioCoreRuntime runtime;
    runtime.OnTransportPlaybackState(10, false, 123);
    runtime.SetUiEnabled(true);
    runtime.OnTransportPlaybackUnavailable(11);
    int converter{}, unit{};
    Bind(runtime, &converter, &unit, 1);
    Require(runtime.Stats().userPaused, "an unrelated rebind must not cancel pause");
}

void TestExpiredSelectionStaysPaused() {
    AudioCoreRuntime runtime;
    runtime.OnTransportPlaybackState(10, false, 123);
    runtime.SetUiEnabled(true);
    runtime.OnTransportPlaybackUnavailable(11);
    runtime.OnTransportSelectionArm(1);
    Sleep(800);
    int converter{}, unit{};
    Bind(runtime, &converter, &unit, 1);
    Require(runtime.Stats().userPaused, "expired selection must not cancel a later pause");
}

struct FaultObservation {
    std::atomic<unsigned> count{};
    std::atomic<HRESULT> error{S_OK};
    static void Event(void* context, const AudioCoreRuntimeEventSnapshot& event) noexcept {
        if (event.event != AudioCoreRuntimeEvent::Faulted) return;
        auto& self = *static_cast<FaultObservation*>(context);
        self.error.store(event.result);
        ++self.count;
    }
};

void TestProducerFaultIsReaped() {
    FaultObservation fault;
    AudioCoreRuntime runtime;
    AudioCoreRuntimeCallbacks callbacks{};
    callbacks.context = &fault;
    callbacks.onEventSnapshot = FaultObservation::Event;
    // No handoff callback/native client: this test never opens an endpoint.
    Require(runtime.Start(callbacks), "worker failed to start");
    runtime.SetUiEnabled(true);
    int converter{}, unit{};
    Bind(runtime, &converter, &unit, 1);
    std::vector<float> samples(kMaxBlockSamples);
    ApplePcmBufferView buffer{2, static_cast<std::uint32_t>(samples.size() * sizeof(float)), samples.data()};
    ApplePcmBufferListView list{1, &buffer};
    bool rejected = false;
    for (unsigned i = 0; i < 300; ++i) {
        if (!runtime.OnPcmOutput(&converter, Pcm(), list, kMaxBlockFrames)) {
            rejected = true;
            break;
        }
    }
    Require(rejected, "test did not overflow the bounded producer queue");
    const auto deadline = GetTickCount64() + 2000;
    while (fault.count.load() == 0 && GetTickCount64() < deadline) Sleep(5);
    Require(fault.count.load() == 1, "producer fault was not published exactly once");
    Require(fault.error.load() == HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW),
            "producer overflow must preserve a useful error code");
    Require(!runtime.UiEnabled() && !runtime.BoundConverter(), "faulted generation was not retired");
    Require(!runtime.Coordinator().ShouldSuppressNative() && !runtime.Coordinator().IsEnabled(),
            "fault must not leave the output gate enabled");
    runtime.SetUiEnabled(true);
    int nextConverter{}, nextUnit{};
    Bind(runtime, &nextConverter, &nextUnit, 2);
    Require(runtime.Coordinator().Phase() == AudioCoreGatePhase::Capturing,
            "runtime must permit a clean enable after a producer fault");
    runtime.Stop();
}

int main() {
    try {
        TestLegacySelectionAfterObserverLoss();
        TestCanonicalPauseWinsOverSelection();
        TestLegacyRebindWithoutSelectionStaysPaused();
        TestExpiredSelectionStaysPaused();
        TestProducerFaultIsReaped();
        std::cout << "5 runtime regression scenarios passed (no audio endpoint opened)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
