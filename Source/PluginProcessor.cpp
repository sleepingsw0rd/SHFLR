#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PatternLibrary.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr std::size_t undoLimit = 128;
static_assert(std::atomic<int>::is_always_lock_free && std::atomic<float>::is_always_lock_free
              && std::atomic<double>::is_always_lock_free && std::atomic<std::uint32_t>::is_always_lock_free);

breakvar::Transport readTransport(juce::AudioPlayHead* playHead)
{
    breakvar::Transport result;
    if (playHead == nullptr) return result;
    const auto position = playHead->getPosition();
    if (!position) return result;
    const auto bpm = position->getBpm();
    const auto ppq = position->getPpqPosition();
    result.valid = bpm.hasValue() && ppq.hasValue();
    result.playing = position->getIsPlaying();
    if (bpm) result.bpm = *bpm;
    if (ppq) result.ppq = *ppq;
    if (const auto meter = position->getTimeSignature())
    {
        result.numerator = meter->numerator;
        result.denominator = meter->denominator;
    }
    result.looping = position->getIsLooping();
    if (const auto loop = position->getLoopPoints())
    {
        result.loopStartPpq = loop->ppqStart;
        result.loopEndPpq = loop->ppqEnd;
    }
    return result;
}

template <typename T, std::size_t N>
juce::String encode(const std::array<T, N>& values, int count)
{
    juce::StringArray tokens;
    for (int i = 0; i < count; ++i) tokens.add(juce::String(static_cast<int>(values[static_cast<std::size_t>(i)])));
    return tokens.joinIntoString(",");
}

template <std::size_t N>
bool decode(const juce::String& text, std::array<int, N>& values, int count)
{
    if (count < 0 || count > static_cast<int>(N)) return false;
    if (count == 0) return text.isEmpty();
    const auto tokens = juce::StringArray::fromTokens(text, ",", "");
    if (tokens.size() != count) return false;
    for (int i = 0; i < count; ++i)
    {
        // Restrict the spelling and length before converting; never accept partial numbers.
        const auto& token = tokens[i];
        const auto digits = token.startsWithChar('-') ? token.substring(1) : token;
        if (digits.isEmpty() || digits.length() > 3 || !digits.containsOnly("0123456789"))
            return false;
        values[static_cast<std::size_t>(i)] = token.getIntValue();
    }
    return true;
}

void remember(std::vector<breakvar::Pattern>& stack, const breakvar::Pattern& value)
{
    if (stack.size() >= undoLimit) stack.erase(stack.begin());
    stack.push_back(value);
}

juce::XmlElement patternXml(const breakvar::Pattern& pattern, const juce::String& name, bool userPreset = false)
{
    juce::XmlElement state("BreakVarState");
    state.setAttribute("version", 4);
    state.setAttribute("name", name);
    state.setAttribute("userPreset", userPreset);
    state.setAttribute("inputBars", pattern.inputBars);
    state.setAttribute("outputBars", pattern.outputBars);
    state.setAttribute("grid", pattern.grid);
    state.setAttribute("orderCount", pattern.orderCount);
    state.setAttribute("cells", encode(pattern.cells, breakvar::maxCells));
    state.setAttribute("order", encode(pattern.order, pattern.orderCount));
    state.setAttribute("orderLengths", encode(pattern.orderLengths, pattern.orderCount));
    state.setAttribute("smoothTenths", pattern.smoothTenths);
    state.setAttribute("tightenPercent", pattern.tightenPercent);
    state.setAttribute("nudgeTenths", encode(pattern.nudgeTenths, breakvar::colourCount));
    state.setAttribute("nudgeManual", encode(pattern.nudgeManual, breakvar::colourCount));
    return state;
}

