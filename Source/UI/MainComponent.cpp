#include "UI/MainComponent.h"

#include "Core/AppPaths.h"
#include "UI/KmLookAndFeel.h"
#include "UI/SettingsComponent.h"

namespace km
{
//==============================================================================
/**
    Floating window with a plug-in's original editor, for live editing while playing.
    A thin toolbar on top offers "always on top"; the window remembers where it was.
*/
class MainComponent::PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow (const juce::String& title, Instrument& inst, juce::PropertiesFile& s, std::function<void()> onClose)
        : DocumentWindow (title, colours::panel, DocumentWindow::closeButton | DocumentWindow::minimiseButton),
          instrument (&inst), settings (s), closeCallback (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        positionKey = "editorPos_" + juce::File::createLegalFileName (inst.getName());

        auto* editor = inst.createEditor();
        holder = new Holder (editor, *this);
        setContentOwned (holder, true);
        setResizable (editor != nullptr && editor->isResizable(), false);

        const bool onTop = settings.getBoolValue ("editorAlwaysOnTop", true);
        holder->onTop.setToggleState (onTop, juce::dontSendNotification);
        setAlwaysOnTop (onTop);

        // Restore the last position if it is still on a screen.
        const auto xy = juce::StringArray::fromTokens (settings.getValue (positionKey), ",", {});
        const juce::Point<int> saved (xy[0].getIntValue(), xy[1].getIntValue());
        if (settings.containsKey (positionKey)
            && juce::Desktop::getInstance().getDisplays().getDisplayForPoint (saved.toFloat()) != nullptr)
            setTopLeftPosition (saved);
        else
            centreWithSize (getWidth(), getHeight());

        setVisible (true);
        toFront (true);
    }

    ~PluginWindow() override
    {
        settings.setValue (positionKey, juce::String (getX()) + "," + juce::String (getY()));
        clearContentComponent(); // the editor must be deleted before the plug-in
    }

    void bringForward()
    {
        if (isMinimised())
            setMinimised (false);
        setVisible (true);
        toFront (true);
    }

    void closeButtonPressed() override
    {
        // Deferred: the callback destroys this window.
        juce::MessageManager::callAsync ([cb = closeCallback] { if (cb) cb(); });
    }

    Instrument* instrument;

private:
    /** Toolbar + the plug-in editor; follows the editor's size. */
    struct Holder final : public juce::Component, private juce::ComponentListener
    {
        Holder (juce::AudioProcessorEditor* ed, PluginWindow& w) : editor (ed), window (w)
        {
            addAndMakeVisible (onTop);
            onTop.setButtonText ("Altijd bovenop");
            onTop.onClick = [this]
            {
                window.setAlwaysOnTop (onTop.getToggleState());
                window.settings.setValue ("editorAlwaysOnTop", onTop.getToggleState());
            };

            addAndMakeVisible (hint);
            hint.setText ("Wijzigingen zijn direct hoorbaar. Opslaan bewaart ze in de performance.", juce::dontSendNotification);
            hint.setColour (juce::Label::textColourId, colours::textDim);
            hint.setFont (juce::FontOptions (12.0f));

            if (editor != nullptr)
            {
                addAndMakeVisible (editor);
                editor->addComponentListener (this);
                setSize (juce::jmax (420, editor->getWidth()), editor->getHeight() + toolbarHeight);
            }
            else
            {
                setSize (420, 120);
            }
        }

        ~Holder() override
        {
            if (editor != nullptr)
            {
                editor->removeComponentListener (this);
                delete editor;
            }
        }

        void resized() override
        {
            auto r = getLocalBounds();
            auto bar = r.removeFromTop (toolbarHeight).reduced (8, 4);
            onTop.setBounds (bar.removeFromLeft (130));
            hint.setBounds (bar);

            if (editor != nullptr && ! updatingFromEditor && editor->isResizable())
            {
                const juce::ScopedValueSetter<bool> svs (updatingFromWindow, true);
                editor->setBounds (r.withWidth (juce::jmax (1, r.getWidth())));
            }
            else if (editor != nullptr)
            {
                editor->setTopLeftPosition (r.getTopLeft());
            }
        }

        void paint (juce::Graphics& g) override { g.fillAll (colours::panel); }

        // The plug-in changed its own size (e.g. a bigger GUI): grow the window with it.
        void componentMovedOrResized (juce::Component& c, bool, bool wasResized) override
        {
            if (! wasResized || &c != editor || updatingFromWindow)
                return;
            const juce::ScopedValueSetter<bool> svs (updatingFromEditor, true);
            setSize (juce::jmax (420, editor->getWidth()), editor->getHeight() + toolbarHeight);
        }

