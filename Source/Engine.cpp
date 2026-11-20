#include "Engine.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace breakvar {
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<unsigned>::is_always_lock_free);
namespace {
constexpr double transportSampleTolerance = 2.0;
constexpr double automaticReserveSeconds = 0.045; // 30 ms search + 4 ms confirmation + 10 ms prejoin.
double positiveModulo(double value, double length) noexcept {
    const double remainder = std::fmod(value, length);
    return remainder < 0.0 ? remainder + length : remainder;
}
double snapSample(double value) noexcept {
    const double rounded = std::round(value);
    return std::abs(value - rounded) < 1.0e-7 ? rounded : value;
}
bool supportedPattern(const Pattern& p) noexcept {
    if ((p.inputBars != 1 && p.inputBars != 2 && p.inputBars != 4)
        || (p.outputBars != 1 && p.outputBars != 2 && p.outputBars != 4 && p.outputBars != 8)
        || p.orderCount < 0 || p.orderCount > maxOrder
        || p.smoothTenths < 0 || p.smoothTenths > 100
        || p.tightenPercent < 0 || p.tightenPercent > 100) return false;
    for (const int nudge : p.nudgeTenths) if (nudge < -300 || nudge > 300) return false;
    for (int i = 0; i < p.activeCells(); ++i)
        if (p.cells[i] < -1 || p.cells[i] >= colourCount) return false;
    // The engine defends its own indices without calling allocating model validation.
    for (int colour = 0; colour < colourCount; ++colour) {
        bool seen = false, ended = false;
        for (int i = 0; i < p.activeCells(); ++i) {
            if (p.cells[i] == colour) { if (ended) return false; seen = true; }
            else if (seen) ended = true;
        }
    }
    for (int i = 0; i < p.orderCount; ++i) {
        if (p.order[i] < 0 || p.order[i] >= orderTokenCount
            || p.orderLengths[i] < 0 || p.orderLengths[i] > maxCells
            || (isRest(p.order[i]) && p.orderLengths[i] != 0)) return false;
    }
    for (int i = p.orderCount; i < maxOrder; ++i)
        if (p.orderLengths[i] != 0) return false;
    return true;
}
}

int requiredLatencySamples(const Pattern& p, double sampleRate, double bpm,
                           int numerator, int denominator) noexcept {
    if (!supportedPattern(p) || !std::isfinite(sampleRate) || sampleRate <= 0.0
        || !std::isfinite(bpm) || bpm <= 0.0 || numerator <= 0 || numerator > 128
        || denominator <= 0 || denominator > 128 || (denominator & (denominator - 1)) != 0)
        return 0;
    const double cellSamples = sampleRate * 60.0 / bpm * numerator * 4.0 / denominator / cellsPerBar;
    const auto sequence = expandSequence(p);
    double forwardSamples = 0.0;
    for (int s = 0; s < sequence.count; ++s) {
        const auto& segment = sequence.segments[static_cast<std::size_t>(s)];
        if (isRest(segment.colour)) continue;
        const auto colour = static_cast<std::size_t>(segment.colour);
        const double extra = p.nudgeManual[colour]
            ? std::max(0.0, p.nudgeTenths[colour] * sampleRate / 10000.0)
            : (p.tightenPercent > 0 ? automaticReserveSeconds * sampleRate : 0.0);
        forwardSamples = std::max(forwardSamples,
            (p.region(segment.colour).first - segment.start) * cellSamples + extra);
    }
    const double latency = std::ceil(snapSample(forwardSamples));
    return std::isfinite(latency) && latency <= std::numeric_limits<int>::max()
        ? static_cast<int>(latency) : std::numeric_limits<int>::max();
}

void Engine::prepare(double rate, int maximumBlockSize, int channels) {
    (void) maximumBlockSize; // Streaming processing has no block-size-dependent storage.
    sampleRate_ = std::isfinite(rate) && rate >= 1.0 && rate <= 768000.0 ? rate : 44100.0;
    preparedChannels_ = std::clamp(channels, 1, 2);
    historyCapacity_ = static_cast<std::size_t>(std::ceil(sampleRate_ * 120.0)) + 2;
    history_.assign(static_cast<std::size_t>(preparedChannels_), std::vector<float>(historyCapacity_, 0.0f));
    captureCapacity_ = static_cast<std::size_t>(std::ceil(sampleRate_ * 32.0));
    for (auto& buffer : captureBuffers_)
        buffer.channels.assign(static_cast<std::size_t>(preparedChannels_), std::vector<float>(captureCapacity_));
    latency_ = 0;
    reset();
}

