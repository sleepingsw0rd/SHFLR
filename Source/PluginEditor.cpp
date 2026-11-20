#include "PluginEditor.h"
#include "SamplerDesk.h"
#include <algorithm>
#include <cmath>

namespace
{
const auto& surface = desk::chassis;
const auto& panel = desk::panel;
const auto& text = desk::ink;
const auto& muted = desk::muted;
const auto& line = desk::rule;
const auto& colours = desk::slices;
const std::array<const char*, 8> colourNames{{"Yellow", "Red", "Blue", "Orange", "Pink", "Violet", "Teal", "Green"}};
const std::array<const char*, 8> colourLetters{{"Y", "R", "B", "O", "P", "V", "T", "G"}};
constexpr float tokenWidth = 116.0f;
constexpr int browserWidth = 240;

juce::String restDuration(int cells)
{
    return cells == breakvar::cellsPerBar ? "1 bar" : "1/" + juce::String(breakvar::cellsPerBar / cells);
}

juce::Colour orderColour(int token)
{
    return token >= 0 && token < breakvar::colourCount ? colours[static_cast<std::size_t>(token)] : muted;
}

juce::String orderName(int token)
{
    return breakvar::isRest(token) ? "Rest " + restDuration(breakvar::restCells(token))
        : token >= 0 && token < breakvar::colourCount ? juce::String(colourNames[static_cast<std::size_t>(token)]) : "";
}

using desk::font;

juce::String duration(double cells, const BreakVarPreview& p)
{
    // A beat is one host quarter-note, including in compound and irregular meters.
    const auto beats = cells / breakvar::cellsPerBar * std::max(1, p.numerator) * 4.0 / std::max(1, p.denominator);
    auto result = juce::String(beats, 2);
    while (result.endsWithChar('0')) result = result.dropLastCharacters(1);
    if (result.endsWithChar('.')) result = result.dropLastCharacters(1);
    return result + " b";
}

juce::String menuReason(const std::string& reason)
{
    if (reason == "Each colour must occupy one contiguous region.") return " (would split a colour)";
    if (reason == "An ordered phrase has no input region.") return " (colour used in Order)";
    return " (unavailable)";
}

juce::String playDuration(int cells, const BreakVarPreview& preview)
{
    const auto beats = duration(cells, preview).dropLastCharacters(2);
    return beats + (beats == "1" ? " beat" : " beats");
}
}

juce::Font BreakVarAudioProcessorEditor::EditorLookAndFeel::getPopupMenuFont()
{
    return font(16.0f);
}

juce::Font BreakVarAudioProcessorEditor::EditorLookAndFeel::getTextButtonFont(juce::TextButton&, int)
{
    return font(15.0f);
}

juce::Font BreakVarAudioProcessorEditor::EditorLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return font(16.0f);
}

void BreakVarAudioProcessorEditor::EditorLookAndFeel::drawButtonBackground(
    juce::Graphics& g, juce::Button& button, const juce::Colour& background, bool highlighted, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const auto fill = down ? background.interpolatedWith(desk::ink, 0.12f)
                           : highlighted ? background.brighter(0.08f) : background;
    g.setColour(button.isEnabled() ? fill : background.interpolatedWith(panel, 0.72f));
    g.fillRoundedRectangle(bounds, 3.0f);
    g.setColour(button.hasKeyboardFocus(true) ? desk::accent : line);
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
}

juce::Label* BreakVarAudioProcessorEditor::EditorLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* label = juce::LookAndFeel_V4::createSliderTextBox(slider);
    label->setFont(font(14));
    return label;
}

bool BreakVarAudioProcessorEditor::SoundSlider::editing() const
{
    if (dragging) return true;
    for (const auto* child : getChildren())
        if (const auto* label = dynamic_cast<const juce::Label*>(child); label && label->isBeingEdited()) return true;
    return false;
}

