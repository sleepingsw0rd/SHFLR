#include "PluginProcessor.h"
#include "PatternLibrary.h"
#include "SamplerDesk.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace
{
constexpr int defaultBlockSize = 512;
constexpr double maximumFileSeconds = 120.0;

struct LoadedAudio
{
    juce::AudioBuffer<float> samples;
    double sampleRate = 0.0;
    double originalSampleRate = 0.0;
    double originalSeconds = 0.0;
};

bool loadAudio(const juce::File& file, LoadedAudio& audio, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr) { error = "Cannot read audio file: " + file.getFullPathName(); return false; }
    const auto frames = reader->lengthInSamples;
    if (!std::isfinite(reader->sampleRate) || reader->sampleRate < 8000.0
        || reader->sampleRate > 384000.0 || frames < 1
        || frames > std::numeric_limits<int>::max()
        || static_cast<double>(frames) / reader->sampleRate > maximumFileSeconds
        || reader->numChannels < 1)
    {
        error = "Choose an audio file of 120 seconds or less, at 8-384 kHz.";
        return false;
    }
    audio.samples.setSize(2, static_cast<int>(frames));
    if (!reader->read(&audio.samples, 0, static_cast<int>(frames), 0, true, reader->numChannels > 1))
    { error = "Could not decode all audio samples."; return false; }
    if (reader->numChannels == 1)
        audio.samples.copyFrom(1, 0, audio.samples, 0, 0, audio.samples.getNumSamples());
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < audio.samples.getNumSamples(); ++sample)
            if (!std::isfinite(audio.samples.getSample(channel, sample)))
            { error = "Audio file contains non-finite samples."; return false; }
    audio.sampleRate = audio.originalSampleRate = reader->sampleRate;
    audio.originalSeconds = static_cast<double>(frames) / reader->sampleRate;
    return true;
}

// Called only before adding the device callback, never from its realtime callback.
juce::AudioBuffer<float> resample(LoadedAudio& audio, double destinationRate)
{
    if (audio.sampleRate == destinationRate) return audio.samples;
    const auto length = static_cast<int>(std::llround(audio.samples.getNumSamples()
                                                      * destinationRate / audio.sampleRate));
    juce::AudioBuffer<float> result(2, std::max(1, length));
    juce::MemoryAudioSource source(audio.samples, false, true);
    juce::ResamplingAudioSource converter(&source, false, 2);
    converter.setResamplingRatio(audio.sampleRate / destinationRate);
    converter.prepareToPlay(4096, destinationRate);
    for (int offset = 0; offset < result.getNumSamples(); offset += 4096)
        converter.getNextAudioBlock({ &result, offset, std::min(4096, result.getNumSamples() - offset) });
    converter.releaseResources();
    return result;
}

class LocalPlayHead final : public juce::AudioPlayHead
{
public:
    double sampleRate = 44100.0;
    double bpm = 120.0;
    juce::int64 frame = 0;
    bool playing = false;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo position;
        const double ppq = static_cast<double>(frame) / sampleRate * bpm / 60.0;
        position.setTimeInSamples(frame);
        position.setTimeInSeconds(static_cast<double>(frame) / sampleRate);
        position.setBpm(bpm);
        position.setPpqPosition(ppq);
        position.setPpqPositionOfLastBarStart(std::floor(ppq / 4.0) * 4.0);
        position.setTimeSignature(TimeSignature { 4, 4 });
        position.setIsPlaying(playing);
        // The source file loops, but the host timeline runs continuously.
        position.setIsLooping(false);
        return position;
    }
};

void fillSource(juce::AudioBuffer<float>& block, const juce::AudioBuffer<float>& source,
                juce::int64 frame, int count) noexcept
{
    int destination = 0;
    const int length = source.getNumSamples();
    while (destination < count)
    {
        const int offset = static_cast<int>((frame + destination) % length);
        const int amount = std::min(count - destination, length - offset);
        for (int channel = 0; channel < 2; ++channel)
            block.copyFrom(channel, destination, source, channel, offset, amount);
        destination += amount;
    }
}

