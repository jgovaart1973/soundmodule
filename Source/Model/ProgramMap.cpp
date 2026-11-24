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
    // Built-in presets: 0 E-Piano, 1 Pad, 2 Organ, 3 Bass, 4 Strings, 5 Brass, 6 Bell, 7 Lead,
    // 8 Grand Piano, 9 Bright Piano, 10 Nylon, 11 Steel, 12 Clean Electric, 13 Fretless, 14 Finger Bass,
    // 15 Soprano Sax, 16 Alto Sax, 17 Tenor Sax, 18 Flute, 19 Choir Aahs, 20 Choir Oohs
    const int p = program & 0x7f;
    switch (p)
    {
        case 0: case 2: case 3:      return 8;    // Acoustic / electric grand, honky-tonk
        case 1:                      return 9;    // Bright acoustic piano
        case 4: case 5: case 7:      return 0;    // Electric pianos, clavinet
        case 24:                     return 10;   // Nylon guitar
        case 25:                     return 11;   // Steel guitar
        case 26: case 27: case 28:   return 12;   // Jazz / clean / muted electric guitar
        case 32: case 33: case 34:   return 14;   // Acoustic / finger / pick bass
        case 35:                     return 13;   // Fretless bass
        case 52: case 53: case 54:   return p == 52 ? 19 : 20;   // Choir Aahs, Voice Oohs, Synth Voice
        case 64:                     return 15;   // Soprano sax
        case 65:                     return 16;   // Alto sax
        case 66: case 67:            return 17;   // Tenor / baritone sax
        case 72: case 73: case 74: case 75: case 76: case 77: case 78: case 79: return 18;   // Piccolo .. ocarina
        default: break;
    }
    static constexpr int familyToPreset[16] = {
        0, // Piano
        6, // Chromatic percussion
        2, // Organ
        12, // Guitar
        3, // Bass
        4, // Strings
        4, // Ensemble
        5, // Brass
        16, // Reed
        18, // Pipe
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