bool readPatternXml(const juce::XmlElement& state, breakvar::Pattern& pattern)
{
    const auto version = state.getStringAttribute("version");
    if (!state.hasTagName("BreakVarState") || (version != "1" && version != "2" && version != "3" && version != "4")) return false;
    pattern.inputBars = state.getIntAttribute("inputBars", -1);
    pattern.outputBars = state.getIntAttribute("outputBars", -1);
    pattern.grid = state.getIntAttribute("grid", -1);
    pattern.orderCount = state.getIntAttribute("orderCount", -1);
    pattern.order.fill(-1);
    pattern.orderLengths.fill(0);
    if (!decode(state.getStringAttribute("cells"), pattern.cells, breakvar::maxCells)
        || !decode(state.getStringAttribute("order"), pattern.order, pattern.orderCount)) return false;
    if ((version == "3" || version == "4") && (!state.hasAttribute("orderLengths")
        || !decode(state.getStringAttribute("orderLengths"), pattern.orderLengths, pattern.orderCount))) return false;
    pattern.smoothTenths = 10;
    pattern.tightenPercent = 0;
    pattern.nudgeTenths.fill(0);
    pattern.nudgeManual.fill(false);
    if (version == "4")
    {
        std::array<int, 1> smooth{}, tighten{};
        std::array<int, breakvar::colourCount> manual{};
        if (!decode(state.getStringAttribute("smoothTenths"), smooth, 1)
            || !decode(state.getStringAttribute("tightenPercent"), tighten, 1)
            || !decode(state.getStringAttribute("nudgeTenths"), pattern.nudgeTenths, breakvar::colourCount)
            || !decode(state.getStringAttribute("nudgeManual"), manual, breakvar::colourCount)) return false;
        pattern.smoothTenths = smooth[0]; pattern.tightenPercent = tighten[0];
        for (std::size_t i = 0; i < manual.size(); ++i)
        {
            if (manual[i] != 0 && manual[i] != 1) return false;
            pattern.nudgeManual[i] = manual[i] != 0;
        }
    }
    // Version 1 only defined colour references. A previously malformed old file
    // must never acquire a different meaning when its numeric tokens become rests.
    if (version == "1")
        for (int i = 0; i < pattern.orderCount; ++i)
            if (pattern.order[static_cast<std::size_t>(i)] >= breakvar::colourCount) return false;
    return pattern.validate();
}

bool validateUserPresetName(const juce::String& supplied, juce::String& name, juce::String& error)
{
    name = supplied.trim();
    if (name.isEmpty() || name.length() > 64)
    { error = "Enter a preset name of 1 to 64 characters."; return false; }
    for (auto character : supplied)
        if (character < 32 || (character >= 127 && character <= 159))
        { error = "Preset names cannot contain control characters."; return false; }
    if (name.containsAnyOf("<>:\"/\\|?*") || name == "." || name == ".." || name.endsWithChar('.'))
    { error = "Preset names cannot contain <> : \" / \\ | ? * or end with a period."; return false; }
    return true;
}

// All instances refresh from individual files while holding the same OS lock.
// There is no cached bank to overwrite another instance's newly saved presets.
juce::String userPresetLockName(const juce::File& directory)
{
    return "SHFLR-user-presets-" + juce::String::toHexString(directory.getFullPathName().toLowerCase().hashCode64());
}

struct UserPresetRecord
{
    juce::File file;
    juce::String name;
    breakvar::Pattern pattern;
};

std::vector<UserPresetRecord> readUserPresets(const juce::File& directory)
{
    std::vector<UserPresetRecord> result;
    for (const auto& file : directory.findChildFiles(juce::File::findFiles, false, "*.shflr"))
    {
        if (file.getSize() > 1024 * 1024) continue;
        const auto xml = juce::XmlDocument::parse(file);
        UserPresetRecord record;
        juce::String ignored;
        if (xml && readPatternXml(*xml, record.pattern)
            && validateUserPresetName(xml->getStringAttribute("name"), record.name, ignored))
        {
            record.file = file;
            result.push_back(std::move(record));
        }
    }
    return result;
}
}

BreakVarAudioProcessor::BreakVarAudioProcessor(bool enableTimer, juce::File userPresetDirectory)
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      userPresetDirectory_(userPresetDirectory == juce::File{}
          ? juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("SHFLR/Presets")
          : std::move(userPresetDirectory))
{
    undo_.reserve(undoLimit);
    redo_.reserve(undoLimit);
    publishPattern();
    if (enableTimer) startTimerHz(20);
}

BreakVarAudioProcessor::~BreakVarAudioProcessor() { stopTimer(); }

void BreakVarAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    const std::lock_guard lifecycleLock(outputLifecycleMutex_);
    receivePattern();
    engine_.prepare(sampleRate, samplesPerBlock, getTotalNumInputChannels());
    transportPlaying_.store(false);
    lastCallbackMs_.store(0.0);
    callbackTimeoutMs_.store(std::max(500.0, samplesPerBlock * 3000.0 / sampleRate));
    presetBeatsRemaining_.store(-1.0);
    publishActivePattern();
    processedRevision_.store(audioRevision_);
    // JUCE permits querying the playhead only during an audio callback. The first
    // callback supplies the actual host tempo; later prepares retain that timing.
    const double tempo = bpm_.load();
    const int numerator = numerator_.load();
    const int denominator = denominator_.load();
    const int initialLatency = breakvar::requiredLatencySamples(audioPattern_, sampleRate, tempo, numerator, denominator);
    desiredLatency_.store(initialLatency);
    setLatencySamples(initialLatency);
    previewCountdown_ = 0;
    resetRequested_.store(false);
}

void BreakVarAudioProcessor::releaseResources()
{
    const std::lock_guard lifecycleLock(outputLifecycleMutex_);
    // Hosts may release on Stop. Keep the completed export until a new prepare
    // or transport change; prepareToPlay resets the processing history.
    engine_.finishOutputOnStop();
    transportPlaying_.store(false);
    lastCallbackMs_.store(0.0);
    presetBeatsRemaining_.store(-1.0);
    engineStatus_.store(static_cast<int>(breakvar::EngineStatus::stopped));
}

bool BreakVarAudioProcessor::isBusesLayoutSupported(const BusesLayout& layout) const
{
    const auto input = layout.getMainInputChannelSet();
    return (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo())
        && input == layout.getMainOutputChannelSet();
}

void BreakVarAudioProcessor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    process(audio, midi, false);
}

void BreakVarAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    process(audio, midi, true);
}

void BreakVarAudioProcessor::process(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi, bool bypass)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    const bool continuingCallbacks = callbacksAreActive();
    receivePattern();
    if (resetRequested_.exchange(false)) engine_.reset();
    const auto transport = readTransport(getPlayHead());
    if (transport.valid && std::isfinite(transport.bpm) && transport.bpm > 0
        && transport.numerator > 0 && transport.denominator > 0)
    {
        bpm_.store(transport.bpm, std::memory_order_relaxed);
        numerator_.store(transport.numerator, std::memory_order_relaxed);
        denominator_.store(transport.denominator, std::memory_order_relaxed);
    }
    const int channelCount = std::min(audio.getNumChannels(), getTotalNumInputChannels());
    if (audio.getNumSamples() <= 0 || channelCount <= 0)
    {
        // Hosts may send transport-only callbacks. Without rendered frames the
        // selected revision has not become audible and cannot own the old WAV.
        if (!transport.valid || !transport.playing)
        {
            engine_.finishOutputOnStop();
            transportPlaying_.store(false);
            presetBeatsRemaining_.store(-1.0);
            engineStatus_.store(static_cast<int>(transport.valid ? breakvar::EngineStatus::stopped
                                                                : breakvar::EngineStatus::waitingForTransport));
        }
        return;
    }
    for (int ch = channelCount; ch < audio.getNumChannels(); ++ch) audio.clear(ch, 0, audio.getNumSamples());
    engine_.process(audio.getArrayOfWritePointers(), channelCount, audio.getNumSamples(), transport, audioPattern_, bypass,
                    audioDefer_ && continuingCallbacks);
    if (!engine_.isPatternPending())
    {
        if (processedRevision_.load() != audioRevision_) publishActivePattern();
        processedRevision_.store(audioRevision_);
    }
    presetBeatsRemaining_.store(engine_.getPendingBeatsRemaining());
    pendingRevision_.store(audioRevision_);
    engineStatus_.store(static_cast<int>(engine_.getStatus()));
    transportPlaying_.store(transport.valid && transport.playing);
    callbackTimeoutMs_.store(std::max(500.0, audio.getNumSamples() * 3000.0 / std::max(1.0, getSampleRate())));
    lastCallbackMs_.store(juce::Time::getMillisecondCounterHiRes());
    // Host notifications happen in timerCallback, never in processBlock.
    if (transport.valid) desiredLatency_.store(engine_.getLatencySamples(), std::memory_order_relaxed);
    previewCountdown_ -= audio.getNumSamples();
    if (previewCountdown_ <= 0)
    {
        publishPreview();
        previewCountdown_ = std::max(1, static_cast<int>(getSampleRate() / 30.0));
    }
}