void Engine::reset() noexcept {
    invalidateOutput();
    clearStream();
    havePattern_ = false;
    patternPending_ = false;
    pendingBeatsRemaining_ = -1.0;
    pendingBoundarySamples_ = -1.0;
}

void Engine::discardCapture() noexcept {
    if (captureSlot_ >= 0)
        captureBuffers_[static_cast<std::size_t>(captureSlot_)].ownership.store(0, std::memory_order_release);
    captureSlot_ = -1;
    captureCount_ = 0;
    captureValid_ = false;
}

void Engine::clearStream() noexcept {
    // Invalidate by provenance rather than clearing 120 seconds on the audio thread.
    discardCapture();
    haveCapturePhase_ = false;
    captureCycleSamples_ = 0.0;
    written_ = 0;
    timingActive_ = false;
    editFadeRemaining_ = 0;
    haveWaveformCycle_ = false;
    waveform_.fill(0.0f);
    inputPhase_ = outputPhase_ = 0.0f;
    status_ = EngineStatus::stopped;
    clearTiming();
}

void Engine::clearTiming() noexcept {
    sourceTiming_ = {};
    timingCache_ = {};
    recentTimingCache_ = 0;
}

double Engine::detectOffset(double nominal, double radius, TimingStatus& status) const noexcept {
    status = TimingStatus::uncertain;
    const int baseline = std::max(2, static_cast<int>(std::ceil(sampleRate_ * 0.004)));
    const int fast = std::max(1, static_cast<int>(std::ceil(sampleRate_ * 0.001)));
    const int confirm = baseline;
    const auto first = static_cast<std::int64_t>(std::ceil(nominal - radius));
    const auto last = static_cast<std::int64_t>(std::floor(nominal + radius));
    const auto oldest = std::max<std::int64_t>(0, written_ - static_cast<std::int64_t>(historyCapacity_));
    if (first - baseline < oldest || last + confirm >= written_ || first > last) return 0.0;
    const auto envelope = [&](std::int64_t sample) noexcept {
        float peak = 0.0f;
        const auto index = static_cast<std::size_t>(sample % static_cast<std::int64_t>(historyCapacity_));
        for (const auto& channel : history_) peak = std::max(peak, std::abs(channel[index]));
        return static_cast<double>(peak);
    };
    double before = 0.0, onset = 0.0, sustained = 0.0;
    for (int i = 1; i <= baseline; ++i) before += envelope(first - i);
    for (int i = 0; i < confirm; ++i) {
        const double value = envelope(first + i);
        sustained += value;
        if (i < fast) onset += value;
    }
    double bestScore = 0.0, competitor = 0.0;
    std::int64_t best = first;
    // Moving sums make the complete bounded search linear in its sample count.
    // Close peaks describe one attack; a similarly strong separate attack is ambiguous.
    const auto separation = static_cast<std::int64_t>(confirm * 2);
    for (auto sample = first; sample <= last; ++sample) {
        const double background = before / baseline;
        const double attack = onset / fast;
        const double tail = sustained / confirm;
        if (attack > 0.012 && attack > background * 2.5 + 0.006
            && tail > background * 1.8 + 0.004 && tail > attack * 0.45) {
            const double score = attack - background;
            if (score > bestScore) {
                if (sample - best > separation) competitor = std::max(competitor, bestScore);
                bestScore = score; best = sample;
            } else if (sample - best > separation) competitor = std::max(competitor, score);
        }
        before += envelope(sample) - envelope(sample - baseline);
        onset += envelope(sample + fast) - envelope(sample);
        sustained += envelope(sample + confirm) - envelope(sample);
    }
    if (bestScore <= 0.0 || competitor > bestScore * 0.72) return 0.0;
    // Isolated vinyl clicks can have a large fast mean; require sustained support
    // in at least half the confirmation samples rather than only a high average.
    int support = 0;
    for (int i = 0; i < confirm; ++i)
        if (envelope(best + i) > bestScore * 0.35) ++support;
    if (support < (confirm + 1) / 2) return 0.0;
    status = TimingStatus::detected;
    return std::clamp(static_cast<double>(best) - nominal, -radius, radius);
}

