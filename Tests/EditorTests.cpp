#include "PluginProcessor.h"
#include "PresetBrowser.h"
#include "PatternLibrary.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
constexpr int sidebarWidth = 240;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
juce::Component* find(juce::Component& parent, const juce::String& id)
{
    if (parent.getComponentID() == id) return &parent;
    for (auto* child : parent.getChildren()) if (auto* match = find(*child, id)) return match;
    return nullptr;
}
template <typename T> T& control(juce::Component& parent, const char* id)
{
    auto* result = dynamic_cast<T*>(find(parent, id));
    require(result != nullptr, "embedded preset controls are present");
    return *result;
}
void render(juce::Component& editor, const juce::File& file)
{
    juce::MemoryOutputStream bytes;
    juce::PNGImageFormat png;
    require(png.writeImageToStream(editor.createComponentSnapshot(editor.getLocalBounds()), bytes), "software render");
    require(file.replaceWithData(bytes.getData(), bytes.getDataSize()), "save software render");
}

// A repeatable two-bar input with distinct kick, snare, ghost and hi-hat
// transients, so the exported sequence and both waveform views have real peaks.
std::vector<float> makeDrumLoop(int sampleCount, double sampleRate)
{
    constexpr std::string_view steps = "K.h.S.h." "h.k.S.hs" "K.h.S.kh" "h.k.S.hs";
    std::vector<float> loop(static_cast<size_t>(sampleCount));
    juce::Random noise(0x5348464c);
    for (size_t step = 0; step < steps.size(); ++step)
    {
        const char hit = steps[step];
        if (hit == '.') continue;
        const int start = static_cast<int>(step * loop.size() / steps.size());
        const bool kick = hit == 'K' || hit == 'k';
        const bool hat = hit == 'h';
        const double gain = hit == 'K' || hit == 'S' ? 0.95 : hit == 'k' ? 0.62 : 0.30;
        const double decay = hat ? 0.022 : kick ? 0.067 : 0.045;
        const int length = std::min(sampleCount - start, static_cast<int>(6.0 * decay * sampleRate));
        for (int i = 0; i < length; ++i)
        {
            const double t = i / sampleRate;
            const double grain = 2.0 * noise.nextFloat() - 1.0;
            const double tone = std::sin(juce::MathConstants<double>::twoPi * (kick ? 68.0 : 178.0) * t);
            const double sound = kick ? tone : hat ? grain : 0.7 * grain + 0.3 * tone;
            loop[static_cast<size_t>(start + i)] += static_cast<float>(gain * std::exp(-t / decay) * sound);
        }
    }
    return loop;
}

class LoopPlayHead final : public juce::AudioPlayHead
{
public:
    double ppq = 16, bpm = 174.0;
    bool playing = true;
    int numerator = 4, denominator = 4;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm(bpm); info.setPpqPosition(ppq);
        info.setTimeSignature(TimeSignature{numerator, denominator}); info.setIsPlaying(playing);
        info.setIsLooping(true); info.setLoopPoints(LoopPoints{16, 24});
        return info;
    }
};
class TickProbe final : private juce::Timer
{
public:
    TickProbe() { startTimer(60); }
    ~TickProbe() override { stopTimer(); }
    int ticks = 0;
private:
    void timerCallback() override { ++ticks; }
};

void runEditorTimer()
{
    TickProbe tick;
    for (int attempt = 0; attempt < 100 && tick.ticks == 0; ++attempt)
    { juce::Thread::sleep(10); juce::Timer::callPendingTimersSynchronously(); }
    require(tick.ticks > 0, "editor refresh timer runs in the software-only harness");
}

void chooseVisiblePreset(PresetBrowser& browser, int row)
{
    auto& list = control<juce::ListBox>(browser, "preset-list");
    require(row >= 0 && row < browser.visiblePresetCount(), "requested preset row exists");
    list.deselectAllRows();
    list.selectRow(row);
    require(browser.selectedPresetId() == browser.visiblePresetId(row),
            "visible preset row becomes the selected browser identity");
}

void chooseFactoryPreset(PresetBrowser& browser, int index)
{
    const auto& preset = breakvar::factoryPatterns().at(static_cast<size_t>(index));
    browser.setFilter(preset.pattern.inputBars, preset.category);
    for (int row = 0; row < browser.visiblePresetCount(); ++row)
        if (browser.visiblePresetId(row) == index + 1) { chooseVisiblePreset(browser, row); return; }
    require(false, "factory preset is reachable through its source/category browser");
}

juce::MouseEvent pointerEvent(juce::Component& target, juce::Point<float> point,
                              juce::Point<float> down, bool dragged = false,
                              int modifiers = juce::ModifierKeys::leftButtonModifier, int clicks = 1)
{
    return {juce::Desktop::getInstance().getMainMouseSource(), point,
            juce::ModifierKeys(modifiers), 1.0f, 0, 0, 0, 0, &target, &target,
            juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), clicks, dragged};
}

void requireSameImage(const juce::Image& before, const juce::Image& after, const char* message)
{
    require(before.getBounds() == after.getBounds(), message);
    for (int y = 0; y < before.getHeight(); ++y)
        for (int x = 0; x < before.getWidth(); ++x)
            require(before.getPixelAt(x, y) == after.getPixelAt(x, y), message);
}