juce::String usage()
{
    return "SHFLR --render <input.wav> <output.wav> [factoryIndex] [sourceBars] "
           "[--sample-rate Hz] [--block-size samples] [--smooth-ms 0..10] [--tighten 0..100] "
           "[--nudge-colour 0..7 --nudge-ms -30..30]\n"
           "sourceBars: 1, 2 or 4; factoryIndex is zero based; defaults: factory 0, its input bars.\n"
           "A JSON report is written beside the WAV as <output.wav>.render.json.\n"
           "SHFLR --catalog <output.json> exports factory indices, categories and arrangements without a GUI.\n";
}

int reportFailure(const juce::String& error)
{
    const auto message = "SHFLR: " + error + "\n";
    std::fputs(message.toRawUTF8(), stderr);
    juce::Logger::outputDebugString(message);
    return 1;
}

int catalogCommand(const juce::StringArray& arguments)
{
    if (arguments.size() != 2) return reportFailure("Supply one catalog output JSON path.\n" + usage());
    const auto destination = juce::File::getCurrentWorkingDirectory().getChildFile(arguments[1]);
    if (!destination.hasFileExtension("json")) return reportFailure("Catalog output must have a .json extension.");
    juce::Array<juce::var> entries;
    const auto& factories = breakvar::factoryPatterns();
    for (std::size_t index = 0; index < factories.size(); ++index)
    {
        const auto& factory = factories[index];
        const auto& pattern = factory.pattern;
        auto* entry = new juce::DynamicObject();
        juce::var value(entry);
        entry->setProperty("index", static_cast<int>(index));
        entry->setProperty("name", juce::String(factory.name));
        entry->setProperty("category", juce::String(factory.category));
        entry->setProperty("sourceBars", pattern.inputBars);
        entry->setProperty("outputBars", pattern.outputBars);
        entry->setProperty("grid", pattern.grid);
        juce::Array<juce::var> cells, order, lengths;
        for (int cell = 0; cell < pattern.activeCells(); ++cell) cells.add(pattern.cells[cell]);
        for (int item = 0; item < pattern.orderCount; ++item)
        { order.add(pattern.order[item]); lengths.add(pattern.orderLengths[item]); }
        entry->setProperty("cells", cells);
        entry->setProperty("order", order);
        entry->setProperty("orderLengths", lengths);
        entries.add(value);
    }
    auto* metadata = new juce::DynamicObject();
    juce::var catalog(metadata);
    metadata->setProperty("application", "SHFLR " SHFLR_VERSION);
    metadata->setProperty("presets", entries);
    if (!destination.replaceWithText(juce::JSON::toString(catalog, true)))
        return reportFailure("Could not write factory catalog.");
    return 0;
}

bool positiveInteger(const juce::String& value, int& result)
{
    if (value.isEmpty() || value.length() > 7 || !value.containsOnly("0123456789")) return false;
    result = value.getIntValue();
    return true;
}

bool tenthsMilliseconds(const juce::String& value, double minimum, double maximum, int& result)
{
    if (value.isEmpty() || value.length() > 12 || !value.containsOnly("-0123456789.")) return false;
    const auto utf8 = value.toUTF8();
    char* end = nullptr;
    const double parsed = std::strtod(utf8.getAddress(), &end);
    if (end == utf8.getAddress() || *end != '\0' || !std::isfinite(parsed)
        || parsed < minimum || parsed > maximum) return false;
    result = static_cast<int>(std::llround(parsed * 10.0));
    return true;
}