double Engine::sourceOffset(const Pattern& p, int colour, double anchor, double cellSamples,
                            bool publish) noexcept {
    const auto index = static_cast<std::size_t>(colour);
    if (p.nudgeManual[index]) {
        const double offset = p.nudgeTenths[index] * sampleRate_ / 10000.0;
        if (publish) {
            sourceTiming_.offsetsMs[index] = p.nudgeTenths[index] / 10.0f;
            sourceTiming_.status[index] = TimingStatus::manual;
        }
        return offset;
    }
    if (p.tightenPercent == 0) return 0.0;
    int slot = -1;
    // Hosts round their PPQ at callback boundaries. The engine already accepts
    // up to two samples as continuous, so those updates identify the same
    // source occurrence. Cap the tolerance for extremely short output cycles.
    const double anchorTolerance = std::min(transportSampleTolerance,
        p.outputBars * cellsPerBar * cellSamples * 0.25);
    for (int i = 0; i < static_cast<int>(timingCache_.size()); ++i)
        if (timingCache_[i].valid && std::abs(timingCache_[i].anchor - anchor) <= anchorTolerance) { slot = i; break; }
    if (slot < 0) {
        slot = 1 - recentTimingCache_;
        timingCache_[slot] = {};
        timingCache_[slot].valid = true;
        timingCache_[slot].anchor = anchor;
    }
    recentTimingCache_ = slot;
    auto& cache = timingCache_[slot];
    if (!cache.decided[index]) {
        const auto region = p.region(colour);
        const double radius = std::min(sampleRate_ * 0.030, (region.end - region.first) * cellSamples * 0.25);
        cache.offsets[index] = detectOffset(anchor + region.first * cellSamples, radius, cache.status[index])
            * p.tightenPercent / 100.0;
        cache.decided[index] = true;
    }
    if (publish) {
        sourceTiming_.offsetsMs[index] = static_cast<float>(cache.offsets[index] * 1000.0 / sampleRate_);
        sourceTiming_.status[index] = cache.status[index];
    }
    return cache.offsets[index];
}

float Engine::readHistory(int channel, double position) const noexcept {
    position = snapSample(position);
    if (!std::isfinite(position) || position < 0.0 || written_ <= 0) return 0.0f;
    const auto oldest = std::max<std::int64_t>(0, written_ - static_cast<std::int64_t>(historyCapacity_));
    if (position < static_cast<double>(oldest) || position > static_cast<double>(written_ - 1)) return 0.0f;
    const auto first = static_cast<std::int64_t>(std::floor(position));
    const double fraction = position - static_cast<double>(first);
    const auto& history = history_[static_cast<std::size_t>(channel)];
    const auto a = history[static_cast<std::size_t>(first % static_cast<std::int64_t>(historyCapacity_))];
    if (fraction == 0.0) return a;
    // Never interpolate into a future sample, including the rounding edge of latency.
    if (first + 1 >= written_) return 0.0f;
    const auto b = history[static_cast<std::size_t>((first + 1) % static_cast<std::int64_t>(historyCapacity_))];
    return static_cast<float>(static_cast<double>(a) + fraction * (static_cast<double>(b) - a));
}