void BreakVarAudioProcessorEditor::PresetArrow::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(down ? line : highlighted ? desk::key.brighter(0.08f) : desk::key);
    g.fillRoundedRectangle(bounds, 3.0f);
    g.setColour(line); g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
    const float x = bounds.getCentreX(), y = bounds.getCentreY();
    const float direction = pointsUp ? -1.0f : 1.0f;
    juce::Path arrow;
    arrow.startNewSubPath(x - 5.0f, y - direction * 2.5f);
    arrow.lineTo(x, y + direction * 2.5f);
    arrow.lineTo(x + 5.0f, y - direction * 2.5f);
    g.setColour(isEnabled() ? ::text : muted);
    g.strokePath(arrow, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

BreakVarAudioProcessorEditor::BreakVarAudioProcessorEditor(BreakVarAudioProcessor& p)
    : AudioProcessorEditor(p), processor_(p), pattern_(p.getPattern()), preview_(p.getPreview())
{
    lookAndFeel_.setColourScheme({surface, desk::key, panel, line, text, desk::accent,
                                 desk::key, desk::accent, text});
    lookAndFeel_.setColour(juce::ComboBox::backgroundColourId, desk::key);
    lookAndFeel_.setColour(juce::ComboBox::outlineColourId, line);
    lookAndFeel_.setColour(juce::ComboBox::textColourId, text);
    lookAndFeel_.setColour(juce::ComboBox::arrowColourId, muted);
    lookAndFeel_.setColour(juce::PopupMenu::backgroundColourId, desk::key);
    lookAndFeel_.setColour(juce::PopupMenu::textColourId, text);
    lookAndFeel_.setColour(juce::PopupMenu::highlightedBackgroundColourId, panel.interpolatedWith(desk::accent, 0.13f));
    lookAndFeel_.setColour(juce::PopupMenu::highlightedTextColourId, text);
    lookAndFeel_.setColour(juce::TextButton::buttonColourId, desk::key);
    lookAndFeel_.setColour(juce::TextButton::textColourOffId, text);
    lookAndFeel_.setColour(juce::ScrollBar::thumbColourId, muted.withAlpha(0.65f));
    lookAndFeel_.setColour(juce::TooltipWindow::backgroundColourId, panel);
    lookAndFeel_.setColour(juce::TooltipWindow::textColourId, text);
    lookAndFeel_.setColour(juce::Slider::textBoxBackgroundColourId, desk::key);
    lookAndFeel_.setColour(juce::Slider::textBoxTextColourId, text);
    lookAndFeel_.setColour(juce::Slider::textBoxOutlineColourId, line);
    lookAndFeel_.setColour(juce::Slider::trackColourId, desk::accent);
    lookAndFeel_.setColour(juce::Slider::backgroundColourId, line);
    lookAndFeel_.setColour(juce::Slider::thumbColourId, desk::accent);
    setLookAndFeel(&lookAndFeel_);
    for (auto* c : {&inputLength_, &grid_, &outputLength_, &restLength_, &playLength_})
    {
        addAndMakeVisible(c);
        c->setWantsKeyboardFocus(false);
    }
    inputLength_.addItem("1 bar", 1); inputLength_.addItem("2 bars", 2); inputLength_.addItem("4 bars", 4);
    grid_.addItem("1/4", 4); grid_.addItem("1/8", 2); grid_.addItem("1/16", 1);
    for (const auto n : {1, 2, 4, 8}) outputLength_.addItem(juce::String(n) + (n == 1 ? " bar" : " bars"), n);
    for (const auto cells : {1, 2, 4, 8, 16}) restLength_.addItem(restDuration(cells), cells);
    restLength_.setSelectedId(1, juce::dontSendNotification);
    restLength_.setComponentID("rest-length");
    restLength_.setTooltip("Length of the next rest, as a fraction of a bar.");
    playLength_.setComponentID("order-play-length");
    playLength_.setTextWhenNothingSelected("Select a box");
    playLength_.setTooltip("Play this Order box from its colour's beginning for Full or a shorter duration. No stretching.");
    playLength_.onChange = [this]
    { if (playLength_.getSelectedId() > 0) setSelectedPlayLength(playLength_.getSelectedId() - 1); };
    inputLength_.setTooltip("Live input window. Colour ranges scale with its length.");
    grid_.setTooltip("Selection divisions per bar: 4, 8 or 16.");
    outputLength_.setTooltip("Output repeats the phrase order and cuts at this loop endpoint.");
    outputLength_.setComponentID("output-loop-length");
    addAndMakeVisible(presetBrowser_);
    presetBrowser_.onUserTabSelected = [this] { refreshPresetMenu(); };
    for (auto* arrow : {&previousPreset_, &nextPreset_})
    { addAndMakeVisible(arrow); arrow->setWantsKeyboardFocus(false); }
    previousPreset_.setComponentID("preset-previous");
    nextPreset_.setComponentID("preset-next");
    previousPreset_.setTooltip("Previous preset in the visible browser category.");
    nextPreset_.setTooltip("Next preset in the visible browser category.");
    previousPreset_.onClick = [this] { cyclePreset(-1); };
    nextPreset_.onClick = [this] { cyclePreset(1); };
    refreshPresetMenu();
    presetBrowser_.onPresetSelected = [this](int id)
    {
        if (id <= 0) return;
        juce::String error;
        const bool ok = id >= 1001 && id < 1001 + userPresetNames_.size()
            ? processor_.loadUserPreset(userPresetNames_[id - 1001], error)
            : processor_.loadFactoryPreset(id - 1, error);
        if (ok) refreshPattern("Preset selected.");
        else { showError(error); syncControls(); }
    };
    inputLength_.onChange = [this]
    {
        auto next = processor_.getPattern();
        std::string reason;
        if (!next.resizeInput(inputLength_.getSelectedId(), &reason)) { showError(reason); syncControls(); return; }
        if (commit(next, "Input window updated."))
        { anchor_ = selectionFirst_ = 0; selectionLast_ = next.grid - 1; }
    };
    grid_.onChange = [this]
    {
        auto next = processor_.getPattern(); next.grid = grid_.getSelectedId();
        if (commit(next, "Selection grid updated."))
        { anchor_ = selectionFirst_ = 0; selectionLast_ = next.grid - 1; }
    };
    outputLength_.onChange = [this]
    { auto next = processor_.getPattern(); next.outputBars = outputLength_.getSelectedId(); commit(next, "Output loop updated."); };
    for (auto* b : {&undo_, &redo_, &duplicate_, &remove_, &savePreset_,
                   &randomSlices_, &randomOrder_, &addRest_, &dragWav_, &saveWav_})
    { addAndMakeVisible(b); b->setWantsKeyboardFocus(false); }
    addAndMakeVisible(presetTitle_);
    presetTitle_.setComponentID("preset-title");
    presetTitle_.setFont(font(21, true));
    presetTitle_.setMinimumHorizontalScale(0.9f);
    presetTitle_.setColour(juce::Label::textColourId, text);
    presetTitle_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(presetLabel_);
    presetLabel_.setComponentID("preset-status");
    presetLabel_.setFont(font(14));
    presetLabel_.setColour(juce::Label::textColourId, muted);
    presetLabel_.setInterceptsMouseClicks(false, false);
    undo_.setTooltip("Undo pattern edit (Ctrl+Z)."); redo_.setTooltip("Redo pattern edit (Ctrl+Y).");
    duplicate_.setTooltip("Duplicate the selected occurrence, including one beyond the loop endpoint.");
    duplicate_.setComponentID("order-duplicate");
    remove_.setTooltip("Remove the selected output occurrence (Delete).");
    undo_.onClick = [this] { processor_.undoPattern(); pattern_ = processor_.getPattern(); syncControls(); repaint(); };
    redo_.onClick = [this] { processor_.redoPattern(); pattern_ = processor_.getPattern(); syncControls(); repaint(); };
    duplicate_.onClick = [this] { duplicateSelected(); }; remove_.onClick = [this] { removeSelected(); };
    savePreset_.setComponentID("preset-save");
    savePreset_.setTooltip("Name and save this arrangement in SHFLR's User presets.");
    savePreset_.onClick = [this] { beginPresetSave(); };
    addChildComponent(presetNameEditor_);
    addChildComponent(confirmPreset_);
    addChildComponent(cancelPreset_);
    presetNameEditor_.setComponentID("preset-name");
    confirmPreset_.setComponentID("preset-confirm");
    cancelPreset_.setComponentID("preset-cancel");
    presetNameEditor_.setFont(font(16));
    presetNameEditor_.setTextToShowWhenEmpty("Name your preset", muted);
    presetNameEditor_.setInputRestrictions(64);
    presetNameEditor_.setColour(juce::TextEditor::backgroundColourId, surface);
    presetNameEditor_.setColour(juce::TextEditor::textColourId, text);
    presetNameEditor_.setColour(juce::TextEditor::outlineColourId, line);
    presetNameEditor_.onReturnKey = [this] { finishPresetSave(); };
    presetNameEditor_.onEscapeKey = [this] { cancelPresetSave(); };
    presetNameEditor_.onTextChange = [this]
    {
        if (!presetNameEditor_.getText().trim().equalsIgnoreCase(replacementName_))
        { replacementName_.clear(); confirmPreset_.setButtonText("Save"); }
    };
    confirmPreset_.onClick = [this] { finishPresetSave(); };
    cancelPreset_.onClick = [this] { cancelPresetSave(); };
    randomSlices_.setTooltip("Randomise contiguous, grid-aligned Input slices. Keeps colours used by Order.");
    randomSlices_.onClick = [this]
    {
        juce::String error;
        if (processor_.randomiseSlices(static_cast<std::uint32_t>(juce::Random::getSystemRandom().nextInt()), error))
            refreshPattern("Input slices randomised.");
        else showError(error);
    };
    randomOrder_.setTooltip("Shuffle existing occurrences or make a new sequence. Input slices stay in place.");
    randomOrder_.setComponentID("order-random");
    remove_.setComponentID("order-remove");
    randomOrder_.onClick = [this] { showRandomOrderMenu(); };
    addRest_.setComponentID("rest-add");
    addRest_.setTooltip("Insert silence after the selected Order box, or append it. Drag it into place. No source colour is needed.");
    addRest_.onClick = [this] { addRest(); };
    dragWav_.setTooltip("Drag the latest complete output loop into your DAW. Exported WAV files are retained.");
    dragWav_.setComponentID("wav-drag");
    saveWav_.setComponentID("wav-save");
    dragWav_.setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    dragWav_.addMouseListener(this, false);
    saveWav_.setTooltip("Save the latest complete output loop as a WAV file (maximum 32 seconds).");
    saveWav_.onClick = [this] { chooseOutputWav(); };
    for (auto* slider : {&smooth_, &tighten_, &nudge_})
    {
        addAndMakeVisible(slider);
        slider->setSliderStyle(juce::Slider::LinearHorizontal);
        slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, slider == &tighten_ ? 50 : 64, 26);
        slider->setChangeNotificationOnlyOnRelease(true);
        slider->setScrollWheelEnabled(false);
        slider->beginEdit = [this, slider] { beginSoundEdit(*slider); };
        slider->commitEdit = [this, slider] { finishSoundEdit(*slider); };
        for (auto* child : slider->getChildren())
        {
            if (auto* label = dynamic_cast<juce::Label*>(child))
            {
                label->onEditorShow = [this, slider] { beginSoundEdit(*slider); };
                // Typing "0" is also an intentional manual override when the
                // displayed numeric value already happens to be zero.
                auto updateSlider = label->onTextChange;
                label->onTextChange = [this, slider, updateSlider]
                {
                    if (updateSlider) updateSlider();
                    if (soundEditing_ == slider) finishSoundEdit(*slider);
                };
            }
        }
    }
    smooth_.setComponentID("sound-smooth"); smooth_.setRange(0, 10, 0.1); smooth_.setTextValueSuffix(" ms");
    tighten_.setComponentID("sound-tighten"); tighten_.setRange(0, 100, 1); tighten_.setTextValueSuffix(" %");
    nudge_.setComponentID("sound-nudge"); nudge_.setRange(-30, 30, 0.1); nudge_.setTextValueSuffix(" ms");
    smooth_.setTooltip("Smooth source jumps with short crossfades. Continuous audio is left untouched. 0 ms disables smoothing.");
    tighten_.setTooltip("Align phrase starts to nearby drum attacks. 0% keeps original timing; 100% applies the full correction. Internal groove and beat lengths stay unchanged.");
    nudge_.setTooltip("Manual source offset for the selected colour. Positive reads later; negative reads earlier. All of this colour's Order boxes share the offset. Auto removes this override.");
    addAndMakeVisible(timingAuto_); timingAuto_.setComponentID("sound-auto"); timingAuto_.setWantsKeyboardFocus(false);
    timingAuto_.setTooltip("Remove this colour's manual override and use automatic timing again.");
    timingAuto_.onClick = [this]
    {
        const int colour = selectedTimingColour();
        if (colour < 0) return;
        auto next = processor_.getPattern();
        next.nudgeManual[colour] = false; next.nudgeTenths[colour] = 0;
        commit(next, juce::String(colourNames[colour]) + " timing returned to Auto.");
    };
    addAndMakeVisible(timingStatus_); timingStatus_.setComponentID("sound-status");
    timingStatus_.setFont(font(14)); timingStatus_.setMinimumHorizontalScale(0.9f);
    timingStatus_.setColour(juce::Label::textColourId, muted);
    timingStatus_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(orderScroll_);
    addAndMakeVisible(outputPreview_);
    orderScroll_.addListener(this);
    orderScroll_.setAutoHide(true);
    orderScroll_.setComponentID("order-scroll");
    orderScroll_.setSingleStepSize(tokenWidth);
    setWantsKeyboardFocus(true);
    setResizable(true, true);
    setResizeLimits(1140, 740, 2040, 1140);
    setSize(1340, 820);
    selectionLast_ = pattern_.grid - 1;
    syncControls();
    startTimerHz(30);
}

BreakVarAudioProcessorEditor::~BreakVarAudioProcessorEditor()
{
    stopTimer();
    dragWav_.removeMouseListener(this);
    fileChooser_.reset();
    orderScroll_.removeListener(this);
    setLookAndFeel(nullptr);
}

