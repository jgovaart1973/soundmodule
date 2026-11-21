#pragma once

#include "Model/Performance.h"

#include <map>

namespace km
{
/**
    Maps MIDI Bank Select + Program Change to sounds, like the patch list of a hardware module.
    Stored as JSON in the app data folder; editable by hand.

    Lookup order for a Program Change on a part:
      1. an explicit entry for (bank, program)
      2. General MIDI mode: a built-in preset from the GM instrument family
      3. otherwise: built-in preset (program % number of presets)
*/
class ProgramMap
{
public:
    static int bankNumber (int msb, int lsb) noexcept { return ((msb & 0x7f) << 7) | (lsb & 0x7f); }

    void set (int bank, int program, const InstrumentKey& key) { entries[index (bank, program)] = key; }
    void remove (int bank, int program)                         { entries.erase (index (bank, program)); }
    bool isEmpty() const noexcept                               { return entries.empty(); }
    size_t size() const noexcept                                { return entries.size(); }

    /** @returns the explicit entry, or nullptr. */
    const InstrumentKey* find (int bank, int program) const;

    /** Built-in preset for a General MIDI program number (0..127). */
    static int gmFamilyToBuiltInPreset (int program) noexcept;

    juce::String toJson() const;
    bool fromJson (const juce::String& json);

private:
    static int index (int bank, int program) noexcept { return (bank << 7) | (program & 0x7f); }
    std::map<int, InstrumentKey> entries;
};

} // namespace km
