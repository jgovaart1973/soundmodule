#pragma once

#include "App/AppController.h"
#include "UI/PartRow.h"

namespace km
{
/** Performance selection, save, panic, MIDI activity per device, CPU/xrun status, settings. */
class TopBar final : public juce::Component
{
public:
    explicit TopBar (AppController& controller);

    void refreshFromModel();
    void refreshDevices();
    void tick();

    std::function<void()> onSettings;
    std::function<void()> onSaveAs;
    std::function<void (float delta)> onZoom;
    std::function<void (bool arranger)> onView;
    void setView (bool arranger);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    struct DeviceLamp
    {
        int slot = -1;
        std::unique_ptr<ActivityLamp> lamp;
        std::unique_ptr<juce::Label> name;
    };

    AppController& controller;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, newButton { "Nieuw" }, saveButton { "Opslaan" },
                     saveAsButton { "Opslaan als..." }, panicButton { "PANIC" }, settingsButton { "Instellingen" },
                     zoomOut { "-" }, zoomIn { "+" }, moduleView { "Module" }, arrangerView { "Arranger" };
    juce::ComboBox performanceBox;
    juce::Label dirtyLabel, cpuLabel;
    std::vector<DeviceLamp> deviceLamps;
};

} // namespace km