void BreakVarAudioProcessorEditor::resized()
{
    const int w = getWidth() - browserWidth, h = getHeight();
    constexpr int headerHeight = 73;
    presetBrowser_.setBounds(0, headerHeight, browserWidth, h - headerHeight);
    const auto place = [](juce::Component& component, int x, int y, int width, int height)
    { component.setBounds(x + browserWidth, y, width, height); };
    const float laneHeight = juce::jlimit(120.0f, 240.0f, static_cast<float>(h - 500) * 0.5f);
    inputRect_ = {28.0f, 146.0f, static_cast<float>(w - 56), laneHeight};
    const float orderTop = inputRect_.getBottom() + 54.0f;
    orderRect_ = {28.0f, orderTop, static_cast<float>(w - 56), 120.0f};
    const int outputTop = static_cast<int>(orderRect_.getBottom()) + 44;
    place(outputPreview_, 28, outputTop, w - 56, std::max(24, h - 188 - outputTop));
    place(undo_, w - 174, 22, 68, 29); place(redo_, w - 96, 22, 68, 29);
    place(presetTitle_, 23, 10, w - 395, 31);
    place(previousPreset_, w - 362, 16, 28, 30);
    place(nextPreset_, w - 330, 16, 28, 30);
    place(savePreset_, w - 286, 16, 100, 30);
    place(presetNameEditor_, 28, 13, w - 422, 30);
    place(confirmPreset_, w - 380, 16, 90, 30);
    place(cancelPreset_, w - 282, 16, 96, 30);
    place(presetLabel_, 23, 39, w - 450, 24);
    place(randomSlices_, 142, 87, 124, 30);
    place(inputLength_, w - 267, 87, 95, 30); place(grid_, w - 104, 87, 76, 30);
    const int loopToolsY = h - 124;
    place(outputLength_, 80, loopToolsY, 95, 30);
    place(orderScroll_, orderRect_.getX(), orderRect_.getBottom() + 3, orderRect_.getWidth(), 8);
    const int orderToolsY = h - 164;
    place(playLength_, 110, orderToolsY, 150, 28);
    place(restLength_, w - 524, orderToolsY, 78, 28);
    place(addRest_, w - 438, orderToolsY, 82, 28);
    place(randomOrder_, w - 342, orderToolsY, 128, 28);
    place(duplicate_, w - 202, orderToolsY, 91, 28); place(remove_, w - 103, orderToolsY, 75, 28);
    place(dragWav_, w - 240, loopToolsY, 102, 30);
    place(saveWav_, w - 126, loopToolsY, 98, 30);
    const int soundY = h - 84;
    place(smooth_, 88, soundY, 150, 30);
    place(tighten_, 306, soundY, 140, 30);
    place(nudge_, 512, soundY, 168, 30);
    place(timingAuto_, 690, soundY + 1, 54, 28);
    place(timingStatus_, 754, soundY, w - 782, 30);
    inputRect_.translate(static_cast<float>(browserWidth), 0.0f);
    orderRect_.translate(static_cast<float>(browserWidth), 0.0f);
    updateOrderScroll();
}

void BreakVarAudioProcessorEditor::drawRuler(juce::Graphics& g, juce::Rectangle<float> r, int bars)
{
    g.setColour(desk::muted);
    g.setFont(font(14));
    for (int i = 0; i < bars; ++i)
        g.drawText("Bar " + juce::String(i + 1), juce::Rectangle<float>(r.getX() + r.getWidth() * i / bars, r.getY() - 22.0f, 70.0f, 16.0f), juce::Justification::left);
    g.drawText("End", juce::Rectangle<float>(r.getRight() - 35.0f, r.getY() - 22.0f, 35.0f, 16.0f), juce::Justification::right);
}

void BreakVarAudioProcessorEditor::drawWaveform(juce::Graphics& g, juce::Rectangle<float> r, double first, double end, juce::Colour c)
{
    g.setColour(c);
    const int columns = std::max(1, static_cast<int>(r.getWidth() / 2.2f));
    const float middle = r.getCentreY(), amplitude = r.getHeight() * 0.42f;
    for (int i = 0; i < columns; ++i)
    {
        const double position = first + (end - first) * static_cast<double>(i) / columns;
        // A nudged read may extend beyond this visible input window. Do not
        // invent peaks by pinning those samples to the first or last column.
        if (position < 0.0 || position >= 1.0) continue;
        const int sample = juce::jlimit(0, breakvar::waveformSize - 1, static_cast<int>(position * breakvar::waveformSize));
        const float peak = juce::jlimit(0.0f, 1.0f, preview_.waveform[static_cast<size_t>(sample)]);
        const float x = r.getX() + r.getWidth() * static_cast<float>(i) / columns;
        if (peak > 0.0002f) g.drawVerticalLine(static_cast<int>(x), middle - peak * amplitude, middle + peak * amplitude);
    }
}

breakvar::PlaybackVisual BreakVarAudioProcessorEditor::currentPlayback() const noexcept
{
    return breakvar::playbackVisual(pattern_, preview_.outputPhase, preview_.bpm,
        preview_.numerator, preview_.denominator,
        preview_.playing && !preview_.presetPending && preview_.status == breakvar::EngineStatus::ready);
}

void BreakVarAudioProcessorEditor::drawInput(juce::Graphics& g)
{
    g.setColour(desk::display);
    g.fillRect(inputRect_);
    juce::Graphics::ScopedSaveState saved(g); g.reduceClipRegion(inputRect_.toNearestInt());
    const float cellWidth = inputRect_.getWidth() / pattern_.activeCells();
    const auto playing = currentPlayback();
    for (int colour = 0; colour < breakvar::colourCount; ++colour)
    {
        const auto region = pattern_.region(colour);
        if (!region.valid()) continue;
        auto r = juce::Rectangle<float>(inputRect_.getX() + region.first * cellWidth, inputRect_.getY(), (region.end - region.first) * cellWidth, inputRect_.getHeight());
        const float pulse = playing.colour == colour ? playing.pulse : 0.0f;
        g.setColour(colours[colour].withAlpha(0.025f + 0.21f * pulse));
        g.fillRect(r);
        g.setColour(desk::displayMuted.withAlpha(0.42f));
        g.drawVerticalLine(static_cast<int>(r.getX()), r.getY(), r.getBottom());
        g.setColour(colours[colour]);
        g.fillRect(r.withHeight(4 + 2 * pulse));
        if (pulse > 0)
        {
            g.setColour(colours[colour].withAlpha(pulse));
            g.fillRect(r.withTop(r.getBottom() - 3));
        }
        g.setColour(colours[colour].interpolatedWith(desk::displayText, 0.22f));
        g.setFont(font(15, true));
        if (r.getWidth() > 24) g.drawText(colourLetters[colour], r.withY(r.getBottom() - 27).withHeight(20).reduced(5, 0), juce::Justification::centredLeft);
        if (r.getWidth() > 76)
        {
            g.setColour(desk::displayMuted); g.setFont(font(14));
            g.drawText(duration(region.end - region.first, preview_), r.withY(r.getBottom() - 27).withHeight(20).reduced(7, 0), juce::Justification::centredRight);
        }
    }
    auto selection = juce::Rectangle<float>(inputRect_.getX() + selectionFirst_ * cellWidth, inputRect_.getY(), (selectionLast_ - selectionFirst_ + 1) * cellWidth, inputRect_.getHeight());
    g.setColour(desk::displayText.withAlpha(0.055f)); g.fillRect(selection);
    g.setColour(desk::waveform.withAlpha(0.15f)); g.drawHorizontalLine(static_cast<int>(inputRect_.getCentreY()), inputRect_.getX(), inputRect_.getRight());
    for (int cell = 0; cell <= pattern_.activeCells(); cell += pattern_.grid)
    {
        g.setColour(desk::waveform.withAlpha(cell % breakvar::cellsPerBar == 0 ? 0.28f : 0.09f));
        g.drawVerticalLine(static_cast<int>(inputRect_.getX() + cell * cellWidth), inputRect_.getY() + 4, inputRect_.getBottom());
    }
    drawWaveform(g, inputRect_.reduced(0, 25), 0, 1, desk::waveform);
    g.setColour(desk::displayText.withAlpha(0.72f)); g.drawRect(selection.reduced(0.5f), 1.0f);
    for (int colour = 0; colour < breakvar::colourCount; ++colour)
    {
        const auto region = pattern_.region(colour);
        const double shift = appliedTimingOffset(colour);
        if (!region.valid() || std::abs(shift) < 0.01) continue;
        const double sourceMs = pattern_.inputBars * std::max(1, preview_.numerator) * 4.0
            / std::max(1, preview_.denominator) * 60000.0 / std::max(1.0, preview_.bpm);
        const float nominal = inputRect_.getX() + region.first * cellWidth;
        const float actual = nominal + static_cast<float>(shift / sourceMs) * inputRect_.getWidth();
        const float x = juce::jlimit(inputRect_.getX() + 2, inputRect_.getRight() - 2, actual);
        g.setColour(colours[colour].brighter(0.35f));
        g.drawLine(x, inputRect_.getY() + 8, x, inputRect_.getBottom() - 31, 1.5f);
        g.drawLine(nominal, inputRect_.getY() + 9, x, inputRect_.getY() + 9, 2.0f);
        juce::Path marker;
        if (actual < inputRect_.getX())
            marker.addTriangle(x, inputRect_.getY() + 14, x + 7, inputRect_.getY() + 10, x + 7, inputRect_.getY() + 18);
        else if (actual > inputRect_.getRight())
            marker.addTriangle(x, inputRect_.getY() + 14, x - 7, inputRect_.getY() + 10, x - 7, inputRect_.getY() + 18);
        else
            marker.addTriangle(x - 4, inputRect_.getY() + 8, x + 4, inputRect_.getY() + 8, x, inputRect_.getY() + 14);
        g.fillPath(marker);
    }
    if (preview_.status == breakvar::EngineStatus::ready || preview_.status == breakvar::EngineStatus::filling)
    {
        const float x = inputRect_.getX() + juce::jlimit(0.0f, 1.0f, preview_.inputPhase) * inputRect_.getWidth();
        g.setColour(desk::displayText); g.drawVerticalLine(static_cast<int>(x), inputRect_.getY(), inputRect_.getBottom());
        juce::Path p; p.addTriangle(x - 4, inputRect_.getY(), x + 4, inputRect_.getY(), x, inputRect_.getY() + 6); g.fillPath(p);
    }
}