void Engine::process(float* const* channels, int channelCount, int sampleCount,
                     const Transport& tr, const Pattern& p, bool bypass, bool deferPatternChange) noexcept {
    if (channels == nullptr || channelCount <= 0 || sampleCount <= 0) return;
    const double previousBoundary = pendingBoundarySamples_;
    patternPending_ = false;
    pendingBeatsRemaining_ = -1.0;
    pendingBoundarySamples_ = -1.0;
    // Preset browsing leaves the current audio intact until its next output
    // boundary. A shorter host cycle is also a boundary, so it cannot prevent
    // a long output arrangement from ever accepting a new preset.
    const bool stableTiming = timingActive_ && tr.valid && tr.playing
        && activeBpm_ == tr.bpm && activeNumerator_ == tr.numerator
        && activeDenominator_ == tr.denominator && previousLooping_ == tr.looping
        && (!tr.looping || (previousLoopStart_ == tr.loopStartPpq
                           && previousLoopEnd_ == tr.loopEndPpq));
    if (deferPatternChange && havePattern_ && previousPattern_ != p
        && stableTiming && std::isfinite(tr.ppq) && !isDiscontinuous(tr)
        && channels != nullptr && channelCount > 0 && channelCount <= 2 && sampleCount > 0)
    {
        const double samplesPerBeat = sampleRate_ * 60.0 / tr.bpm;
        const double outputBeats = previousPattern_.outputBars * tr.numerator * 4.0 / tr.denominator;
        const double phase = snapSample(positiveModulo(tr.ppq - latency_ / samplesPerBeat, outputBeats)
                                       * samplesPerBeat);
        const double cycleSamples = outputBeats * samplesPerBeat;
        double boundary = phase == 0.0 || phase >= cycleSamples ? 0.0
            : std::ceil(snapSample(cycleSamples - phase));
        // A fractional musical endpoint can lie between the last sample of
        // one callback and the first of the next. Remember that a previously
        // queued boundary was due; phase need not equal exactly zero. Restrict
        // this to the accepted transport rounding range so a correction in
        // the middle of a cycle cannot falsely release the queue.
        if (previousBoundary >= 0.0 && previousBoundary <= transportSampleTolerance
            && phase <= transportSampleTolerance + 1.0)
            boundary = 0.0;
        if (tr.looping && tr.ppq >= tr.loopStartPpq && tr.ppq < tr.loopEndPpq)
        {
            const double tolerance = std::max(transportSampleTolerance / samplesPerBeat, 1.0e-7);
            const bool wrappedAtStart = expectedPpq_ >= tr.loopEndPpq - tolerance
                && std::abs(tr.ppq - expectedPpq_) > tolerance
                && std::abs(tr.ppq - tr.loopStartPpq) <= tolerance;
            const double hostBoundary = wrappedAtStart ? 0.0
                : std::ceil(snapSample((tr.loopEndPpq - tr.ppq) * samplesPerBeat));
            boundary = std::min(boundary, hostBoundary);
        }
        const auto active = previousPattern_;
        if (boundary >= sampleCount)
        {
            processActive(channels, channelCount, sampleCount, tr, active, bypass);
            patternPending_ = true;
            pendingBoundarySamples_ = boundary - sampleCount;
            pendingBeatsRemaining_ = (boundary - sampleCount) / samplesPerBeat;
            return;
        }
        const int beforeSwitch = static_cast<int>(std::max(0.0, boundary));
        if (beforeSwitch > 0)
            processActive(channels, channelCount, beforeSwitch, tr, active, bypass);
        auto nextTransport = tr;
        nextTransport.ppq += beforeSwitch / samplesPerBeat;
        if (tr.looping && tr.ppq >= tr.loopStartPpq && nextTransport.ppq >= tr.loopEndPpq)
            nextTransport.ppq = tr.loopStartPpq + positiveModulo(nextTransport.ppq - tr.loopStartPpq,
                                                               tr.loopEndPpq - tr.loopStartPpq);
        std::array<float*, 2> remaining{};
        for (int ch = 0; ch < channelCount; ++ch)
            remaining[static_cast<std::size_t>(ch)] = channels[ch] == nullptr ? nullptr : channels[ch] + beforeSwitch;
        processActive(remaining.data(), channelCount, sampleCount - beforeSwitch, nextTransport, p, bypass);
        return;
    }
    processActive(channels, channelCount, sampleCount, tr, p, bypass);
}

bool Engine::isDiscontinuous(const Transport& tr) const noexcept {
    if (!timingActive_ && !(havePattern_ && status_ == EngineStatus::stopped)) return false;
    const double samplesPerBeat = sampleRate_ * 60.0 / tr.bpm;
    const double tolerance = std::max(transportSampleTolerance / samplesPerBeat, 1.0e-7);
    bool discontinuity = std::abs(tr.ppq - expectedPpq_) > tolerance;
    if (discontinuity && tr.looping && previousLooping_
        && tr.loopStartPpq == previousLoopStart_ && tr.loopEndPpq == previousLoopEnd_
        && expectedPpq_ >= tr.loopEndPpq - tolerance) {
        const double wrapped = tr.loopStartPpq + positiveModulo(expectedPpq_ - tr.loopStartPpq,
                                                             tr.loopEndPpq - tr.loopStartPpq);
        const double difference = std::abs(tr.ppq - wrapped);
        const double loopLength = tr.loopEndPpq - tr.loopStartPpq;
        // Hosts may round a fractional sample endpoint down. Distance around
        // the loop recognizes that as a wrap rather than a seek.
        discontinuity = std::min(difference, std::abs(loopLength - difference)) > tolerance;
    }
    return discontinuity;
}