void checkSoundControls(const juce::File& directory)
{
    BreakVarAudioProcessor processor(false, directory.getChildFile(juce::Uuid().toString()));
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    require(find(*editor, "preset-browser") != nullptr,
            "editor exposes a permanent left preset browser");
    for (const auto* id : {"sound-smooth", "sound-tighten", "sound-nudge"})
        require(dynamic_cast<juce::Slider*>(find(*editor, id)) != nullptr,
                "sound toolbar exposes Smooth, Tighten and per-colour Nudge sliders");
    require(find(*editor, "sound-auto") != nullptr && find(*editor, "sound-status") != nullptr,
            "Nudge offers Auto reset and a visible timing status");
    auto& smooth = control<juce::Slider>(*editor, "sound-smooth");
    auto& tighten = control<juce::Slider>(*editor, "sound-tighten");
    const auto initial = processor.getPattern();
    smooth.setValue(4.7, juce::sendNotificationSync);
    tighten.setValue(65, juce::sendNotificationSync);
    require(processor.getPattern().smoothTenths == 47 && processor.getPattern().tightenPercent == 65,
            "sound slider units persist as tenths of milliseconds and percent");
    require(processor.getPattern().cells == initial.cells && processor.getPattern().order == initial.order
            && processor.getPattern().orderLengths == initial.orderLengths,
            "sound adjustments preserve source regions, Order and beat lengths");
    editor->keyPressed(juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier, 0));
    require(processor.getPattern().tightenPercent == 0 && processor.getPattern().smoothTenths == 47,
            "one undo restores the prior Tighten amount without undoing Smooth");
    editor->keyPressed(juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier, 0));
    require(processor.getPattern() == initial, "second undo restores the prior Smooth amount");

    // Real Slider drag events must create one model edit, not one per mouse move.
    const auto start = juce::Point<float>{15, smooth.getHeight() * 0.5f};
    const auto middle = juce::Point<float>{45, start.y};
    const auto finish = juce::Point<float>{70, start.y};
    smooth.mouseDown(pointerEvent(smooth, start, start));
    smooth.mouseDrag(pointerEvent(smooth, middle, start, true));
    smooth.mouseDrag(pointerEvent(smooth, finish, start, true));
    smooth.mouseUp(pointerEvent(smooth, finish, start, true));
    require(processor.getPattern().smoothTenths != initial.smoothTenths, "drag changes the Smooth amount");
    editor->keyPressed(juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier, 0));
    require(processor.getPattern() == initial, "one undo restores a complete Smooth drag");

    // Editing a real embedded value box must survive the normal refresh timer.
    juce::Label* valueBox = nullptr;
    for (auto* child : smooth.getChildren())
        if (auto* label = dynamic_cast<juce::Label*>(child)) valueBox = label;
    require(valueBox != nullptr, "Smooth exposes an embedded value editor");
    valueBox->showEditor();
    require(valueBox->getCurrentTextEditor() != nullptr, "embedded sound value begins editing");
    valueBox->getCurrentTextEditor()->setText("3.3", false);
    TickProbe textTick;
    for (int attempt = 0; attempt < 100 && textTick.ticks == 0; ++attempt)
    { juce::Thread::sleep(10); juce::Timer::callPendingTimersSynchronously(); }
    require(textTick.ticks > 0 && valueBox->getCurrentTextEditor()->getText() == "3.3"
            && processor.getPattern() == initial, "timer never overwrites or prematurely commits typed sound values");
    valueBox->hideEditor(false);
    require(processor.getPattern().smoothTenths == 33, "typed Smooth value commits in correct units");
    editor->keyPressed(juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier, 0));
    require(processor.getPattern() == initial, "typed sound adjustment is one undo step");
    valueBox->showEditor(); valueBox->getCurrentTextEditor()->setText("6.6", false);
    auto external = initial; external.tightenPercent = 50;
    juce::String externalError;
    require(processor.updatePattern(external, externalError), "external pattern change during text edit");
    const int beforeTick = textTick.ticks;
    for (int attempt = 0; attempt < 100 && textTick.ticks == beforeTick; ++attempt)
    { juce::Thread::sleep(10); juce::Timer::callPendingTimersSynchronously(); }
    require(textTick.ticks > beforeTick && valueBox->getCurrentTextEditor()->getText() == "6.6",
            "new pattern refresh leaves an in-progress text edit intact");
    valueBox->hideEditor(false);
    require(processor.getPattern() == external, "old sound edit cannot overwrite a newly loaded pattern");

    auto fixture = initial;
    fixture.cells.fill(-1); std::fill_n(fixture.cells.begin(), 8, 0);
    std::fill_n(fixture.cells.begin() + 8, 8, 1);
    fixture.order.fill(-1); fixture.orderCount = 4;
    fixture.order[0] = 0; fixture.order[1] = 1; fixture.order[2] = 0; fixture.order[3] = 8;
    juce::String error;
    require(processor.updatePattern(fixture, error), "sound selection fixture");
    editor.reset(processor.createEditor());
    auto& selectedNudge = control<juce::Slider>(*editor, "sound-nudge");
    auto& autoReset = control<juce::Button>(*editor, "sound-auto");
    const float y = static_cast<float>(control<juce::ScrollBar>(*editor, "order-scroll").getY() - 60);
    const auto select = [&](juce::Point<float> point)
    { editor->mouseDown(pointerEvent(*editor, point, point)); editor->mouseUp(pointerEvent(*editor, point, point)); };
    select({sidebarWidth + 200, y});
    selectedNudge.setValue(-12.3, juce::sendNotificationAsync);
    select({sidebarWidth + 84, y});
    require(processor.getPattern().nudgeManual[1] && processor.getPattern().nudgeTenths[1] == -123
            && !processor.getPattern().nudgeManual[0],
            "pending slider notification cannot apply a Red nudge to newly selected Yellow");
    selectedNudge.setValue(7.6, juce::sendNotificationSync);
    select({sidebarWidth + 316, y});
    require(std::abs(selectedNudge.getValue() - 7.6) < 0.00001 && autoReset.isEnabled(),
            "repeated Order occurrences share their source colour's manual nudge");
    selectedNudge.setValue(0, juce::sendNotificationSync);
    require(processor.getPattern().nudgeManual[0] && processor.getPattern().nudgeTenths[0] == 0,
            "deliberate manual zero remains an override");
    autoReset.onClick();
    require(!processor.getPattern().nudgeManual[0] && processor.getPattern().nudgeTenths[0] == 0,
            "Auto restores automatic timing for only the selected colour");
    require(processor.getPattern().nudgeManual[1] && processor.getPattern().nudgeTenths[1] == -123,
            "Auto reset preserves other source nudges");
    juce::Label* nudgeBox = nullptr;
    for (auto* child : selectedNudge.getChildren())
        if (auto* label = dynamic_cast<juce::Label*>(child)) nudgeBox = label;
    require(nudgeBox != nullptr, "Nudge has an editable source offset");
    nudgeBox->showEditor(); nudgeBox->getCurrentTextEditor()->setText("0", false); nudgeBox->hideEditor(false);
    require(processor.getPattern().nudgeManual[0] && processor.getPattern().nudgeTenths[0] == 0,
            "typing zero also establishes a manual override when the previous numeric value is zero");
    autoReset.onClick();
    select({sidebarWidth + 432, y});
    require(!selectedNudge.isEnabled() && !autoReset.isEnabled(), "rest selection disables source timing controls");
    select({sidebarWidth + 800, 200});
    require(!selectedNudge.isEnabled(), "unassigned Input selection disables Nudge");
    select({sidebarWidth + 80, 200});
    require(selectedNudge.isEnabled(), "Input source selection enables its colour's Nudge");
    const juce::Point<float> mixed{sidebarWidth + 400, 200};
    editor->mouseDown(pointerEvent(*editor, mixed, mixed, false,
        juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier));
    editor->mouseUp(pointerEvent(*editor, mixed, mixed));
    require(!selectedNudge.isEnabled(), "selection spanning multiple source colours cannot nudge an arbitrary one");
    select({sidebarWidth + 80, 200});

    auto& pending = control<juce::Slider>(*editor, "sound-tighten");
    pending.setValue(43, juce::sendNotificationAsync);
    TickProbe tick;
    for (int attempt = 0; attempt < 100 && tick.ticks == 0; ++attempt)
    { juce::Thread::sleep(10); juce::Timer::callPendingTimersSynchronously(); }
    require(tick.ticks > 0 && pending.getValue() == 43 && processor.getPattern().tightenPercent == 43,
            "editor timer preserves a pending sound value");
    for (const auto size : {juce::Point<int>{1140, 740}, juce::Point<int>{1340, 820}})
    {
        editor->setSize(size.x, size.y);
        std::vector<juce::Rectangle<int>> controls;
        const auto output = find(*editor, "output-preview")->getBounds();
        for (const auto* id : {"sound-smooth", "sound-tighten", "sound-nudge", "sound-auto", "sound-status",
                              "output-loop-length", "order-play-length", "rest-length", "rest-add",
                              "order-random", "order-duplicate", "order-remove", "wav-save", "wav-drag"})
        {
            const auto* item = find(*editor, id);
            require(item && item->isVisible() && editor->getLocalBounds().contains(item->getBounds())
                    && item->getY() >= output.getBottom(), "all three toolbar rows fit below Output");
            for (const auto prior : controls)
                require(!prior.intersects(item->getBounds()), "sound and arrangement toolbar controls never overlap");
            controls.push_back(item->getBounds());
        }
        render(*editor, directory.getChildFile("sound-controls-" + juce::String(size.x) + ".png"));
    }
    require(editor->getPeer() == nullptr, "sound controls need no native window");
}