void BreakVarAudioProcessorEditor::drawOrder(juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState saved(g); g.reduceClipRegion(orderRect_.toNearestInt());
    const auto sequence = breakvar::expandSequence(pattern_);
    const double total = pattern_.outputBars * breakvar::cellsPerBar;
    const auto visual = currentPlayback();
    const breakvar::Segment* playing = visual.sequenceIndex >= 0 ? &sequence.segments[visual.sequenceIndex] : nullptr;
    std::array<bool, breakvar::maxOrder> cuts{}, repeatCuts{};
    for (int i = 0; i < sequence.count; ++i)
    {
        const auto& segment = sequence.segments[i];
        if (segment.length < segment.fullLength)
            (segment.repeated ? repeatCuts : cuts)[segment.orderIndex] = true;
    }
    double accumulated = 0;
    for (int i = 0; i < pattern_.orderCount; ++i)
    {
        const int token = pattern_.order[static_cast<size_t>(i)];
        const bool rest = breakvar::isRest(token);
        const int length = pattern_.orderLength(i);
        const bool beyond = accumulated >= total;
        accumulated += length;
        auto r = juce::Rectangle<float>(orderRect_.getX() + i * tokenWidth - static_cast<float>(orderOffset_),
                                       orderRect_.getY(), tokenWidth - 6, orderRect_.getHeight());
        if (!r.intersects(orderRect_)) continue;
        const bool active = playing != nullptr && playing->orderIndex == i;
        const bool selected = i == selectedOrder_;
        const auto colour = rest ? desk::displayMuted : orderColour(token);
        const auto face = r.withTrimmedBottom(4.0f);
        const auto faceColour = selected ? desk::ink : beyond ? desk::panel : desk::key;
        const auto primary = selected ? desk::key : beyond ? desk::muted : desk::ink;
        const auto secondary = selected ? desk::displayMuted : desk::muted;
        const auto captions = face.reduced(9.0f, 0.0f);

        g.setColour(selected ? desk::accent : desk::rule);
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(faceColour);
        g.fillRoundedRectangle(face, 4.0f);
        g.setColour(colour.withAlpha(beyond ? 0.5f : 1.0f));
        g.fillRect(face.withHeight(active ? 4.0f + visual.pulse * 3.0f : 4.0f).reduced(2.0f, 0.0f));

        g.setFont(font(14, true));
        g.setColour(secondary);
        g.drawText(juce::String(i + 1).paddedLeft('0', 2), captions.withY(r.getY() + 9).withHeight(16), juce::Justification::left);
        if (active)
        {
            g.setColour(selected ? desk::key : desk::ink);
            g.fillEllipse(captions.getRight() - 8, r.getY() + 14, 7.0f, 7.0f);
        }

        g.setColour(primary);
        g.setFont(font(rest ? 24.0f : 34.0f, true));
        g.drawText(rest ? "Rest" : colourLetters[token], captions.withY(r.getY() + 26).withHeight(37), juce::Justification::left);
        g.setFont(font(18, true));
        g.drawText(playDuration(length, preview_), captions.withY(r.getY() + 64).withHeight(23), juce::Justification::left);
        g.setFont(font(14));
        g.setColour(secondary);
        const auto detail = beyond ? juce::String("Not played") : cuts[i] ? juce::String("Cut at loop end")
            : repeatCuts[i] ? juce::String("Repeat cut at end") : active && playing->repeated ? juce::String("Repeating")
            : rest ? restDuration(breakvar::restCells(token))
            : pattern_.orderLengths[i] > pattern_.phraseLength(token) ? juce::String("Source limit")
            : length == pattern_.phraseLength(token) ? juce::String("Full phrase") : juce::String("Trimmed");
        g.drawFittedText(detail, captions.withY(r.getY() + 89).withHeight(19).toNearestInt(), juce::Justification::centredLeft, 1);
        if (active)
        {
            const auto progress = static_cast<float>(visual.progress);
            const auto track = face.withTop(face.getBottom() - 4.0f).reduced(2.0f, 0.0f);
            g.setColour(primary.withAlpha(0.15f));
            g.fillRect(track);
            g.setColour(primary);
            g.fillRect(track.withWidth(track.getWidth() * progress));
        }
    }
    if (pattern_.orderCount == 0)
    {
        g.setColour(line); g.drawRoundedRectangle(orderRect_.reduced(0.5f), 5, 1);
        g.setColour(muted); g.setFont(font(16));
        g.drawText("Drag a colour here to arrange | Empty order is silent", orderRect_, juce::Justification::centred);
    }
    if (draggingPhrase_ && dropIndex_ >= 0 && orderRect_.contains(dragPoint_))
    {
        const float boundary = orderRect_.getX() + dropIndex_ * tokenWidth - static_cast<float>(orderOffset_);
        if (boundary >= orderRect_.getX() && boundary <= orderRect_.getRight())
        {
            const float x = juce::jlimit(orderRect_.getX(), orderRect_.getRight() - 2.0f, boundary - 3.0f);
            g.setColour(desk::ink);
            g.fillRect(x, orderRect_.getY(), 2.0f, orderRect_.getHeight());
        }
    }
}

void BreakVarAudioProcessorEditor::drawOutput(juce::Graphics& g)
{
    const auto bounds = outputPreview_.getLocalBounds().toFloat();
    g.setColour(desk::display);
    g.fillRect(bounds);
    const auto sequence = breakvar::expandSequence(pattern_);
    const auto visual = currentPlayback();
    const float unit = bounds.getWidth() / (pattern_.outputBars * breakvar::cellsPerBar);
    if (sequence.count == 0)
    {
        g.setColour(desk::displayMuted); g.setFont(font(15));
        g.drawText("Empty order | Output is silent", bounds, juce::Justification::centred);
        return;
    }
    for (int i = 0; i < sequence.count; ++i)
    {
        const auto& segment = sequence.segments[i];
        const bool rest = breakvar::isRest(segment.colour);
        const bool cut = segment.length < segment.fullLength;
        const auto colour = rest ? desk::displayMuted : orderColour(segment.colour);
        const auto region = pattern_.region(segment.colour);
        const auto r = juce::Rectangle<float>(static_cast<float>(segment.start) * unit, 0,
                                              static_cast<float>(segment.length) * unit, bounds.getHeight());
        g.setColour(colour.withAlpha(segment.repeated ? 0.035f : 0.075f)); g.fillRect(r);
        g.setColour(desk::waveform.withAlpha(0.25f));
        g.drawVerticalLine(static_cast<int>(r.getX()), r.getY(), r.getBottom());
        g.setColour(colour.withAlpha(segment.repeated ? 0.60f : 1.0f)); g.fillRect(r.withHeight(3).reduced(0.7f, 0));
        if (rest)
        {
            g.setColour(desk::displayMuted.withAlpha(0.55f));
            g.drawHorizontalLine(static_cast<int>(r.getCentreY()), r.getX() + 1, r.getRight() - 1);
        }
        else
        {
            const double sourceMs = pattern_.inputBars * std::max(1, preview_.numerator) * 4.0
                / std::max(1, preview_.denominator) * 60000.0 / std::max(1.0, preview_.bpm);
            const double shift = appliedTimingOffset(segment.colour) / sourceMs;
            drawWaveform(g, r.reduced(1, 18), static_cast<double>(region.first) / pattern_.activeCells() + shift,
                         (region.first + segment.length) / pattern_.activeCells() + shift, desk::waveform.withAlpha(segment.repeated ? 0.60f : 0.90f));
        }
        if (cut)
        {
            juce::Graphics::ScopedSaveState clipped(g); g.reduceClipRegion(r.toNearestInt());
            g.setColour(desk::displayText.withAlpha(0.15f));
            for (float x = r.getX() - r.getHeight(); x < r.getRight(); x += 10)
                g.drawLine(x, r.getBottom(), x + r.getHeight(), r.getY(), 1);
        }
        if (r.getWidth() >= 20)
        {
            g.setColour(colour.interpolatedWith(desk::displayText, 0.22f)); g.setFont(font(14, true));
            const auto label = rest ? juce::String(r.getWidth() > 45 ? "Rest" : "-") : juce::String(colourLetters[segment.colour]);
            g.drawText(label + (r.getWidth() > 85 ? "  " + duration(segment.length, preview_) : juce::String{}) + (cut ? " /" : ""),
                       r.reduced(3, 0).withY(r.getBottom() - 18).withHeight(16), juce::Justification::centred);
        }
        if (i == visual.sequenceIndex)
        { g.setColour(desk::displayText.withAlpha(0.55f)); g.drawRect(r.reduced(1), 1); }
    }
    for (int bar = 0; bar < pattern_.outputBars; ++bar)
    {
        const float x = bounds.getWidth() * bar / pattern_.outputBars;
        g.setColour(desk::waveform.withAlpha(0.3f)); g.drawVerticalLine(static_cast<int>(x), 3, bounds.getBottom());
        g.setFont(font(13)); g.setColour(desk::displayMuted);
        g.drawText("Bar " + juce::String(bar + 1), juce::Rectangle<float>(x + 5, 4, 55, 14), juce::Justification::left);
    }
    if (visual.sequenceIndex >= 0)
    {
        const float x = preview_.outputPhase * bounds.getWidth();
        g.setColour(desk::displayText); g.drawVerticalLine(static_cast<int>(x), 0, bounds.getBottom());
    }
}