void Engine::processActive(float* const* channels, int channelCount, int sampleCount,
                          const Transport& tr, const Pattern& p, bool bypass) noexcept {
    if (channels == nullptr || sampleCount <= 0 || channelCount <= 0) return;
    const auto mute = [&] {
        for (int ch = 0; ch < channelCount; ++ch)
            if (channels[ch] != nullptr) std::fill_n(channels[ch], sampleCount, 0.0f);
    };
    if (historyCapacity_ == 0 || history_.empty()) {
        mute(); status_ = EngineStatus::waitingForTransport; return;
    }
    if (!tr.valid) {
        reset(); status_ = EngineStatus::waitingForTransport; mute(); return;
    }
    const bool validMeter = tr.numerator > 0 && tr.numerator <= 128
        && tr.denominator > 0 && tr.denominator <= 128
        && (tr.denominator & (tr.denominator - 1)) == 0;
    const bool validLoop = !tr.looping || (std::isfinite(tr.loopStartPpq)
        && std::isfinite(tr.loopEndPpq) && tr.loopEndPpq > tr.loopStartPpq
        && std::abs(tr.loopStartPpq) <= 1.0e12 && std::abs(tr.loopEndPpq) <= 1.0e12);
    if (!std::isfinite(tr.bpm) || tr.bpm <= 0.0 || tr.bpm > 10000.0
        || !std::isfinite(tr.ppq) || std::abs(tr.ppq) > 1.0e12
        || !validMeter || !validLoop || !supportedPattern(p)) {
        reset(); status_ = EngineStatus::unsupportedTiming; mute(); return;
    }
    const double samplesPerBeat = sampleRate_ * 60.0 / tr.bpm;
    const double beatsPerBar = static_cast<double>(tr.numerator) * 4.0 / tr.denominator;
    const double cellSamples = samplesPerBeat * beatsPerBar / cellsPerBar;
    const double inputSamples = cellSamples * p.activeCells();
    const double outputSamples = cellSamples * p.outputBars * cellsPerBar;
    // Two extra ring samples allow bounded interpolation at an otherwise exact 120 s budget.
    if (!std::isfinite(inputSamples + outputSamples) || inputSamples < 1.0
        || outputSamples < 1.0 || inputSamples + outputSamples > sampleRate_ * 120.0
        || inputSamples > std::numeric_limits<int>::max()) {
        reset(); status_ = EngineStatus::unsupportedTiming; mute(); return;
    }
    const int desiredLatency = requiredLatencySamples(p, sampleRate_, tr.bpm, tr.numerator, tr.denominator);
    latency_ = desiredLatency;
    const bool discontinuity = isDiscontinuous(tr);
    const bool patternChanged = havePattern_ && previousPattern_ != p;
    const bool timingChanged = havePattern_ && (activeBpm_ != tr.bpm
        || activeNumerator_ != tr.numerator || activeDenominator_ != tr.denominator
        || previousLooping_ != tr.looping || (tr.looping
            && (previousLoopStart_ != tr.loopStartPpq || previousLoopEnd_ != tr.loopEndPpq)));
    if (patternChanged || timingChanged || discontinuity) {
        invalidateOutput(); discardCapture(); haveCapturePhase_ = false;
        clearTiming();
    }
    if (!timingActive_ || discontinuity || timingChanged)
        clearStream();
    else if (activeInputBars_ != p.inputBars) {
        // The ring stores chronological samples, independent of source-window
        // length. Only its visual bins depend on that length.
        waveform_.fill(0.0f);
        haveWaveformCycle_ = false;
    }
    activeBpm_ = tr.bpm; activeNumerator_ = tr.numerator;
    activeDenominator_ = tr.denominator; activeInputBars_ = p.inputBars;
    timingActive_ = true;
    const int fadeSamples = std::max(1, static_cast<int>(std::llround(sampleRate_ * 0.001)));
    if (patternChanged) editFadeRemaining_ = fadeSamples;
    previousPattern_ = p; havePattern_ = true;
    previousLooping_ = tr.looping; previousLoopStart_ = tr.loopStartPpq; previousLoopEnd_ = tr.loopEndPpq;
    if (!tr.playing) {
        finishOutputOnStop();
        expectedPpq_ = tr.ppq;
        clearStream(); status_ = EngineStatus::stopped;
        for (int colour = 0; colour < colourCount; ++colour) {
            if (!p.region(colour).valid()) continue;
            if (p.nudgeManual[colour]) {
                sourceTiming_.status[colour] = TimingStatus::manual;
                sourceTiming_.offsetsMs[colour] = p.nudgeTenths[colour] / 10.0f;
            } else if (p.tightenPercent > 0) sourceTiming_.status[colour] = TimingStatus::searching;
        }
        mute(); return;
    }
    const auto sequence = expandSequence(p);
    std::array<Region, colourCount> regions{};
    for (int colour = 0; colour < colourCount; ++colour) regions[static_cast<std::size_t>(colour)] = p.region(colour);
    const double inputBeats = p.inputBars * beatsPerBar;
    const double outputBeats = p.outputBars * beatsPerBar;
    const int processedChannels = std::min(channelCount, preparedChannels_);
    const double smoothSamples = p.smoothTenths * sampleRate_ / 10000.0;
    for (int colour = 0; colour < colourCount; ++colour)
        if (regions[colour].valid() && sourceTiming_.status[colour] == TimingStatus::off) {
            if (p.nudgeManual[colour]) {
                sourceTiming_.status[colour] = TimingStatus::manual;
                sourceTiming_.offsetsMs[colour] = p.nudgeTenths[colour] / 10.0f;
            } else if (p.tightenPercent > 0) sourceTiming_.status[colour] = TimingStatus::searching;
        }

    for (int i = 0; i < sampleCount; ++i) {
        double hostPpq = tr.ppq + static_cast<double>(i) / samplesPerBeat;
        if (tr.looping && tr.ppq >= tr.loopStartPpq && hostPpq >= tr.loopEndPpq)
            hostPpq = tr.loopStartPpq + positiveModulo(hostPpq - tr.loopStartPpq,
                                                     tr.loopEndPpq - tr.loopStartPpq);
        const double inputPhase = positiveModulo(hostPpq, inputBeats);
        const double cycle = std::floor(hostPpq / inputBeats);
        if (!haveWaveformCycle_ || cycle != waveformCycle_) {
            waveform_.fill(0.0f); waveformCycle_ = cycle; haveWaveformCycle_ = true;
        }
        const auto bin = static_cast<std::size_t>(std::clamp(
            static_cast<int>(inputPhase / inputBeats * waveformSize), 0, waveformSize - 1));
        const auto ringIndex = static_cast<std::size_t>(written_ % static_cast<std::int64_t>(historyCapacity_));
        for (int ch = 0; ch < preparedChannels_; ++ch) {
            float value = ch < processedChannels && channels[ch] != nullptr ? channels[ch][i] : 0.0f;
            if (!std::isfinite(value)) value = 0.0f;
            history_[static_cast<std::size_t>(ch)][ringIndex] = value;
            waveform_[bin] = std::max(waveform_[bin], std::abs(value));
        }
        const double physicalSample = static_cast<double>(written_);
        ++written_; // The current input sample is available before any source read.
        const double compensatedPpq = hostPpq - static_cast<double>(latency_) / samplesPerBeat;
        double phaseSamples = snapSample(positiveModulo(compensatedPpq, outputBeats) * samplesPerBeat);
        if (phaseSamples >= outputSamples) phaseSamples = 0.0;
        const double phaseCells = phaseSamples / cellSamples;
        const double anchor = snapSample(physicalSample - latency_ - phaseSamples);
        double readPosition = physicalSample - latency_;
        double blendPosition = readPosition;
        float blend = 0.0f;
        float gain = 1.0f;
        bool haveSource = bypass;
        if (!bypass) {
            for (int s = 0; s < sequence.count; ++s) {
                const auto& segment = sequence.segments[static_cast<std::size_t>(s)];
                if (phaseCells >= segment.start && phaseCells < segment.start + segment.length) {
                    // Input was recorded above even when this output phrase is
                    // silent. A rest has no source index or history requirement.
                    if (isRest(segment.colour)) break;
                    const auto region = regions[static_cast<std::size_t>(segment.colour)];
                    const double localSamples = phaseSamples - segment.start * cellSamples;
                    const double offset = sourceOffset(p, segment.colour, anchor, cellSamples, true);
                    readPosition = anchor + region.first * cellSamples + offset + localSamples;
                    const double segmentSamples = segment.length * cellSamples;
                    if (smoothSamples > 0.0) {
                        const auto& previous = sequence.segments[static_cast<std::size_t>(s == 0 ? sequence.count - 1 : s - 1)];
                        if (isRest(previous.colour)) {
                            const double length = std::min({smoothSamples, segmentSamples * 0.25,
                                                           previous.length * cellSamples * 0.25});
                            gain = static_cast<float>(std::min(1.0, localSamples / length));
                        }
                        const auto& next = sequence.segments[static_cast<std::size_t>((s + 1) % sequence.count)];
                        const double remaining = segmentSamples - localSamples;
                        const double length = std::min({smoothSamples, segmentSamples * 0.25,
                                                       next.length * cellSamples * 0.25});
                        if (remaining <= length) {
                            if (isRest(next.colour)) gain *= static_cast<float>(remaining / length);
                            else {
                                const double nextAnchor = anchor + (s + 1 == sequence.count ? outputSamples : 0.0);
                                const double nextOffset = sourceOffset(p, next.colour, nextAnchor, cellSamples, false);
                                const double nextStart = nextAnchor + regions[next.colour].first * cellSamples + nextOffset;
                                // The preroll crossfade completes at the boundary. The incoming attack
                                // itself is unattenuated, unlike a fade to/from silence on every phrase.
                                // Identical chronological source positions need no crossfade at all.
                                if (std::abs(nextStart - (readPosition + remaining)) > 0.00001) {
                                    blendPosition = nextStart - remaining;
                                    blend = static_cast<float>(1.0 - remaining / length);
                                }
                            }
                        }
                    }
                    haveSource = region.valid();
                    break;
                }
            }
            if (editFadeRemaining_ > 0)
                gain *= 1.0f - static_cast<float>(editFadeRemaining_) / fadeSamples;
        }
        if (editFadeRemaining_ > 0) --editFadeRemaining_;
        for (int ch = 0; ch < channelCount; ++ch)
            if (channels[ch] != nullptr)
                channels[ch][i] = ch < processedChannels && haveSource
                    ? (readHistory(ch, readPosition) * (1.0f - blend)
                       + (blend > 0.0f ? readHistory(ch, blendPosition) * blend : 0.0f)) * gain : 0.0f;
        const float lastNormalizedPhase = std::nextafter(1.0f, 0.0f);
        inputPhase_ = std::min(static_cast<float>(inputPhase / inputBeats), lastNormalizedPhase);
        outputPhase_ = std::min(static_cast<float>(phaseSamples / outputSamples), lastNormalizedPhase);
        const double needed = snapSample(readPosition);
        const auto oldest = std::max<std::int64_t>(0, written_ - static_cast<std::int64_t>(historyCapacity_));
        const double blendNeeded = snapSample(blendPosition);
        const bool captured = !haveSource || (needed >= static_cast<double>(oldest)
            && std::ceil(needed) <= physicalSample
            && (blend == 0.0f || (blendNeeded >= static_cast<double>(oldest)
                                 && std::ceil(blendNeeded) <= physicalSample)));
        status_ = captured ? EngineStatus::ready : EngineStatus::filling;
        captureSample(channels, processedChannels, i, phaseSamples, outputSamples,
                      tr.bpm, p.outputBars, captured && !bypass && editFadeRemaining_ == 0);
    }
    expectedPpq_ = tr.ppq + static_cast<double>(sampleCount) / samplesPerBeat;
    previousLooping_ = tr.looping; previousLoopStart_ = tr.loopStartPpq; previousLoopEnd_ = tr.loopEndPpq;
}

