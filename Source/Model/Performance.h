#pragma once

#include "Engine/PartFilter.h"

#include <juce_core/juce_core.h>
#include <array>

namespace km
{
constexpr int kNumParts = 16;

/** Identifies a sound source. Stored in performances and used as the instrument-pool key. */
struct InstrumentKey
{
    enum class Type
    {
        none = 0,
        builtIn,  ///< ref = preset index ("0".."7")
        sfz,      ///< ref = absolute path to .sfz file
        plugin    ///< ref = juce::PluginDescription::createIdentifierString()
    };

    Type type = Type::none;
    juce::String ref;
    juce::String displayName;
    juce::String pluginState;   ///< base64 plug-in state (plugins only)

    bool isEmpty() const noexcept { return type == Type::none; }

    /** Pool key: two keys with the same poolKey can share a pre-loaded instance. */
    juce::String poolKey() const
    {
        return juce::String ((int) type) + "|" + ref + "|"
             + (pluginState.isEmpty() ? juce::String() : juce::String::toHexString (pluginState.hashCode64()));
    }

    bool operator== (const InstrumentKey& o) const { return poolKey() == o.poolKey(); }
    bool operator!= (const InstrumentKey& o) const { return ! operator== (o); }

    static InstrumentKey builtIn (int presetIndex, const juce::String& name)
    {
        return { Type::builtIn, juce::String (presetIndex), name, {} };
    }

    static InstrumentKey sfz (const juce::File& file)
    {
        return { Type::sfz, file.getFullPathName(), file.getFileNameWithoutExtension(), {} };
    }

    juce::var toVar() const;
    static InstrumentKey fromVar (const juce::var&);
};

struct PartConfig
{
    bool enabled = false;
    juce::String inputDeviceId;    ///< empty = all MIDI inputs
    juce::String inputDeviceName;  ///< for display when the device is unplugged
    int channel = 0;               ///< 0 = omni, 1..16
    InstrumentKey instrument;

    float volumeDb = 0.0f;
    float pan = 0.0f;              ///< -1..1
    int transpose = 0;             ///< semitones
    int keyLow = 0, keyHigh = 127;
    int velLow = 1, velHigh = 127;
    filter::VelocityCurve velocityCurve = filter::VelocityCurve::linear;
    int fixedVelocity = 100;
    float reverbSend = 0.15f;      ///< 0..1
    float delaySend = 0.0f;        ///< 0..1
    bool mute = false;
    bool solo = false;

    // Arpeggiator
    bool arpOn = false;
    int arpMode = 0;       ///< Arpeggiator::Mode
    int arpRate = 1;       ///< index into Arpeggiator::rates() (1 = 1/8)
    int arpOctaves = 1;
    float arpGate = 0.6f;
    bool arpLatch = false;

    juce::var toVar() const;
    static PartConfig fromVar (const juce::var&);
};

struct MasterConfig
{
    float volumeDb = 0.0f;
    float reverbSize = 0.55f;      ///< 0..1
    float reverbDamping = 0.4f;    ///< 0..1
    float delayMs = 375.0f;
    float delayFeedback = 0.35f;   ///< 0..0.95
    float limiterThresholdDb = -1.0f;

    juce::var toVar() const;
    static MasterConfig fromVar (const juce::var&);
};

struct Performance
{
    static constexpr int currentVersion = 1;

    juce::String name { "Nieuwe performance" };
    std::array<PartConfig, kNumParts> parts {};
    MasterConfig master;

    /** A ready-to-play default: part 1 = electric piano on all inputs, omni. */
    static Performance makeDefault();

    juce::String toJson() const;
    /** @returns false and leaves *this untouched if the text is not a valid performance. */
    bool fromJson (const juce::String& json, juce::String& error);

    bool saveTo (const juce::File& file) const;
    bool loadFrom (const juce::File& file, juce::String& error);
};

} // namespace km