void checkOutputPreview(const juce::File& directory)
{
    BreakVarAudioProcessor processor(false, directory.getChildFile(juce::Uuid().toString()));
    auto fixture = breakvar::Pattern::makeDefault();
    fixture.outputBars = 2;
    while (fixture.orderCount < 10)
        require(fixture.insert(fixture.orderCount, fixture.orderCount % 3), "scrollable preview fixture");
    require(fixture.setOrderLength(1, 4), "selected preview occurrence has its own trim");
    juce::String error;
    require(processor.updatePattern(fixture, error), "publish preview fixture");
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    require(editor->getWidth() == 1340 && editor->getHeight() == 820, "default editor is 1340 by 820");
    auto* limits = editor->getConstrainer();
    require(limits != nullptr && limits->getMinimumWidth() == 1140 && limits->getMinimumHeight() == 740,
            "minimum editor size leaves room for both waveforms and large Order cards");
    // Fail before dispatching popup events if the display child has disappeared.
    auto* output = find(*editor, "output-preview");
    require(output != nullptr, "arrangement has a display-only Output waveform child");
    bool selfIntercepts = true, childrenIntercept = true;
    output->getInterceptsMouseClicks(selfIntercepts, childrenIntercept);
    require(!selfIntercepts && !childrenIntercept, "Output intercepts neither its own nor child mouse events");
    require(!output->getWantsKeyboardFocus(), "Output never takes keyboard focus");
    require(output->getParentComponent() == editor.get(), "Output has readable editor-local bounds");
    const int desktopWindows = juce::Desktop::getInstance().getNumComponents();

    for (const auto size : {juce::Point<int>{1140, 740}, juce::Point<int>{1340, 820}})
    {
        editor->setSize(size.x, size.y);
        const auto bounds = output->getBounds();
        require(output->isVisible() && bounds.getWidth() > 0 && bounds.getHeight() >= 60
                && editor->getLocalBounds().contains(bounds), "Output has usable visible bounds at both sizes");
        auto& scroll = control<juce::ScrollBar>(*editor, "order-scroll");
        require(scroll.getBounds().getBottom() < bounds.getY(), "large Order cards and scrollbar stay above Output");
        std::vector<juce::Rectangle<int>> toolbar;
        for (const char* id : {"output-loop-length", "order-play-length", "rest-length", "rest-add",
                              "order-random", "order-duplicate", "order-remove", "wav-save", "wav-drag"})
        {
            auto* item = find(*editor, id);
            require(item != nullptr && item->isVisible(), "all bottom toolbar controls are present");
            const auto itemBounds = editor->getLocalArea(item->getParentComponent(), item->getBounds());
            require(itemBounds.getWidth() > 0 && itemBounds.getHeight() > 0
                    && editor->getLocalBounds().contains(itemBounds) && itemBounds.getY() >= bounds.getBottom(),
                    "Loop, Play length, rest, arrangement and WAV controls fit below Output");
            for (const auto earlier : toolbar)
                require(!earlier.intersects(itemBounds), "bottom toolbar controls do not overlap");
            toolbar.push_back(itemBounds);
        }
        scroll.setCurrentRangeStart(0, juce::sendNotificationSync);
        const juce::Point<float> selected{sidebarWidth + 200.0f, static_cast<float>(scroll.getY() - 60)};
        editor->mouseDown(pointerEvent(*editor, selected, selected));
        editor->mouseUp(pointerEvent(*editor, selected, selected));
        auto& length = control<juce::ComboBox>(*editor, "order-play-length");
        require(length.isEnabled() && length.getSelectedId() == 5, "preview checks begin with the trimmed second card selected");
        const double orderStart = scroll.getCurrentRangeStart();
        const auto unchanged = [&]
        {
            require(processor.getPattern() == fixture, "Output events leave arrangement and source colours unchanged");
            require(length.isEnabled() && length.getSelectedId() == 5, "Output events preserve selected Order occurrence");
            require(scroll.getCurrentRangeStart() == orderStart, "Output events do not scroll Order");
            require(editor->getPeer() == nullptr && output->getPeer() == nullptr
                    && juce::Desktop::getInstance().getNumComponents() == desktopWindows,
                    "Output events create no native editor or popup window");
        };
        const auto before = editor->createComponentSnapshot(editor->getLocalBounds());
        for (const auto point : {bounds.getTopLeft().translated(2, 2).toFloat(), bounds.getCentre().toFloat(),
                                 bounds.getBottomRight().translated(-3, -3).toFloat()})
        {
            const auto local = point.toInt() - bounds.getPosition();
            require(!output->hitTest(local.x, local.y), "Output passes pointer events through to its parent");
            editor->mouseDown(pointerEvent(*editor, point, point));
            editor->mouseUp(pointerEvent(*editor, point, point));
            unchanged();
            editor->mouseDown(pointerEvent(*editor, point, point, false, juce::ModifierKeys::rightButtonModifier));
            editor->mouseUp(pointerEvent(*editor, point, point, false, juce::ModifierKeys::rightButtonModifier));
            unchanged();
            editor->mouseDoubleClick(pointerEvent(*editor, point, point, false, juce::ModifierKeys::leftButtonModifier, 2));
            unchanged();
            for (const auto delta : {juce::Point<float>{0, -2}, juce::Point<float>{-2, 0}})
            {
                juce::MouseWheelDetails wheel{}; wheel.deltaX = delta.x; wheel.deltaY = delta.y;
                editor->mouseWheelMove(pointerEvent(*editor, point, point), wheel);
                unchanged();
            }
        }
        const auto destination = bounds.getCentre().toFloat();
        editor->mouseDown(pointerEvent(*editor, destination, destination));
        editor->mouseDrag(pointerEvent(*editor, selected, destination, true));
        editor->mouseUp(pointerEvent(*editor, selected, destination, true));
        unchanged();
        requireSameImage(before, editor->createComponentSnapshot(editor->getLocalBounds()),
                         "Output clicks and drags preserve the visible Input and Order selection");

        editor->mouseDown(pointerEvent(*editor, selected, selected));
        editor->mouseDrag(pointerEvent(*editor, destination, selected, true));
        editor->mouseUp(pointerEvent(*editor, destination, selected, true));
        unchanged();
        const juce::Point<float> source{sidebarWidth + 80, 210};
        editor->mouseDown(pointerEvent(*editor, source, source));
        require(!length.isEnabled(), "source drag begins in the editable Input waveform");
        const auto selectedInput = editor->createComponentSnapshot(editor->getLocalBounds());
        editor->mouseDrag(pointerEvent(*editor, destination, source, true));
        editor->mouseUp(pointerEvent(*editor, destination, source, true));
        require(processor.getPattern() == fixture && !length.isEnabled()
                && scroll.getCurrentRangeStart() == orderStart, "dropping a source colour on Output leaves it unchanged");
        requireSameImage(selectedInput, editor->createComponentSnapshot(editor->getLocalBounds()),
                         "dropping on Output preserves the Input selection");
        require(editor->getPeer() == nullptr && juce::Desktop::getInstance().getNumComponents() == desktopWindows,
                "preview drag checks use synthetic events without native windows");
    }
}

