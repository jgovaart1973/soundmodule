#pragma once

#include "App/AppController.h"
#include "UI/MasterPanel.h"
#include "UI/PartRow.h"
#include "UI/TopBar.h"
#include "UI/ArrangerComponent.h"

#include <map>
#include <set>

namespace km
{
class SettingsComponent;

/** The main window content: top bar, 16 part rows, master panel and status line. */
class MainComponent final : public juce::Component, private juce::Timer
{
public:
    explicit MainComponent (AppController& controller);
    ~MainComponent() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class PluginWindow;
    class ColumnHeader;

    void timerCallback() override;
    void refreshAll();
    void openPluginEditor (int part);
    void closePluginEditor (int part);
    bool isPluginEditorOpen (int part) const;
    void openSettings();
    void saveAs();
    void zoom (float delta);
    void showArranger (bool arranger);

    AppController& controller;
    TopBar topBar;
    std::unique_ptr<ColumnHeader> header;
    juce::Viewport partsViewport;
    juce::Component partsContainer;
    juce::OwnedArray<PartRow> rows;
    MasterPanel masterPanel;
    ArrangerComponent arranger;
    bool arrangerVisible = false;
    juce::Label statusLabel;
    juce::TooltipWindow tooltips { this, 600 };

    std::map<int, std::unique_ptr<PluginWindow>> pluginWindows;
    std::set<int> openEditorWhenLoaded;
    juce::Component::SafePointer<juce::DialogWindow> settingsWindow;
    SettingsComponent* settingsContent = nullptr;
    std::unique_ptr<juce::FileChooser> fileChooser;
};

} // namespace km
