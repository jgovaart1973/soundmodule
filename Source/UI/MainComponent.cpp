#include "UI/MainComponent.h"

#include "Core/AppPaths.h"
#include "UI/KmLookAndFeel.h"
#include "UI/SettingsComponent.h"

namespace km
{
//==============================================================================
/** Floating window that shows a plug-in's own editor. */
class MainComponent::PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow (const juce::String& title, Instrument& inst, std::function<void()> onClose)
        : DocumentWindow (title, colours::panel, DocumentWindow::closeButton | DocumentWindow::minimiseButton),
          instrument (&inst), closeCallback (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        if (auto* editor = inst.createEditor())
        {
            setContentOwned (editor, true);
            setResizable (editor->isResizable(), false);
        }
        centreWithSize (juce::jmax (200, getWidth()), juce::jmax (120, getHeight()));
        setVisible (true);
    }

    ~PluginWindow() override { clearContentComponent(); } // editor must go before the plug-in

    void closeButtonPressed() override
    {
        // Defer: the callback destroys this window.
        juce::MessageManager::callAsync ([cb = closeCallback] { if (cb) cb(); });
    }

    Instrument* instrument;

private:
    std::function<void()> closeCallback;
};

//==============================================================================
class MainComponent::ColumnHeader final : public juce::Component
{
public:
    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::background);
        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (12.0f));
        int x = 4;
        for (auto& [title, width] : PartRow::getColumns())
        {
            g.drawText (title, x, 0, width, getHeight(), juce::Justification::centredLeft, true);
            x += width + 4;
        }
    }
};

//==============================================================================
MainComponent::MainComponent (AppController& c)
    : controller (c), topBar (c), header (std::make_unique<ColumnHeader>()), masterPanel (c)
{
    setWantsKeyboardFocus (true);

    addAndMakeVisible (topBar);
    addAndMakeVisible (*header);
    addAndMakeVisible (partsViewport);
    addAndMakeVisible (masterPanel);
    addAndMakeVisible (statusLabel);
    statusLabel.setColour (juce::Label::textColourId, colours::textDim);
    statusLabel.setText ("Klaar. Sluit je MIDI-keyboard aan en speel - part 1 luistert naar alle ingangen.", juce::dontSendNotification);

    partsViewport.setViewedComponent (&partsContainer, false);
    partsViewport.setScrollBarsShown (true, true);

    for (int i = 0; i < kNumParts; ++i)
    {
        auto* row = rows.add (new PartRow (controller, i));
        row->onOpenEditor = [this] (int part) { openPluginEditor (part); };
        partsContainer.addAndMakeVisible (row);
    }

    topBar.onSettings = [this] { openSettings(); };
    topBar.onSaveAs = [this] { saveAs(); };
    topBar.onZoom = [this] (float d) { zoom (d); };

    controller.onPerformanceChanged = [this] { refreshAll(); };
    controller.onPartChanged = [this] (int part)
    {
        if (auto it = pluginWindows.find (part); it != pluginWindows.end()
            && it->second->instrument != controller.getAssignedInstrument (part))
            closePluginEditor (part);
        rows[part]->refreshFromModel();
    };
    controller.onDevicesChanged = [this]
    {
        for (auto* r : rows)
            r->refreshFromModel();
        topBar.refreshDevices();
        if (settingsContent != nullptr && settingsWindow != nullptr)
            settingsContent->refreshMidi();
    };
    controller.onStatus = [this] (const juce::String& msg) { statusLabel.setText (msg, juce::dontSendNotification); };

    setSize (1640, 940);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    controller.onPerformanceChanged = nullptr;
    controller.onPartChanged = nullptr;
    controller.onDevicesChanged = nullptr;
    controller.onStatus = nullptr;
    pluginWindows.clear();
    if (settingsWindow != nullptr)
        delete settingsWindow.getComponent();
}