juce::AudioProcessorEditor* BreakVarAudioProcessor::createEditor()
{
    return new BreakVarAudioProcessorEditor(*this);
}

double BreakVarAudioProcessor::getTailLengthSeconds() const
{
    const double bpm = bpm_.load(std::memory_order_relaxed);
    const int denominator = denominator_.load(std::memory_order_relaxed);
    if (!std::isfinite(bpm) || bpm <= 0 || denominator <= 0) return 0;
    const double bars = publishedInput_.load() + publishedOutput_.load();
    return std::clamp(bars * 60.0 / bpm * numerator_.load() * 4.0 / denominator, 0.0, 120.0);
}

breakvar::Pattern BreakVarAudioProcessor::getPattern() const
{
    const std::lock_guard lock(modelMutex_);
    return model_;
}

bool BreakVarAudioProcessor::updatePattern(const breakvar::Pattern& pattern, juce::String& error)
{
    return applyPattern(pattern, "Custom", false, error);
}

bool BreakVarAudioProcessor::applyPattern(const breakvar::Pattern& pattern, const juce::String& name,
                                        bool renameUnchanged, juce::String& error, bool userPreset)
{
    std::string reason;
    if (!pattern.validate(&reason)) { error = reason; return false; }
    const std::lock_guard lock(modelMutex_);
    error.clear();
    if (pattern == model_)
    {
        if (renameUnchanged)
        {
            presetName_ = name;
            currentUserPreset_ = userPreset;
            stateDirty_.store(true);
        }
        return true;
    }
    remember(undo_, model_);
    redo_.clear();
    model_ = pattern;
    presetName_ = name;
    currentUserPreset_ = userPreset;
    publishPattern(renameUnchanged);
    stateDirty_.store(true);
    return true;
}

bool BreakVarAudioProcessor::undoPattern()
{
    const std::lock_guard lock(modelMutex_);
    if (undo_.empty()) return false;
    remember(redo_, model_);
    model_ = undo_.back();
    presetName_ = "Custom";
    currentUserPreset_ = false;
    undo_.pop_back();
    publishPattern();
    stateDirty_.store(true);
    return true;
}

bool BreakVarAudioProcessor::redoPattern()
{
    const std::lock_guard lock(modelMutex_);
    if (redo_.empty()) return false;
    remember(undo_, model_);
    model_ = redo_.back();
    presetName_ = "Custom";
    currentUserPreset_ = false;
    redo_.pop_back();
    publishPattern();
    stateDirty_.store(true);
    return true;
}

bool BreakVarAudioProcessor::canUndo() const { const std::lock_guard lock(modelMutex_); return !undo_.empty(); }
bool BreakVarAudioProcessor::canRedo() const { const std::lock_guard lock(modelMutex_); return !redo_.empty(); }

void BreakVarAudioProcessor::publishPattern(bool defer) noexcept
{
    // All mailbox operations use the single sequentially-consistent atomic order.
    // The audio reader accepts only reads wholly between matching even revisions.
    modelRevision_.fetch_add(1);
    publishedDefer_.store(defer);
    publishedInput_.store(model_.inputBars);
    publishedOutput_.store(model_.outputBars);
    publishedGrid_.store(model_.grid);
    publishedCount_.store(model_.orderCount);
    publishedSmooth_.store(model_.smoothTenths);
    publishedTighten_.store(model_.tightenPercent);
    for (std::size_t i = 0; i < model_.nudgeTenths.size(); ++i)
    { publishedNudges_[i].store(model_.nudgeTenths[i]); publishedManual_[i].store(model_.nudgeManual[i]); }
    for (std::size_t i = 0; i < model_.cells.size(); ++i) publishedCells_[i].store(model_.cells[i]);
    for (std::size_t i = 0; i < model_.order.size(); ++i) publishedOrder_[i].store(model_.order[i]);
    for (std::size_t i = 0; i < model_.orderLengths.size(); ++i) publishedOrderLengths_[i].store(model_.orderLengths[i]);
    modelRevision_.fetch_add(1);
}

