#pragma once
#include "PluginProcessor.h"
#include "PlaybackVisuals.h"
#include "PresetBrowser.h"
#include <juce_gui_basics/juce_gui_basics.h>

class BreakVarAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           private juce::Timer,
                                           private juce::ScrollBar::Listener
{
public:
    explicit BreakVarAudioProcessorEditor(BreakVarAudioProcessor&);
    ~BreakVarAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    struct SoundSlider final : juce::Slider
    {
        std::function<void()> beginEdit, commitEdit;
        bool dragging = false;
        bool editing() const;
        void startedDragging() override { dragging = true; if (beginEdit) beginEdit(); }
        void stoppedDragging() override { dragging = false; if (commitEdit) commitEdit(); }
        // Capture the value synchronously, before JUCE's asynchronous listeners
        // can run after a different source colour has been selected.
        void valueChanged() override { if (!dragging && commitEdit) commitEdit(); }
    };
    struct OutputPreview final : juce::Component
    {
        explicit OutputPreview(BreakVarAudioProcessorEditor& editor) : owner(editor)
        {
            setComponentID("output-preview");
            setName("Output preview");
            setInterceptsMouseClicks(false, false);
            setWantsKeyboardFocus(false);
        }
        void paint(juce::Graphics& g) override { owner.drawOutput(g); }
        BreakVarAudioProcessorEditor& owner;
    };
    struct EditorLookAndFeel final : juce::LookAndFeel_V4
    {
        juce::Font getPopupMenuFont() override;
        juce::Font getTextButtonFont(juce::TextButton&, int) override;
        juce::Font getComboBoxFont(juce::ComboBox&) override;
        void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                                  bool highlighted, bool down) override;
        juce::Label* createSliderTextBox(juce::Slider&) override;
    };
    struct PresetArrow final : juce::Button
    {
        explicit PresetArrow(bool up) : Button(up ? "Previous preset" : "Next preset"), pointsUp(up) {}
        void paintButton(juce::Graphics&, bool highlighted, bool down) override;
        const bool pointsUp;
    };
    void timerCallback() override;
    void scrollBarMoved(juce::ScrollBar*, double) override;
    void syncControls();
    void syncPlayLength();
    void syncSoundControls();
    void syncSoundStatus();
    void beginSoundEdit(SoundSlider&);
    void finishSoundEdit(SoundSlider&);
    int selectedTimingColour() const noexcept;
    double appliedTimingOffset(int colour) const noexcept;
    void setSelectedPlayLength(int cells);
    void updateOrderScroll(bool revealSelection = false);
    bool commit(const breakvar::Pattern&, const juce::String& success);
    void showError(const juce::String&);
    void assignColour(int);
    void showColourMenu(juce::Point<int>);
    void showOrderMenu(int, juce::Point<int>);
    void showRandomOrderMenu();
    void addRest();
    void refreshPresetMenu();
    void syncPresetSelection();
    void syncPresetStatus();
    void cyclePreset(int direction);
    void beginPresetSave();
    void finishPresetSave();
    void cancelPresetSave();
    void chooseOutputWav();
    void refreshPattern(const juce::String&);
    void syncExportControls();
    void removeSelected();
    void duplicateSelected();
    void selectThrough(int);
    int sourceCellAt(float) const;
    int orderAt(juce::Point<float>) const;
    int insertionAt(juce::Point<float>) const;
    void drawRuler(juce::Graphics&, juce::Rectangle<float>, int);
    void drawWaveform(juce::Graphics&, juce::Rectangle<float>, double, double, juce::Colour);
    void drawInput(juce::Graphics&);
    void drawOrder(juce::Graphics&);
    void drawOutput(juce::Graphics&);
    breakvar::PlaybackVisual currentPlayback() const noexcept;

    BreakVarAudioProcessor& processor_;
    EditorLookAndFeel lookAndFeel_;
    juce::TooltipWindow tooltips_{this, 650};
    juce::ComboBox inputLength_, grid_, outputLength_, restLength_, playLength_;
    PresetBrowser presetBrowser_;
    PresetArrow previousPreset_{true}, nextPreset_{false};
    juce::TextButton undo_{"Undo"}, redo_{"Redo"}, duplicate_{"Duplicate"}, remove_{"Remove"};
    juce::TextButton savePreset_{"Save preset"}, confirmPreset_{"Save"}, cancelPreset_{"Cancel"};
    juce::TextEditor presetNameEditor_;
    juce::StringArray userPresetNames_;
    juce::String replacementName_;
    juce::String syncedPresetName_;
    bool syncedUserPreset_ = false;
    juce::TextButton randomSlices_{"Random slices"}, randomOrder_{"Random order"};
    juce::TextButton addRest_{"Add rest"};
    juce::TextButton dragWav_{"Drag WAV"}, saveWav_{"Save WAV"};
    SoundSlider smooth_, tighten_, nudge_;
    juce::TextButton timingAuto_{"Auto"};
    juce::Label timingStatus_;
    SoundSlider* soundEditing_ = nullptr;
    int soundEditColour_ = -1;
    breakvar::Pattern soundEditPattern_;
    juce::Label presetTitle_, presetLabel_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
    bool fileChooserOpen_ = false;
    juce::File exportDragFile_;
    bool exportDragStarted_ = false;
    juce::ScrollBar orderScroll_{false};
    OutputPreview outputPreview_{*this};
    breakvar::Pattern pattern_;
    BreakVarPreview preview_;
    juce::Rectangle<float> inputRect_, orderRect_;
    int selectionFirst_ = 0, selectionLast_ = 3, anchor_ = 0, selectedOrder_ = -1;
    int sourceDragColour_ = -1, draggedOrder_ = -1, dropIndex_ = -1;
    bool draggingPhrase_ = false, draggingSelection_ = false;
    double orderOffset_ = 0.0;
    juce::Point<float> dragPoint_;
    juce::String message_;
    int messageTicks_ = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BreakVarAudioProcessorEditor)
};