int renderCommand(const juce::StringArray& arguments)
{
    if (arguments.size() < 3) return reportFailure(usage());
    int factory = 0, sourceBars = 0, blockSize = defaultBlockSize, requestedRate = 0;
    int smoothTenths = -1, tightenPercent = -1, nudgeColour = -1, nudgeTenths = 0;
    bool haveNudge = false;
    int position = 3;
    if (position < arguments.size() && !arguments[position].startsWith("--"))
        if (!positiveInteger(arguments[position++], factory)) return reportFailure("Invalid factory index.");
    if (position < arguments.size() && !arguments[position].startsWith("--"))
        if (!positiveInteger(arguments[position++], sourceBars) || sourceBars == 0)
            return reportFailure("Source bars must be 1, 2 or 4.");
    while (position < arguments.size())
    {
        const auto option = arguments[position++];
        if (position >= arguments.size()) return reportFailure("Missing option value.\n" + usage());
        const auto optionValue = arguments[position++];
        if (option == "--smooth-ms")
        {
            if (!tenthsMilliseconds(optionValue, 0.0, 10.0, smoothTenths)) return reportFailure("Smooth must be 0 to 10 ms.");
            continue;
        }
        if (option == "--nudge-ms")
        {
            if (!tenthsMilliseconds(optionValue, -30.0, 30.0, nudgeTenths)) return reportFailure("Nudge must be -30 to 30 ms.");
            haveNudge = true; continue;
        }
        int value = 0;
        if (!positiveInteger(optionValue, value)) return reportFailure("Invalid option value.");
        if (option == "--sample-rate" && value >= 8000 && value <= 192000) requestedRate = value;
        else if (option == "--block-size" && value >= 1 && value <= 8192) blockSize = value;
        else if (option == "--tighten" && value <= 100) tightenPercent = value;
        else if (option == "--nudge-colour" && value < breakvar::colourCount) nudgeColour = value;
        else return reportFailure("Unknown option or unsupported value: " + option);
    }
    if (sourceBars != 0 && sourceBars != 1 && sourceBars != 2 && sourceBars != 4)
        return reportFailure("Source bars must be 1, 2 or 4.");
    if (haveNudge != (nudgeColour >= 0)) return reportFailure("Nudge requires both --nudge-colour and --nudge-ms.");

    const auto input = juce::File::getCurrentWorkingDirectory().getChildFile(arguments[1]);
    const auto output = juce::File::getCurrentWorkingDirectory().getChildFile(arguments[2]);
    if (input == output) return reportFailure("Input and output must be different files.");
    LoadedAudio loaded;
    juce::String error;
    if (!loadAudio(input, loaded, error)) return reportFailure(error);
    // No GUI initialiser, editor, device manager, or processor timer on this path.
    BreakVarAudioProcessor processor(false);
    if (!processor.loadFactoryPreset(factory, error)) return reportFailure(error);
    auto pattern = processor.getPattern();
    const auto factoryName = processor.getPresetName();
    if (smoothTenths >= 0) pattern.smoothTenths = smoothTenths;
    if (tightenPercent >= 0) pattern.tightenPercent = tightenPercent;
    if (haveNudge)
    { pattern.nudgeManual[static_cast<std::size_t>(nudgeColour)] = true; pattern.nudgeTenths[static_cast<std::size_t>(nudgeColour)] = nudgeTenths; }
    if (!processor.updatePattern(pattern, error)) return reportFailure(error);
    if (sourceBars == 0) sourceBars = pattern.inputBars;
    const double rate = requestedRate > 0 ? requestedRate : loaded.sampleRate;
    auto source = resample(loaded, rate);
    const double sourceSeconds = source.getNumSamples() / rate;
    const double bpm = sourceBars * 240.0 / sourceSeconds;
    const double outputSeconds = pattern.outputBars * 240.0 / bpm;
    if (outputSeconds > 32.0 || (pattern.inputBars + pattern.outputBars) * 240.0 / bpm > 120.0)
        return reportFailure("This tempo and pattern exceed the 32-second export or 120-second history limit.");
    LocalPlayHead playhead;
    playhead.sampleRate = rate;
    playhead.bpm = bpm;
    playhead.playing = true;
    processor.setPlayHead(&playhead);
    processor.setPlayConfigDetails(2, 2, rate, blockSize);
    processor.setNonRealtime(true);
    processor.prepareToPlay(rate, blockSize);
    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    const auto minimumFrames = static_cast<juce::int64>(std::ceil(rate *
        (2.0 * pattern.inputBars * 240.0 / bpm + 2.0 * outputSeconds)));
    const auto maximumFrames = minimumFrames + static_cast<juce::int64>(std::ceil(rate * outputSeconds * 2.0))
                                              + blockSize * 2;
    while (playhead.frame < maximumFrames)
    {
        fillSource(buffer, source, playhead.frame, blockSize);
        processor.processBlock(buffer, midi);
        playhead.frame += blockSize;
        if (playhead.frame >= minimumFrames && processor.hasOutputReady()) break;
    }
    if (!processor.hasOutputReady()) return reportFailure("No complete valid output cycle was captured.");
    if (!processor.writeOutputWav(output, error)) return reportFailure(error);
    juce::AudioFormatManager outputFormats;
    outputFormats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> exported(outputFormats.createReaderFor(output));
    if (exported == nullptr) return reportFailure("Could not read back the exported WAV header.");

    auto* metadata = new juce::DynamicObject();
    juce::var report(metadata);
    metadata->setProperty("application", "SHFLR " SHFLR_VERSION);
    metadata->setProperty("input", input.getFullPathName());
    metadata->setProperty("output", output.getFullPathName());
    metadata->setProperty("factoryIndex", factory);
    metadata->setProperty("factoryName", factoryName);
    metadata->setProperty("sourceBars", sourceBars);
    metadata->setProperty("patternInputBars", pattern.inputBars);
    metadata->setProperty("outputBars", pattern.outputBars);
    metadata->setProperty("sourceSampleRate", loaded.originalSampleRate);
    metadata->setProperty("sourceSeconds", loaded.originalSeconds);
    metadata->setProperty("sampleRate", rate);
    metadata->setProperty("blockSize", blockSize);
    metadata->setProperty("bpm", bpm);
    metadata->setProperty("expectedOutputSeconds", outputSeconds);
    metadata->setProperty("outputFrames", exported->lengthInSamples);
    metadata->setProperty("outputSeconds", static_cast<double>(exported->lengthInSamples) / exported->sampleRate);
    metadata->setProperty("outputChannels", static_cast<int>(exported->numChannels));
    metadata->setProperty("processedFrames", playhead.frame);
    metadata->setProperty("latencySamples", processor.getPreview().latencySamples);
    metadata->setProperty("smoothMs", pattern.smoothTenths / 10.0);
    metadata->setProperty("tightenPercent", pattern.tightenPercent);
    juce::Array<juce::var> offsets, timingStates;
    const auto sourceTiming = processor.getPreview().sourceTiming;
    for (std::size_t i = 0; i < sourceTiming.offsetsMs.size(); ++i)
    { offsets.add(sourceTiming.offsetsMs[i]); timingStates.add(static_cast<int>(sourceTiming.status[i])); }
    metadata->setProperty("sourceOffsetsMs", offsets);
    metadata->setProperty("sourceTimingStates", timingStates);
    metadata->setProperty("headless", true);
    const auto reportFile = juce::File(output.getFullPathName() + ".render.json");
    if (!reportFile.replaceWithText(juce::JSON::toString(report)))
        return reportFailure("WAV saved, but could not write render report: " + reportFile.getFullPathName());
    std::printf("SHFLR rendered %s | %.3f BPM | %.6f seconds | %.0f Hz | block %d\n",
                output.getFullPathName().toRawUTF8(), bpm, outputSeconds, rate, blockSize);
    processor.setPlayHead(nullptr);
    return 0;
}