int Engine::getLatencySamples() const noexcept { return latency_; }
EngineStatus Engine::getStatus() const noexcept { return status_; }
float Engine::getInputPhase() const noexcept { return inputPhase_; }
float Engine::getOutputPhase() const noexcept { return outputPhase_; }
void Engine::copyWaveform(std::array<float, waveformSize>& destination) const noexcept { destination = waveform_; }
void Engine::captureSample(float* const* channels, int channelCount, int sample,
                           double phase, double cycleSamples, double bpm, int bars, bool valid) noexcept {
    constexpr double epsilon = 1.0e-5;
    // The transport already accepts up to two samples of host PPQ rounding.
    // In particular, a DAW loop has integer sample endpoints even when its
    // musical length is fractional. Capture must accept that same continuity.
    const bool wrapped = haveCapturePhase_ && (phase < lastCapturePhase_ - epsilon
        || (cycleSamples == 1.0 && phase == lastCapturePhase_));
    const bool continuousWrap = wrapped
        && std::abs(phase + cycleSamples - lastCapturePhase_ - 1.0) <= transportSampleTolerance;
    const bool continuous = !haveCapturePhase_ || continuousWrap
        || std::abs(phase - lastCapturePhase_ - 1.0) <= transportSampleTolerance;
    if (continuousWrap || !continuous) {
        // A rounded-down host loop can wrap just before the ideal endpoint.
        // The observed wrap closes that actual emitted cycle without inventing
        // samples or waiting for an endpoint that this host never supplies.
        if (continuousWrap) completeCapture(bpm, bars);
        else discardCapture();
    }
    const bool start = continuousWrap || (!haveCapturePhase_ && phase < epsilon);
    if (start && cycleSamples <= sampleRate_ * 32.0 && valid) {
        capturePublication_ = publication_.load(std::memory_order_acquire);
        const int latest = static_cast<int>(capturePublication_ & 3) - 1;
        for (int slot = 0; slot < static_cast<int>(captureBuffers_.size()); ++slot) {
            if (slot == latest) continue;
            unsigned expected = 0;
            if (captureBuffers_[static_cast<std::size_t>(slot)].ownership.compare_exchange_strong(
                    expected, 1, std::memory_order_acquire)) {
                captureSlot_ = slot; captureValid_ = true; break;
            }
        }
    }
    if (captureSlot_ >= 0) {
        captureValid_ = captureValid_ && valid && continuous;
        if (captureCount_ < captureCapacity_) {
            auto& buffer = captureBuffers_[static_cast<std::size_t>(captureSlot_)];
            for (int ch = 0; ch < preparedChannels_; ++ch)
                buffer.channels[static_cast<std::size_t>(ch)][captureCount_]
                    = ch < channelCount && channels[ch] != nullptr ? channels[ch][sample] : 0.0f;
            ++captureCount_;
        } else captureValid_ = false;
    }
    // Wait for the observed wrap: the host may repeat or slightly adjust its
    // nominal final position in the next callback. Publishing here would omit
    // those accepted wet frames. A Stop can explicitly finish this candidate.
    captureCycleSamples_ = cycleSamples;
    lastCapturePhase_ = phase; haveCapturePhase_ = true;
}