        static constexpr int toolbarHeight = 32;
        juce::AudioProcessorEditor* editor;
        PluginWindow& window;
        juce::ToggleButton onTop;
        juce::Label hint;
        bool updatingFromEditor = false, updatingFromWindow = false;
    };

    juce::PropertiesFile& settings;
    std::function<void()> closeCallback;
    juce::String positionKey;
    Holder* holder = nullptr;
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
    : controller (c), topBar (c), header (std::make_unique<ColumnHeader>()), masterPanel (c), arranger (c)
{
    setWantsKeyboardFocus (true);

    addAndMakeVisible (topBar);
    addAndMakeVisible (*header);
    addAndMakeVisible (partsViewport);
    addAndMakeVisible (masterPanel);
    addChildComponent (arranger);
    addAndMakeVisible (statusLabel);
    statusLabel.setColour (juce::Label::textColourId, colours::textDim);
    statusLabel.setText ("Klaar. Sluit je MIDI-keyboard aan en speel - part 1 luistert naar alle ingangen.", juce::dontSendNotification);

    partsViewport.setViewedComponent (&partsContainer, false);
    partsViewport.setScrollBarsShown (true, true);

    for (int i = 0; i < kNumParts; ++i)
    {
        auto* row = rows.add (new PartRow (controller, i));
        row->onOpenEditor = [this] (int part) { openPluginEditor (part); };
        row->onPluginChosen = [this] (int part) { openEditorWhenLoaded.insert (part); };
        row->isEditorOpen = [this] (int part) { return isPluginEditorOpen (part); };
        partsContainer.addAndMakeVisible (row);
    }

    topBar.onSettings = [this] { openSettings(); };
    topBar.onSaveAs = [this] { saveAs(); };
    topBar.onZoom = [this] (float d) { zoom (d); };
    topBar.onView = [this] (bool a) { showArranger (a); };
    controller.getSongController().onSongChanged = [this] { arranger.refresh(); };

    controller.onPerformanceChanged = [this] { refreshAll(); };
    controller.onPartChanged = [this] (int part)
    {
        if (auto it = pluginWindows.find (part); it != pluginWindows.end()
            && it->second->instrument != controller.getAssignedInstrument (part))
            closePluginEditor (part);
        rows[part]->refreshFromModel();

        // A plug-in was just picked from the menu: show its own window as soon as it is ready.
        if (openEditorWhenLoaded.count (part) > 0 && ! controller.isPartLoading (part))
        {
            openEditorWhenLoaded.erase (part);
            auto* inst = controller.getAssignedInstrument (part);
            if (inst != nullptr && inst->hasEditor() && ! isPluginEditorOpen (part))
                openPluginEditor (part);
        }
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
    if (controller.getSettings().getBoolValue ("arrangerVisible", false))
        showArranger (true);
}

MainComponent::~MainComponent()
{
    controller.onPerformanceChanged = nullptr;
    controller.onPartChanged = nullptr;
    controller.onDevicesChanged = nullptr;
    controller.onStatus = nullptr;
    controller.getSongController().onSongChanged = nullptr;
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
        // Already open: first press brings it forward, a press while it is in front closes it.
        if (it->second->isActiveWindow() && ! it->second->isMinimised())
            closePluginEditor (part);
        else
            it->second->bringForward();
        return;
    }

    pluginWindows[part] = std::make_unique<PluginWindow> ("Part " + juce::String (part + 1) + " - " + inst->getName(), *inst,
                                                          controller.getSettings(),
                                                          [this, part] { closePluginEditor (part); });
}

bool MainComponent::isPluginEditorOpen (int part) const
{
    return pluginWindows.find (part) != pluginWindows.end();
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

void MainComponent::showArranger (bool a)
{
    arrangerVisible = a;
    topBar.setView (a);
    header->setVisible (! a);
    partsViewport.setVisible (! a);
    arranger.setVisible (a);
    if (a)
    {
        arranger.refresh();
        arranger.grabKeyboardFocus();
    }
    controller.getSettings().setValue ("arrangerVisible", a);
    resized();
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
    if (key == juce::KeyPress::F1Key)            { showArranger (false); return true; }
    if (key == juce::KeyPress::F2Key)            { showArranger (true); return true; }
    if (key == juce::KeyPress::spaceKey)         { controller.getSongController().togglePlay(); return true; }
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
    arranger.setBounds (area);

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
