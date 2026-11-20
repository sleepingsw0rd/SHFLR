#include "PresetBrowser.h"
#include "SamplerDesk.h"
#include <algorithm>

void PresetBrowser::SourceTab::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const bool active = getToggleState();
    auto area = getLocalBounds();
    if (highlighted || down)
    {
        g.setColour(down ? desk::panel : desk::key);
        g.fillRect(area);
    }
    g.setColour(active ? desk::accent : desk::rule);
    g.fillRect(area.removeFromBottom(active ? 3 : 1));
    g.setColour(active ? desk::ink : desk::muted);
    g.setFont(desk::font(16.0f, active));
    g.drawText(getButtonText(), area, juce::Justification::centred);
    if (hasKeyboardFocus(false))
    {
        g.setColour(desk::ink);
        g.drawRect(area.reduced(1));
    }
}

PresetBrowser::PresetBrowser()
{
    setComponentID("preset-browser");
    setName("Preset browser");
    for (auto* button : {&oneBar_, &twoBars_, &user_})
    {
        addAndMakeVisible(button);
        button->setWantsKeyboardFocus(true);
        button->setRadioGroupId(1);
    }
    oneBar_.setComponentID("preset-source-one");
    twoBars_.setComponentID("preset-source-two");
    user_.setComponentID("preset-source-user");
    oneBar_.setTooltip("Factory presets for one-bar input breaks. Browsing does not change the arrangement.");
    twoBars_.setTooltip("Factory presets for two-bar input breaks. Browsing does not change the arrangement.");
    user_.setTooltip("Your named presets. Save the current arrangement with Save preset.");
    oneBar_.onClick = [this] { setFilter(1, category_); };
    twoBars_.onClick = [this] { setFilter(2, category_); };
    user_.onClick = [this]
    {
        if (onUserTabSelected) onUserTabSelected();
        setFilter(0, category_);
    };
    addAndMakeVisible(categorySelector_);
    categorySelector_.setComponentID("preset-category");
    categorySelector_.setWantsKeyboardFocus(false);
    categorySelector_.setTooltip("Filter the Factory bank by style or purpose.");
    int categoryId = 1;
    for (const auto* category : breakvar::presetCategories)
        categorySelector_.addItem(category, categoryId++);
    categorySelector_.onChange = [this]
    {
        if (!updating_ && sourceBars_ != 0 && categorySelector_.getSelectedId() > 0)
            setFilter(sourceBars_, categorySelector_.getText());
    };
    addAndMakeVisible(list_);
    list_.setComponentID("preset-list");
    list_.setRowHeight(50);
    list_.setMultipleSelectionEnabled(false);
    list_.setClickingTogglesRowSelection(false);
    list_.setOutlineThickness(0);
    list_.setColour(juce::ListBox::backgroundColourId, desk::chassis);
    list_.setColour(juce::ListBox::textColourId, desk::ink);
    list_.setWantsKeyboardFocus(true);
    setFilter(2, category_);
}

PresetBrowser::~PresetBrowser() { list_.setModel(nullptr); }

bool PresetBrowser::keyPressed(const juce::KeyPress& key)
{
    // An unselected ListBox passes these keys to its parent. Keep browser
    // focus from deleting an Order card or clearing the Input selection.
    return key.getKeyCode() == juce::KeyPress::deleteKey
        || key.getKeyCode() == juce::KeyPress::backspaceKey;
}

int PresetBrowser::visiblePresetId(int row) const noexcept
{
    return row >= 0 && row < visiblePresetCount() ? visibleIds_[static_cast<std::size_t>(row)] : 0;
}

void PresetBrowser::setUserPresets(const juce::StringArray& names)
{
    const auto selectedName = selectedId_ >= 1001 && selectedId_ - 1001 < userNames_.size()
        ? userNames_[selectedId_ - 1001] : juce::String{};
    userNames_ = names;
    if (selectedId_ >= 1001)
    {
        const int index = userNames_.indexOf(selectedName, true);
        selectedId_ = index >= 0 ? index + 1001 : 0;
    }
    rebuild();
}