void checkAppliedTimingMarker(const juce::File& directory)
{
    BreakVarAudioProcessor processor(false, directory.getChildFile(juce::Uuid().toString()));
    auto fixture = breakvar::Pattern::makeDefault();
    fixture.inputBars = fixture.outputBars = 1; fixture.cells.fill(-1); fixture.order.fill(-1);
    std::fill_n(fixture.cells.begin(), 4, 0); std::fill_n(fixture.cells.begin() + 4, 4, 1);
    std::fill_n(fixture.cells.begin() + 8, 8, 2);
    fixture.order[0] = 1; fixture.orderCount = 1;
    fixture.nudgeTenths[1] = 200; fixture.nudgeManual[1] = true;
    juce::String error;
    require(processor.updatePattern(fixture, error), "manual timing marker fixture");
    LoopPlayHead playhead; playhead.bpm = 120;
    processor.setPlayHead(&playhead); processor.setRateAndBufferSizeDetails(8000, 64); processor.prepareToPlay(8000, 64);
    juce::AudioBuffer<float> audio(2, 64); juce::MidiBuffer midi;
    for (int position = 0; position < 48000; position += 64)
    {
        playhead.ppq = 16.0 + position / 4000.0;
        for (int s = 0; s < 64; ++s) { audio.setSample(0, s, 0.05f); audio.setSample(1, s, -0.05f); }
        processor.processBlock(audio, midi);
    }
    require(processor.getPreview().sourceTiming.status[1] == breakvar::TimingStatus::manual
            && std::abs(processor.getPreview().sourceTiming.offsetsMs[1] - 20.0f) < 0.01f,
            "applied manual offset reaches the software editor preview");
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    const auto brightness = [](const juce::Image& image, int x)
    {
        const auto pixel = image.getPixelAt(sidebarWidth + x, 165);
        return static_cast<int>(pixel.getRed()) + pixel.getGreen() + pixel.getBlue();
    };
    // 1340-wide editor: Input x=268,width=1044; Red begins at 1/4 of a
    // 2000ms bar. +20ms shifts the source marker from x529 to x539.44.
    const auto applied = editor->createComponentSnapshot(editor->getLocalBounds());
    require(brightness(applied, 299) > brightness(applied, 311) + 50,
            "Input draws the actually applied +20ms source-start marker");
    render(*editor, directory.getChildFile("sound-applied-marker.png"));
    require(processor.loadFactoryPreset(0, error) && processor.getPreview().presetPending,
            "running preset change waits for the loop boundary");
    editor.reset(processor.createEditor());
    const auto pending = editor->createComponentSnapshot(editor->getLocalBounds());
    require(std::abs(brightness(pending, 299) - brightness(pending, 311)) < 5,
            "queued arrangement never displays a stale source-timing marker");
    require(editor->getPeer() == nullptr, "timing marker regression creates no native window");
    processor.setPlayHead(nullptr);
}

void checkSourcePlaybackPulse(const juce::File& directory)
{
    BreakVarAudioProcessor processor(false, directory.getChildFile(juce::Uuid().toString()));
    auto fixture = breakvar::Pattern::makeDefault();
    fixture.inputBars = fixture.outputBars = 1;
    fixture.cells.fill(-1); fixture.order.fill(-1); fixture.orderLengths.fill(0);
    std::fill_n(fixture.cells.begin(), 4, 0);
    std::fill_n(fixture.cells.begin() + 4, 4, 2);
    fixture.orderCount = 4;
    fixture.order[0] = fixture.order[1] = 0;
    fixture.order[2] = breakvar::restToken(4); fixture.order[3] = 2;
    juce::String error;
    require(processor.updatePattern(fixture, error), "Yellow, Yellow, Rest, Blue playback fixture");
    LoopPlayHead playhead; playhead.bpm = 120.0;
    processor.setPlayHead(&playhead);
    processor.setRateAndBufferSizeDetails(8000, 64); processor.prepareToPlay(8000, 64);
    juce::AudioBuffer<float> audio(2, 64); juce::MidiBuffer midi;
    int position = 0;
    const auto process = [&]
    {
        playhead.ppq = position / 4000.0;
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
        { audio.setSample(0, sample, 0.2f); audio.setSample(1, sample, -0.3f); }
        processor.processBlock(audio, midi);
        position += audio.getNumSamples();
    };
    // Feed memory buffers through two complete bars before sampling fresh,
    // peerless editors. No timer callbacks or audio device are involved.
    while (position < 32000) process();
    const auto frame = [&](float phaseStart, float phaseEnd, const char* filename)
    {
        bool found = false;
        for (int block = 0; block < 300 && !found; ++block)
        {
            process();
            const auto preview = processor.getPreview();
            found = preview.playing && !preview.presetPending && preview.status == breakvar::EngineStatus::ready
                    && preview.outputPhase >= phaseStart && preview.outputPhase < phaseEnd;
        }
        require(found, "processor publishes an active preview inside the requested occurrence phase");
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        editor->setSize(1140, 740);
        const auto result = editor->createComponentSnapshot(editor->getLocalBounds());
        render(*editor, directory.getChildFile(filename));
        require(editor->getPeer() == nullptr, "source pulse snapshots create no native window");
        return result;
    };
    const auto early = frame(0.0f, 0.02f, "source-pulse-first-1140.png");
    const auto late = frame(0.18f, 0.22f, "source-pulse-faded-1140.png");
    const auto repeated = frame(0.25f, 0.27f, "source-pulse-repeat-1140.png");
    const auto rest = frame(0.50f, 0.52f, "source-pulse-rest-1140.png");
    // Interior wash pixels are clear of text, grid lines, waveform amplitude,
    // selection borders and the input playhead at all four sampled phases.
    const auto brightness = [](const juce::Image& image, int left)
    {
        double sum = 0;
        for (int y = 164; y < 172; ++y)
            for (int x = sidebarWidth + left; x < sidebarWidth + left + 16; ++x)
            {
                const auto pixel = image.getPixelAt(x, y);
                sum += pixel.getRed() + pixel.getGreen() + pixel.getBlue();
            }
        return sum / 128.0;
    };
    const double restingYellow = brightness(late, 80);
    require(brightness(early, 80) > restingYellow + 30,
            "rendered Input source colour flashes when its first occurrence starts");
    require(brightness(repeated, 80) > restingYellow + 30,
            "adjacent occurrence of the same colour retriggers the rendered Input flash");
    require(std::abs(brightness(rest, 80) - restingYellow) < 1,
            "rest occurrence leaves the preceding Input source colour unflashed");
    require(std::abs(brightness(rest, 320) - brightness(late, 320)) < 1
            && std::abs(brightness(early, 320) - brightness(late, 320)) < 1,
            "rest and Yellow occurrences do not flash the other Input colour");
    processor.setPlayHead(nullptr);
}

