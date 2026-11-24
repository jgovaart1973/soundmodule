#pragma once

#include "Engine/Instrument.h"
#include "Midi/MidiOutHub.h"

namespace km
{
/**
    A "sound" that is a hardware synth on a MIDI output (phase 3c). It produces no audio: the
    part's notes (live, from the song or from the arpeggiator) go to the MIDI output on the chosen
    channel. Because it is an ordinary Instrument, sound switches, ringing tails and panic work
    the same as for the built-in sounds: no hanging notes on the hardware.

    Settings are kept in InstrumentKey::pluginState as a short JSON text.
*/
class ExternalMidiInstrument final : public Instrument
{
public:
    struct Settings
    {
        int channel = 0;       ///< 1..16, 0 = keep the channel the notes arrive on
        int bank = -1;         ///< bank select (MSB*128+LSB), -1 = don't send
        int program = -1;      ///< 0..127, -1 = don't send

        juce::String toString() const;
        static Settings fromString (const juce::String&);
    };

    static InstrumentKey makeKey (const juce::String& identifier, const juce::String& deviceName, const Settings&);
    static juce::String describe (const juce::String& deviceName, const Settings&);

    /** @param hub may be nullptr (offline render): everything is dropped. */
    ExternalMidiInstrument (MidiOutHub* hub, const InstrumentKey& key);

    void prepare (double, int) override {}
    void process (juce::AudioBuffer<float>& stereoOut, juce::MidiBuffer& midi) noexcept override;
    void allNotesOff() noexcept override;
    juce::String getName() const override { return name; }

    int getPort() const noexcept { return port; }
    const Settings& getSettings() const noexcept { return settings; }

private:
    void out (const uint8_t* d, int size, int pos) noexcept;

    MidiOutHub* hub;
    int port = -1;
    Settings settings;
    juce::String name;
    bool programSent = false;
    uint8_t sounding[16][128] {};
    bool usedChannel[16] {};
};

} // namespace km