void BreakVarAudioProcessorEditor::paint(juce::Graphics& g)
{
    const juce::Graphics::ScopedSaveState state(g);
    g.fillAll(surface);
    g.setColour(line);
    g.drawHorizontalLine(72, 0.0f, static_cast<float>(getWidth()));

    const auto wordmark = juce::Font(juce::FontOptions("Arial Black", 42.0f, juce::Font::plain))
                              .withExtraKerningFactor(-0.055f);
    const auto logoWidth = juce::GlyphArrangement::getStringWidthInt(wordmark, "SHFLR");
    g.setColour(text);
    g.setFont(wordmark);
    g.drawText("SHFLR", 20, 4, logoWidth + 2, 44, juce::Justification::centredLeft, false);
    g.setColour(desk::accent);
    g.drawLine(23.0f + logoWidth, 40, 31.0f + logoWidth, 14, 3.0f);
    g.setColour(muted);
    g.setFont(desk::mono(13));
    g.drawText("BREAK REARRANGER  /  " JucePlugin_VersionString, 22, 46, browserWidth - 30, 19, juce::Justification::left);

    g.addTransform(juce::AffineTransform::translation(static_cast<float>(browserWidth), 0.0f));
    const int w = getWidth() - browserWidth;
    const auto host = "Host  " + juce::String(preview_.bpm, 1) + " BPM  |  " + juce::String(preview_.numerator) + "/" + juce::String(preview_.denominator);
    g.setFont(desk::mono(14)); g.drawText(host, w - 420, 47, 234, 20, juce::Justification::right);
    g.setColour(text); g.setFont(font(16, true).withExtraKerningFactor(0.08f));
    g.drawText("INPUT", 28, 86, 106, 32, juce::Justification::left);
    g.drawText("ORDER", juce::Rectangle<float>(28, orderRect_.getY() - 26, 65, 22), juce::Justification::left);
    g.drawText("OUTPUT", juce::Rectangle<float>(28, outputPreview_.getY() - 24.0f, 80, 20), juce::Justification::left);
    g.setColour(muted); g.setFont(font(14));
    g.drawText("Window", w - 325, 87, 52, 30, juce::Justification::right);
    g.drawText("Grid", w - 156, 87, 42, 30, juce::Justification::right);
    g.drawText("Drag to reorder | Right-click to edit", juce::Rectangle<float>(100, orderRect_.getY() - 26, 350, 22), juce::Justification::left);
    g.drawText("Preview only", juce::Rectangle<float>(w - 200.0f, outputPreview_.getY() - 24.0f, 172, 20), juce::Justification::right);
    {
        // Lane rectangles also serve hit-testing and therefore use editor coordinates.
        const juce::Graphics::ScopedSaveState laneState(g);
        g.addTransform(juce::AffineTransform::translation(-static_cast<float>(browserWidth), 0.0f));
        drawRuler(g, inputRect_, pattern_.inputBars);
        drawInput(g); drawOrder(g);
    }
    g.setColour(muted); g.setFont(font(14));
    g.drawText("Select cells | Shift-click range | Right-click colour", juce::Rectangle<float>(28, inputRect_.getBottom() + 6, w - 350.0f, 24), juce::Justification::left);
    g.drawText("Drag a colour down to arrange", juce::Rectangle<float>(w - 330.0f, inputRect_.getBottom() + 6, 302, 24), juce::Justification::right);
    g.setFont(font(14));
    const int toolbarTop = getHeight() - 176;
    const int soundRowTop = getHeight() - 90;
    g.setColour(panel); g.fillRect(0, soundRowTop, w, 50);
    g.setColour(line); g.drawHorizontalLine(toolbarTop, 0, static_cast<float>(w));
    g.drawHorizontalLine(soundRowTop, 0, static_cast<float>(w));
    g.setColour(muted);
    g.drawText("Play length", 28, getHeight() - 164, 77, 28, juce::Justification::left);
    g.drawText("Rest", w - 566, getHeight() - 164, 34, 28, juce::Justification::right);
    g.drawText("Loop", 28, getHeight() - 124, 43, 30, juce::Justification::left);
    g.drawText("Smooth", 28, getHeight() - 84, 58, 30, juce::Justification::left);
    g.drawText("Tighten", 250, getHeight() - 84, 54, 30, juce::Justification::left);
    g.drawText("Nudge", 462, getHeight() - 84, 48, 30, juce::Justification::left);
    const double outputSeconds = pattern_.outputBars * std::max(1, preview_.numerator) * 4.0
        / std::max(1, preview_.denominator) * 60.0 / std::max(1.0, preview_.bpm);
    const auto exportStatus = processor_.hasOutputReady() ? "WAV ready - latest complete loop"
        : outputSeconds > 32.0 ? "WAV unavailable - loop exceeds 32 s"
        : "WAV waiting - play a complete loop";
    g.setColour(processor_.hasOutputReady() ? desk::accent : muted);
    g.drawText(exportStatus, juce::Rectangle<float>(195, getHeight() - 124.0f, w - 450.0f, 30), juce::Justification::centredLeft);
    const int footer = getHeight() - 40;
    g.setColour(panel); g.fillRect(0, footer, w, 40); g.setColour(line); g.drawHorizontalLine(footer, 0, static_cast<float>(w));
    juce::String status;
    switch (preview_.status)
    {
        case breakvar::EngineStatus::stopped: status = "Stopped | Start playback to fill Input"; break;
        case breakvar::EngineStatus::waitingForTransport: status = "Waiting for host tempo and transport"; break;
        case breakvar::EngineStatus::filling: status = "Filling history | Missing audio is silent"; break;
        case breakvar::EngineStatus::ready: status = pattern_.orderCount == 0 ? "Live input | Empty arrangement is silent" : "Live | Repeats fill the loop; excess is cut"; break;
        case breakvar::EngineStatus::unsupportedTiming: status = "Host timing exceeds the available history window"; break;
    }
    g.setColour(preview_.status == breakvar::EngineStatus::ready ? desk::accent : muted);
    g.fillEllipse(28, footer + 17.0f, 6, 6);
    g.setColour(messageTicks_ > 0 ? text : muted); g.setFont(font(16));
    g.drawFittedText(messageTicks_ > 0 ? message_ : status, 43, footer + 3, w - 340, 34, juce::Justification::centredLeft, 2, 0.9f);
    g.setColour(muted);
    const auto latency = processor_.getSampleRate() > 0 ? juce::String(static_cast<int>(std::lround(preview_.latencySamples * 1000.0 / processor_.getSampleRate()))) + " ms" : "--";
    g.drawText("Host delay " + latency + " | " + juce::String(pattern_.orderCount) + " phrases", w - 283, footer + 7, 255, 26, juce::Justification::right);
    if (draggingPhrase_)
    {
        const int c = draggedOrder_ >= 0 && draggedOrder_ < pattern_.orderCount ? pattern_.order[draggedOrder_] : sourceDragColour_;
        if (c >= 0)
        {
            auto r = juce::Rectangle<float>(dragPoint_.x - browserWidth + 12, dragPoint_.y - 22, 100, 29);
            g.setColour(desk::display); g.fillRoundedRectangle(r, 3);
            g.setColour(orderColour(c)); g.fillRect(r.withHeight(3).reduced(2, 0));
            g.setColour(desk::displayText); g.setFont(font(14, true));
            g.drawText(orderName(c), r.reduced(8, 0), juce::Justification::centredLeft);
        }
    }
}

void BreakVarAudioProcessorEditor::syncControls()
{
    preview_ = processor_.getPreview();
    inputLength_.setSelectedId(pattern_.inputBars, juce::dontSendNotification);
    grid_.setSelectedId(pattern_.grid, juce::dontSendNotification);
    outputLength_.setSelectedId(pattern_.outputBars, juce::dontSendNotification);
    syncPresetSelection();
    syncPresetStatus();
    undo_.setEnabled(processor_.canUndo()); redo_.setEnabled(processor_.canRedo());
    if (selectedOrder_ >= pattern_.orderCount) selectedOrder_ = -1;
    duplicate_.setEnabled(selectedOrder_ >= 0 && pattern_.orderCount < breakvar::maxOrder);
    addRest_.setEnabled(pattern_.orderCount < breakvar::maxOrder);
    remove_.setEnabled(selectedOrder_ >= 0);
    syncPlayLength();
    selectionFirst_ = juce::jlimit(0, pattern_.activeCells() - 1, selectionFirst_);
    selectionLast_ = juce::jlimit(selectionFirst_, pattern_.activeCells() - 1, selectionLast_);
    anchor_ = juce::jlimit(0, pattern_.activeCells() - 1, anchor_);
    syncSoundControls();
    updateOrderScroll();
    syncExportControls();
}

int BreakVarAudioProcessorEditor::selectedTimingColour() const noexcept
{
    if (selectedOrder_ >= 0 && selectedOrder_ < pattern_.orderCount)
    {
        const int token = pattern_.order[selectedOrder_];
        return token >= 0 && token < breakvar::colourCount ? token : -1;
    }
    const int colour = pattern_.cells[selectionFirst_];
    if (colour < 0 || colour >= breakvar::colourCount) return -1;
    for (int i = selectionFirst_; i <= selectionLast_; ++i)
        if (pattern_.cells[i] != colour) return -1;
    return colour;
}