void Engine::finishOutputOnStop() noexcept {
    // Transport rounding tolerance proves continuity, not completion. Without
    // an observed wrap, require the emitted samples to reach the endpoint;
    // otherwise stopping a frame or two early would publish a partial loop.
    if (haveCapturePhase_ && captureCycleSamples_ > 0.0
        && snapSample(lastCapturePhase_ + 1.0 - captureCycleSamples_) >= 0.0)
        completeCapture(activeBpm_, previousPattern_.outputBars);
    else
        discardCapture();
    haveCapturePhase_ = false;
    captureCycleSamples_ = 0.0;
}

void Engine::completeCapture(double bpm, int bars) noexcept {
    if (captureSlot_ >= 0 && captureValid_ && captureCount_ > 0) {
        auto& buffer = captureBuffers_[static_cast<std::size_t>(captureSlot_)];
        buffer.count = captureCount_; buffer.bpm = bpm; buffer.bars = bars;
        const auto next = ((capturePublication_ & ~std::uint64_t{3}) + 4)
            | static_cast<std::uint64_t>(captureSlot_ + 1);
        buffer.serial = next >> 2;
        // A concurrent invalidation changes the revision, so stale work cannot publish.
        auto expected = capturePublication_;
        publication_.compare_exchange_strong(expected, next, std::memory_order_acq_rel);
    }
    discardCapture();
}