class StandaloneContent final : public juce::Component,
                                private juce::AudioIODeviceCallback,
                                public juce::FileDragAndDropTarget,
                                private juce::Timer
{
public:
    StandaloneContent()
    {
        processor_.setPlayHead(&playhead_);
        editor_.reset(processor_.createEditor());
        setLookAndFeel(&editor_->getLookAndFeel());
        setOpaque(true);
        addAndMakeVisible(*editor_);
        for (auto* button : { &open_, &play_, &stop_, &settings_ }) addAndMakeVisible(button);
        addAndMakeVisible(bars_);
        addAndMakeVisible(status_);
        status_.setFont(desk::font(15.0f));
        status_.setColour(juce::Label::textColourId, desk::ink);
        play_.setColour(juce::TextButton::buttonOnColourId, desk::ink);
        play_.setColour(juce::TextButton::textColourOnId, desk::key);
        bars_.addItem("1 source bar", 1);
        bars_.addItem("2 source bars", 2);
        bars_.addItem("4 source bars", 4);
        bars_.setSelectedId(2, juce::dontSendNotification);
        bars_.onChange = [this]
        {
            requestedPlaying_.store(false);
            deviceManager_.removeAudioCallback(this);
            sourceBars_ = bars_.getSelectedId();
            deviceManager_.addAudioCallback(this);
        };
        open_.onClick = [this] { openFile(); };
        play_.onClick = [this]
        {
            if (original_.samples.getNumSamples() > 0 && deviceManager_.getCurrentAudioDevice() != nullptr)
                requestedPlaying_.store(true);
        };
        stop_.onClick = [this] { requestedPlaying_.store(false); };
        settings_.onClick = [this] { showSettings(); };
        play_.setEnabled(false);
        stop_.setEnabled(false);
        status_.setText("Open a WAV, select its source bars, then press Play.", juce::dontSendNotification);
        setSize(editor_->getWidth(), editor_->getHeight() + toolbarHeight);
        // No callbacks produce output until Play is explicitly requested.
        const auto error = deviceManager_.initialise(0, 2, nullptr, true);
        if (error.isNotEmpty()) status_.setText("Audio device: " + error, juce::dontSendNotification);
        deviceManager_.addAudioCallback(this);
        startTimerHz(10);
    }

    ~StandaloneContent() override
    {
        stopTimer();
        chooser_.reset();
        delete settingsWindow_.getComponent();
        deviceManager_.removeAudioCallback(this);
        deviceManager_.closeAudioDevice();
        processor_.setPlayHead(nullptr);
        setLookAndFeel(nullptr);
        editor_.reset();
        processor_.releaseResources();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(desk::panel);
        g.setColour(desk::rule);
        g.drawHorizontalLine(toolbarHeight - 1, 0.0f, static_cast<float>(getWidth()));
        g.drawVerticalLine(119, 12.0f, static_cast<float>(toolbarHeight - 12));
        g.setColour(desk::ink);
        g.setFont(desk::mono(16.0f));
        g.drawText("WAV PLAYER", 15, 12, 100, toolbarHeight - 24, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto toolbar = area.removeFromTop(toolbarHeight).reduced(14, 7);
        toolbar.removeFromLeft(118);
        auto buttons = toolbar.removeFromTop(34);
        open_.setBounds(buttons.removeFromLeft(112).reduced(2, 1));
        play_.setBounds(buttons.removeFromLeft(66).reduced(2, 1));
        stop_.setBounds(buttons.removeFromLeft(66).reduced(2, 1));
        buttons.removeFromLeft(8);
        bars_.setBounds(buttons.removeFromLeft(154).reduced(2, 1));
        settings_.setBounds(buttons.removeFromRight(135).reduced(2, 1));
        status_.setBounds(toolbar);
        editor_->setBounds(area);
    }

    bool isInterestedInFileDrag(const juce::StringArray& files) override
    { return files.size() == 1 && files[0].endsWithIgnoreCase(".wav"); }

    void filesDropped(const juce::StringArray& files, int, int) override
    { if (!files.isEmpty()) loadFile(juce::File(files[0])); }

private:
    static constexpr int toolbarHeight = 78;
    BreakVarAudioProcessor processor_;
    LocalPlayHead playhead_;
    juce::AudioDeviceManager deviceManager_;
    std::unique_ptr<juce::AudioProcessorEditor> editor_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::Component::SafePointer<juce::DialogWindow> settingsWindow_;
    juce::TextButton open_ { "Open WAV..." }, play_ { "Play" }, stop_ { "Stop" }, settings_ { "Audio settings..." };
    juce::ComboBox bars_;
    juce::Label status_;
    LoadedAudio original_;
    juce::AudioBuffer<float> source_, block_;
    juce::MidiBuffer midi_;
    std::atomic<bool> requestedPlaying_ { false };
    bool wasPlaying_ = false;
    int sourceBars_ = 2;
    int preparedBlockSize_ = defaultBlockSize;
    juce::String loadedName_;
    std::atomic<double> displayedBpm_ { 120.0 };

    void openFile()
    {
        chooser_ = std::make_unique<juce::FileChooser>("Open a source WAV", juce::File(), "*.wav");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = juce::Component::SafePointer<StandaloneContent>(this)](const juce::FileChooser& chooser)
            {
                if (safe != nullptr && chooser.getResult().existsAsFile()) safe->loadFile(chooser.getResult());
            });
    }

    void loadFile(const juce::File& file)
    {
        LoadedAudio candidate;
        juce::String error;
        if (!loadAudio(file, candidate, error))
        { status_.setText(error, juce::dontSendNotification); return; }
        requestedPlaying_.store(false);
        // removeAudioCallback waits for any in-flight callback to finish.
        deviceManager_.removeAudioCallback(this);
        original_ = std::move(candidate);
        loadedName_ = file.getFileName();
        deviceManager_.addAudioCallback(this); // aboutToStart resamples and prepares while detached.
    }

    void showSettings()
    {
        if (settingsWindow_ != nullptr) { settingsWindow_->toFront(true); return; }
        juce::DialogWindow::LaunchOptions options;
        options.dialogTitle = "SHFLR audio settings";
        options.dialogBackgroundColour = juce::Colour(0xff20242c);
        options.content.setOwned(new juce::AudioDeviceSelectorComponent(deviceManager_, 0, 0, 0, 2,
                                                                       false, false, true, false));
        options.content->setSize(520, 430);
        options.componentToCentreAround = this;
        options.useNativeTitleBar = true;
        options.resizable = false;
        settingsWindow_ = options.launchAsync();
    }

    void timerCallback() override
    {
        const bool loaded = original_.samples.getNumSamples() > 0;
        const bool device = deviceManager_.getCurrentAudioDevice() != nullptr;
        play_.setEnabled(loaded && device);
        stop_.setEnabled(loaded && device);
        play_.setToggleState(device && requestedPlaying_.load(), juce::dontSendNotification);
        if (loaded)
            status_.setText(loadedName_ + "  |  " + juce::String(sourceBars_) + " bars  |  "
                            + juce::String(displayedBpm_.load(), 2) + " BPM  |  "
                            + (device ? (requestedPlaying_.load() ? "Playing" : "Stopped") : "Choose an audio device"),
                            juce::dontSendNotification);
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override
    {
        requestedPlaying_.store(false);
        wasPlaying_ = false;
        playhead_.playing = false;
        playhead_.frame = 0;
        playhead_.sampleRate = device->getCurrentSampleRate();
        preparedBlockSize_ = std::max(1, device->getCurrentBufferSizeSamples());
        block_.setSize(2, preparedBlockSize_);
        if (original_.samples.getNumSamples() > 0)
        {
            source_ = resample(original_, playhead_.sampleRate);
            playhead_.bpm = sourceBars_ * 240.0 * playhead_.sampleRate / source_.getNumSamples();
            displayedBpm_.store(playhead_.bpm);
        }
        else source_.setSize(2, 0);
        processor_.setPlayConfigDetails(2, 2, playhead_.sampleRate, preparedBlockSize_);
        processor_.prepareToPlay(playhead_.sampleRate, preparedBlockSize_);
    }

    void audioDeviceStopped() override
    {
        requestedPlaying_.store(false);
        // Keep completed output available while stopped; prepare on the next start.
    }

    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputs,
                                         int outputChannels, int sampleCount,
                                         const juce::AudioIODeviceCallbackContext&) override
    {
        const juce::ScopedNoDenormals noDenormals;
        const bool playing = requestedPlaying_.load(std::memory_order_relaxed) && source_.getNumSamples() > 0;
        if (playing && !wasPlaying_) playhead_.frame = 0;
        wasPlaying_ = playing;
        playhead_.playing = playing;
        for (int offset = 0; offset < sampleCount; offset += preparedBlockSize_)
        {
            const int count = std::min(preparedBlockSize_, sampleCount - offset);
            if (playing) fillSource(block_, source_, playhead_.frame, count);
            else block_.clear();
            // A non-owning view handles a shortened device block without allocation.
            juce::AudioBuffer<float> view(block_.getArrayOfWritePointers(), 2, count);
            processor_.processBlock(view, midi_);
            for (int channel = 0; channel < outputChannels; ++channel)
                if (outputs[channel] != nullptr)
                {
                    if (playing)
                        juce::FloatVectorOperations::copy(outputs[channel] + offset,
                                                         block_.getReadPointer(std::min(channel, 1)), count);
                    else juce::FloatVectorOperations::clear(outputs[channel] + offset, count);
                }
            if (playing) playhead_.frame += count;
        }
    }
};

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow() : DocumentWindow("SHFLR", desk::chassis, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new StandaloneContent(), true);
        setResizable(true, false);
        const auto contentBorder = getContentComponentBorder();
        setResizeLimits(1140 + contentBorder.getLeftAndRight(),
                        818 + contentBorder.getTopAndBottom(), 2040, 1400);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class StandaloneApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "SHFLR"; }
    const juce::String getApplicationVersion() override { return SHFLR_VERSION; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void initialise(const juce::String&) override { window_ = std::make_unique<MainWindow>(); }
    void shutdown() override { window_.reset(); }
    void systemRequestedQuit() override { quit(); }
private:
    std::unique_ptr<MainWindow> window_;
};
}