double BreakVarAudioProcessorEditor::appliedTimingOffset(int colour) const noexcept
{
    if (colour < 0 || colour >= breakvar::colourCount || preview_.presetPending) return 0;
    const auto status = preview_.sourceTiming.status[colour];
    if (status == breakvar::TimingStatus::manual) return preview_.sourceTiming.offsetsMs[colour];
    if (status == breakvar::TimingStatus::detected && preview_.playing
        && (preview_.status == breakvar::EngineStatus::ready || preview_.status == breakvar::EngineStatus::filling))
        return preview_.sourceTiming.offsetsMs[colour];
    return 0;
}

void BreakVarAudioProcessorEditor::syncSoundControls()
{
    if (!smooth_.editing() && soundEditing_ != &smooth_) smooth_.setValue(pattern_.smoothTenths / 10.0, juce::dontSendNotification);
    if (!tighten_.editing() && soundEditing_ != &tighten_) tighten_.setValue(pattern_.tightenPercent, juce::dontSendNotification);
    const int colour = selectedTimingColour();
    nudge_.setEnabled(colour >= 0);
    timingAuto_.setEnabled(colour >= 0 && pattern_.nudgeManual[colour]);
    if (!nudge_.editing() && soundEditing_ != &nudge_)
        nudge_.setValue(colour >= 0 && pattern_.nudgeManual[colour] ? pattern_.nudgeTenths[colour] / 10.0 : 0.0,
                        juce::dontSendNotification);
    syncSoundStatus();
}

void BreakVarAudioProcessorEditor::syncSoundStatus()
{
    const int colour = selectedTimingColour();
    juce::String status = "Select a colour";
    if (colour >= 0)
    {
        const auto prefix = juce::String(colourNames[colour]) + ": ";
        if (preview_.presetPending) status = prefix + "queued";
        else if (pattern_.nudgeManual[colour]) status = prefix + "manual";
        else if (pattern_.tightenPercent == 0) status = prefix + "auto off";
        else
        {
            switch (preview_.sourceTiming.status[colour])
            {
                case breakvar::TimingStatus::detected:
                {
                    const auto offset = appliedTimingOffset(colour);
                    status = prefix + (offset > 0 ? "+" : "") + juce::String(offset, 1) + " ms";
                    break;
                }
                case breakvar::TimingStatus::uncertain: status = prefix + "uncertain"; break;
                default: status = prefix + "searching"; break;
            }
        }
    }
    timingStatus_.setText(status, juce::dontSendNotification);
    timingStatus_.setColour(juce::Label::textColourId, colour >= 0 ? desk::sliceInk(colour) : muted);
    timingStatus_.setTooltip(status + ". Nudge reads earlier (-) or later (+); a manual value overrides Tighten. Markers show the applied source start.");
}

void BreakVarAudioProcessorEditor::beginSoundEdit(SoundSlider& slider)
{
    if (soundEditing_ == &slider) return;
    soundEditing_ = &slider;
    soundEditColour_ = selectedTimingColour();
    soundEditPattern_ = processor_.getPattern();
}

void BreakVarAudioProcessorEditor::finishSoundEdit(SoundSlider& slider)
{
    const int colour = soundEditing_ == &slider ? soundEditColour_ : selectedTimingColour();
    const bool changedDuringEdit = soundEditing_ == &slider && !(processor_.getPattern() == soundEditPattern_);
    soundEditing_ = nullptr;
    if (changedDuringEdit) { syncSoundControls(); return; }
    auto next = processor_.getPattern();
    if (&slider == &smooth_) next.smoothTenths = static_cast<int>(std::lround(slider.getValue() * 10));
    else if (&slider == &tighten_) next.tightenPercent = static_cast<int>(std::lround(slider.getValue()));
    else if (colour >= 0 && colour < breakvar::colourCount)
    {
        next.nudgeTenths[colour] = static_cast<int>(std::lround(slider.getValue() * 10));
        next.nudgeManual[colour] = true;
    }
    else return;
    if (!(next == processor_.getPattern())) commit(next, &slider == &nudge_ ? "Manual source timing updated." : "Sound settings updated.");
    else syncSoundControls();
}

void BreakVarAudioProcessorEditor::syncPlayLength()
{
    playLength_.clear(juce::dontSendNotification);
    const bool selected = selectedOrder_ >= 0 && selectedOrder_ < pattern_.orderCount;
    const bool source = selected && !breakvar::isRest(pattern_.order[selectedOrder_]);
    playLength_.setEnabled(source);
    playLength_.setTextWhenNothingSelected(selected ? "Rest: right-click" : "Select a box");
    if (!source) return;
    playLength_.addItem("Full", 1);
    const int available = pattern_.phraseLength(pattern_.order[selectedOrder_]);
    for (int cells = 1; cells <= available; ++cells)
        playLength_.addItem(playDuration(cells, preview_), cells + 1);
    const int requested = pattern_.orderLengths[selectedOrder_];
    if (requested > available)
        playLength_.addItem(playDuration(requested, preview_) + " (max)", requested + 1);
    playLength_.setSelectedId(requested + 1, juce::dontSendNotification);
}

void BreakVarAudioProcessorEditor::setSelectedPlayLength(int cells)
{
    auto next = processor_.getPattern(); std::string reason;
    if (!next.setOrderLength(selectedOrder_, cells, &reason)) { showError(reason); syncControls(); return; }
    commit(next, cells == 0 ? "Order box plays the full colour phrase." : "Order box play length updated.");
}

void BreakVarAudioProcessorEditor::syncExportControls()
{
    const bool ready = processor_.hasOutputReady();
    dragWav_.setEnabled(ready);
    saveWav_.setEnabled(ready);
}

void BreakVarAudioProcessorEditor::refreshPattern(const juce::String& success)
{
    draggingPhrase_ = draggingSelection_ = false;
    sourceDragColour_ = draggedOrder_ = dropIndex_ = selectedOrder_ = -1;
    pattern_ = processor_.getPattern();
    anchor_ = selectionFirst_ = 0;
    selectionLast_ = pattern_.grid - 1;
    message_ = success; messageTicks_ = 70;
    syncControls(); repaint();
}

void BreakVarAudioProcessorEditor::showRandomOrderMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&lookAndFeel_);
    menu.addItem(1, "Shuffle existing order", pattern_.orderCount > 1);
    bool hasRegion = false;
    for (int c = 0; c < breakvar::colourCount; ++c) hasRegion = hasRegion || pattern_.region(c).valid();
    for (int i = 0; i < pattern_.orderCount; ++i) hasRegion = hasRegion || breakvar::isRest(pattern_.order[i]);
    menu.addItem(2, "New sequence", hasRegion);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&randomOrder_)
        .withMinimumWidth(230).withStandardItemHeight(30),
        [safe = juce::Component::SafePointer<BreakVarAudioProcessorEditor>(this)](int result)
        {
            if (safe == nullptr || result == 0) return;
            juce::String error;
            const auto seed = static_cast<std::uint32_t>(juce::Random::getSystemRandom().nextInt());
            const bool changed = result == 1 ? safe->processor_.shuffleOrder(seed, error)
                                             : safe->processor_.generateOrder(seed, error);
            if (changed) safe->refreshPattern(result == 1 ? "Existing order shuffled." : "New order generated.");
            else safe->showError(error);
        });
}

void BreakVarAudioProcessorEditor::refreshPresetMenu()
{
    userPresetNames_ = processor_.getUserPresetNames();
    presetBrowser_.setUserPresets(userPresetNames_);
    syncPresetSelection();
}

void BreakVarAudioProcessorEditor::syncPresetSelection()
{
    const auto name = processor_.getPresetName();
    const bool user = processor_.isCurrentUserPreset();
    const int index = user ? userPresetNames_.indexOf(name, true)
                           : processor_.getFactoryPresetNames().indexOf(name);
    const int id = index < 0 ? 0 : index + (user ? 1001 : 1);
    // Reveal an actual loaded/saved preset, but leave a user's browser filter
    // alone while the current model remains unchanged or becomes Custom.
    if (id > 0 && (name != syncedPresetName_ || user != syncedUserPreset_))
        presetBrowser_.revealPreset(id);
    else presetBrowser_.syncSelection(id);
    presetTitle_.setText((user ? "User: " : "") + name, juce::dontSendNotification);
    presetTitle_.setTooltip((user ? "User preset: " : "Factory preset: ") + name);
    syncedPresetName_ = name;
    syncedUserPreset_ = user;
}

void BreakVarAudioProcessorEditor::syncPresetStatus()
{
    preview_ = processor_.getPreview();
    juce::String status;
    if (preview_.presetPending)
    {
        status = "Queued - next loop";
        if (preview_.presetBeatsRemaining >= 0.0)
            status += " (" + juce::String(preview_.presetBeatsRemaining, 1) + " beats)";
    }
    else if (preview_.status == breakvar::EngineStatus::waitingForTransport)
        status = "Waiting for host timing";
    else if (preview_.status == breakvar::EngineStatus::unsupportedTiming)
        status = "Host timing unsupported";
    else if (!preview_.playing)
        status = "Ready for playback";
    else if (preview_.status == breakvar::EngineStatus::filling)
        status = "Filling input...";
    else
        status = "Active";
    presetLabel_.setText(status, juce::dontSendNotification);
    presetLabel_.setColour(juce::Label::textColourId, preview_.presetPending ? desk::accent : muted);
    presetLabel_.setTooltip(processor_.getPresetName() + " | " + status
        + (preview_.presetPending ? ". The lanes show your queued choice; the previous arrangement is still playing."
                                  : ". Preset changes during playback wait for the next loop; the latest choice wins."));
}