void BreakVarAudioProcessor::receivePattern() noexcept
{
    const auto before = modelRevision_.load();
    if ((before & 1u) != 0 || before == audioRevision_) return;
    breakvar::Pattern candidate;
    candidate.inputBars = publishedInput_.load();
    candidate.outputBars = publishedOutput_.load();
    candidate.grid = publishedGrid_.load();
    candidate.orderCount = publishedCount_.load();
    candidate.smoothTenths = publishedSmooth_.load();
    candidate.tightenPercent = publishedTighten_.load();
    for (std::size_t i = 0; i < candidate.nudgeTenths.size(); ++i)
    { candidate.nudgeTenths[i] = publishedNudges_[i].load(); candidate.nudgeManual[i] = publishedManual_[i].load() != 0; }
    const bool defer = publishedDefer_.load();
    for (std::size_t i = 0; i < candidate.cells.size(); ++i) candidate.cells[i] = publishedCells_[i].load();
    for (std::size_t i = 0; i < candidate.order.size(); ++i) candidate.order[i] = publishedOrder_[i].load();
    for (std::size_t i = 0; i < candidate.orderLengths.size(); ++i) candidate.orderLengths[i] = publishedOrderLengths_[i].load();
    const auto after = modelRevision_.load();
    if (before == after)
    {
        audioPattern_ = candidate;
        audioDefer_ = defer;
        audioRevision_ = after;
    }
}

void BreakVarAudioProcessor::publishActivePattern() noexcept
{
    activePatternRevision_.fetch_add(1);
    activePatternValues_[0].store(audioPattern_.inputBars);
    activePatternValues_[1].store(audioPattern_.outputBars);
    activePatternValues_[2].store(audioPattern_.grid);
    activePatternValues_[3].store(audioPattern_.orderCount);
    activePatternValues_[soundValuesOffset].store(audioPattern_.smoothTenths);
    activePatternValues_[soundValuesOffset + 1].store(audioPattern_.tightenPercent);
    for (std::size_t i = 0; i < audioPattern_.nudgeTenths.size(); ++i)
    {
        activePatternValues_[soundValuesOffset + 2 + i].store(audioPattern_.nudgeTenths[i]);
        activePatternValues_[soundValuesOffset + 2 + breakvar::colourCount + i].store(audioPattern_.nudgeManual[i]);
    }
    for (std::size_t i = 0; i < audioPattern_.cells.size(); ++i)
        activePatternValues_[4 + i].store(audioPattern_.cells[i]);
    for (std::size_t i = 0; i < audioPattern_.order.size(); ++i)
        activePatternValues_[4 + breakvar::maxCells + i].store(audioPattern_.order[i]);
    for (std::size_t i = 0; i < audioPattern_.orderLengths.size(); ++i)
        activePatternValues_[4 + breakvar::maxCells + breakvar::maxOrder + i].store(audioPattern_.orderLengths[i]);
    activePatternRevision_.fetch_add(1);
}

bool BreakVarAudioProcessor::selectedPatternIsActive() const noexcept
{
    const auto modelBefore = modelRevision_.load();
    const auto activeBefore = activePatternRevision_.load();
    if ((modelBefore & 1u) != 0 || (activeBefore & 1u) != 0) return false;
    if (publishedInput_.load() != activePatternValues_[0].load()
        || publishedOutput_.load() != activePatternValues_[1].load()
        || publishedGrid_.load() != activePatternValues_[2].load()
        || publishedCount_.load() != activePatternValues_[3].load()) return false;
    if (publishedSmooth_.load() != activePatternValues_[soundValuesOffset].load()
        || publishedTighten_.load() != activePatternValues_[soundValuesOffset + 1].load()) return false;
    for (std::size_t i = 0; i < publishedNudges_.size(); ++i)
        if (publishedNudges_[i].load() != activePatternValues_[soundValuesOffset + 2 + i].load()
            || publishedManual_[i].load() != activePatternValues_[soundValuesOffset + 2 + breakvar::colourCount + i].load()) return false;
    for (std::size_t i = 0; i < publishedCells_.size(); ++i)
        if (publishedCells_[i].load() != activePatternValues_[4 + i].load()) return false;
    for (std::size_t i = 0; i < publishedOrder_.size(); ++i)
        if (publishedOrder_[i].load() != activePatternValues_[4 + breakvar::maxCells + i].load()) return false;
    for (std::size_t i = 0; i < publishedOrderLengths_.size(); ++i)
        if (publishedOrderLengths_[i].load() != activePatternValues_[4 + breakvar::maxCells + breakvar::maxOrder + i].load()) return false;
    return modelBefore == modelRevision_.load() && activeBefore == activePatternRevision_.load();
}

