#include "Engine/ExternalMidiInstrument.h"

namespace km
{
juce::String ExternalMidiInstrument::Settings::toString() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("channel", channel);
    o->setProperty ("bank", bank);
    o->setProperty ("program", program);
    return juce::JSON::toString (juce::var (o), true);
}

ExternalMidiInstrument::Settings ExternalMidiInstrument::Settings::fromString (const juce::String& s)
{
    Settings r;
    const auto v = juce::JSON::parse (s);
    if (auto* o = v.getDynamicObject())
    {
        r.channel = juce::jlimit (0, 16, (int) o->getProperty ("channel"));
        r.bank = o->hasProperty ("bank") ? juce::jlimit (-1, 16383, (int) o->getProperty ("bank")) : -1;
        r.program = o->hasProperty ("program") ? juce::jlimit (-1, 127, (int) o->getProperty ("program")) : -1;
    }
    return r;
}

juce::String ExternalMidiInstrument::describe (const juce::String& deviceName, const Settings& s)
{
    juce::String d = deviceName + (s.channel > 0 ? " kan " + juce::String (s.channel) : juce::String (" (kanaal zoals binnenkomt)"));
    if (s.program >= 0)
        d << " pr " << (s.program + 1);
    return d;
}

InstrumentKey ExternalMidiInstrument::makeKey (const juce::String& identifier, const juce::String& deviceName, const Settings& s)
{
    InstrumentKey k;
    k.type = InstrumentKey::Type::midiOut;
    k.ref = identifier;
    k.displayName = describe (deviceName, s);
    k.pluginState = s.toString();
    return k;
}

ExternalMidiInstrument::ExternalMidiInstrument (MidiOutHub* h, const InstrumentKey& k) : hub (h)
{
    key = k;
    settings = Settings::fromString (k.pluginState);
    name = "MIDI-uit: " + k.displayName;
    if (hub != nullptr)
        port = hub->acquirePort (k.ref, k.displayName.upToFirstOccurrenceOf (" kan ", false, false)
                                                  .upToFirstOccurrenceOf (" (kanaal", false, false));
}

void ExternalMidiInstrument::out (const uint8_t* d, int size, int pos) noexcept
{
    if (hub != nullptr && port >= 0)
        hub->send (port, d, size, pos);
}

void ExternalMidiInstrument::process (juce::AudioBuffer<float>&, juce::MidiBuffer& midi) noexcept
{
    if (! programSent)
    {
        programSent = true;
        if (settings.program >= 0)
        {
            const int ch = (settings.channel > 0 ? settings.channel : 1) - 1;
            if (settings.bank >= 0)
            {
                const uint8_t msb[3] = { (uint8_t) (0xb0 | ch), 0, (uint8_t) ((settings.bank >> 7) & 0x7f) };
                const uint8_t lsb[3] = { (uint8_t) (0xb0 | ch), 32, (uint8_t) (settings.bank & 0x7f) };
                out (msb, 3, 0);
                out (lsb, 3, 0);
            }
            const uint8_t pc[2] = { (uint8_t) (0xc0 | ch), (uint8_t) settings.program };
            out (pc, 2, 0);
        }
    }

    for (const auto meta : midi)
    {
        if (meta.numBytes < 1 || meta.numBytes > 3 || meta.data[0] < 0x80 || meta.data[0] >= 0xf0)
            continue; // channel messages only (SysEx goes through the hub directly)

        uint8_t d[3] { meta.data[0], meta.numBytes > 1 ? meta.data[1] : (uint8_t) 0, meta.numBytes > 2 ? meta.data[2] : (uint8_t) 0 };
        if (settings.channel > 0)
            d[0] = (uint8_t) ((d[0] & 0xf0) | (settings.channel - 1));

        const int ch = d[0] & 0x0f, type = d[0] & 0xf0;
        usedChannel[ch] = true;
        if (type == 0x90 && d[2] > 0)
        {
            if (sounding[ch][d[1] & 0x7f] < 255)
                ++sounding[ch][d[1] & 0x7f];
        }
        else if (type == 0x80 || (type == 0x90 && d[2] == 0))
        {
            if (sounding[ch][d[1] & 0x7f] > 0)
                --sounding[ch][d[1] & 0x7f];
        }
        out (d, meta.numBytes, meta.samplePosition);
    }
}

void ExternalMidiInstrument::allNotesOff() noexcept
{
    for (int ch = 0; ch < 16; ++ch)
    {
        if (! usedChannel[ch])
            continue;
        for (int n = 0; n < 128; ++n)
            while (sounding[ch][n] > 0)
            {
                const uint8_t off[3] = { (uint8_t) (0x80 | ch), (uint8_t) n, 0 };
                out (off, 3, 0);
                --sounding[ch][n];
            }
        const uint8_t sustainOff[3] = { (uint8_t) (0xb0 | ch), 64, 0 };
        const uint8_t notesOff[3] = { (uint8_t) (0xb0 | ch), 123, 0 };
        out (sustainOff, 3, 0);
        out (notesOff, 3, 0);
    }
}

} // namespace km