void BreakVarAudioProcessorEditor::cyclePreset(int direction)
{
    // Refresh user names without changing the current filter or loading a preset.
    refreshPresetMenu();
    presetBrowser_.cycle(direction);
}

void BreakVarAudioProcessorEditor::beginPresetSave()
{
    refreshPresetMenu();
    const auto current = processor_.getPresetName();
    presetNameEditor_.setText(current == "Custom" ? "" : current, false);
    replacementName_.clear(); confirmPreset_.setButtonText("Save");
    presetTitle_.setVisible(false); savePreset_.setVisible(false);
    presetBrowser_.setEnabled(false); presetBrowser_.setAlpha(0.55f);
    previousPreset_.setVisible(false); nextPreset_.setVisible(false);
    presetNameEditor_.setVisible(true); confirmPreset_.setVisible(true); cancelPreset_.setVisible(true);
    presetNameEditor_.selectAll();
    if (getPeer() != nullptr) presetNameEditor_.grabKeyboardFocus();
    message_ = "Name this arrangement, then Save. It will appear under User presets.";
    messageTicks_ = 120;
    repaint();
}

void BreakVarAudioProcessorEditor::finishPresetSave()
{
    const auto name = presetNameEditor_.getText().trim();
    const bool replacing = replacementName_.isNotEmpty() && name.equalsIgnoreCase(replacementName_);
    userPresetNames_ = processor_.getUserPresetNames();
    if (userPresetNames_.contains(name, true) && !replacing)
    {
        replacementName_ = name;
        confirmPreset_.setButtonText("Replace");
        showError("This User preset already exists. Choose another name or click Replace.");
        return;
    }
    juce::String error;
    if (!processor_.saveUserPreset(name, replacing, error)) { showError(error); return; }
    cancelPresetSave();
    refreshPresetMenu();
    presetBrowser_.revealPreset(presetBrowser_.selectedPresetId());
    refreshPattern("Preset saved in SHFLR.");
}

void BreakVarAudioProcessorEditor::cancelPresetSave()
{
    presetNameEditor_.setVisible(false); confirmPreset_.setVisible(false); cancelPreset_.setVisible(false);
    presetTitle_.setVisible(true); savePreset_.setVisible(true);
    presetBrowser_.setEnabled(true); presetBrowser_.setAlpha(1.0f);
    previousPreset_.setVisible(true); nextPreset_.setVisible(true);
    replacementName_.clear(); confirmPreset_.setButtonText("Save");
    if (getPeer() != nullptr) grabKeyboardFocus();
    messageTicks_ = 0;
    repaint();
}

void BreakVarAudioProcessorEditor::chooseOutputWav()
{
    if (fileChooserOpen_) { showError("Finish the open file dialog first."); return; }
    const auto initial = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("SHFLR-" + juce::File::createLegalFileName(processor_.getPresetName()) + ".wav");
    fileChooser_ = std::make_unique<juce::FileChooser>("Save SHFLR output WAV", initial, "*.wav");
    fileChooserOpen_ = true;
    fileChooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
        | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe = juce::Component::SafePointer<BreakVarAudioProcessorEditor>(this)](const juce::FileChooser& chooser)
        {
            const auto selected = chooser.getResult();
            if (safe == nullptr) return;
            if (selected != juce::File{})
            {
                juce::String error;
                if (safe->processor_.writeOutputWav(selected, error))
                { safe->message_ = "Output WAV saved."; safe->messageTicks_ = 70; safe->repaint(); }
                else safe->showError(error);
            }
            safe->fileChooserOpen_ = false;
        });
}

void BreakVarAudioProcessorEditor::updateOrderScroll(bool revealSelection)
{
    const double total = std::max(static_cast<double>(orderRect_.getWidth()), pattern_.orderCount * static_cast<double>(tokenWidth));
    orderScroll_.setRangeLimits(0.0, total, juce::dontSendNotification);
    if (revealSelection && selectedOrder_ >= 0)
    {
        const double start = selectedOrder_ * tokenWidth, end = start + tokenWidth;
        if (start < orderOffset_) orderOffset_ = start;
        if (end > orderOffset_ + orderRect_.getWidth()) orderOffset_ = end - orderRect_.getWidth();
    }
    orderOffset_ = juce::jlimit(0.0, std::max(0.0, total - orderRect_.getWidth()), orderOffset_);
    orderScroll_.setCurrentRange(orderOffset_, orderRect_.getWidth(), juce::dontSendNotification);
}

void BreakVarAudioProcessorEditor::scrollBarMoved(juce::ScrollBar*, double start)
{ orderOffset_ = start; repaint(); }

void BreakVarAudioProcessorEditor::timerCallback()
{
    if (soundEditing_ != nullptr && !soundEditing_->editing())
    {
        soundEditing_ = nullptr;
        syncSoundControls();
    }
    const auto nextPreview = processor_.getPreview();
    const bool meterChanged = preview_.numerator != nextPreview.numerator || preview_.denominator != nextPreview.denominator;
    preview_ = nextPreview;
    const auto latest = processor_.getPattern();
    if (!(latest == pattern_))
    {
        draggingPhrase_ = draggingSelection_ = false;
        sourceDragColour_ = draggedOrder_ = dropIndex_ = -1;
        pattern_ = latest; syncControls();
    }
    else if (meterChanged && playLength_.isEnabled())
    {
        // Refresh labels without clearing the menu or replacing a choice whose
        // asynchronous notification is still waiting to reach onChange.
        const int selectedId = playLength_.getSelectedId();
        const int available = pattern_.phraseLength(pattern_.order[selectedOrder_]);
        playLength_.hidePopup();
        for (int i = 0; i < playLength_.getNumItems(); ++i)
        {
            const int id = playLength_.getItemId(i);
            if (id > 1) playLength_.changeItemText(id, playDuration(id - 1, preview_) + (id - 1 > available ? " (max)" : ""));
        }
        playLength_.setSelectedId(selectedId, juce::dontSendNotification);
    }
    if (messageTicks_ > 0) --messageTicks_;
    syncExportControls();
    // Keep unrelated browser navigation intact; a category ComboBox change
    // may still be waiting for its asynchronous notification.
    if (processor_.getPresetName() != syncedPresetName_
        || processor_.isCurrentUserPreset() != syncedUserPreset_)
        syncPresetSelection();
    syncPresetStatus();
    syncSoundStatus();
    repaint();
}

bool BreakVarAudioProcessorEditor::commit(const breakvar::Pattern& next, const juce::String& success)
{
    juce::String error;
    if (!processor_.updatePattern(next, error)) { showError(error); syncControls(); return false; }
    pattern_ = processor_.getPattern(); message_ = success; messageTicks_ = 70;
    syncControls(); repaint(); return true;
}

void BreakVarAudioProcessorEditor::showError(const juce::String& error)
{ message_ = error; messageTicks_ = 150; repaint(); }

void BreakVarAudioProcessorEditor::assignColour(int colour)
{
    auto next = processor_.getPattern(); std::string reason;
    if (!next.assign(selectionFirst_, selectionLast_, colour, &reason)) { showError(reason); return; }
    commit(next, colour < 0 ? "Selected range cleared." : juce::String(colourNames[colour]) + " phrase updated.");
}

void BreakVarAudioProcessorEditor::showColourMenu(juce::Point<int> point)
{
    juce::PopupMenu menu; menu.setLookAndFeel(&lookAndFeel_);
    menu.addSectionHeader("Colour selected range");
    for (int c = 0; c < 8; ++c)
    {
        auto proposed = pattern_; std::string reason;
        const bool valid = proposed.assign(selectionFirst_, selectionLast_, c, &reason);
        menu.addColouredItem(c + 1, juce::String(colourNames[c]) + (valid ? juce::String{} : menuReason(reason)), desk::sliceInk(c), valid, false);
    }
    menu.addSeparator(); auto proposed = pattern_; std::string reason;
    const bool clearable = proposed.assign(selectionFirst_, selectionLast_, -1, &reason);
    menu.addItem(20, "Clear range" + (clearable ? juce::String{} : menuReason(reason)), clearable);
    menu.addSectionHeader("One contiguous region per colour");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea({point.x, point.y, 1, 1})
        .withMinimumWidth(310).withStandardItemHeight(30).withMaximumNumColumns(1),
        [safe = juce::Component::SafePointer<BreakVarAudioProcessorEditor>(this)](int result)
        { if (safe != nullptr) { if (result >= 1 && result <= 8) safe->assignColour(result - 1); else if (result == 20) safe->assignColour(-1); } });
}