bool BreakVarAudioProcessor::callbacksAreActive() const noexcept
{
    const double last = lastCallbackMs_.load();
    return transportPlaying_.load() && last > 0.0
        && juce::Time::getMillisecondCounterHiRes() - last <= callbackTimeoutMs_.load();
}

void BreakVarAudioProcessor::publishPreview() noexcept
{
    std::array<float, breakvar::waveformSize> waveform;
    engine_.copyWaveform(waveform);
    for (std::size_t i = 0; i < waveform.size(); ++i) peaks_[i].store(waveform[i], std::memory_order_relaxed);
    inputPhase_.store(engine_.getInputPhase(), std::memory_order_relaxed);
    outputPhase_.store(engine_.getOutputPhase(), std::memory_order_relaxed);
    engineStatus_.store(static_cast<int>(engine_.getStatus()), std::memory_order_relaxed);
    const auto timing = engine_.getSourceTiming();
    for (std::size_t i = 0; i < timing.offsetsMs.size(); ++i)
    {
        timingOffsets_[i].store(timing.offsetsMs[i], std::memory_order_relaxed);
        timingStatuses_[i].store(static_cast<int>(timing.status[i]), std::memory_order_relaxed);
    }
}

BreakVarPreview BreakVarAudioProcessor::getPreview() const noexcept
{
    BreakVarPreview result;
    for (std::size_t i = 0; i < result.waveform.size(); ++i) result.waveform[i] = peaks_[i].load(std::memory_order_relaxed);
    result.bpm = bpm_.load(std::memory_order_relaxed);
    result.numerator = numerator_.load(std::memory_order_relaxed);
    result.denominator = denominator_.load(std::memory_order_relaxed);
    result.inputPhase = inputPhase_.load(std::memory_order_relaxed);
    result.outputPhase = outputPhase_.load(std::memory_order_relaxed);
    result.status = static_cast<breakvar::EngineStatus>(engineStatus_.load(std::memory_order_relaxed));
    result.latencySamples = desiredLatency_.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < result.sourceTiming.offsetsMs.size(); ++i)
    {
        result.sourceTiming.offsetsMs[i] = timingOffsets_[i].load(std::memory_order_relaxed);
        result.sourceTiming.status[i] = static_cast<breakvar::TimingStatus>(timingStatuses_[i].load(std::memory_order_relaxed));
    }
    result.playing = callbacksAreActive();
    const auto revision = modelRevision_.load();
    result.presetPending = result.playing && publishedDefer_.load()
        && revision != processedRevision_.load() && !selectedPatternIsActive();
    if (result.presetPending && pendingRevision_.load() == revision)
        result.presetBeatsRemaining = presetBeatsRemaining_.load();
    // A stopped host may suspend callbacks. Never display an old source offset
    // against a newly selected or manually edited arrangement while waiting.
    if (!selectedPatternIsActive()) result.sourceTiming = {};
    return result;
}

void BreakVarAudioProcessor::timerCallback()
{
    const int desired = desiredLatency_.load(std::memory_order_relaxed);
    if (desired != getLatencySamples()) setLatencySamples(desired);
    if (stateDirty_.exchange(false)) updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
}

void BreakVarAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    const std::lock_guard lock(modelMutex_);
    const auto state = patternXml(model_, presetName_, currentUserPreset_);
    copyXmlToBinary(state, destination);
}

void BreakVarAudioProcessor::setStateInformation(const void* data, int bytes)
{
    if (data == nullptr || bytes <= 0 || bytes > 1024 * 1024) return;
    const auto state = getXmlFromBinary(data, bytes);
    if (!state) return;
    auto pattern = breakvar::Pattern::makeDefault();
    if (!readPatternXml(*state, pattern)) return;
    const std::lock_guard lock(modelMutex_);
    model_ = pattern;
    presetName_ = state->getStringAttribute("name", "Custom").substring(0, 128);
    currentUserPreset_ = state->getBoolAttribute("userPreset", false);
    undo_.clear();
    redo_.clear();
    publishPattern();
    resetRequested_.store(true);
}

