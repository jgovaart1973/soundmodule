#pragma once

#include "App/AppController.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace km
{
/** Settings window content: Audio, MIDI, Plug-ins and Sounds tabs. */
class SettingsComponent final : public juce::Component
{
public:
    explicit SettingsComponent (AppController& controller);
    ~SettingsComponent() override;

    void resized() override;

    /** Call when MIDI devices were plugged in or removed. */
    void refreshMidi();

private:
    class AudioPage;
    class MidiPage;
    class SoundsPage;
    class MidiOutPage;

    AppController& controller;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    MidiPage* midiPage = nullptr;
};

} // namespace km