void BreakVarAudioProcessorEditor::showOrderMenu(int index, juce::Point<int> point)
{
    selectedOrder_ = index; syncControls(); repaint();
    juce::PopupMenu menu; menu.setLookAndFeel(&lookAndFeel_);
    const int token = pattern_.order[index];
    menu.addSectionHeader(orderName(token) + " | occurrence " + juce::String(index + 1));
    menu.addItem(1, "Duplicate", pattern_.orderCount < breakvar::maxOrder); menu.addItem(2, "Remove");
    if (breakvar::isRest(token))
    {
        juce::PopupMenu lengths;
        for (const int cells : {1, 2, 4, 8, 16})
            lengths.addItem(100 + cells, restDuration(cells), true, breakvar::restCells(token) == cells);
        menu.addSubMenu("Rest length", lengths);
    }
    else
    {
        juce::PopupMenu lengths;
        lengths.addItem(200, "Full", true, pattern_.orderLengths[index] == 0);
        for (int cells = 1; cells <= pattern_.phraseLength(token); ++cells)
            lengths.addItem(200 + cells, playDuration(cells, preview_), true, pattern_.orderLengths[index] == cells);
        menu.addSubMenu("Play length", lengths);
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea({point.x, point.y, 1, 1})
        .withMinimumWidth(220).withStandardItemHeight(30).withMaximumNumColumns(1),
        [safe = juce::Component::SafePointer<BreakVarAudioProcessorEditor>(this), expected = pattern_, index,
         numerator = preview_.numerator, denominator = preview_.denominator](int result)
        {
            if (safe == nullptr || result == 0) return;
            if (safe->processor_.getPattern() != expected)
            { safe->showError("Arrangement changed. Reopen the Order menu to edit it."); return; }
            const auto timing = safe->processor_.getPreview();
            if (result > 200 && (timing.numerator != numerator || timing.denominator != denominator))
            { safe->showError("Host meter changed. Reopen Play length for current beat values."); return; }
            safe->selectedOrder_ = index;
            if (result == 1) safe->duplicateSelected();
            else if (result == 2) safe->removeSelected();
            else if (result >= 200 && result <= 200 + breakvar::maxCells)
                safe->setSelectedPlayLength(result - 200);
            else if (result > 100)
            {
                auto next = safe->processor_.getPattern();
                const int selected = safe->selectedOrder_;
                const int rest = breakvar::restToken(result - 100);
                if (selected >= 0 && selected < next.orderCount && breakvar::isRest(next.order[selected]) && rest >= 0)
                { next.order[selected] = rest; safe->commit(next, "Rest length updated."); }
            }
        });
}

void BreakVarAudioProcessorEditor::addRest()
{
    auto next = processor_.getPattern(); std::string reason;
    const int at = selectedOrder_ >= 0 ? selectedOrder_ + 1 : next.orderCount;
    if (!next.insert(at, breakvar::restToken(restLength_.getSelectedId()), &reason))
    { showError(reason); return; }
    selectedOrder_ = at;
    commit(next, "Rest added. Drag the Order box into place.");
    updateOrderScroll(true);
}

void BreakVarAudioProcessorEditor::removeSelected()
{
    auto next = processor_.getPattern();
    if (selectedOrder_ < 0 || !next.erase(selectedOrder_)) return;
    selectedOrder_ = std::min(selectedOrder_, next.orderCount - 1); commit(next, "Output occurrence removed.");
}

void BreakVarAudioProcessorEditor::duplicateSelected()
{
    auto next = processor_.getPattern(); std::string reason;
    if (selectedOrder_ < 0 || selectedOrder_ >= next.orderCount) return;
    if (!next.duplicate(selectedOrder_, &reason)) { showError(reason); return; }
    ++selectedOrder_; commit(next, "Output occurrence duplicated."); updateOrderScroll(true);
}

int BreakVarAudioProcessorEditor::sourceCellAt(float x) const
{
    const int cell = juce::jlimit(0, pattern_.activeCells() - 1, static_cast<int>((x - inputRect_.getX()) / inputRect_.getWidth() * pattern_.activeCells()));
    return cell / pattern_.grid * pattern_.grid;
}

void BreakVarAudioProcessorEditor::selectThrough(int cell)
{
    selectionFirst_ = std::min(anchor_, cell);
    selectionLast_ = std::min(pattern_.activeCells() - 1, std::max(anchor_, cell) + pattern_.grid - 1);
    repaint();
}

int BreakVarAudioProcessorEditor::orderAt(juce::Point<float> point) const
{
    if (orderRect_.contains(point))
    {
        const int i = static_cast<int>((point.x - orderRect_.getX() + orderOffset_) / tokenWidth);
        return i >= 0 && i < pattern_.orderCount ? i : -1;
    }
    return -1;
}

int BreakVarAudioProcessorEditor::insertionAt(juce::Point<float> point) const
{
    if (orderRect_.contains(point))
        return juce::jlimit(0, pattern_.orderCount, static_cast<int>((point.x - orderRect_.getX() + orderOffset_ + tokenWidth / 2) / tokenWidth));
    return -1;
}

void BreakVarAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    if (e.eventComponent == &dragWav_)
    {
        exportDragFile_ = juce::File{}; exportDragStarted_ = false;
        if (!e.mods.isLeftButtonDown()) return;
        juce::String error;
        exportDragFile_ = processor_.createOutputWav(error);
        if (exportDragFile_ == juce::File{}) showError(error);
        return;
    }
    if (e.position.x < browserWidth || outputPreview_.getBounds().toFloat().contains(e.position)) return;
    grabKeyboardFocus(); draggingPhrase_ = draggingSelection_ = false; sourceDragColour_ = draggedOrder_ = dropIndex_ = -1;
    if (inputRect_.contains(e.position))
    {
        const int cell = sourceCellAt(e.position.x);
        selectedOrder_ = -1;
        if (e.mods.isPopupMenu())
        {
            if (cell < selectionFirst_ || cell > selectionLast_) { anchor_ = cell; selectThrough(cell); }
            syncSoundControls(); showColourMenu(e.getScreenPosition()); return;
        }
        if (!e.mods.isShiftDown()) anchor_ = cell;
        selectThrough(cell);
        const int exactCell = juce::jlimit(0, pattern_.activeCells() - 1, static_cast<int>((e.position.x - inputRect_.getX()) / inputRect_.getWidth() * pattern_.activeCells()));
        sourceDragColour_ = pattern_.cells[exactCell]; draggingSelection_ = true;
        selectedOrder_ = -1; syncControls();
    }
    else if (const int index = orderAt(e.position); index >= 0)
    {
        selectedOrder_ = index; syncControls(); updateOrderScroll(true); repaint();
        if (e.mods.isPopupMenu()) showOrderMenu(index, e.getScreenPosition());
        else draggedOrder_ = index;
    }
}

void BreakVarAudioProcessorEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (e.eventComponent == &dragWav_)
    {
        if (!exportDragStarted_ && e.getDistanceFromDragStart() >= 5 && exportDragFile_.existsAsFile())
        {
            exportDragStarted_ = true;
            if (!juce::DragAndDropContainer::performExternalDragDropOfFiles(
                    juce::StringArray{exportDragFile_.getFullPathName()}, false, &dragWav_))
                showError("WAV drag could not start. Use Save WAV.");
        }
        return;
    }
    dragPoint_ = e.position;
    if (e.getDistanceFromDragStart() < 5) return;
    if (draggedOrder_ >= 0) draggingPhrase_ = true;
    if (draggingSelection_)
    {
        if (sourceDragColour_ >= 0 && (draggingPhrase_ || e.position.y > inputRect_.getBottom() + 8))
            draggingPhrase_ = true;
        else selectThrough(sourceCellAt(e.position.x));
    }
    if (draggingPhrase_)
    {
        if (orderRect_.contains(e.position))
        {
            if (e.position.x < orderRect_.getX() + 24) orderOffset_ -= 16;
            if (e.position.x > orderRect_.getRight() - 24) orderOffset_ += 16;
            updateOrderScroll();
        }
        dropIndex_ = insertionAt(e.position);
    }
    repaint();
}

void BreakVarAudioProcessorEditor::mouseUp(const juce::MouseEvent& e)
{
    if (e.eventComponent == &dragWav_) { exportDragFile_ = juce::File{}; exportDragStarted_ = false; return; }
    if (draggingPhrase_)
    {
        const int at = insertionAt(e.position);
        if (at >= 0)
        {
            auto next = processor_.getPattern(); std::string reason;
            if (draggedOrder_ >= 0)
            {
                if (next.move(draggedOrder_, at))
                {
                    selectedOrder_ = juce::jlimit(0, next.orderCount - 1, at - (draggedOrder_ < at ? 1 : 0));
                    commit(next, "Phrase order updated."); updateOrderScroll(true);
                }
            }
            else if (sourceDragColour_ >= 0)
            {
                if (next.insert(at, sourceDragColour_, &reason))
                { selectedOrder_ = at; commit(next, "Whole colour phrase inserted."); updateOrderScroll(true); }
                else showError(reason);
            }
        }
    }
    draggingPhrase_ = draggingSelection_ = false; sourceDragColour_ = draggedOrder_ = dropIndex_ = -1;
    syncSoundControls(); repaint();
}

void BreakVarAudioProcessorEditor::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (const int index = orderAt(e.position); index >= 0)
    { selectedOrder_ = index; duplicateSelected(); }
}

void BreakVarAudioProcessorEditor::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (orderRect_.expanded(0, 16).contains(e.position))
    {
        orderOffset_ -= (wheel.deltaX != 0 ? wheel.deltaX : wheel.deltaY) * 420.0;
        updateOrderScroll(); repaint();
    }
}

bool BreakVarAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    if (mods.isCommandDown() && (code == 'Z' || code == 'z'))
    { if (mods.isShiftDown()) redo_.onClick(); else undo_.onClick(); return true; }
    if (mods.isCommandDown() && (code == 'Y' || code == 'y')) { redo_.onClick(); return true; }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    { if (selectedOrder_ >= 0) removeSelected(); else assignColour(-1); return true; }
    if (code == juce::KeyPress::escapeKey)
    { draggingPhrase_ = draggingSelection_ = false; dropIndex_ = -1; repaint(); return true; }
    return false;
}