void PresetBrowser::setFilter(int sourceBars, const juce::String& category)
{
    sourceBars_ = sourceBars == 0 ? 0 : sourceBars == 1 ? 1 : 2;
    for (const auto* candidate : breakvar::presetCategories)
        if (category == candidate) { category_ = category; break; }
    const juce::ScopedValueSetter<bool> guard(updating_, true);
    oneBar_.setToggleState(sourceBars_ == 1, juce::dontSendNotification);
    twoBars_.setToggleState(sourceBars_ == 2, juce::dontSendNotification);
    user_.setToggleState(sourceBars_ == 0, juce::dontSendNotification);
    categorySelector_.setEnabled(sourceBars_ != 0);
    categorySelector_.setTextWhenNothingSelected("User presets");
    int categoryId = 0;
    if (sourceBars_ != 0)
        for (int i = 0; i < categorySelector_.getNumItems(); ++i)
            if (categorySelector_.getItemText(i) == category_) { categoryId = i + 1; break; }
    categorySelector_.setSelectedId(categoryId, juce::dontSendNotification);
    rebuild();
}

void PresetBrowser::rebuild()
{
    const juce::ScopedValueSetter<bool> guard(updating_, true);
    visibleIds_.clear();
    if (sourceBars_ == 0)
    {
        for (int i = 0; i < userNames_.size(); ++i) visibleIds_.push_back(i + 1001);
    }
    else
    {
        const auto& presets = breakvar::factoryPatterns();
        for (int i = 0; i < static_cast<int>(presets.size()); ++i)
            if (presets[i].pattern.inputBars == sourceBars_ && category_ == juce::String(presets[i].category))
                visibleIds_.push_back(i + 1);
    }
    list_.updateContent();
    list_.setVisible(!visibleIds_.empty());
    syncRow();
    repaint();
}

void PresetBrowser::syncSelection(int presetId)
{
    selectedId_ = presetId;
    syncRow();
}

void PresetBrowser::revealPreset(int presetId)
{
    selectedId_ = presetId;
    if (presetId >= 1001 && presetId - 1001 < userNames_.size())
        setFilter(0, category_);
    else if (presetId > 0 && presetId <= static_cast<int>(breakvar::factoryPatterns().size()))
    {
        const auto& preset = breakvar::factoryPatterns()[presetId - 1];
        setFilter(preset.pattern.inputBars, preset.category);
    }
    else syncRow();
}

void PresetBrowser::syncRow()
{
    const juce::ScopedValueSetter<bool> guard(updating_, true);
    const auto found = std::find(visibleIds_.begin(), visibleIds_.end(), selectedId_);
    if (found == visibleIds_.end()) list_.deselectAllRows();
    else list_.selectRow(static_cast<int>(std::distance(visibleIds_.begin(), found)), false, true);
    list_.repaint();
}

void PresetBrowser::cycle(int direction)
{
    const int count = visiblePresetCount();
    if (count == 0 || direction == 0) return;
    const auto found = std::find(visibleIds_.begin(), visibleIds_.end(), selectedId_);
    const int current = found == visibleIds_.end() ? -1 : static_cast<int>(std::distance(visibleIds_.begin(), found));
    const int next = current < 0 ? (direction > 0 ? 0 : count - 1)
        : (current + (direction > 0 ? 1 : -1) + count) % count;
    selectedId_ = visiblePresetId(next);
    syncRow();
    if (onPresetSelected) onPresetSelected(selectedId_);
}

void PresetBrowser::selectedRowsChanged(int lastRowSelected)
{
    if (updating_ || lastRowSelected < 0) return;
    const int id = visiblePresetId(lastRowSelected);
    if (id == 0) return;
    selectedId_ = id;
    // This callback is synchronous, including keyboard and rapid row choices.
    // The editor publishes the most recent choice before its timer next runs.
    if (onPresetSelected) onPresetSelected(id);
}

