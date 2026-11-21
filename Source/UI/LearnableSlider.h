#pragma once

#include "Midi/MidiLearn.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace km
{
/**
    A slider that can be bound to a MIDI controller: right-click (or long-press) -> "MIDI Learn",
    then move a knob on the keyboard. Shows an orange ring while waiting and a dot when bound.
*/
class LearnableSlider final : public juce::Slider
{
public:
    LearnableSlider (MidiLearn& learn, juce::String paramId);

    void mouseDown (const juce::MouseEvent&) override;
    void paint (juce::Graphics&) override;

    /** Poll the learn state (call from a UI timer). */
    void refreshLearnState();

    const juce::String& getParamId() const noexcept { return paramId; }

private:
    void showLearnMenu();

    MidiLearn& learn;
    juce::String paramId;
    bool armed = false, mapped = false;
};

} // namespace km
