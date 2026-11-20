#include "Engine.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace breakvar;
namespace { thread_local bool watchAllocations = false; thread_local int callbackAllocations = 0; }
void* operator new(std::size_t size) {
    if (watchAllocations) ++callbackAllocations;
    if (void* result = std::malloc(size == 0 ? 1 : size)) return result;
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void near(float actual, float expected, const char* message) {
    if (std::abs(actual - expected) > 0.002f) {
        std::cerr << message << ": actual " << actual << ", expected " << expected << '\n';
        throw std::runtime_error(message);
    }
}
Pattern pattern(int inputBars = 1, int outputBars = 1) {
    auto p = Pattern::makeDefault();
    p.inputBars = inputBars; p.outputBars = outputBars;
    p.cells.fill(-1); p.order.fill(0); p.orderCount = 1;
    for (int i = 0; i < inputBars * cellsPerBar; ++i) p.cells[i] = 0;
    return p;
}
struct Fixture {
    Engine engine;
    double sr = 160.0;
    double bpm = 60.0;
    std::int64_t t = 0;
    Fixture(double rate = 160.0) : sr(rate) { engine.prepare(sr, 1024, 2); }
    std::vector<std::vector<float>> run(int count, const Pattern& p, int block = 37,
                                      bool bypass = false, bool loop = false,
                                      double ppqShift = 0.0, bool playing = true,
                                      bool defer = false) {
        std::vector<std::vector<float>> output(2, std::vector<float>(count));
        for (int offset = 0; offset < count; ) {
            const int n = std::min(block, count - offset);
            for (int i = 0; i < n; ++i) {
                output[0][offset + i] = static_cast<float>(t + i + 1);
                output[1][offset + i] = -2.0f * output[0][offset + i];
            }
            Transport tr;
            tr.valid = true; tr.playing = playing; tr.bpm = bpm;
            tr.ppq = static_cast<double>(t) * bpm / (60.0 * sr) + ppqShift;
            tr.looping = loop; tr.loopStartPpq = 0; tr.loopEndPpq = 4;
            if (loop) tr.ppq = std::fmod(tr.ppq, 4.0);
            float* ptrs[] {output[0].data() + offset, output[1].data() + offset};
            engine.process(ptrs, 2, n, tr, p, bypass, defer);
            offset += n; t += n;
        }
        return output;
    }
};
void delayAndStereo() {
    Fixture f; const auto p = pattern();
    const auto out = f.run(2100, p, 37, true);
    require(f.engine.getLatencySamples() == 0, "identity requires zero latency");
    for (int i = 0; i < 2100; ++i) {
        near(out[0][i], static_cast<float>(i + 1), "exact delayed bypass");
        near(out[1][i], -2.0f * out[0][i], "stereo provenance");
    }
    require(f.engine.getStatus() == EngineStatus::ready, "ready after capture");
}
void phraseOrderAndCut() {
    auto p = pattern(); p.cells.fill(-1);
    std::fill(p.cells.begin(), p.cells.begin() + 4, 0);
    std::fill(p.cells.begin() + 8, p.cells.begin() + 16, 1);
    p.orderCount = 2; p.order[0] = 1; p.order[1] = 0;
    Fixture f; const auto out = f.run(2100, p);
    require(f.engine.getLatencySamples() == 320, "end-to-front minimum latency");
    near(out[0][400], 401, "first reordered phrase");
    near(out[0][680], 41, "second reordered phrase");
    near(out[0][880], 401, "repeat cut at output endpoint");
    near(out[0][1040], 1041, "new cycle uses new coherent pass");
    near(out[1][680], -82, "reorder stereo");
    auto cut = pattern(); std::fill(cut.cells.begin() + 12, cut.cells.end(), -1);
    Fixture g; const auto partial = g.run(1280, cut);
    near(partial[0][600], 121, "partial final phrase preserves local offset");
}
void unequalWindowsAndBlockSizes() {
    auto p = pattern(1, 2); std::fill(p.cells.begin() + 12, p.cells.end(), -1);
    Fixture a, b;
    const auto first = a.run(4000, p, 1);
    const auto second = b.run(4000, p, 251);
    require(first == second, "block size independence");
    near(first[0][700], 221, "long output repeats same source pass");
    near(first[0][1360], 1361, "long output next anchor");
    auto shortOutput = pattern(2, 1); shortOutput.cells.fill(-1);
    std::fill(shortOutput.cells.begin() + 28, shortOutput.cells.begin() + 32, 0);
    Fixture c; const auto shortResult = c.run(2000, shortOutput);
    near(shortResult[0][1200], 1201, "short output can read end of long input");
}
void fractionalTiming() {
    Fixture f(161.0); f.bpm = 123.0;
    const auto out = f.run(2500, pattern(), 13, true);
    const int delay = 0;
    require(f.engine.getLatencySamples() == delay, "fractional identity has zero latency");
    for (int i = delay; i < 2500; ++i)
        near(out[0][i], static_cast<float>(i - delay + 1), "fractional timing exact bypass delay");
    Fixture g(161.0); g.bpm = 123.0;
    const auto wet = g.run(2500, pattern(), 127);
    for (int i = delay + 10; i < 2500; ++i) {
        const double phase = std::fmod((i - delay) * 123.0 / (60.0 * 161.0), 4.0);
        if (phase > 0.02 && phase < 3.98)
            near(wet[0][i], static_cast<float>(i - delay + 1), "identity at fractional timing");
    }
    auto shifted = pattern(); shifted.cells.fill(-1);
    std::fill(shifted.cells.begin() + 4, shifted.cells.begin() + 8, 0);
    Fixture h(161.0); h.bpm = 123.0;
    const auto interpolated = h.run(900, shifted, 23);
    // First source starts one beat ahead: ceil(161*60/123) = 79.
    // At t=300 the third repetition reads 300-79-78.536585... .
    require(h.engine.getLatencySamples() == 79, "fractional forward lookahead uses ceil");
    near(interpolated[0][300], 143.4634146f, "fractional source interpolation");
    require(requiredLatencySamples(Pattern::makeDefault(), 44100, 123) == 0, "YYYBRR zero latency");
    require(requiredLatencySamples(shifted, 161, 123) == 79, "public latency calculator");
}
void loopSeekAndRestart() {
    Fixture f; const auto out = f.run(2300, pattern(), 73, true, true);
    for (int i = 0; i < 2300; ++i)
        near(out[0][i], static_cast<float>(i + 1), "expected DAW loop preserves history");
    auto ahead = pattern(); ahead.cells.fill(-1);
    std::fill(ahead.cells.begin() + 8, ahead.cells.begin() + 16, 0);
    const auto sought = f.run(200, ahead, 31, true, false, 20.0);
    for (float value : sought[0]) near(value, 0, "unexpected seek reprimes");
    const int previousLatency = f.engine.getLatencySamples();
    const auto stopped = f.run(20, ahead, 20, false, false, 20.0, false);
    for (float value : stopped[0]) near(value, 0, "stopped is muted");
    require(f.engine.getStatus() == EngineStatus::stopped, "stopped status");
    require(f.engine.getLatencySamples() == previousLatency, "stopped retains latency");
    const auto restarted = f.run(100, ahead, 19, true, false, 20.0);
    for (float value : restarted[0]) near(value, 0, "restart reprimes");
    Fixture tempo; tempo.run(900, ahead, 40, true); tempo.bpm = 120;
    const auto changed = tempo.run(100, ahead, 20, true);
    require(tempo.engine.getLatencySamples() == 160, "tempo latency changes");
    for (float value : changed[0]) near(value, 0, "tempo change reprimes");
    tempo.bpm = 60;
    const auto stoppedChange = tempo.run(20, ahead, 20, false, false, 0.0, false);
    require(tempo.engine.getLatencySamples() == 320, "stopped transport publishes revised latency");
    for (float value : stoppedChange[0]) near(value, 0, "stopped timing update stays muted");
}
void malformedAndEmpty() {
    Fixture f; auto empty = pattern(); empty.orderCount = 0;
    const auto out = f.run(1500, empty);
    for (float value : out[0]) near(value, 0, "empty order silence");
    Transport tr; tr.valid = true; tr.playing = true;
    float values[4] {1, 2, 3, 4}; float* ptrs[] {values};
    tr.bpm = std::nan("");
    f.engine.process(ptrs, 1, 4, tr, pattern());
    for (float value : values) require(std::isfinite(value) && value == 0, "malformed timing finite silence");
    require(f.engine.getStatus() == EngineStatus::unsupportedTiming, "invalid timing status");
    tr.bpm = 0.01; tr.ppq = 0;
    f.engine.process(ptrs, 1, 4, tr, pattern());
    require(f.engine.getStatus() == EngineStatus::unsupportedTiming, "history budget rejection");
    tr.bpm = 120; tr.valid = false;
    f.engine.process(ptrs, 1, 4, tr, pattern());
    require(f.engine.getStatus() == EngineStatus::waitingForTransport, "missing transport status");
    tr.valid = true; tr.ppq = std::numeric_limits<double>::infinity();
    f.engine.process(ptrs, 1, 4, tr, pattern());
    require(f.engine.getStatus() == EngineStatus::unsupportedTiming, "infinite PPQ rejection");
    tr.ppq = 0; tr.bpm = 60; tr.numerator = 7; tr.denominator = 3;
    f.engine.process(ptrs, 1, 4, tr, pattern());
    require(f.engine.getStatus() == EngineStatus::unsupportedTiming, "nonstandard denominator rejection");
    tr.numerator = tr.denominator = 4;
    auto badPattern = pattern(); badPattern.inputBars = std::numeric_limits<int>::max();
    f.engine.process(ptrs, 1, 4, tr, badPattern);
    require(f.engine.getStatus() == EngineStatus::unsupportedTiming, "malformed pattern indexing rejected");
    tr.looping = true; tr.loopStartPpq = tr.loopEndPpq = 0;
    f.engine.process(ptrs, 1, 4, tr, pattern());
    require(f.engine.getStatus() == EngineStatus::unsupportedTiming, "malformed loop rejected");
}
void editsWaveformAndMeters() {
    Fixture f; auto p = pattern(); f.run(1000, p);
    p.outputBars = 2;
    const auto edited = f.run(100, p);
    near(edited[0][5], 366, "output length edits preserve captured history");
    p.inputBars = 2; std::fill(p.cells.begin(), p.cells.begin() + 32, 0);
    const auto changed = f.run(100, p);
    near(changed[0][5], 1106, "identity input edits need only the current sample");
    require(f.engine.getLatencySamples() == 0, "identity input length keeps latency zero");
    std::array<float, waveformSize> peaks{}; f.engine.copyWaveform(peaks);
    require(std::any_of(peaks.begin(), peaks.end(), [](float v) { return v > 0; }), "waveform records live input");
    require(f.engine.getInputPhase() >= 0 && f.engine.getInputPhase() < 1, "input phase normalized");
    require(f.engine.getOutputPhase() >= 0 && f.engine.getOutputPhase() < 1, "output phase normalized");
    Engine meter; meter.prepare(160, 100, 1);
    float samples[100]{}; float* ptrs[] {samples};
    Transport tr; tr.valid = true; tr.playing = true; tr.bpm = 60;
    tr.numerator = 6; tr.denominator = 8;
    auto ahead = pattern(); ahead.cells.fill(-1);
    std::fill(ahead.cells.begin() + 8, ahead.cells.begin() + 16, 0);
    meter.process(ptrs, 1, 100, tr, ahead, true);
    require(meter.getLatencySamples() == 240, "compound meter uses quarter-note PPQ");
    require(meter.getStatus() == EngineStatus::filling, "truthful startup filling");
    tr.numerator = 3; tr.denominator = 4; tr.ppq = 100.0 / 160.0;
    meter.process(ptrs, 1, 100, tr, ahead, true);
    require(meter.getLatencySamples() == 240, "three quarter meter length");
    for (float value : samples) near(value, 0, "meter change reprimes despite identical duration");
}
void ringBudgetAndAllocation() {
    Fixture f(16); auto p = pattern(4, 8);
    const auto result = f.run(5000, p, 97, true);
    const int latency = 0;
    for (int i = latency; i < 5000; ++i)
        near(result[0][i], static_cast<float>(i - latency + 1), "ring wraps preserve delayed audio");
    // prepare owns all memory; callback paths include pattern edit, seek, invalid
    // timing and bypass while the global operator new guard is enabled.
    Engine engine; engine.prepare(160, 64, 2);
    float left[64]{}, right[64]{}; float* ptrs[] {left, right};
    Transport tr; tr.valid = true; tr.playing = true; tr.bpm = 60;
    p = pattern();
    watchAllocations = true;
    engine.process(ptrs, 2, 64, tr, p);
    tr.ppq = 64.0 / 160.0; p.outputBars = 2;
    engine.process(ptrs, 2, 64, tr, p);
    tr.ppq = 20;
    engine.process(ptrs, 2, 64, tr, p, true);
    tr.ppq += 64.0 / 160.0;
    p.orderCount = 3; p.order[0] = 8; p.order[1] = 0; p.order[2] = 12;
    engine.process(ptrs, 2, 64, tr, p);
    tr.ppq += 64.0 / 160.0;
    p.cells.fill(-1); p.orderCount = 1; p.order[0] = 12;
    engine.process(ptrs, 2, 64, tr, p);
    tr.bpm = std::numeric_limits<double>::infinity();
    engine.process(ptrs, 2, 64, tr, p);
    watchAllocations = false;
    require(callbackAllocations == 0, "audio callbacks must not allocate");
    // Finite output even when the upstream effect emits NaN/infinity.
    tr.bpm = 60; tr.ppq = 0;
    for (int pass = 0; pass < 20; ++pass) {
        std::fill_n(left, 64, std::numeric_limits<float>::infinity());
        std::fill_n(right, 64, std::nanf(""));
        engine.process(ptrs, 2, 64, tr, p, true);
        tr.ppq += 64.0 / 160.0;
        for (int i = 0; i < 64; ++i)
            require(std::isfinite(left[i]) && std::isfinite(right[i]), "nonfinite input sanitized");
    }
}
void completedOutputCapture() {
    Fixture f; const auto p = pattern(); OutputSnapshot output;
    f.run(639, p);
    require(!f.engine.hasOutputReady() && !f.engine.copyLatestOutput(output), "partial loop cannot export");
    const auto rendered = f.run(1282, p);
    require(f.engine.hasOutputReady() && f.engine.copyLatestOutput(output), "complete loop export available");
    require(output.channels.size() == 2 && output.channels[0].size() == 640, "capture full stereo loop duration");
    require(output.sampleRate == 160 && output.bpm == 60 && output.bars == 1 && output.serial > 0,
            "capture metadata");
    for (int i = 0; i < 640; ++i) {
        near(output.channels[0][i], rendered[0][641 + i], "latest complete wet cycle samples");
        near(output.channels[1][i], -2 * output.channels[0][i], "capture stereo coherence");
    }
    const auto serial = output.serial;
    f.run(20, p, 20, false, false, 0, false);
    require(f.engine.copyLatestOutput(output) && output.serial == serial, "stop retains last completed loop");
    auto edited = p; edited.outputBars = 2;
    f.run(20, edited, 20, false, false, 0, false);
    require(!f.engine.hasOutputReady(), "stopped pattern edit invalidates export");
    f.run(2600, edited);
    require(f.engine.copyLatestOutput(output) && output.channels[0].size() == 1280,
            "changed loop length captured completely");
    near(output.channels[0][20], 2581, "long captured loop starts from its own pass");
    near(output.channels[0][660], 2581, "long captured loop repeats the same input pass");
    f.engine.invalidateOutput();
    require(!f.engine.hasOutputReady() && !f.engine.copyLatestOutput(output), "explicit invalidation");
    f.run(10, edited);
    require(!f.engine.hasOutputReady(), "invalidated partial capture cannot publish");

    Fixture mid; mid.t = 100;
    mid.run(550, p);
    require(!mid.engine.hasOutputReady(), "mid-cycle startup cannot publish partial first pass");
    mid.run(631, p);
    require(mid.engine.copyLatestOutput(output), "first full pass after mid-cycle startup exports");
    near(output.channels[0][20], 661, "mid-cycle history never substituted from prior pass");
    mid.run(5, p, 5, false, false, 20);
    require(!mid.engine.hasOutputReady(), "seek invalidates capture immediately");

    Fixture longLoop; auto longPattern = pattern(1, 8); longLoop.bpm = 30;
    longLoop.run(11000, longPattern, 97);
    require(!longLoop.engine.hasOutputReady(), "export longer than 32 seconds unavailable");

    Fixture exact; exact.run(640, p);
    require(!exact.engine.hasOutputReady(), "playing endpoint waits for an observed wrap or stop");
    exact.run(1, p, 1, false, false, 0, false);
    require(exact.engine.copyLatestOutput(output), "stop exactly at loop boundary finalizes completed cycle");
    require(output.channels[0].size() == 640, "exact endpoint stop preserves every emitted frame");
    const auto stoppedSerial = output.serial;
    exact.engine.finishOutputOnStop();
    require(exact.engine.copyLatestOutput(output) && output.serial == stoppedSerial,
            "repeated stop finalization retains the same published cycle");

    Fixture partialStop; partialStop.run(320, p);
    partialStop.run(1, p, 1, false, false, 0, false);
    require(!partialStop.engine.hasOutputReady(), "stop cannot publish a partial first loop");

    Fixture released; released.run(640, p);
    released.engine.finishOutputOnStop();
    require(released.engine.copyLatestOutput(output) && output.channels[0].size() == 640,
            "host release without a stop callback finalizes a complete endpoint");

    for (const int frames : {638, 639}) {
        Fixture nearEndStop; nearEndStop.run(frames, p);
        nearEndStop.run(1, p, 1, false, false, 0, false);
        require(!nearEndStop.engine.hasOutputReady(), "stop one or two frames before endpoint cannot export");

        Fixture nearEndRelease; nearEndRelease.run(frames, p);
        nearEndRelease.engine.finishOutputOnStop();
        require(!nearEndRelease.engine.hasOutputReady(), "release one or two frames before endpoint cannot export");
    }
}

void startupReadinessAndCaptureInvalidation() {
    Fixture original;
    original.run(8, Pattern::makeDefault(), 8);
    require(original.engine.getStatus() == EngineStatus::ready, "unused source tail does not delay default readiness");
    Fixture identity; identity.t = 100;
    const auto immediate = identity.run(8, pattern(), 8);
    near(immediate[0][5], 106, "mid-cycle identity reads current pass without full preceding window");
    require(identity.engine.getStatus() == EngineStatus::ready, "needed current sample is ready");

    // Repeated start phrase at a mid-cycle launch needs earlier samples from the
    // same pass; current input from a different phrase cannot replace them.
    auto repeated = pattern();
    std::fill(repeated.cells.begin() + 4, repeated.cells.end(), -1);
    Fixture missing; missing.t = 200;
    const auto unavailable = missing.run(100, repeated, 25);
    for (float value : unavailable[0]) near(value, 0, "mid-cycle missing history remains silence");
    require(missing.engine.getStatus() == EngineStatus::filling, "actual missing phrase reports filling");
    require(!missing.engine.hasOutputReady(), "incomplete historical startup cannot export");

    Fixture bypass; auto ahead = pattern(); ahead.cells.fill(-1);
    std::fill(ahead.cells.begin() + 12, ahead.cells.begin() + 16, 0);
    const auto dry = bypass.run(1100, ahead, 41, true);
    require(bypass.engine.getLatencySamples() == 480, "bypass matches current pattern latency");
    near(dry[0][479], 0, "lookahead bypass startup");
    near(dry[0][500], 21, "lookahead bypass delayed ramp");
    require(!bypass.engine.hasOutputReady(), "bypassed cycles cannot become wet export");

    Fixture changed; const auto p = pattern(); changed.run(641, p);
    changed.bpm = 120;
    changed.run(1, p, 1);
    require(!changed.engine.hasOutputReady(), "tempo change invalidates completed loop");

    Fixture stopped; stopped.run(641, p);
    Transport paused; paused.valid = true; paused.playing = false; paused.bpm = 60;
    paused.ppq = 641.0 / 160;
    float silence[1]{}; float* pausedChannels[]{silence};
    stopped.engine.process(pausedChannels, 1, 1, paused, p);
    require(stopped.engine.hasOutputReady(), "plain stop retains completed wet cycle");
    stopped.engine.process(pausedChannels, 1, 1, paused, p);
    require(stopped.engine.hasOutputReady(), "stationary stopped callbacks retain completed wet cycle");
    paused.ppq = 20;
    stopped.engine.process(pausedChannels, 1, 1, paused, p);
    require(!stopped.engine.hasOutputReady(), "seek while stopped invalidates completed wet cycle");

    Fixture looped; looped.run(1921, p, 73, false, true);
    OutputSnapshot snapshot;
    require(looped.engine.copyLatestOutput(snapshot), "expected host loop permits complete export");
    near(snapshot.channels[0][20], 1301, "loop export retains current coherent pass");
    Fixture fractional(161); fractional.bpm = 123;
    const auto reference = fractional.run(2000, p, 19);
    require(fractional.engine.copyLatestOutput(snapshot), "fractional duration completes export");
    const double period = 161.0 * 60 / 123 * 4;
    require(std::abs(static_cast<double>(snapshot.channels[0].size()) - period) < 1,
            "fractional capture duration is within one sample");
    // Last completed cycle is cycle 5: samples ceil(5*period)..ceil(6*period)-1.
    const int start = static_cast<int>(std::ceil(5 * period));
    for (std::size_t i = 0; i < snapshot.channels[0].size(); ++i)
        near(snapshot.channels[0][i], reference[0][start + i], "fractional capture samples match emitted cycle");
}

void concurrentCaptureAndAllocation() {
    Fixture f; const auto p = pattern(); f.run(641, p);
    std::atomic<bool> done{false}, readerStarted{false};
    std::atomic<int> copied{0}; std::atomic<bool> coherent{true};
    std::thread reader([&] {
        OutputSnapshot snapshot;
        while (!done.load(std::memory_order_relaxed)) {
            if (!f.engine.copyLatestOutput(snapshot)) continue;
            ++copied;
            if (snapshot.channels.size() != 2 || snapshot.channels[0].size() != 640) coherent = false;
            for (std::size_t i = 0; i < snapshot.channels[0].size(); ++i)
                if (snapshot.channels[1][i] != -2 * snapshot.channels[0][i]) coherent = false;
            // At every non-faded identity sample, the original pass has a unit ramp.
            for (int i = 2; i < 638; ++i)
                if (snapshot.channels[0][i] - snapshot.channels[0][i - 1] != 1) coherent = false;
            if (copied.load() % 17 == 0) f.engine.invalidateOutput();
            readerStarted = true;
        }
    });
    while (!readerStarted.load()) std::this_thread::yield();
    // A real producer and reader overlap across hundreds of publications.
    f.run(320000, p, 31);
    done = true; reader.join();
    require(copied > 0 && coherent, "concurrent export copies cannot tear or mix cycles");

    Engine engine; engine.prepare(160, 64, 2);
    float left[64]{}, right[64]{}; float* ptrs[] {left, right};
    Transport tr; tr.valid = true; tr.playing = true; tr.bpm = 60;
    callbackAllocations = 0; watchAllocations = true;
    for (int i = 0; i < 100; ++i) {
        engine.process(ptrs, 2, 64, tr, p); tr.ppq += .4;
    }
    engine.invalidateOutput();
    watchAllocations = false;
    require(callbackAllocations == 0, "capture and publication must not allocate in callback");
}

void roundedHostLoopCapture() {
    // A musical two-bar loop is 132413.793... samples at 174 BPM/48 kHz.
    // A DAW loops at its integer sample boundary and reports the left locator
    // again, rather than carrying the fractional remainder across each wrap.
    constexpr double sampleRate = 48000.0, bpm = 174.0;
    constexpr int hostLoopSamples = 132414;
    constexpr int latency = 41380; // source starts 2.5 beats after the output.
    auto p = pattern(2, 2); p.cells.fill(-1);
    std::fill(p.cells.begin() + 10, p.cells.begin() + 14, 0);
    Engine engine; engine.prepare(sampleRate, 511, 2);
    constexpr int count = 4 * hostLoopSamples + latency + 1;
    std::vector<float> left(count), right(count);
    for (int offset = 0; offset < count;) {
        const int loopOffset = offset % hostLoopSamples;
        const int block = std::min({511, count - offset, hostLoopSamples - loopOffset});
        for (int i = 0; i < block; ++i) {
            left[offset + i] = static_cast<float>((offset + i) % 17003) / 17003.0f;
            right[offset + i] = -2.0f * left[offset + i];
        }
        Transport tr; tr.valid = true; tr.playing = true; tr.bpm = bpm;
        tr.looping = true; tr.loopStartPpq = 16; tr.loopEndPpq = 24;
        tr.ppq = 16 + loopOffset * bpm / (60.0 * sampleRate);
        float* channels[]{left.data() + offset, right.data() + offset};
        engine.process(channels, 2, block, tr, p);
        offset += block;
    }
    require(engine.getLatencySamples() == latency, "rounded-loop fixture forward latency");
    OutputSnapshot output;
    require(engine.copyLatestOutput(output), "integer-sample host loop must complete delayed WAV capture");
    require(output.channels[0].size() == hostLoopSamples,
            "rounded host loop capture preserves actual emitted duration");
    const int start = 3 * hostLoopSamples + latency;
    for (int i = 0; i < hostLoopSamples; ++i) {
        near(output.channels[0][i], left[start + i], "rounded-loop export matches actual wet samples");
        near(output.channels[1][i], right[start + i], "rounded-loop export keeps stereo provenance");
    }

    // The shorter integer boundary must also close a valid cycle. Its final
    // sample does not reach the ideal fractional endpoint until the host wraps.
    auto identity = pattern(2, 2);
    Engine shortLoop; shortLoop.prepare(sampleRate, 512, 1);
    constexpr int shorterLoop = 132413;
    std::vector<float> samples(512);
    for (int offset = 0; offset < 2 * shorterLoop + 1;) {
        const int loopOffset = offset % shorterLoop;
        const int block = std::min({512, 2 * shorterLoop + 1 - offset, shorterLoop - loopOffset});
        std::fill(samples.begin(), samples.end(), 0.25f);
        Transport tr; tr.valid = true; tr.playing = true; tr.bpm = bpm;
        tr.looping = true; tr.loopStartPpq = 0; tr.loopEndPpq = 8;
        tr.ppq = loopOffset * bpm / (60.0 * sampleRate);
        float* channels[]{samples.data()};
        shortLoop.process(channels, 1, block, tr, identity);
        offset += block;
    }
    require(shortLoop.copyLatestOutput(output), "rounded-down host endpoint publishes its completed loop");
    require(output.channels[0].size() == shorterLoop, "short host loop includes every emitted cycle sample");
}

void roundedBlockPositionsCapture() {
    constexpr double sampleRate = 44100, bpm = 174;
    auto p = pattern(2, 2); p.cells.fill(-1);
    std::fill(p.cells.begin() + 10, p.cells.begin() + 14, 0);
    Engine engine; engine.prepare(sampleRate, 257, 2);
    constexpr int count = 650000;
    std::vector<float> left(count), right(count);
    for (int offset = 0; offset < count;) {
        const int block = std::min(257, count - offset);
        for (int i = 0; i < block; ++i) {
            left[offset + i] = static_cast<float>(offset + i + 1) / count;
            right[offset + i] = -2 * left[offset + i];
        }
        Transport tr; tr.valid = true; tr.playing = true; tr.bpm = bpm;
        // Start partway through a cycle far from project zero, and model the
        // small independent PPQ rounding at each host callback boundary.
        const double exactPpq = 1281.375 + offset * bpm / (60 * sampleRate);
        tr.ppq = std::round(exactPpq * 1.0e7) / 1.0e7;
        float* channels[]{left.data() + offset, right.data() + offset};
        engine.process(channels, 2, block, tr, p);
        offset += block;
    }
    OutputSnapshot output;
    require(engine.copyLatestOutput(output), "rounded callback PPQ permits complete WAV capture");
    require(output.channels[0].size() == 121655 || output.channels[0].size() == 121656,
            "rounded callback capture has a complete two-bar duration");
    const auto first = std::search(left.begin(), left.end(), output.channels[0].begin(), output.channels[0].end());
    require(first != left.end(), "rounded callback export is one continuous interval of emitted wet audio");
    const auto start = first - left.begin();
    require(std::equal(output.channels[1].begin(), output.channels[1].end(), right.begin() + start),
            "rounded callback export keeps the same interval in both channels");

    // A real seek still invalidates an already completed cycle immediately.
    float sample[1]{0.5f}; float* channels[]{sample};
    Transport seek; seek.valid = true; seek.playing = true; seek.bpm = bpm; seek.ppq = 2000;
    engine.process(channels, 1, 1, seek, p);
    require(!engine.hasOutputReady(), "PPQ rounding tolerance must not preserve export across a real seek");
}

void repeatedEndpointCapture() {
    const auto p = pattern();
    Engine engine; engine.prepare(160, 640, 1);
    std::vector<float> samples(640);
    for (int i = 0; i < 640; ++i) samples[i] = static_cast<float>(i + 1);
    Transport tr; tr.valid = true; tr.playing = true; tr.bpm = 60;
    float* channels[]{samples.data()};
    engine.process(channels, 1, 640, tr, p);
    // The next host callback repeats the nominal final sample's position.
    // This one-sample PPQ correction is accepted transport continuity, so its
    // wet frame still belongs to the cycle until the actual observed wrap.
    samples[0] = 641;
    tr.ppq = 639.0 / 160.0;
    engine.process(channels, 1, 1, tr, p);
    samples[0] = 642;
    tr.ppq = 4;
    engine.process(channels, 1, 1, tr, p);
    OutputSnapshot output;
    require(engine.copyLatestOutput(output), "repeated endpoint completes at the observed wrap");
    require(output.channels[0].size() == 641, "capture includes accepted repeated endpoint wet frame");
    near(output.channels[0].back(), 641, "repeated endpoint exports the actual final emitted sample");
}

void backwardsRoundingWithinCycleCapture() {
    const auto p = pattern();
    Engine engine; engine.prepare(160, 515, 1);
    std::vector<float> samples(515);
    for (int i = 0; i < 128; ++i) samples[i] = static_cast<float>(i + 1);
    Transport tr; tr.valid = true; tr.playing = true; tr.bpm = 60;
    float* channels[]{samples.data()};
    engine.process(channels, 1, 128, tr, p);
    // Last phase was 127; the host corrects the next callback to 126.5.
    // Its 1.5-sample correction is within transport tolerance and is not a
    // cycle wrap. Preserve all emitted frames through the real wrap at 640.
    for (int i = 0; i < 515; ++i) samples[i] = static_cast<float>(129 + i);
    tr.ppq = 126.5 / 160.0;
    engine.process(channels, 1, 515, tr, p);
    OutputSnapshot output;
    require(engine.copyLatestOutput(output), "accepted backwards PPQ correction keeps current capture");
    require(output.channels[0].size() == 642, "backwards correction capture keeps all emitted wet frames");
    near(output.channels[0].back(), 642, "backwards correction preserves unmodified chronological endpoint");
    tr.ppq = 20;
    engine.process(channels, 1, 1, tr, p);
    require(!engine.hasOutputReady(), "real seek still invalidates after accepted backwards rounding");
}

void queuedFractionalEndpoint() {
    const auto original = pattern(2, 2);
    auto changed = original;
    std::fill(changed.cells.begin() + 12, changed.cells.end(), -1);
    Fixture f(48000); f.bpm = 174;
    f.run(1000, original);
    // 8 beats at174BPM is132413.793... samples. A block ends immediately
    // before the first integer sample at or after that fractional endpoint.
    f.run(131414, changed, 131414, false, false, 0.0, true, true);
    require(f.engine.isPatternPending(), "fractional boundary remains queued until its first sample");
    f.run(1, changed, 1, false, false, 0.0, true, true);
    require(!f.engine.isPatternPending(), "fractional boundary crossed between callbacks activates queued preset");
}

void queuedPatternBoundaries() {
    const auto original = pattern();
    auto changed = original;
    std::fill(changed.cells.begin() + 8, changed.cells.begin() + 16, 1);
    changed.orderCount = 2; changed.order[0] = 1; changed.order[1] = 0;
    Fixture f;
    f.run(700, original);
    const auto waiting = f.run(100, changed, 100, false, false, 0.0, true, true);
    require(f.engine.isPatternPending(), "midcycle preset queues");
    near(waiting[0][99], 800, "queued preset preserves old audio");
    require(std::abs(f.engine.getPendingBeatsRemaining() - 3.0) < 0.000001, "queue countdown reaches next old output boundary");
    const auto switched = f.run(550, changed, 550, false, false, 0.0, true, true);
    require(!f.engine.isPatternPending() && f.engine.getLatencySamples() == 320, "midblock boundary activates new latency");
    near(switched[0][479], 1280, "sample immediately before switch belongs to old preset");
    near(switched[0][480], 0, "switch begins fixed edit fade exactly at boundary");
    near(switched[0][481], 642, "sample after switch reads new arrangement from retained history");

    Fixture hostLoop;
    auto longOutput = original; longOutput.outputBars = 8;
    auto longChanged = changed; longChanged.outputBars = 8;
    hostLoop.run(700, longOutput, 37, false, true);
    hostLoop.run(100, longChanged, 100, false, true, 0.0, true, true);
    require(hostLoop.engine.isPatternPending(), "long output queues within shorter host cycle");
    const auto hostSwitched = hostLoop.run(550, longChanged, 550, false, true, 0.0, true, true);
    require(!hostLoop.engine.isPatternPending(), "short host wrap releases long output preset queue");
    near(hostSwitched[0][479], 1280, "host wrap leaves preceding audio unchanged");

    Fixture seek;
    seek.run(700, original);
    seek.run(10, changed, 10, false, false, 20.0, true, true);
    require(!seek.engine.isPatternPending(), "actual seek applies selected preset instead of stale pending wait");

    Fixture rounded;
    rounded.run(100, original);
    rounded.run(10, changed, 10, false, false, 0.0, true, true);
    rounded.run(1, changed, 1, false, false, -1.5 / 160.0, true, true);
    require(rounded.engine.isPatternPending(), "accepted negative phase correction does not activate queue midcycle");
}

void sourceWindowKeepsChronologicalHistory() {
    auto original = pattern();
    std::fill(original.cells.begin() + 8, original.cells.end(), -1);
    auto wider = original; wider.inputBars = 2;
    Fixture f;
    f.run(1100, original);
    const auto after = f.run(20, wider, 20);
    require(f.engine.getStatus() == EngineStatus::ready, "source-window change preserves usable history");
    near(after[0][1], 782, "changed source window reads samples from before the edit");
}
void restOnlyCapturesSilenceImmediately() {
    auto p = pattern(); p.cells.fill(-1); p.order.fill(-1); p.order[0] = 12;
    Fixture f;
    const auto audio = f.run(641, p);
    require(f.engine.getStatus() == EngineStatus::ready, "all-rest arrangement is ready without source history");
    require(f.engine.getLatencySamples() == 0, "all-rest arrangement needs no lookahead");
    for (const auto& channel : audio)
        for (float value : channel) require(value == 0.0f, "rest emits exact silence with nonzero input");
    OutputSnapshot output;
    require(f.engine.copyLatestOutput(output), "first complete all-rest loop is exportable");
    require(output.channels.size() == 2 && output.channels[0].size() == 640,
            "all-rest export retains complete stereo duration");
    for (const auto& channel : output.channels)
        for (float value : channel) require(value == 0.0f, "all-rest export contains actual zero output");
}
void restsKeepSourceHistoryAndBoundaries() {
    auto p = pattern(); p.cells.fill(-1); p.order.fill(-1);
    std::fill(p.cells.begin(), p.cells.begin() + 4, 0);
    p.orderCount = 3; p.order[0] = 10; p.order[1] = 0; p.order[2] = 11;
    Fixture f, other;
    const auto audio = f.run(641, p, 1);
    require(audio == other.run(641, p, 251), "rest transitions are independent of callback block size");
    require(f.engine.getLatencySamples() == 0, "leading rest supplies time for earlier source phrase");
    for (int i = 0; i < 160; ++i) require(audio[0][i] == 0.0f, "leading beat rest stays exactly silent");
    near(audio[0][160], 0, "audio after rest begins its existing anti-click fade");
    near(audio[0][161], 2, "audio recorded while resting remains available afterwards");
    near(audio[1][161], -4, "recording through a rest retains independent stereo input");
    near(audio[0][319], 160, "source phrase reaches its last frame before rest");
    for (int i = 320; i < 640; ++i) require(audio[0][i] == 0.0f, "following rest never leaks preceding source audio");
    OutputSnapshot output;
    require(f.engine.copyLatestOutput(output) && output.channels[0].size() == 640, "mixed rest loop exports immediately");
    require(std::equal(output.channels[0].begin(), output.channels[0].end(), audio[0].begin()),
            "mixed rest WAV captures exactly emitted output");
    p.cells.fill(-1); std::fill(p.cells.begin() + 8, p.cells.begin() + 12, 0);
    require(requiredLatencySamples(p, 160, 60) == 160, "rest duration reduces only actual forward source lookahead");
    Fixture bypass;
    const auto dry = bypass.run(641, p, 73, true);
    near(dry[0][321], 162, "bypass passes delayed input even inside a rest");
}
void restsDoNotNeedHistoryOrLeakAfterSeek() {
    auto p = pattern(); p.cells.fill(-1); p.order.fill(-1);
    std::fill(p.cells.begin() + 4, p.cells.begin() + 8, 0);
    p.orderCount = 2; p.order[0] = 9; p.order[1] = 0;
    Fixture f;
    // Start at the compensated output origin while delayed dry history is
    // unavailable. The first eighty output frames are deliberately silent,
    // and the later phrase's source arrives in time; this first loop is valid.
    const auto output = f.run(641, p, 97, false, false, 0.5);
    require(f.engine.getLatencySamples() == 80, "mixed rest fixture has half-beat lookahead");
    for (int i = 0; i < 80; ++i) require(output[0][i] == 0.0f, "rest is exact silence before delayed history exists");
    near(output[0][81], 82, "source following initial rest uses available current input");
    OutputSnapshot snapshot;
    require(f.engine.copyLatestOutput(snapshot) && snapshot.channels[0].size() == 640,
            "missing history during rest does not invalidate complete output export");
    p.orderCount = 1; p.order[0] = 12; p.cells.fill(-1);
    const auto sought = f.run(640, p, 53, false, false, 20.0);
    for (const auto& channel : sought)
        for (float value : channel) require(value == 0.0f, "seek into rest cannot expose previous ring contents");
    require(!f.engine.hasOutputReady(), "seek invalidates previous capture and partial rest loop cannot export");
}
void occurrenceLengthsMapThreeTwoThreeBeats() {
    auto p = pattern(1, 2); p.cells.fill(-1);
    std::fill(p.cells.begin(), p.cells.begin() + 12, 0);
    p.orderCount = 3; p.order[0] = p.order[1] = p.order[2] = 0;
    require(p.setOrderLength(1, 8), "trim only middle occurrence to two beats");
    Fixture oneSample, largeBlocks;
    const auto output = oneSample.run(1281, p, 1);
    require(output == largeBlocks.run(1281, p, 251), "3+2+3 mapping is callback-size invariant");
    require(oneSample.engine.getLatencySamples() == 0, "first-three-beat source needs no lookahead");
    OutputSnapshot exported;
    require(oneSample.engine.copyLatestOutput(exported) && exported.channels[0].size() == 1280,
            "exact eight-beat 3+2+3 output is available for export");
    // At 160 Hz and 60 BPM, three beats = 480 frames and two beats = 320.
    // Each of the three occurrences restarts at source frame zero, un-stretched.
    for (int i = 0; i < 1280; ++i) {
        const int source = i < 480 ? i : (i < 800 ? i - 480 : i - 800);
        // Prejoin smoothing keeps each incoming source attack intact. At this
        // deliberately low sample rate the default 2 ms is less than a frame.
        const float expected = static_cast<float>(source + 1);
        near(output[0][i], expected, "literal 3+2+3 PCM mapping");
        near(output[1][i], -2.0f * expected, "literal 3+2+3 stereo mapping");
        near(exported.channels[0][i], expected, "export contains literal 3+2+3 PCM mapping");
        near(exported.channels[1][i], -2.0f * expected, "export contains literal trimmed stereo PCM");
    }
    require(p.region(0).end == 12, "trimmed playback leaves all three source beats available");
}
void trimmedOccurrencesUseShiftedSourceStart() {
    auto p = pattern(2, 2); p.cells.fill(-1);
    std::fill(p.cells.begin() + 8, p.cells.begin() + 20, 0);
    p.orderCount = 3; p.order[0] = p.order[1] = p.order[2] = 0;
    require(p.setOrderLength(1, 8), "shifted source with middle trim");
    Fixture f;
    const auto output = f.run(1601, p, 73);
    require(requiredLatencySamples(p, 160, 60) == 320 && f.engine.getLatencySamples() == 320,
            "trim preserves forward lookahead for shifted source start");
    near(output[0][321], 322, "Full occurrence starts from shifted source beginning");
    near(output[0][799], 800, "first Full occurrence preserves all three beats");
    near(output[0][801], 322, "trimmed occurrence restarts at shifted source beginning");
    near(output[0][1119], 640, "trim ends after two beats at original playback speed");
    near(output[0][1121], 322, "third Full occurrence restarts at shifted source beginning");
    near(output[0][1599], 800, "third Full occurrence still reads complete source region");
    OutputSnapshot exported;
    require(f.engine.copyLatestOutput(exported) && exported.channels[0].size() == 1280,
            "shifted trimmed complete loop exports after latency");
    require(std::equal(exported.channels[0].begin(), exported.channels[0].end(), output[0].begin() + 320),
            "shifted capture equals complete compensated wet output");
    auto lookahead = pattern(); lookahead.cells.fill(-1);
    std::fill(lookahead.cells.begin(), lookahead.cells.begin() + 8, 0);
    std::fill(lookahead.cells.begin() + 12, lookahead.cells.begin() + 16, 1);
    lookahead.orderCount = 2; lookahead.order[0] = 0; lookahead.order[1] = 1;
    require(requiredLatencySamples(lookahead, 160, 60) == 160, "Full predecessor supplies two beats before later source");
    require(lookahead.setOrderLength(0, 2) && requiredLatencySamples(lookahead, 160, 60) == 400,
            "trimmed predecessor updates later-source lookahead using effective duration");
}
void invalidOccurrenceLengthsMuteSafely() {
    for (const int cap : {-1, 65, std::numeric_limits<int>::max()}) {
        auto p = pattern(); p.orderLengths[0] = cap;
        Fixture f;
        const auto output = f.run(3, p);
        require(f.engine.getStatus() == EngineStatus::unsupportedTiming && f.engine.getLatencySamples() == 0,
                "engine rejects malformed active cap before rendering");
        for (const auto value : output[0]) require(value == 0, "invalid cap cannot read source memory");
    }
    for (const bool inactive : {false, true}) {
        auto p = pattern();
        if (inactive) p.orderLengths[127] = 1;
        else { p.order[0] = restToken(4); p.orderLengths[0] = 1; }
        Fixture f; f.run(3, p);
        require(f.engine.getStatus() == EngineStatus::unsupportedTiming,
                "engine rejects rest and inactive caps before capture");
        require(!f.engine.hasOutputReady(), "invalid occurrence cannot become an exportable loop");
    }
}
void trimmedPlaybackDoesNotAllocate() {
    auto p = pattern(1, 2); p.cells.fill(-1);
    std::fill(p.cells.begin(), p.cells.begin() + 12, 0);
    p.orderCount = 3; p.order[0] = p.order[1] = p.order[2] = 0;
    require(p.setOrderLength(1, 8), "allocation fixture trim");
    Engine engine; engine.prepare(160, 64, 2);
    float left[64]{}, right[64]{}; float* buffers[] {left, right};
    Transport tr; tr.valid = true; tr.playing = true; tr.bpm = 60;
    callbackAllocations = 0;
    watchAllocations = true;
    for (int block = 0; block < 60; ++block) {
        tr.ppq = block * 64.0 / 160.0;
        if (block == 10) p.orderLengths[1] = 4;
        if (block == 30) p.orderLengths[1] = 0;
        engine.process(buffers, 2, 64, tr, p, false, block < 30);
    }
    watchAllocations = false;
    require(callbackAllocations == 0, "trimmed render, queued edits and capture allocate no callback memory");
}

struct SignalFixture {
    Engine engine;
    int time = 0;
    double rate = 8000.0;
    explicit SignalFixture(double sampleRate = 8000) : rate(sampleRate) { engine.prepare(rate, 511, 2); }
    std::vector<std::vector<float>> run(int count, const Pattern& p,
        const std::function<float(int, int)>& signal, int block = 127, double shift = 0.0) {
        std::vector<std::vector<float>> result(2, std::vector<float>(count));
        for (int offset = 0; offset < count;) {
            const int n = std::min(block, count - offset);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i) result[ch][offset + i] = signal(time + i, ch);
            float* channels[]{result[0].data() + offset, result[1].data() + offset};
            Transport tr; tr.valid = tr.playing = true; tr.bpm = 120;
            tr.ppq = 2.0 * time / rate + shift;
            engine.process(channels, 2, n, tr, p);
            time += n; offset += n;
        }
        return result;
    }
};
Pattern timingPattern() {
    auto p = pattern(); p.cells.fill(-1);
    std::fill(p.cells.begin() + 4, p.cells.begin() + 8, 0);
    p.smoothTenths = 0;
    return p;
}
float drum(int time, int onset) {
    const int age = time - onset;
    return age >= 0 && age < 400 ? 0.8f * std::exp(-static_cast<float>(age) / 200.0f) : 0.0f;
}
void smoothPreservesContinuousAudio() {
    auto p = pattern(); p.smoothTenths = 100;
    std::fill(p.cells.begin() + 8, p.cells.begin() + 16, 1);
    p.orderCount = 2; p.order[0] = 0; p.order[1] = 1;
    SignalFixture f;
    const auto audio = f.run(33000, p, [](int i, int ch) {
        return (0.3f + 0.2f * std::sin(i * 0.02f)) * (ch == 0 ? 1 : -0.5f);
    });
    require(f.engine.getLatencySamples() == 0, "Smooth alone adds no latency to chronological identity");
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 33000; ++i)
            near(audio[ch][i], (0.3f + 0.2f * std::sin(i * 0.02f)) * (ch == 0 ? 1 : -0.5f),
                 "continuous colour boundaries and output wraps remain unmodified");
}
void smoothCrossfadesJumpsAndKeepsRests() {
    auto p = timingPattern();
    auto signal = [](int i, int ch) { return (i % 16000 < 5000 ? 0.8f : -0.8f) * (ch == 0 ? 1 : -0.5f); };
    SignalFixture hard;
    const auto original = hard.run(25000, p, signal);
    p.smoothTenths = 100;
    SignalFixture smooth;
    const auto softened = smooth.run(25000, p, signal);
    require(std::abs(original[0][8000] - original[0][7999]) > 1.5f, "fixture has actual source jump");
    for (int i = 7920; i <= 8000; ++i) {
        require(std::abs(softened[0][i] - softened[0][i - 1]) < 0.03f, "10 ms Smooth suppresses edit discontinuity");
        near(softened[1][i], -0.5f * softened[0][i], "crossfade shares coefficients across stereo");
    }
    near(softened[0][8000], original[0][8000], "incoming onset remains untouched at source jump");
    auto rest = p; rest.orderCount = 2; rest.order[0] = 0; rest.order[1] = restToken(4);
    SignalFixture silence;
    const auto muted = silence.run(22000, rest, [](int, int) { return 0.75f; });
    const int delay = silence.engine.getLatencySamples();
    for (int i = delay + 4000; i < delay + 8000; ++i)
        require(muted[0][i] == 0.0f && muted[1][i] == 0.0f, "crossfade never extends into a rest");
    require(muted[0][delay + 3999] < 0.02f, "entry into rest fades within source phrase");
    require(muted[0][delay + 8000] == 0.0f, "exit from rest starts cleanly at exact boundary");
}
void tightenFindsEarlyLateAndPartialCorrections() {
    for (const int onset : {3920, 4080}) {
        for (const int amount : {0, 50, 100}) {
            auto p = timingPattern(); p.tightenPercent = amount;
            SignalFixture f;
            const auto audio = f.run(8500, p, [onset](int i, int ch) { return drum(i, onset) * (ch == 0 ? 1 : -0.25f); });
            const auto timing = f.engine.getSourceTiming();
            const float expectedMs = (onset - 4000) / 8.0f * amount / 100.0f;
            near(timing.offsetsMs[0], expectedMs, "Tighten reports scaled early or late onset offset");
            require(timing.status[0] == (amount == 0 ? TimingStatus::off : TimingStatus::detected),
                    "clear sustained attack receives correct detection status");
            const int delay = f.engine.getLatencySamples();
            require(delay == 4000 + (amount == 0 ? 0 : 360), "automatic lookahead is exactly 45 ms beyond source reorder");
            if (amount == 100) near(audio[0][delay], 0.8f, "full Tighten aligns actual attack with phrase onset");
            const int residual = onset - 4000 - static_cast<int>(expectedMs * 8);
            if (residual > 0) {
                near(audio[0][delay + residual - 1], 0, "partial Tighten preserves the remaining late groove");
                near(audio[0][delay + residual], 0.8f, "partial Tighten moves actual attack halfway");
            }
            for (int i = delay; i < 8500; ++i) near(audio[1][i], -.25f * audio[0][i], "timing shifts keep stereo linked");
            for (int i = 0; i < 120; ++i)
                near(audio[0][delay + 4000 + i], audio[0][delay + i], "repeats use one stable source-occurrence decision");
        }
    }
}
void tightenRejectsCrackleSteadyAndAmbiguousAudio() {
    auto p = timingPattern(); p.tightenPercent = 100;
    const std::array<std::function<float(int, int)>, 5> signals{{
        [](int, int) { return 0.0f; },
        [](int, int) { return 0.3f; },
        [](int i, int) { return i == 4080 ? 1.0f : 0.0f; },
        [](int i, int) { return 0.3f * std::sin(i * .2f); },
        [](int i, int) { return drum(i, 3840) + drum(i, 4160); }
    }};
    for (const auto& signal : signals) {
        SignalFixture f; f.run(5000, p, signal);
        const auto timing = f.engine.getSourceTiming();
        require(timing.status[0] == TimingStatus::uncertain, "silence, steady audio, crackle and competing attacks stay uncertain");
        near(timing.offsetsMs[0], 0, "uncertain detection does not move source timing");
    }
    SignalFixture unilateral;
    const auto audio = unilateral.run(5000, p, [](int i, int ch) { return ch == 0 ? 0.0f : drum(i, 4080); });
    require(unilateral.engine.getSourceTiming().status[0] == TimingStatus::detected,
            "attack in either stereo channel can establish shared timing");
    near(audio[1][4360], .8f, "right-channel attack aligns without requiring left-channel energy");
}
void manualNudgeOverridesAutoAndBudgetsFutureAudio() {
    for (const int nudge : {-100, 0, 100, 300}) {
        auto p = timingPattern(); p.tightenPercent = 100; p.nudgeManual[0] = true; p.nudgeTenths[0] = nudge;
        SignalFixture f;
        const auto audio = f.run(9000, p, [](int i, int ch) { return (i + 1) / 10000.0f * (ch == 0 ? 1 : -0.5f); });
        const int delay = 4000 + std::max(0, nudge * 8 / 10);
        require(f.engine.getLatencySamples() == delay, "manual-only source adds only its positive lookahead");
        require(f.engine.getSourceTiming().status[0] == TimingStatus::manual, "explicit zero remains a manual override");
        near(f.engine.getSourceTiming().offsetsMs[0], nudge / 10.0f, "manual status reports exact source nudge");
        near(audio[0][delay], (4001 + nudge * .8f) / 10000.0f, "manual shift reads actual available chronological input");
    }
}
void timingResetsAndIsCallbackIndependent() {
    auto p = timingPattern(); p.tightenPercent = 100; p.smoothTenths = 100;
    auto signal = [](int i, int ch) { return drum(i, i < 16000 ? 4080 : 19904) * (ch == 0 ? 1 : -.5f); };
    SignalFixture small, large;
    const auto first = small.run(25000, p, signal, 1);
    const auto second = large.run(25000, p, signal, 511);
    require(first == second, "automatic corrections and prejoin crossfades are block-size invariant");
    near(small.engine.getSourceTiming().offsetsMs[0], -12, "new chronological source occurrence receives fresh detection");
    small.engine.reset();
    const auto reset = small.engine.getSourceTiming();
    near(reset.offsetsMs[0], 0, "reset clears old offsets");
    require(reset.status[0] == TimingStatus::off, "reset clears stale detected status");
    small.run(10, p, [](int, int) { return .25f; }, 10, 40);
    require(small.engine.getSourceTiming().status[0] != TimingStatus::detected,
            "seek cannot reuse another source pass's detected onset");
    auto rest = p; rest.order[0] = restToken(16);
    require(requiredLatencySamples(rest, 8000, 120) == 0, "unused auto colours do not impose latency on rest-only order");
}
void tightenBoundsShortSourcesAndPreservesFractionalAudio() {
    auto p = timingPattern(); p.cells.fill(-1); p.cells[4] = 0; p.tightenPercent = 100;
    // At 240 BPM this sixteenth is 62.5 ms; correction radius is capped at
    // 15.625 ms, so a clearly audible hit 25 ms late must remain untouched.
    Engine engine; engine.prepare(8000, 173, 2);
    std::array<float, 173> left{}, right{}; float* channels[]{left.data(), right.data()};
    for (int time = 0; time < 3000;) {
        const int n = std::min(173, 3000 - time);
        for (int i = 0; i < n; ++i) left[i] = right[i] = drum(time + i, 2200);
        Transport tr; tr.valid = tr.playing = true; tr.bpm = 240; tr.ppq = time / 2000.0;
        engine.process(channels, 2, n, tr, p); time += n;
    }
    require(engine.getSourceTiming().status[0] == TimingStatus::uncertain,
            "short source searches no farther than one quarter of its duration");
    near(engine.getSourceTiming().offsetsMs[0], 0, "unrelated later hit cannot pull a short source forward");

    auto tiny = pattern(); tiny.cells.fill(-1); tiny.cells[0] = 0;
    tiny.orderCount = 2; tiny.order[0] = 0; tiny.order[1] = restToken(1); tiny.smoothTenths = 100;
    SignalFixture shortAudio(8000);
    const auto result = shortAudio.run(10000, tiny, [](int, int) { return .7f; });
    for (int i = 0; i < 10000; ++i) {
        const int local = i % 2000;
        require(std::isfinite(result[0][i]) && result[0][i] >= 0 && result[0][i] <= .7001f,
                "bounded short-phrase smoothing has no overshoot or invalid samples");
        if (local >= 1000) require(result[0][i] == 0, "short alternating rests stay exact");
    }
    // Non-integral source/sample ratios and manual tenths of a millisecond
    // exercise interpolation up to the latest sample guaranteed by latency.
    auto manual = timingPattern(); manual.nudgeManual[0] = true; manual.nudgeTenths[0] = 1;
    SignalFixture fractional(44100);
    const auto shifted = fractional.run(24000, manual, [](int i, int ch) { return (i + 1) / 100000.0f * (ch == 0 ? 1 : -.2f); });
    require(fractional.engine.getLatencySamples() == 22055, "fractional positive nudge rounds latency upward safely");
    near(shifted[0][22055], .2205541f, "fractional source shift interpolates only received samples");
    require(fractional.engine.getStatus() == EngineStatus::ready, "fractional nudge has sufficient reported lookahead");
}
void soundControlsRejectMalformedAndAllocateNothing() {
    for (int control = 0; control < 3; ++control) {
        auto p = timingPattern();
        if (control == 0) p.smoothTenths = 101;
        if (control == 1) p.tightenPercent = -1;
        if (control == 2) p.nudgeTenths[7] = 301;
        SignalFixture f; const auto audio = f.run(10, p, [](int, int) { return .8f; });
        require(f.engine.getStatus() == EngineStatus::unsupportedTiming,
                "audio callback rejects malformed sound controls without model allocation");
        for (float value : audio[0]) require(value == 0, "malformed controls mute safely");
    }
    auto p = timingPattern(); p.tightenPercent = 100; p.smoothTenths = 100;
    Engine engine; engine.prepare(8000, 127, 2);
    float left[127]{}, right[127]{}; float* channels[]{left, right};
    callbackAllocations = 0; watchAllocations = true;
    for (int time = 0; time < 40000; time += 127) {
        for (int i = 0; i < 127; ++i) left[i] = right[i] = drum((time + i) % 16000, 4080);
        Transport tr; tr.valid = tr.playing = true; tr.bpm = 120; tr.ppq = time / 4000.0;
        if (time == 12700) p.nudgeManual[0] = true;
        if (time == 25400) { p.nudgeManual[0] = false; tr.ppq += 20; }
        engine.process(channels, 2, 127, tr, p);
    }
    watchAllocations = false;
    require(callbackAllocations == 0, "search, cache, edits, source shifts, crossfades and resets allocate no callback memory");
}
void timingCacheSurvivesAcceptedHostRounding() {
    auto p = timingPattern(); p.tightenPercent = 100;
    SignalFixture f;
    const auto signal = [](int i, int) { return drum(i, 4080); };
    f.run(4500, p, signal);
    near(f.engine.getSourceTiming().offsetsMs[0], 10, "initial stable correction before host rounding");
    for (int pass = 0; pass < 12; ++pass) {
        // A one-frame host-position correction is accepted as continuous
        // transport. It must not redo onset detection within this source pass.
        f.run(100, p, signal, 100, (pass % 2 == 0 ? 1.0 : -0.5) / 4000.0);
        near(f.engine.getSourceTiming().offsetsMs[0], 10,
             "accepted host rounding cannot jitter cached source correction");
    }
}
void startupTimingNeedsActualPrerollAndExportsWetSamples() {
    auto p = pattern(); p.cells.fill(-1); std::fill(p.cells.begin(), p.cells.begin() + 4, 0);
    p.smoothTenths = 100; p.tightenPercent = 100;
    SignalFixture f;
    const auto signal = [](int i, int ch) { return drum(i % 16000, 80) * (ch == 0 ? 1 : -.5f); };
    f.run(1000, p, signal);
    require(f.engine.getSourceTiming().status[0] == TimingStatus::uncertain,
            "first source at stream origin cannot invent unavailable detector preroll");
    const auto audio = f.run(32000, p, signal);
    near(f.engine.getSourceTiming().offsetsMs[0], 10, "later complete source occurrence can establish timing");
    require(f.engine.getSourceTiming().status[0] == TimingStatus::detected,
            "startup uncertainty does not persist into a later available source pass");
    OutputSnapshot exported;
    require(f.engine.copyLatestOutput(exported) && exported.channels[0].size() == 16000,
            "tightened and smoothed output still exports exact musical loop duration");
    // Second emitted output cycle occupies physical frames 16360..32359.
    for (int i = 0; i < 16000; ++i) {
        near(exported.channels[0][i], audio[0][15360 + i], "WAV contains actual corrected and smoothed wet output");
        near(exported.channels[1][i], -.5f * exported.channels[0][i], "corrected WAV retains linked stereo");
    }
}
}
int main() {
    try {
        smoothPreservesContinuousAudio();
        smoothCrossfadesJumpsAndKeepsRests();
        tightenFindsEarlyLateAndPartialCorrections();
        tightenRejectsCrackleSteadyAndAmbiguousAudio();
        manualNudgeOverridesAutoAndBudgetsFutureAudio();
        timingResetsAndIsCallbackIndependent();
        tightenBoundsShortSourcesAndPreservesFractionalAudio();
        soundControlsRejectMalformedAndAllocateNothing();
        timingCacheSurvivesAcceptedHostRounding();
        startupTimingNeedsActualPrerollAndExportsWetSamples();
        occurrenceLengthsMapThreeTwoThreeBeats();
        trimmedOccurrencesUseShiftedSourceStart();
        invalidOccurrenceLengthsMuteSafely();
        trimmedPlaybackDoesNotAllocate();
        delayAndStereo(); phraseOrderAndCut(); unequalWindowsAndBlockSizes();
        fractionalTiming(); loopSeekAndRestart(); malformedAndEmpty();
        editsWaveformAndMeters(); ringBudgetAndAllocation();
        completedOutputCapture(); startupReadinessAndCaptureInvalidation(); concurrentCaptureAndAllocation();
        roundedHostLoopCapture();
        roundedBlockPositionsCapture();
        repeatedEndpointCapture();
        backwardsRoundingWithinCycleCapture();
        queuedFractionalEndpoint();
        queuedPatternBoundaries();
        sourceWindowKeepsChronologicalHistory();
        restOnlyCapturesSilenceImmediately();
        restsKeepSourceHistoryAndBoundaries();
        restsDoNotNeedHistoryOrLeakAfterSeek();
        std::cout << "Engine checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "Engine test failed: " << e.what() << '\n'; return 1; }
}