void MainComponent::refreshAll()
{
    for (int i = 0; i < kNumParts; ++i)
    {
        if (auto it = pluginWindows.find (i); it != pluginWindows.end()
            && it->second->instrument != controller.getAssignedInstrument (i))
            closePluginEditor (i);
        rows[i]->refreshFromModel();
    }
    topBar.refreshFromModel();
    masterPanel.refreshFromModel();
}

void MainComponent::timerCallback()
{
    topBar.tick();
    masterPanel.tick();
    for (auto* r : rows)
        r->tick();
}

//==============================================================================
void MainComponent::openPluginEditor (int part)
{
    auto* inst = controller.getAssignedInstrument (part);
    if (inst == nullptr || ! inst->hasEditor())
        return;

    if (auto it = pluginWindows.find (part); it != pluginWindows.end())
    {
        it->second->toFront (true);
        return;
    }

    pluginWindows[part] = std::make_unique<PluginWindow> ("Part " + juce::String (part + 1) + " - " + inst->getName(), *inst,
                                                          [this, part] { closePluginEditor (part); });
}

void MainComponent::closePluginEditor (int part)
{
    pluginWindows.erase (part);
}

void MainComponent::openSettings()
{
    if (settingsWindow != nullptr)
    {
        settingsWindow->toFront (true);
        return;
    }

    settingsContent = new SettingsComponent (controller);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (settingsContent);
    options.dialogTitle = "Instellingen";
    options.dialogBackgroundColour = colours::panel;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    settingsWindow = options.launchAsync();
}

void MainComponent::saveAs()
{
    const auto suggested = paths::performancesDir().getChildFile (juce::File::createLegalFileName (controller.getPerformance().name) + ".kmperf");
    fileChooser = std::make_unique<juce::FileChooser> ("Performance opslaan", suggested, "*.kmperf");

    juce::Component::SafePointer<MainComponent> safe (this);
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe] (const juce::FileChooser& fc)
        {
            if (safe == nullptr || fc.getResult() == juce::File())
                return;
            const auto file = fc.getResult().withFileExtension ("kmperf");
            safe->controller.savePerformanceAs (file, file.getFileNameWithoutExtension());
        });
}

void MainComponent::zoom (float delta)
{
    auto& settings = controller.getSettings();
    const float scale = juce::jlimit (0.6f, 2.0f, (float) settings.getDoubleValue ("uiScale", 1.0) + delta);
    settings.setValue ("uiScale", scale);
    juce::Desktop::getInstance().setGlobalScaleFactor (scale);
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)        { controller.panic(); return true; }
    if (key == juce::KeyPress::pageUpKey)        { controller.previousPerformance(); return true; }
    if (key == juce::KeyPress::pageDownKey)      { controller.nextPerformance(); return true; }
    if (key == juce::KeyPress ('s', juce::ModifierKeys::commandModifier, 0))
    {
        if (! controller.savePerformance())
            saveAs();
        return true;
    }
    if (key == juce::KeyPress ('=', juce::ModifierKeys::commandModifier, 0)
        || key == juce::KeyPress ('+', juce::ModifierKeys::commandModifier, 0)) { zoom (0.1f); return true; }
    if (key == juce::KeyPress ('-', juce::ModifierKeys::commandModifier, 0))     { zoom (-0.1f); return true; }
    return false;
}

//==============================================================================
void MainComponent::resized()
{
    auto area = getLocalBounds();
    topBar.setBounds (area.removeFromTop (52));
    statusLabel.setBounds (area.removeFromBottom (26).reduced (8, 0));
    masterPanel.setBounds (area.removeFromRight (240));

    header->setBounds (area.removeFromTop (22));

    int rowWidth = 8;
    for (auto& col : PartRow::getColumns())
        rowWidth += col.second + 4;

    partsViewport.setBounds (area);
    partsContainer.setSize (juce::jmax (rowWidth, area.getWidth() - partsViewport.getScrollBarThickness()),
                            PartRow::rowHeight * kNumParts);
    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * PartRow::rowHeight, partsContainer.getWidth(), PartRow::rowHeight);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

} // namespace km
