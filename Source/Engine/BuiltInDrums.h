#pragma once

#include "Engine/Instrument.h"

namespace km
{
/**
    Built-in drum kit on the General MIDI drum map (kick 36, snare 38, hihats 42/44/46, toms,
    cymbals, percussion). Fully synthesised, so the drum editor is playable without plug-ins.
    Closed and pedal hihat choke the open hihat, like a real kit.
*/
class BuiltInDrums final : public Instrument
{
public:
    /** Built-in "preset" numbers used in InstrumentKey: 100 = acoustic kit, 101 = electronic kit.
        (Older files used 8 for the electronic kit; InstrumentKey::fromVar converts that.) */
    static constexpr int firstRef = 100;
    static constexpr int numKits = 2;
    static constexpr int presetIndex = firstRef;   ///< default kit (acoustic)
    static bool isDrumRef (int ref) noexcept { return ref >= firstRef && ref < firstRef + numKits; }
    static juce::String getKitName (int kit) { return kit == 1 ? "Drumstel Elektronisch" : "Drumstel Akoestisch"; }
    /** General MIDI / GS drum program -> kit (25 = TR-808 / electronic, everything else acoustic). */
    static int kitForProgram (int program) noexcept { return (program >= 24 && program <= 26) ? 1 : 0; }

    explicit BuiltInDrums (int kit = 0);
    int getKit() const noexcept { return kit; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void process (juce::AudioBuffer<float>& stereoOut, juce::MidiBuffer& midi) noexcept override;
    void allNotesOff() noexcept override;
    juce::String getName() const override { return getKitName (kit); }

private:
    enum class Kind { kick, snare, rim, clap, hatClosed, hatPedal, hatOpen, tom, crash, ride, rideBell,
                      tambourine, cowbell, shaker, clave, blip };

    struct Voice
    {
        bool active = false;
        Kind kind = Kind::blip;
        int note = 0;
        float velocity = 1.0f, panL = 1.0f, panR = 1.0f;
        float t = 0.0f;             ///< seconds since the hit
        float freq = 100.0f, phase = 0.0f;
        float metalPhase[6] {};
        float hpState = 0.0f, hpPrev = 0.0f, lpState = 0.0f, bpLow = 0.0f, bpBand = 0.0f;
        float phase2 = 0.0f, bp2Low = 0.0f, bp2Band = 0.0f, lp2 = 0.0f;
        float choke = 1.0f;         ///< fades to 0 when choked
        bool choking = false;
    };

    void trigger (int note, int velocity) noexcept;
    float renderVoice (Voice& v) noexcept;
    bool renderAcoustic (Voice& v, float& s) noexcept;   ///< acoustic kit sounds; false = use the shared sound
    float noise() noexcept;

    std::array<Voice, 32> voices {};
    int kit = 0;
    float sr = 48000.0f, dt = 1.0f / 48000.0f;
    uint32_t rng = 0x12345678u;
};

} // namespace km