juce::StringArray BreakVarAudioProcessor::getFactoryPresetNames() const
{
    juce::StringArray result;
    for (const auto& item : breakvar::factoryPatterns()) result.add(item.name);
    return result;
}

juce::String BreakVarAudioProcessor::getPresetName() const
{
    const std::lock_guard lock(modelMutex_);
    return presetName_;
}

bool BreakVarAudioProcessor::isCurrentUserPreset() const
{
    const std::lock_guard lock(modelMutex_);
    return currentUserPreset_;
}

bool BreakVarAudioProcessor::loadFactoryPreset(int index, juce::String& error)
{
    const auto& presets = breakvar::factoryPatterns();
    if (index < 0 || index >= static_cast<int>(presets.size())) { error = "Unknown factory preset."; return false; }
    const auto& preset = presets[static_cast<std::size_t>(index)];
    return applyPattern(preset.pattern, preset.name, true, error);
}

juce::StringArray BreakVarAudioProcessor::getUserPresetNames() const
{
    juce::InterProcessLock libraryLock(userPresetLockName(userPresetDirectory_));
    const juce::InterProcessLock::ScopedLockType locked(libraryLock);
    juce::StringArray names;
    if (!locked.isLocked()) return names;
    for (const auto& preset : readUserPresets(userPresetDirectory_))
        names.addIfNotAlreadyThere(preset.name, true);
    names.sort(true);
    return names;
}

bool BreakVarAudioProcessor::saveUserPreset(const juce::String& supplied, bool replaceExisting, juce::String& error)
{
    juce::String name;
    if (!validateUserPresetName(supplied, name, error)) return false;
    juce::InterProcessLock libraryLock(userPresetLockName(userPresetDirectory_));
    const juce::InterProcessLock::ScopedLockType locked(libraryLock);
    if (!locked.isLocked()) { error = "Could not lock the user preset library. Try again."; return false; }
    if (userPresetDirectory_.createDirectory().failed())
    { error = "Could not create the user preset library. Check folder permissions."; return false; }
    juce::File target;
    for (const auto& preset : readUserPresets(userPresetDirectory_))
    {
        if (!preset.name.equalsIgnoreCase(name)) continue;
        if (target != juce::File{})
        { error = "More than one user preset has this name. Choose a different name."; return false; }
        target = preset.file;
    }
    if (target != juce::File{} && !replaceExisting)
    { error = "A user preset with this name already exists. Confirm replacement or choose another name."; return false; }
    if (target == juce::File{})
        do { target = userPresetDirectory_.getChildFile(juce::Uuid().toString() + ".shflr"); }
        while (target.exists());

    // Keep the serialized arrangement, successful save name and project state
    // together even if another thread edits the processor during disk I/O.
    const std::lock_guard modelLock(modelMutex_);
    juce::TemporaryFile temporary(target);
    const auto xml = patternXml(model_, name, true);
    if (!xml.writeTo(temporary.getFile()) || !temporary.overwriteTargetFileWithTemporary())
    { error = "Could not save the user preset. Check folder permissions and free disk space."; return false; }
    presetName_ = name;
    currentUserPreset_ = true;
    stateDirty_.store(true);
    error.clear();
    return true;
}

bool BreakVarAudioProcessor::loadUserPreset(const juce::String& supplied, juce::String& error)
{
    juce::String name;
    if (!validateUserPresetName(supplied, name, error)) return false;
    juce::InterProcessLock libraryLock(userPresetLockName(userPresetDirectory_));
    const juce::InterProcessLock::ScopedLockType locked(libraryLock);
    if (!locked.isLocked()) { error = "Could not lock the user preset library. Try again."; return false; }
    const auto presets = readUserPresets(userPresetDirectory_);
    const UserPresetRecord* found = nullptr;
    for (const auto& preset : presets)
    {
        if (!preset.name.equalsIgnoreCase(name)) continue;
        if (found != nullptr)
        { error = "More than one user preset has this name. Choose another preset."; return false; }
        found = &preset;
    }
    if (found == nullptr) { error = "User preset is missing or invalid."; return false; }
    return applyPattern(found->pattern, found->name, true, error, true);
}

