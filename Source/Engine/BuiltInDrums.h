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
    static constexpr int presetIndex = 8;   ///< built-in "preset" number used in InstrumentKey

    BuiltInDrums();

    void prepare (double sampleRate, int maxBlockSize) override;
    void process (juce::AudioBuffer<float>& stereoOut, juce::MidiBuffer& midi) noexcept override;
    void allNotesOff() noexcept override;
    juce::String getName() const override { return "Drumstel (GM)"; }

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
        float choke = 1.0f;         ///< fades to 0 when choked
        bool choking = false;
    };

    void trigger (int note, int velocity) noexcept;
    float renderVoice (Voice& v) noexcept;
    float noise() noexcept;

    std::array<Voice, 32> voices {};
    float sr = 48000.0f, dt = 1.0f / 48000.0f;
    uint32_t rng = 0x12345678u;
};

} // namespace km
