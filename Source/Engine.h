#pragma once
#include "Pattern.h"
#include <atomic>
#include <cstdint>
#include <vector>

namespace breakvar
{
struct Transport
{
    bool valid = false;
    bool playing = false;
    double bpm = 120.0;
    double ppq = 0.0;
    int numerator = 4;
    int denominator = 4;
    bool looping = false;
    double loopStartPpq = 0.0;
    double loopEndPpq = 0.0;
};

enum class EngineStatus { stopped, waitingForTransport, filling, ready, unsupportedTiming };
enum class TimingStatus { off, searching, detected, uncertain, manual };
struct SourceTiming {
    std::array<float, colourCount> offsetsMs{};
    std::array<TimingStatus, colourCount> status{};
};

int requiredLatencySamples(const Pattern&, double sampleRate, double bpm,
                           int numerator = 4, int denominator = 4) noexcept;

struct OutputSnapshot
{
    std::vector<std::vector<float>> channels;
    double sampleRate = 0.0;
    double bpm = 0.0;
    int bars = 0;
    std::uint64_t serial = 0;
};

class Engine
{
public:
    void prepare(double sampleRate, int maximumBlockSize, int channels);
    void reset() noexcept;
    void process(float* const* channels, int channelCount, int sampleCount,
                 const Transport&, const Pattern&, bool bypass = false,
                 bool deferPatternChange = false) noexcept;
    bool isPatternPending() const noexcept { return patternPending_; }
    double getPendingBeatsRemaining() const noexcept { return pendingBeatsRemaining_; }
    int getLatencySamples() const noexcept;
    EngineStatus getStatus() const noexcept;
    float getInputPhase() const noexcept;
    float getOutputPhase() const noexcept;
    SourceTiming getSourceTiming() const noexcept { return sourceTiming_; }
    void copyWaveform(std::array<float, waveformSize>& destination) const noexcept;
    // Copy on a non-audio thread. A concurrent publication/invalidation may return false.
    bool copyLatestOutput(OutputSnapshot&) const;
    bool hasOutputReady() const noexcept;
    void invalidateOutput() noexcept;
    // Call only after audio processing stops. Finalizes an endpoint candidate;
    // a genuinely partial cycle is discarded and an earlier export is retained.
    void finishOutputOnStop() noexcept;

private:
    // Engine implementer owns private representation; public interface is shared.
    double sampleRate_ = 44100.0;
    int preparedChannels_ = 2;
    std::vector<std::vector<float>> history_;
    std::array<float, waveformSize> waveform_{};
    std::int64_t written_ = 0;
    int latency_ = 0;
    EngineStatus status_ = EngineStatus::stopped;
    float inputPhase_ = 0.0f;
    float outputPhase_ = 0.0f;
    std::size_t historyCapacity_ = 0;
    bool timingActive_ = false;
    double activeBpm_ = 120.0;
    int activeNumerator_ = 4;
    int activeDenominator_ = 4;
    int activeInputBars_ = 2;
    double expectedPpq_ = 0.0;
    bool previousLooping_ = false;
    double previousLoopStart_ = 0.0;
    double previousLoopEnd_ = 0.0;
    Pattern previousPattern_{};
    bool havePattern_ = false;
    bool patternPending_ = false;
    double pendingBeatsRemaining_ = -1.0;
    double pendingBoundarySamples_ = -1.0;
    int editFadeRemaining_ = 0;
    double waveformCycle_ = 0.0;
    bool haveWaveformCycle_ = false;
    SourceTiming sourceTiming_{};
    struct TimingCache {
        double anchor = 0.0;
        bool valid = false;
        std::array<bool, colourCount> decided{};
        std::array<double, colourCount> offsets{};
        std::array<TimingStatus, colourCount> status{};
    };
    std::array<TimingCache, 2> timingCache_{};
    int recentTimingCache_ = 0;
    void clearTiming() noexcept;
    double sourceOffset(const Pattern&, int colour, double anchor, double cellSamples,
                        bool publish) noexcept;
    double detectOffset(double nominal, double radius, TimingStatus&) const noexcept;

    // Ownership: 0 available, 1 audio writer, 2 non-audio reader. The latest
    // published slot is never selected by the writer, even when available.
    struct CaptureBuffer {
        std::vector<std::vector<float>> channels;
        mutable std::atomic<unsigned> ownership{0};
        std::size_t count = 0;
        double bpm = 0.0;
        int bars = 0;
        std::uint64_t serial = 0;
    };
    mutable std::array<CaptureBuffer, 3> captureBuffers_;
    // Low two bits: slot+1, or zero when invalid. Upper bits: revision.
    std::atomic<std::uint64_t> publication_{0};
    int captureSlot_ = -1;
    std::size_t captureCapacity_ = 0;
    std::size_t captureCount_ = 0;
    std::uint64_t capturePublication_ = 0;
    bool captureValid_ = false;
    bool haveCapturePhase_ = false;
    double lastCapturePhase_ = 0.0;
    double captureCycleSamples_ = 0.0;

    void clearStream() noexcept;
    bool isDiscontinuous(const Transport&) const noexcept;
    void processActive(float* const*, int, int, const Transport&, const Pattern&, bool) noexcept;
    void discardCapture() noexcept;
    void completeCapture(double bpm, int bars) noexcept;
    void captureSample(float* const*, int, int, double phase, double cycleSamples,
                       double bpm, int bars, bool valid) noexcept;

    float readHistory(int channel, double position) const noexcept;
};
}