bool BreakVarAudioProcessor::savePreset(const juce::File& file, juce::String& error)
{
    const std::lock_guard lock(modelMutex_);
    const auto name = file.getFileNameWithoutExtension().substring(0, 128);
    juce::TemporaryFile temporary(file);
    const auto xml = patternXml(model_, name);
    if (!xml.writeTo(temporary.getFile()) || !temporary.overwriteTargetFileWithTemporary())
    { error = "Could not save preset. Choose a writable folder."; return false; }
    presetName_ = name;
    currentUserPreset_ = false;
    stateDirty_.store(true);
    error.clear();
    return true;
}

bool BreakVarAudioProcessor::loadPreset(const juce::File& file, juce::String& error)
{
    if (!file.existsAsFile() || file.getSize() > 1024 * 1024)
    { error = "Preset is missing or too large."; return false; }
    const auto xml = juce::XmlDocument::parse(file);
    breakvar::Pattern pattern;
    if (!xml || !readPatternXml(*xml, pattern)) { error = "Invalid SHFLR preset."; return false; }
    return applyPattern(pattern, xml->getStringAttribute("name", file.getFileNameWithoutExtension()).substring(0, 128), true, error);
}

bool BreakVarAudioProcessor::randomiseSlices(std::uint32_t seed, juce::String& error)
{
    auto pattern = getPattern();
    std::string reason;
    if (!breakvar::randomiseSlices(pattern, seed, &reason)) { error = reason; return false; }
    return updatePattern(pattern, error);
}

bool BreakVarAudioProcessor::shuffleOrder(std::uint32_t seed, juce::String& error)
{
    auto pattern = getPattern();
    std::string reason;
    if (!breakvar::shuffleOrder(pattern, seed, &reason)) { error = reason; return false; }
    return updatePattern(pattern, error);
}

bool BreakVarAudioProcessor::generateOrder(std::uint32_t seed, juce::String& error)
{
    auto pattern = getPattern();
    std::string reason;
    if (!breakvar::generateOrder(pattern, seed, &reason)) { error = reason; return false; }
    return updatePattern(pattern, error);
}

bool BreakVarAudioProcessor::hasOutputReady() const noexcept
{
    return modelRevision_.load() == processedRevision_.load() && engine_.hasOutputReady();
}

bool BreakVarAudioProcessor::writeOutputWav(const juce::File& file, juce::String& error)
{
    const std::lock_guard lifecycleLock(outputLifecycleMutex_);
    const auto revision = modelRevision_.load();
    breakvar::OutputSnapshot snapshot;
    if (!hasOutputReady() || !engine_.copyLatestOutput(snapshot) || modelRevision_.load() != revision
        || snapshot.channels.empty() || snapshot.channels.front().empty())
    { error = "Play a complete output loop first (maximum 32 seconds)."; return false; }
    juce::TemporaryFile temporary(file);
    std::unique_ptr<juce::OutputStream> stream = temporary.getFile().createOutputStream();
    if (!stream) { error = "Could not create WAV. Choose a writable folder."; return false; }
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions{}
        .withSampleRate(snapshot.sampleRate).withNumChannels(static_cast<int>(snapshot.channels.size()))
        .withBitsPerSample(24));
    if (!writer) { error = "Could not create WAV writer."; return false; }
    std::vector<const float*> channels;
    for (const auto& channel : snapshot.channels) channels.push_back(channel.data());
    if (!writer->writeFromFloatArrays(channels.data(), static_cast<int>(channels.size()),
                                     static_cast<int>(snapshot.channels.front().size())))
    { error = "Could not finish writing WAV."; return false; }
    writer.reset();
    if (!temporary.overwriteTargetFileWithTemporary()) { error = "Could not save WAV."; return false; }
    error.clear();
    return true;
}

juce::File BreakVarAudioProcessor::createOutputWav(juce::String& error)
{
    const auto directory = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("SHFLR Exports");
    if (directory.createDirectory().failed()) { error = "Could not create SHFLR Exports folder."; return {}; }
    const auto file = directory.getChildFile("SHFLR-" + juce::Uuid().toString() + ".wav");
    return writeOutputWav(file, error) ? file : juce::File{};
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BreakVarAudioProcessor(); }