void checkMeterLengthLabels(const juce::File& directory)
{
    BreakVarAudioProcessor processor(false, directory.getChildFile(juce::Uuid().toString()));
    LoopPlayHead playhead;
    processor.setPlayHead(&playhead);
    processor.setRateAndBufferSizeDetails(8000, 64); processor.prepareToPlay(8000, 64);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    editor->setSize(1140, 740);
    const auto& scroll = control<juce::ScrollBar>(*editor, "order-scroll");
    const juce::Point<float> first{sidebarWidth + 80, static_cast<float>(scroll.getY() - 60)};
    editor->mouseDown(pointerEvent(*editor, first, first));
    editor->mouseUp(pointerEvent(*editor, first, first));
    auto& length = control<juce::ComboBox>(*editor, "order-play-length");
    length.setSelectedId(9, juce::sendNotificationAsync);
    require(length.getText() == "2 beats", "initial 4/4 length label");
    playhead.numerator = 3;
    juce::AudioBuffer<float> audio(2, 64); audio.clear(); juce::MidiBuffer midi;
    processor.processBlock(audio, midi);
    TickProbe tick;
    for (int attempt = 0; attempt < 100 && tick.ticks == 0; ++attempt)
    { juce::Thread::sleep(10); juce::Timer::callPendingTimersSynchronously(); }
    require(tick.ticks > 0, "meter regression actually runs the editor refresh timer");
    if (length.getSelectedId() != 9 || length.getText() != "1.5 beats")
        std::cerr << "Meter chooser: " << length.getSelectedId() << " / " << length.getText()
                  << "; preview " << processor.getPreview().numerator << '/' << processor.getPreview().denominator << '\n';
    require(length.getSelectedId() == 9 && length.getText() == "1.5 beats",
            "host meter refresh updates beat label without discarding pending selection");
    length.onChange();
    require(processor.getPattern().orderLengths[0] == 8, "pending length choice still reaches model after meter refresh");
    require(editor->getPeer() == nullptr, "meter check uses no native window");
    processor.setPlayHead(nullptr);
}

void checkPlayLengthEditing(const juce::File& directory)
{
    BreakVarAudioProcessor processor(false, directory.getChildFile(juce::Uuid().toString()));
    auto fixture = breakvar::Pattern::makeDefault();
    fixture.outputBars = 2; fixture.order.fill(-1); fixture.orderCount = 3;
    fixture.order[0] = fixture.order[1] = fixture.order[2] = 0;
    juce::String error;
    require(processor.updatePattern(fixture, error), "three repeated source phrases fixture");
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    editor->setSize(1140, 740);
    auto& length = control<juce::ComboBox>(*editor, "order-play-length");
    auto& scroll = control<juce::ScrollBar>(*editor, "order-scroll");
    require(!length.isEnabled(), "Play length waits for a selected source box");
    const float y = static_cast<float>(scroll.getY() - 60);
    const juce::Point<float> middle{sidebarWidth + 200, y};
    editor->mouseDown(pointerEvent(*editor, middle, middle));
    editor->mouseUp(pointerEvent(*editor, middle, middle));
    require(length.isEnabled() && length.getSelectedId() == 1, "large middle card selects Full");
    length.setSelectedId(9, juce::dontSendNotification); length.onChange();
    auto changed = processor.getPattern();
    require(changed.orderLengths[0] == 0 && changed.orderLengths[1] == 8 && changed.orderLengths[2] == 0,
            "length edits only the selected occurrence of the shared colour");
    const auto sequence = breakvar::expandSequence(changed);
    require(sequence.count == 3 && sequence.segments[0].length == 12 && sequence.segments[1].length == 8
            && sequence.segments[2].length == 12, "UI builds the requested 3 + 2 + 3 beats");
    require(changed.cells == fixture.cells, "per-box trimming leaves source colours unchanged");
    length.setSelectedId(1, juce::dontSendNotification); length.onChange();
    require(processor.getPattern() == fixture, "Full restores the colour duration without a fixed cap");
    editor->keyPressed(juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier, 0));
    require(processor.getPattern() == changed && length.getSelectedId() == 9,
            "undo restores the cap and selected length control");
    control<juce::Button>(*editor, "order-duplicate").onClick();
    require(processor.getPattern().orderCount == 4 && processor.getPattern().orderLengths[2] == 8,
            "duplicating a trimmed box retains its independent length");
    const juce::Point<float> third{sidebarWidth + 316, y}, first{sidebarWidth + 35, y};
    editor->mouseDown(pointerEvent(*editor, third, third));
    editor->mouseDrag(pointerEvent(*editor, first, third, true));
    editor->mouseUp(pointerEvent(*editor, first, third, true));
    require(processor.getPattern().orderLengths[0] == 8 && processor.getPattern().orderLengths[1] == 0,
            "dragging a large box moves its length with it");
    editor->keyPressed(juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier, 0));
    editor->mouseDown(pointerEvent(*editor, third, third));
    editor->mouseUp(pointerEvent(*editor, third, third));
    editor->keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
    require(processor.getPattern() == changed, "deleting the duplicated trim restores 3 + 2 + 3");
    editor->mouseDown(pointerEvent(*editor, middle, middle));
    editor->mouseUp(pointerEvent(*editor, middle, middle));
    render(*editor, directory.getChildFile("order-lengths-1140.png"));
    editor->setSize(1340, 820);
    render(*editor, directory.getChildFile("order-lengths-1340.png"));
    for (const char* id : {"output-loop-length", "wav-save", "wav-drag", "rest-add"})
    {
        auto* c = find(*editor, id);
        require(c != nullptr && c->isVisible() && editor->getLocalBounds().contains(c->getBounds()),
                "loop, export and rest controls remain visible below the Output preview");
    }
    control<juce::Button>(*editor, "rest-add").onClick();
    require(!length.isEnabled() && processor.getPattern().orderLengths[2] == 0,
            "rest selection keeps dedicated rest duration without an audio cap");
    require(editor->getPeer() == nullptr, "Order edits use synthetic events without a native window");

    require(processor.updatePattern(fixture, error), "reset for source drag");
    editor.reset(processor.createEditor()); editor->setSize(1140, 740);
    const juce::Point<float> source{sidebarWidth + 80, 210}, target{sidebarWidth + 35, y};
    editor->mouseDown(pointerEvent(*editor, source, source));
    editor->mouseDrag(pointerEvent(*editor, target, source, true));
    editor->mouseUp(pointerEvent(*editor, target, source, true));
    require(processor.getPattern().orderCount == 4 && processor.getPattern().order[0] == 0
            && processor.getPattern().orderLengths[0] == 0, "source drag inserts a Full card into Order");

    auto longOrder = fixture;
    while (longOrder.orderCount < 10) require(longOrder.insert(longOrder.orderCount, 0), "long order fixture");
    require(longOrder.setOrderLength(9, 4), "last card has one beat");
    require(processor.updatePattern(longOrder, error), "publish scrolling fixture");
    editor.reset(processor.createEditor()); editor->setSize(1140, 740);
    juce::MouseWheelDetails wheel{}; wheel.deltaY = -2.0f;
    editor->mouseWheelMove(pointerEvent(*editor, middle, middle), wheel);
    const auto& scrolled = control<juce::ScrollBar>(*editor, "order-scroll");
    require(scrolled.getCurrentRangeStart() > 0, "larger cards scroll to later occurrences");
    const juce::Point<float> last{static_cast<float>(sidebarWidth + 28 + 9 * 116 + 55 - scrolled.getCurrentRangeStart()), y};
    editor->mouseDown(pointerEvent(*editor, last, last));
    editor->mouseUp(pointerEvent(*editor, last, last));
    auto& lastLength = control<juce::ComboBox>(*editor, "order-play-length");
    require(lastLength.getSelectedId() == 5, "scrolled card selects its own one-beat cap");
    lastLength.setSelectedId(9, juce::dontSendNotification); lastLength.onChange();
    require(processor.getPattern().orderLengths[9] == 8 && processor.getPattern().orderLengths[0] == 0,
            "scrolled length edit targets the correct occurrence");
    render(*editor, directory.getChildFile("order-scrolled-1140.png"));
    require(editor->getPeer() == nullptr, "source drag and scrolling never open a window");
}

