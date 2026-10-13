#pragma once

#include "Engine/RackProcessor.h"
#include "Theme.h"
#include <map>

namespace sr::ui
{
// Keeps one editor window per plugin slot.
class PluginWindows
{
public:
    ~PluginWindows() { closeAll(); }

    void open (PluginSlot&);
    void close (PluginSlot& slot) { windows.erase (&slot); }
    void closeAll() { windows.clear(); }

private:
    std::map<PluginSlot*, std::unique_ptr<juce::DocumentWindow>> windows;
    JUCE_DECLARE_WEAK_REFERENCEABLE (PluginWindows)
};

// Button that shows the plugin in an insert slot and offers load/edit/bypass/remove.
class SlotButton : public juce::TextButton
{
public:
    SlotButton (RackProcessor&, PluginWindows&);

    void setSlot (PluginSlot* newSlot);
    void setEmptyText (const juce::String& newText) { emptyText = newText; refresh(); }
    void refresh();

private:
    using juce::TextButton::clicked;
    void clicked() override;
    void load (const juce::PluginDescription&);

    RackProcessor& rack;
    PluginWindows& windows;
    PluginSlot* slot = nullptr;
    juce::String emptyText { "+ insert" };
};
} // namespace sr::ui