bool Engine::copyLatestOutput(OutputSnapshot& destination) const {
    const auto published = publication_.load(std::memory_order_acquire);
    const int slot = static_cast<int>(published & 3) - 1;
    if (slot < 0) return false;
    auto& buffer = captureBuffers_[static_cast<std::size_t>(slot)];
    unsigned available = 0;
    if (!buffer.ownership.compare_exchange_strong(available, 2, std::memory_order_acquire)) return false;
    // Ownership must precede validation: a reader may have loaded a slot that
    // ceased being latest before it acquired ownership.
    if (publication_.load(std::memory_order_acquire) != published) {
        buffer.ownership.store(0, std::memory_order_release); return false;
    }
    try {
        destination.channels.resize(static_cast<std::size_t>(preparedChannels_));
        for (int ch = 0; ch < preparedChannels_; ++ch)
            destination.channels[static_cast<std::size_t>(ch)].assign(buffer.channels[static_cast<std::size_t>(ch)].begin(),
                buffer.channels[static_cast<std::size_t>(ch)].begin() + buffer.count);
        destination.sampleRate = sampleRate_; destination.bpm = buffer.bpm;
        destination.bars = buffer.bars; destination.serial = buffer.serial;
    } catch (...) {
        buffer.ownership.store(0, std::memory_order_release); throw;
    }
    const bool current = publication_.load(std::memory_order_acquire) == published;
    buffer.ownership.store(0, std::memory_order_release);
    return current;
}

bool Engine::hasOutputReady() const noexcept { return (publication_.load(std::memory_order_acquire) & 3) != 0; }
void Engine::invalidateOutput() noexcept {
    auto previous = publication_.load(std::memory_order_relaxed);
    while (!publication_.compare_exchange_weak(previous, (previous & ~std::uint64_t{3}) + 4,
                                               std::memory_order_acq_rel)) {}
}
}