void checkRestEditing(const juce::File& directory)
{
    BreakVarAudioProcessor processor(false, directory.getChildFile(juce::Uuid().toString()));
    juce::String error;
    require(processor.loadFactoryPreset(1, error), "rest editing fixture");
    auto fixture = processor.getPattern(); fixture.outputBars = 2;
    require(processor.updatePattern(fixture, error), "two-bar rest preview");
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    auto& length = control<juce::ComboBox>(*editor, "rest-length");
    auto& add = control<juce::Button>(*editor, "rest-add");
    length.setSelectedId(4, juce::dontSendNotification);
    add.onClick();
    auto pattern = processor.getPattern();
    require(pattern.orderCount == 5 && pattern.order[4] == 10, "quarter-bar rest appended without a source colour");
    require(pattern.cells == fixture.cells, "adding rest leaves all source colours intact");
    length.setSelectedId(1, juce::dontSendNotification); add.onClick();
    pattern = processor.getPattern();
    require(pattern.orderCount == 6 && pattern.order[5] == 8, "sixteenth rest inserted after selection");
    control<juce::Button>(*editor, "order-duplicate").onClick();
    pattern = processor.getPattern();
    require(pattern.orderCount == 7 && pattern.order[6] == 8, "rest duplication retains its duration");
    editor->keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
    require(processor.getPattern().orderCount == 6, "selected rest deletes normally");
    editor->keyPressed(juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier, 0));
    require(processor.getPattern() == pattern, "undo restores the removed rest");
    editor->setSize(1140, 740);
    render(*editor, directory.getChildFile("rests-1140.png"));
    editor->setSize(1340, 820);
    render(*editor, directory.getChildFile("rests-1340.png"));
    require(editor->getPeer() == nullptr, "rest editing never opens a native window");
}
}