juce::String PresetBrowser::displayName(int presetId) const
{
    if (presetId >= 1001)
        return presetId - 1001 < userNames_.size() ? userNames_[presetId - 1001] : juce::String{};
    if (presetId <= 0 || presetId > static_cast<int>(breakvar::factoryPatterns().size())) return {};
    juce::String name(breakvar::factoryPatterns()[presetId - 1].name);
    for (const auto* prefix : {"One bar - ", "Two bars - "})
        if (name.startsWith(prefix)) return name.substring(static_cast<int>(std::char_traits<char>::length(prefix)));
    return name;
}

juce::String PresetBrowser::getTooltipForRow(int row)
{
    const int id = visiblePresetId(row);
    if (id == 0) return {};
    if (id >= 1001) return displayName(id) + " | User preset";
    const auto& preset = breakvar::factoryPatterns()[id - 1];
    return juce::String(preset.name) + " | " + juce::String(preset.pattern.inputBars) + " bar input, "
        + juce::String(preset.pattern.outputBars) + " bar output loop";
}

void PresetBrowser::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected)
{
    const int id = visiblePresetId(row);
    if (id == 0) return;
    if (selected)
    {
        g.fillAll(desk::key);
        g.setColour(desk::accent);
        g.fillRect(0, 0, 3, height);
    }
    g.setColour(desk::rule);
    g.drawHorizontalLine(height - 1, 0.0f, static_cast<float>(width));
    g.setFont(desk::font(16.0f, selected));
    g.setColour(desk::ink);
    g.drawFittedText(displayName(id), 10, 4, width - 15, 21, juce::Justification::centredLeft, 1, 0.9f);
    juce::String detail = "User preset";
    if (id < 1001)
    {
        const int bars = breakvar::factoryPatterns()[id - 1].pattern.outputBars;
        detail = juce::String(bars) + (bars == 1 ? " bar output" : " bars output");
    }
    g.setFont(desk::mono(14.0f)); g.setColour(desk::muted);
    g.drawText(detail, 10, 25, width - 15, 18, juce::Justification::left);
}

void PresetBrowser::paint(juce::Graphics& g)
{
    g.fillAll(desk::chassis);
    g.setColour(desk::rule); g.drawVerticalLine(getWidth() - 1, 0.0f, static_cast<float>(getHeight()));
    g.setFont(desk::font(17.0f, true)); g.setColour(desk::ink);
    g.drawText("PRESETS", 16, 11, getWidth() - 32, 24, juce::Justification::left);
    g.setFont(desk::mono(14.0f)); g.setColour(desk::muted);
    g.drawText(sourceBars_ == 0 ? "Your library" : "Category", 16, 91, getWidth() - 32, 19, juce::Justification::left);
    const auto count = juce::String(visiblePresetCount()) + (visiblePresetCount() == 1 ? " preset" : " presets");
    g.drawText(count, 16, 150, getWidth() - 32, 22, juce::Justification::left);
    if (visibleIds_.empty())
    {
        g.setFont(desk::font(16.0f));
        g.drawFittedText(sourceBars_ == 0 ? "No User presets yet.\nUse Save preset to keep an arrangement."
            : "No presets in this category.", 20, 196, getWidth() - 40, 88, juce::Justification::centred, 4);
    }
    g.setColour(desk::rule); g.drawHorizontalLine(getHeight() - 40, 0, static_cast<float>(getWidth()));
    g.setFont(desk::mono(14.0f)); g.setColour(desk::muted);
    const int factoryCount = static_cast<int>(breakvar::factoryPatterns().size());
    g.drawText(juce::String(factoryCount) + " factory presets", 16, getHeight() - 34,
               getWidth() - 32, 24, juce::Justification::left);
}

void PresetBrowser::resized()
{
    const int usable = getWidth() - 32, gap = 6, tabWidth = (usable - gap * 2) / 3;
    oneBar_.setBounds(16, 40, tabWidth, 35);
    twoBars_.setBounds(16 + tabWidth + gap, 40, tabWidth, 35);
    user_.setBounds(16 + (tabWidth + gap) * 2, 40, usable - (tabWidth + gap) * 2, 35);
    categorySelector_.setBounds(16, 112, usable, 32);
    list_.setBounds(12, 176, getWidth() - 25, std::max(40, getHeight() - 221));
    // Let the empty-library hint in this component remain visible.
    list_.setVisible(!visibleIds_.empty());
}
