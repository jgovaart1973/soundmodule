#pragma once

#include "Midi/MidiEvent.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <functional>
#include <map>

namespace km
{
/**
    MIDI Learn: binds incoming controllers (CC) to parameters and actions.

    Parameters are registered once at start-up with a setter. Setters run on the MIDI
    thread and must be thread-safe (they write atomics or post to the message thread).
    A mapped CC is consumed and not forwarded to the parts.
*/
class MidiLearn
{
public:
    using Setter = std::function<void (float normalised)>;

    struct Mapping
    {
        int source = -1;   ///< device slot, -1 = any device
        int channel = 0;   ///< 1..16, 0 = any
        int cc = -1;
        juce::String paramId;
    };

    /** isAction = trigger once when the controller crosses 64 upwards (buttons/footswitches). */
    void registerParameter (const juce::String& id, const juce::String& displayName, Setter setter, bool isAction = false);

    void arm (const juce::String& paramId);
    void cancelArm();
    juce::String getArmedParameter() const;

    void removeMapping (const juce::String& paramId);
    bool hasMapping (const juce::String& paramId) const;
    juce::String describeMapping (const juce::String& paramId) const;
    juce::String getDisplayName (const juce::String& paramId) const;

    /** MIDI thread. @returns true if the event was used for learn/mapping and must not be forwarded. */
    bool handle (const MidiEvent& e);

    juce::String toJson() const;
    void fromJson (const juce::String& json);

    /** Message thread: a new mapping was learned. */
    std::function<void (const juce::String& paramId)> onLearned;

private:
    struct Param
    {
        juce::String displayName;
        Setter setter;
        bool isAction = false;
    };

    mutable juce::CriticalSection lock;
    std::map<juce::String, Param> params;
    juce::Array<Mapping> mappings;
    std::map<int, bool> actionLatched; // index into mappings -> currently "pressed"
    juce::String armed;
};

} // namespace km
