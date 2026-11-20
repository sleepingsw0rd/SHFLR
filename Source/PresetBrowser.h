#pragma once
#include "PatternLibrary.h"
#include <juce_gui_basics/juce_gui_basics.h>

// Factory IDs are their stable catalog index + 1. User IDs are their current
// name-list index + 1001. Filtering and model synchronisation never load audio.
class PresetBrowser final : public juce::Component, private juce::ListBoxModel
{
public:
    PresetBrowser();
    ~PresetBrowser() override;
    std::function<void(int)> onPresetSelected;
    std::function<void()> onUserTabSelected;
    void setUserPresets(const juce::StringArray&);
    void syncSelection(int presetId);
    void revealPreset(int presetId);
    int selectedPresetId() const noexcept { return selectedId_; }
    int visiblePresetCount() const noexcept { return static_cast<int>(visibleIds_.size()); }
    int visiblePresetId(int row) const noexcept;
    void setFilter(int sourceBars, const juce::String& category);
    int filterSourceBars() const noexcept { return sourceBars_; }
    juce::String filterCategory() const { return category_; }
    void cycle(int direction);
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    int getNumRows() override { return visiblePresetCount(); }
    void paintListBoxItem(int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged(int lastRowSelected) override;
    juce::String getTooltipForRow(int row) override;
    void rebuild();
    void syncRow();
    juce::String displayName(int presetId) const;
    struct SourceTab final : juce::TextButton
    {
        using juce::TextButton::TextButton;
        void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    };
    SourceTab oneBar_{"1 bar"}, twoBars_{"2 bars"}, user_{"User"};
    juce::ComboBox categorySelector_;
    juce::ListBox list_{"Presets", this};
    juce::StringArray userNames_;
    std::vector<int> visibleIds_;
    juce::String category_{"Essentials"};
    int sourceBars_ = 2, selectedId_ = 0;
    bool updating_ = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBrowser)
};
