#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine.h"
#include <atomic>
#include <mutex>
#include <vector>

struct BreakVarPreview
{
    std::array<float, breakvar::waveformSize> waveform{};
    double bpm = 120.0;
    int numerator = 4, denominator = 4;
    float inputPhase = 0.0f, outputPhase = 0.0f;
    int latencySamples = 0;
    breakvar::EngineStatus status = breakvar::EngineStatus::stopped;
    bool playing = false;
    bool presetPending = false;
    double presetBeatsRemaining = -1.0;
    breakvar::SourceTiming sourceTiming;
};

class BreakVarAudioProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    explicit BreakVarAudioProcessor(bool enableTimer = true, juce::File userPresetDirectory = {});
    ~BreakVarAudioProcessor() override;
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "SHFLR"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    breakvar::Pattern getPattern() const;
    bool updatePattern(const breakvar::Pattern&, juce::String& error);
    bool undoPattern();
    bool redoPattern();
    bool canUndo() const;
    bool canRedo() const;
    BreakVarPreview getPreview() const noexcept;
    juce::StringArray getFactoryPresetNames() const;
    bool loadFactoryPreset(int index, juce::String& error);
    juce::String getPresetName() const;
    bool isCurrentUserPreset() const;
    juce::StringArray getUserPresetNames() const;
    bool saveUserPreset(const juce::String& name, bool replaceExisting, juce::String& error);
    bool loadUserPreset(const juce::String& name, juce::String& error);
    bool savePreset(const juce::File&, juce::String& error);
    bool loadPreset(const juce::File&, juce::String& error);
    bool randomiseSlices(std::uint32_t seed, juce::String& error);
    bool shuffleOrder(std::uint32_t seed, juce::String& error);
    bool generateOrder(std::uint32_t seed, juce::String& error);
    bool hasOutputReady() const noexcept;
    bool writeOutputWav(const juce::File&, juce::String& error);
    juce::File createOutputWav(juce::String& error);

private:
    bool applyPattern(const breakvar::Pattern&, const juce::String& name, bool renameUnchanged,
                      juce::String& error, bool userPreset = false);
    void timerCallback() override;
    void process(juce::AudioBuffer<float>&, juce::MidiBuffer&, bool bypass);
    void publishPattern(bool defer = false) noexcept; // Caller owns modelMutex_.
    void receivePattern() noexcept; // Audio thread, bounded seqlock read.
    void publishActivePattern() noexcept;
    bool selectedPatternIsActive() const noexcept;
    bool callbacksAreActive() const noexcept;
    void publishPreview() noexcept;
    mutable std::mutex modelMutex_;
    const juce::File userPresetDirectory_;
    mutable std::mutex outputLifecycleMutex_; // prepare/export only, never the audio callback.
    breakvar::Pattern model_ = breakvar::Pattern::makeDefault();
    juce::String presetName_{"Original"};
    bool currentUserPreset_ = false; // Protected with the name and model by modelMutex_.
    std::vector<breakvar::Pattern> undo_, redo_;
    std::atomic<std::uint32_t> modelRevision_{0};
    std::array<std::atomic<int>, breakvar::maxCells> publishedCells_{};
    std::array<std::atomic<int>, breakvar::maxOrder> publishedOrder_{};
    std::array<std::atomic<int>, breakvar::maxOrder> publishedOrderLengths_{};
    std::atomic<int> publishedSmooth_{20}, publishedTighten_{0};
    std::array<std::atomic<int>, breakvar::colourCount> publishedNudges_{}, publishedManual_{};
    std::atomic<int> publishedInput_{2}, publishedOutput_{4}, publishedGrid_{4}, publishedCount_{6};
    std::atomic<bool> publishedDefer_{false};
    breakvar::Pattern audioPattern_ = breakvar::Pattern::makeDefault();
    bool audioDefer_ = false;
    std::uint32_t audioRevision_ = 0;
    std::atomic<std::uint32_t> processedRevision_{0};
    static constexpr std::size_t soundValuesOffset = 4 + breakvar::maxCells + 2 * breakvar::maxOrder;
    std::array<std::atomic<int>, soundValuesOffset + 2 + 2 * breakvar::colourCount> activePatternValues_{};
    std::atomic<std::uint32_t> activePatternRevision_{0};
    std::atomic<bool> transportPlaying_{false};
    std::atomic<double> lastCallbackMs_{0.0}, callbackTimeoutMs_{500.0};
    std::atomic<double> presetBeatsRemaining_{-1.0};
    std::atomic<std::uint32_t> pendingRevision_{0};
    breakvar::Engine engine_;
    std::array<std::atomic<float>, breakvar::waveformSize> peaks_{};
    std::atomic<double> bpm_{120.0};
    std::atomic<int> numerator_{4}, denominator_{4}, desiredLatency_{0};
    std::atomic<float> inputPhase_{0}, outputPhase_{0};
    std::array<std::atomic<float>, breakvar::colourCount> timingOffsets_{};
    std::array<std::atomic<int>, breakvar::colourCount> timingStatuses_{};
    std::atomic<int> engineStatus_{static_cast<int>(breakvar::EngineStatus::stopped)};
    int previewCountdown_ = 0;
    std::atomic<bool> stateDirty_{false};
    std::atomic<bool> resetRequested_{false};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BreakVarAudioProcessor)
};
