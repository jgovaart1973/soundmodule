#include "Model/ProgramMap.h"

namespace km
{
const InstrumentKey* ProgramMap::find (int bank, int program) const
{
    auto it = entries.find (index (bank, program));
    return it != entries.end() ? &it->second : nullptr;
}

int ProgramMap::gmFamilyToBuiltInPreset (int program) noexcept
{
    // Built-in presets: 0 E-Piano, 1 Pad, 2 Organ, 3 Bass, 4 Strings, 5 Brass, 6 Bell, 7 Lead
    static constexpr int familyToPreset[16] = {
        0, // Piano
        6, // Chromatic percussion
        2, // Organ
        0, // Guitar (closest: e-piano)
        3, // Bass
        4, // Strings
        4, // Ensemble
        5, // Brass
        7, // Reed
        7, // Pipe
        7, // Synth lead
        1, // Synth pad
        1, // Synth effects
        6, // Ethnic
        6, // Percussive
        1  // Sound effects
    };
    return familyToPreset[(program & 0x7f) / 8];
}

juce::String ProgramMap::toJson() const
{
    juce::Array<juce::var> list;
    for (auto& [idx, key] : entries)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("bank", idx >> 7);
        o->setProperty ("program", idx & 0x7f);
        o->setProperty ("instrument", key.toVar());
        list.add (juce::var (o));
    }

    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "KlankModule.ProgramMap");
    root->setProperty ("version", 1);
    root->setProperty ("entries", list);
    return juce::JSON::toString (juce::var (root), false);
}

bool ProgramMap::fromJson (const juce::String& json)
{
    juce::var root;
    if (juce::JSON::parse (json, root).failed())
        return false;

    if (root["format"].toString() != "KlankModule.ProgramMap")
        return false;

    std::map<int, InstrumentKey> loaded;
    if (auto* arr = root["entries"].getArray())
    {
        for (auto& e : *arr)
        {
            const int bank = juce::jlimit (0, 16383, (int) e["bank"]);
            const int program = juce::jlimit (0, 127, (int) e["program"]);
            auto key = InstrumentKey::fromVar (e["instrument"]);
            if (! key.isEmpty())
                loaded[index (bank, program)] = key;
        }
    }

    entries = std::move (loaded);
    return true;
}

} // namespace km
