#pragma once

#include "App/AppController.h"
#include "UI/LearnableSlider.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace km
{
/** Small level meter with peak decay. */
class LevelMeter final : public juce::Component
{
public:
    void setLevel (float newPeak);
    void paint (juce::Graphics&) override;

private:
    float level = 0.0f;
};

/** Small round lamp that flashes when its counter changes (MIDI activity). */
class ActivityLamp final : public juce::Component
{
public:
    void update (uint32_t counter);
    void paint (juce::Graphics&) override;

private:
    uint32_t last = 0;
    float glow = 0.0f;
};

/** One line of the multitimbral part list: routing, sound, mixer strip and zones. */
class PartRow final : public juce::Component
{
public:
    PartRow (AppController& controller, int partIndex);

    /** Column layout shared with the header. */
    static juce::Array<std::pair<juce::String, int>> getColumns();
    static constexpr int rowHeight = 46;

    void refreshFromModel();   ///< structure changed (performance loaded, instrument, devices)
    void tick();               ///< 30 Hz: meters, lamps, values changed via MIDI Learn

    std::function<void (int part)> onOpenEditor;      ///< "VST" button: open / bring to front / close
    std::function<void (int part)> onPluginChosen;    ///< a plug-in was picked from the menu
    std::function<bool (int part)> isEditorOpen;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void showInstrumentMenu();
    void showArpPanel();
    void chooseSfzFile();
    void rebuildInputList();
    static juce::String noteName (int note);

    AppController& controller;
    const int index;
    juce::StringArray inputIds;

    juce::ToggleButton enableButton;
    juce::Label numberLabel;
    juce::ComboBox inputBox, channelBox, curveBox;
    juce::TextButton instrumentButton, editorButton { "VST" }, arpButton { "Arp" }, muteButton { "M" }, soloButton { "S" };
    LearnableSlider volume, pan, reverb, delay;
    juce::Slider transpose, keyLow, keyHigh, velLow, velHigh;
    LevelMeter meter;
    ActivityLamp lamp;
    std::unique_ptr<juce::FileChooser> fileChooser;
};

} // namespace km