// Intercept rendering before JUCEApplicationBase::main initialises the GUI.
JUCE_CREATE_APPLICATION_DEFINE(StandaloneApplication)
JUCE_MAIN_FUNCTION
{
   #if JUCE_WINDOWS && !defined(_CONSOLE)
    const auto arguments = juce::JUCEApplicationBase::getCommandLineParameterArray();
    if (!arguments.isEmpty() && AttachConsole(ATTACH_PARENT_PROCESS) != 0)
    {
        FILE* ignored = nullptr;
        if (_fileno(stdout) < 0) freopen_s(&ignored, "CONOUT$", "w", stdout);
        if (_fileno(stderr) < 0) freopen_s(&ignored, "CONOUT$", "w", stderr);
    }
   #else
    juce::StringArray arguments;
    for (int index = 1; index < argc; ++index) arguments.add(juce::String::fromUTF8(argv[index]));
   #endif
    if (!arguments.isEmpty())
    {
        if (arguments[0] == "--help" || arguments[0] == "-h")
        { std::fputs(usage().toRawUTF8(), stdout); return 0; }
        if (arguments[0] != "--render" && arguments[0] != "--catalog")
            return reportFailure("Unknown command.\n" + usage());
        try { return arguments[0] == "--catalog" ? catalogCommand(arguments) : renderCommand(arguments); }
        catch (const std::exception& exception) { return reportFailure(juce::String(exception.what())); }
        catch (...) { return reportFailure("Rendering failed with an unknown error."); }
    }
    juce::JUCEApplicationBase::createInstance = &juce_CreateApplication;
    return juce::JUCEApplicationBase::main(JUCE_MAIN_FUNCTION_ARGS);
}