int main()
{
    try {
        juce::ScopedJuceInitialiser_GUI initialise;
        const auto directory = juce::File::getCurrentWorkingDirectory().getChildFile("editor-test-artifacts");
        require(directory.createDirectory().wasOk(), "artifact directory");
        checkSoundControls(directory);
        checkAppliedTimingMarker(directory);
        checkOutputPreview(directory);
        checkSourcePlaybackPulse(directory);
        checkMeterLengthLabels(directory);
        checkPlayLengthEditing(directory);
        checkRestEditing(directory);
        const auto bank = directory.getChildFile(juce::Uuid().toString());
        BreakVarAudioProcessor processor(false, bank);
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        require(editor->getPeer() == nullptr, "tests must not open a native window");
        require(find(*editor, "order-play-length") != nullptr,
                "selected Order box exposes its own Play length control");
        auto& name = control<juce::TextEditor>(*editor, "preset-name");
        auto& start = control<juce::TextButton>(*editor, "preset-save");
        auto& save = control<juce::TextButton>(*editor, "preset-confirm");
        auto& cancel = control<juce::TextButton>(*editor, "preset-cancel");
        auto& browser = control<PresetBrowser>(*editor, "preset-browser");
        auto& list = control<juce::ListBox>(browser, "preset-list");
        auto& category = control<juce::ComboBox>(browser, "preset-category");
        auto& previous = control<juce::Button>(*editor, "preset-previous");
        auto& next = control<juce::Button>(*editor, "preset-next");
        const auto factoryNames = processor.getFactoryPresetNames();
        const auto original = processor.getPattern();
        require(browser.getParentComponent() == editor.get() && browser.isVisible(),
                "preset browser is a permanent visible editor child");
        require(browser.filterSourceBars() == 2 && browser.filterCategory() == "Essentials"
                && browser.selectedPresetId() == 1, "initial browser reveals the Original preset");
        control<juce::Button>(browser, "preset-source-user").onClick();
        require(browser.filterSourceBars() == 0 && browser.visiblePresetCount() == 0,
                "empty User library is a separate accessible browser section");
        previous.onClick(); next.onClick();
        require(processor.getPattern() == original && processor.getPresetName() == "Original",
                "cycling an empty User library leaves the loaded arrangement unchanged");
        control<juce::Button>(browser, "preset-source-one").onClick();
        require(browser.filterSourceBars() == 1 && list.isVisible() && browser.visiblePresetCount() > 0,
                "leaving empty User reveals Factory rows without resizing the editor");
        control<juce::Button>(browser, "preset-source-two").onClick();
        require(browser.filterSourceBars() == 2 && list.isVisible(), "two-bar source tab selects two-bar factories");
        for (const int sourceBars : {1, 2})
            for (const char* genre : {"DnB", "Jungle", "Hip-hop", "Buildups", "Breakdowns", "Shuffles", "Fills"})
            {
                browser.setFilter(sourceBars, genre);
                require(browser.visiblePresetCount() >= 10 && list.getModel()->getNumRows() == browser.visiblePresetCount(),
                        "each source length has at least ten browsable presets in every new category");
                for (int row = 0; row < browser.visiblePresetCount(); ++row)
                {
                    const int id = browser.visiblePresetId(row);
                    require(id > 0 && id <= factoryNames.size(), "Factory browser rows retain stable catalog IDs");
                    const auto& entry = breakvar::factoryPatterns()[static_cast<size_t>(id - 1)];
                    require(entry.pattern.inputBars == sourceBars && entry.category == genre,
                            "browser filters every visible row by source length and category");
                }
                require(processor.getPattern() == original && processor.getPresetName() == "Original",
                        "browsing source lengths and categories never loads an arrangement");
            }
        browser.setFilter(1, "DnB");
        chooseVisiblePreset(browser, 0);
        const int firstId = browser.visiblePresetId(0);
        const int secondId = browser.visiblePresetId(1);
        const int lastId = browser.visiblePresetId(browser.visiblePresetCount() - 1);
        next.onClick();
        require(processor.getPresetName() == factoryNames[secondId - 1], "down arrow selects the next visible preset");
        previous.onClick();
        require(processor.getPresetName() == factoryNames[firstId - 1], "up arrow selects the previous visible preset");
        previous.onClick();
        require(processor.getPresetName() == factoryNames[lastId - 1], "previous arrow wraps inside the chosen category");
        next.onClick();
        require(processor.getPresetName() == factoryNames[firstId - 1], "next arrow wraps to the first visible preset");
        const auto chosen = processor.getPattern();
        browser.setFilter(2, "Shuffles");
        runEditorTimer();
        require(browser.filterSourceBars() == 2 && browser.filterCategory() == "Shuffles"
                && processor.getPattern() == chosen, "timer does not force browsing back to the loaded preset's category");

        // Simulate JUCE bubbling a key from a filtered ListBox with no selected
        // row, without opening a native peer or requesting desktop keyboard focus.
        require(list.getNumSelectedRows() == 0, "hidden current preset leaves the browsed list unselected");
        const auto& orderScroll = control<juce::ScrollBar>(*editor, "order-scroll");
        const juce::Point<float> firstOrder{sidebarWidth + 80, static_cast<float>(orderScroll.getY() - 60)};
        editor->mouseDown(pointerEvent(*editor, firstOrder, firstOrder));
        editor->mouseUp(pointerEvent(*editor, firstOrder, firstOrder));
        for (const int code : {juce::KeyPress::deleteKey, juce::KeyPress::backspaceKey})
        {
            const auto before = processor.getPattern();
            const juce::KeyPress key(code);
            const bool consumed = browser.keyPressed(key);
            if (!consumed) editor->keyPressed(key);
            require(consumed && processor.getPattern() == before,
                    "Delete and Backspace in the browser cannot bubble into Order or source deletion");
        }

        // A delayed category notification must survive the normal refresh timer.
        int jungleId = 0;
        for (int item = 0; item < category.getNumItems(); ++item)
            if (category.getItemText(item) == "Jungle") jungleId = category.getItemId(item);
        require(jungleId > 0, "category menu exposes Jungle");
        category.setSelectedId(jungleId, juce::sendNotificationAsync);
        runEditorTimer();
        require(category.getSelectedId() == jungleId && processor.getPattern() == chosen,
                "refresh preserves a pending category choice without changing audio");
        category.onChange();
        require(browser.filterCategory() == "Jungle" && browser.filterSourceBars() == 2,
                "pending category notification reaches the browser");
        chooseVisiblePreset(browser, browser.visiblePresetCount() - 1);
        require(processor.getPresetName() == factoryNames[browser.selectedPresetId() - 1],
                "a clicked row loads its stable Factory identity immediately");
        render(*editor, directory.getChildFile("browser-jungle-last-1340.png"));
        chooseFactoryPreset(browser, 0);
        require(!name.isVisible(), "name field initially hidden");
        start.onClick();
        require(name.isVisible() && browser.isVisible() && !browser.isEnabled(),
                "inline naming keeps the browser visible but prevents an accidental preset switch");
        require(!previous.isVisible() && !next.isVisible(), "preset arrows hidden while naming");
        name.setText("My break", false);
        save.onClick();
        require(!name.isVisible() && browser.isVisible() && browser.isEnabled() && list.isVisible(),
                "first saved preset reveals User rows without resizing the editor");
        require(processor.getUserPresetNames().contains("My break"), "named preset saved internally");
        require(browser.filterSourceBars() == 0 && browser.visiblePresetCount() == 1
                && browser.selectedPresetId() == 1001, "save immediately reveals the newly saved User preset");
        require(previous.isVisible() && next.isVisible(), "preset arrows return after save");
        const auto savedPattern = processor.getPattern();
        previous.onClick();
        require(processor.getPresetName() == "My break" && processor.isCurrentUserPreset(),
                "previous arrow stays within a one-entry User list");
        next.onClick();
        require(processor.getPresetName() == "My break" && processor.isCurrentUserPreset(),
                "next arrow stays within a one-entry User list");
        juce::String error;
        require(processor.loadFactoryPreset(3, error), "change pattern");
        start.onClick();
        name.setText("my BREAK", false);
        save.onClick();
        require(name.isVisible() && save.getButtonText() == "Replace", "collision needs inline replacement confirmation");
        cancel.onClick();
        require(!name.isVisible(), "cancel closes inline form");
        require(processor.loadUserPreset("My break", error) && processor.getPattern() == savedPattern,
                "cancel does not replace stored preset");
        require(processor.loadFactoryPreset(9, error), "replacement fixture");
        const auto replacement = processor.getPattern();
        start.onClick(); name.setText("My break", false); save.onClick(); save.onClick();
        require(!name.isVisible(), "explicit replace succeeds");
        require(processor.loadFactoryPreset(1, error), "selection fixture");
        browser.setFilter(0, {}); chooseVisiblePreset(browser, 0);
        require(processor.getPattern() == replacement, "User browser row loads the replaced arrangement");
        require(processor.loadFactoryPreset(0, error), "same-name factory fixture");
        start.onClick(); name.setText("Original", false); save.onClick();
        require(processor.isCurrentUserPreset() && browser.selectedPresetId() == 1002,
                "saved Factory-name preset selects the separate User identity");
        next.onClick();
        require(processor.getPresetName() == "My break" && processor.isCurrentUserPreset(),
                "last User row wraps to the first User row");
        previous.onClick();
        require(processor.getPresetName() == "Original" && processor.isCurrentUserPreset(),
                "first User row wraps to the last User row");
        chooseFactoryPreset(browser, 0);
        require(!processor.isCurrentUserPreset() && browser.selectedPresetId() == 1,
                "same-named Factory selection stays distinct");
        start.onClick(); name.setText("", false); save.onClick();
        require(name.isVisible(), "invalid name remains editable");
        name.setText("New variation", false);
        editor->setSize(1140, 740);
        require(editor->getLocalBounds().contains(name.getBounds()) && name.getX() >= sidebarWidth
                && !name.getBounds().intersects(save.getBounds()) && !save.getBounds().intersects(cancel.getBounds()),
                "embedded save form fits in the main header at minimum width");
        render(*editor, directory.getChildFile("preset-save-1140.png"));
        cancel.onClick();
        render(*editor, directory.getChildFile("editor-1140.png"));
        editor->setSize(1340, 820);
        render(*editor, directory.getChildFile("editor-1340.png"));
        for (const auto size : {juce::Point<int>{1140, 740}, juce::Point<int>{1340, 820}})
        {
            editor->setSize(size.x, size.y);
            require(browser.getWidth() == sidebarWidth && browser.getX() == 0
                    && editor->getLocalBounds().contains(browser.getBounds()), "sidebar fits at both supported editor sizes");
            for (const char* id : {"preset-source-one", "preset-source-two", "preset-source-user", "preset-category", "preset-list"})
            {
                const auto* item = find(browser, id);
                require(item != nullptr, "browser layout control exists");
                const auto bounds = browser.getLocalArea(item->getParentComponent(), item->getBounds());
                require(item->isVisible() && browser.getLocalBounds().contains(bounds),
                        "source tabs, category and scrollable rows fit inside the sidebar");
            }
            for (const char* id : {"preset-title", "preset-status", "preset-previous", "preset-next", "preset-save",
                                  "output-preview", "order-scroll", "sound-smooth", "sound-tighten", "sound-nudge"})
            {
                const auto* item = find(*editor, id);
                // Short arrangements intentionally auto-hide their scrollbar.
                require(item && (item->isVisible() || juce::String(id) == "order-scroll") && item->getX() >= sidebarWidth
                        && editor->getLocalBounds().contains(item->getBounds()), "main controls remain clear of the sidebar");
            }
            browser.setFilter(2, "Buildups");
            chooseVisiblePreset(browser, browser.visiblePresetCount() - 1);
            render(*editor, directory.getChildFile("browser-buildups-last-" + juce::String(size.x) + ".png"));
            auto& scroll = control<juce::ScrollBar>(*editor, "order-scroll");
            const auto before = processor.getPattern();
            const auto orderPosition = scroll.getCurrentRangeStart();
            const juce::Point<float> sidebarPoint{100, 210};
            editor->mouseDown(pointerEvent(*editor, sidebarPoint, sidebarPoint));
            editor->mouseUp(pointerEvent(*editor, sidebarPoint, sidebarPoint));
            juce::MouseWheelDetails wheel{}; wheel.deltaY = -2;
            editor->mouseWheelMove(pointerEvent(*editor, sidebarPoint, sidebarPoint), wheel);
            require(processor.getPattern() == before && scroll.getCurrentRangeStart() == orderPosition,
                    "sidebar coordinates cannot edit source colours or scroll the Order lane");
        }
        require(processor.loadFactoryPreset(40, error), "external preset load fixture");
        runEditorTimer();
        require(browser.filterSourceBars() == breakvar::factoryPatterns()[40].pattern.inputBars
                && browser.filterCategory() == juce::String(breakvar::factoryPatterns()[40].category)
                && browser.selectedPresetId() == 41,
                "a project or external preset change reveals its category and selection");
        require(control<juce::Label>(*editor, "preset-title").getText().contains(factoryNames[40]),
                "current preset title stays visible separately from browsing filters");
        require(editor->getPeer() == nullptr, "no native editor peer created");

        // Reproduce DAW loop rounding through processor -> editor -> WAV writer.
        // Musical two-bar duration at 174 BPM/48 kHz is 132413.793... samples.
        LoopPlayHead playhead;
        processor.setPlayHead(&playhead);
        const int breakExchange = factoryNames.indexOf("Two bars - Jungle break exchange");
        require(breakExchange >= 0 && processor.loadFactoryPreset(breakExchange, error)
                && breakvar::requiredLatencySamples(processor.getPattern(), 48000, playhead.bpm) > 0,
                "Jungle break exchange is a two-bar forward-lookahead loop fixture");
        processor.setRateAndBufferSizeDetails(48000, 511);
        processor.prepareToPlay(48000, 511);
        juce::AudioBuffer<float> audio(2, 511);
        juce::MidiBuffer midi;
        constexpr int loop = 132414;
        const auto drums = makeDrumLoop(loop, 48000.0);
        for (int position = 0; position < 5 * loop;)
        {
            const int offset = position % loop;
            const int count = std::min(511, loop - offset);
            playhead.ppq = 16.0 + offset * 174.0 / (60.0 * 48000.0);
            for (int channel = 0; channel < audio.getNumChannels(); ++channel)
                audio.copyFrom(channel, 0, drums.data() + offset, count);
            juce::AudioBuffer<float> view(audio.getArrayOfWritePointers(), 2, count);
            processor.processBlock(view, midi);
            position += count;
        }
        require(processor.hasOutputReady(), "rounded host loop reaches processor export readiness");
        editor.reset(processor.createEditor());
        require(control<juce::TextButton>(*editor, "wav-save").isEnabled()
             && control<juce::TextButton>(*editor, "wav-drag").isEnabled(), "rounded host loop enables both WAV controls");
        require(processor.writeOutputWav(directory.getChildFile("rounded-host-output.wav"), error), "ready loop writes actual WAV");
        const auto& readyScroll = control<juce::ScrollBar>(*editor, "order-scroll");
        const juce::Point<float> secondPad{sidebarWidth + 198.0f, static_cast<float>(readyScroll.getY() - 60)};
        editor->mouseDown(pointerEvent(*editor, secondPad, secondPad));
        editor->mouseUp(pointerEvent(*editor, secondPad, secondPad));
        render(*editor, directory.getChildFile("wav-ready.png"));

        // The selected preset is visible immediately, with a persistent status
        // until the audio callback reaches the loop boundary.
        auto& queuedBrowser = control<PresetBrowser>(*editor, "preset-browser");
        queuedBrowser.setFilter(2, "Jungle");
        chooseVisiblePreset(queuedBrowser, 0);
        require(control<juce::Label>(*editor, "preset-status").getText().contains("Queued"),
                "preset selection immediately shows queued status while playing");
        require(!control<juce::TextButton>(*editor, "wav-save").isEnabled(),
                "queued preset cannot export the previous arrangement");
        control<juce::Button>(*editor, "preset-next").onClick();
        const int latestId = queuedBrowser.visiblePresetId(1);
        require(queuedBrowser.selectedPresetId() == latestId && processor.getPresetName() == factoryNames[latestId - 1]
                && processor.getPattern() == breakvar::factoryPatterns()[static_cast<size_t>(latestId - 1)].pattern,
                "rapid arrow choices immediately replace the previously queued preset");
        queuedBrowser.setFilter(1, "DnB");
        runEditorTimer();
        require(processor.getPresetName() == factoryNames[latestId - 1] && queuedBrowser.filterSourceBars() == 1
                && queuedBrowser.filterCategory() == "DnB"
                && control<juce::Label>(*editor, "preset-status").getText().contains("Queued"),
                "browsing another category preserves the latest queued choice and waiting status");
        render(*editor, directory.getChildFile("preset-queued-1340.png"));
        editor->setSize(1140, 740);
        render(*editor, directory.getChildFile("preset-queued-1140.png"));
        playhead.ppq = 16;
        audio.clear(); processor.processBlock(audio, midi);
        editor.reset(processor.createEditor());
        require(!control<juce::Label>(*editor, "preset-status").getText().contains("Queued"),
                "loop-boundary activation clears the queued indicator");
        require(processor.getPresetName() == factoryNames[latestId - 1]
                && control<PresetBrowser>(*editor, "preset-browser").selectedPresetId() == latestId,
                "reopened editor reveals the latest activated preset identity");
        playhead.playing = false;
        processor.processBlock(audio, midi);
        control<juce::Button>(*editor, "preset-next").onClick();
        require(control<juce::Label>(*editor, "preset-status").getText().contains("Ready"),
                "stopped selection shows ready instead of waiting for a loop");
        render(*editor, directory.getChildFile("preset-stopped-1340.png"));
        processor.setPlayHead(nullptr);
        std::cout << "Preset browser filters, cycling, embedded saves, queued selection and software layout checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "Editor test failed: " << error.what() << '\n'; return 1;
    }
}
