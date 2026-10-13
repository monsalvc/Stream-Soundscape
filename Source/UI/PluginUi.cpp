#include "PluginUi.h"

namespace sr::ui
{
namespace
{
    class PluginWindow : public juce::DocumentWindow
    {
    public:
        PluginWindow (juce::AudioPluginInstance& plugin, std::function<void()> onCloseRequest)
            : DocumentWindow (plugin.getName(), colours::panel, DocumentWindow::closeButton),
              onClose (std::move (onCloseRequest))
        {
            setUsingNativeTitleBar (true);

            juce::AudioProcessorEditor* editor = plugin.hasEditor() ? plugin.createEditorIfNeeded() : nullptr;
            if (editor == nullptr)
                editor = new juce::GenericAudioProcessorEditor (plugin);

            setContentOwned (editor, true);
            setResizable (editor->isResizable(), false);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        ~PluginWindow() override { clearContentComponent(); }

        void closeButtonPressed() override { onClose(); }

    private:
        std::function<void()> onClose;
    };
} // namespace

void PluginWindows::open (PluginSlot& slot)
{
    auto* plugin = slot.getPlugin();
    if (plugin == nullptr)
        return;

    if (auto it = windows.find (&slot); it != windows.end())
    {
        it->second->toFront (true);
        return;
    }

    juce::WeakReference<PluginWindows> weakThis (this);
    auto* slotPtr = &slot;
    windows[&slot] = std::make_unique<PluginWindow> (*plugin, [weakThis, slotPtr]
    {
        // Defer: the window can't delete itself from inside its own callback.
        juce::MessageManager::callAsync ([weakThis, slotPtr]
        {
            if (weakThis != nullptr)
                weakThis->close (*slotPtr);
        });
    });
}

//==============================================================================
SlotButton::SlotButton (RackProcessor& r, PluginWindows& w) : rack (r), windows (w) { refresh(); }

void SlotButton::setSlot (PluginSlot* newSlot)
{
    slot = newSlot;
    refresh();
}

void SlotButton::refresh()
{
    if (slot == nullptr || slot->getPlugin() == nullptr)
    {
        setButtonText ("+ insert");
        setColour (juce::TextButton::buttonColourId, colours::panelRaised);
        setColour (juce::TextButton::textColourOffId, colours::dim);
        return;
    }

    setButtonText ((slot->bypassed ? "(byp) " : "") + slot->getPluginName());
    setColour (juce::TextButton::buttonColourId, slot->bypassed ? colours::panelRaised : colours::accent.darker (0.6f));
    setColour (juce::TextButton::textColourOffId, colours::text);
}

void SlotButton::load (const juce::PluginDescription& desc)
{
    juce::String error;
    if (! rack.loadPlugin (*slot, &desc, error))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't load plugin",
                                                desc.name + "\n\n" + error);
        return;
    }

    refresh();
    windows.open (*slot);
}

void SlotButton::clicked()
{
    if (slot == nullptr)
        return;

    const auto effects = rack.getHost().getEffects();
    const bool hasPlugin = slot->getPlugin() != nullptr;

    juce::PopupMenu pluginMenu;
    if (effects.isEmpty())
        pluginMenu.addItem (-1, "No plugins scanned yet. Use \"Plugins...\" in the top bar.", false);
    else
        juce::KnownPluginList::addToMenu (pluginMenu, effects, juce::KnownPluginList::sortByManufacturer,
                                          hasPlugin ? slot->getPlugin()->getPluginDescription().createIdentifierString()
                                                    : juce::String());

    juce::PopupMenu menu;
    if (hasPlugin)
    {
        menu.addItem (1, "Open editor");
        menu.addItem (2, "Bypass", true, slot->bypassed);
        menu.addItem (3, "Remove");
        menu.addSeparator();
        menu.addSubMenu ("Replace with", pluginMenu);
    }
    else
    {
        menu = pluginMenu;
    }

    juce::Component::SafePointer<SlotButton> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safeThis, effects] (int result)
    {
        if (safeThis == nullptr || result == 0)
            return;

        auto& self = *safeThis;
        if (result == 1)
            self.windows.open (*self.slot);
        else if (result == 2)
            self.slot->bypassed = ! self.slot->bypassed;
        else if (result == 3)
        {
            juce::String error;
            self.rack.loadPlugin (*self.slot, nullptr, error);
        }
        else if (const int index = juce::KnownPluginList::getIndexChosenByMenu (effects, result); index >= 0)
            self.load (effects.getReference (index));

        if (safeThis != nullptr)
            safeThis->refresh();
    });
}
} // namespace sr::ui
