#include "Engine/SfzInstrument.h"

#if KM_WITH_SFIZZ
 #include <sfizz.hpp>
#endif

namespace km
{
#if KM_WITH_SFIZZ

bool SfzInstrument::isAvailable() noexcept { return true; }

SfzInstrument::SfzInstrument() : synth (std::make_unique<sfz::Sfizz>())
{
    synth->setNumVoices (128);
}

SfzInstrument::~SfzInstrument() = default;

bool SfzInstrument::load (const juce::File& sfzFile, juce::String& error)
{
    if (! sfzFile.existsAsFile())
    {
        error = "SFZ-bestand niet gevonden: " + sfzFile.getFullPathName();
        return false;
    }

    if (! synth->loadSfzFile (sfzFile.getFullPathName().toStdString()))
    {
        error = "Kon SFZ-bestand niet laden: " + sfzFile.getFileName();
        return false;
    }

    if (synth->getNumRegions() == 0)
    {
        error = "SFZ-bestand bevat geen regio's (samples): " + sfzFile.getFileName();
        return false;
    }

    name = sfzFile.getFileNameWithoutExtension();
    key = InstrumentKey::sfz (sfzFile);
    return true;
}

void SfzInstrument::prepare (double sampleRate, int maxBlockSize)
{
    synth->setSampleRate ((float) sampleRate);
    synth->setSamplesPerBlock (maxBlockSize);
}

void SfzInstrument::process (juce::AudioBuffer<float>& stereoOut, juce::MidiBuffer& midi) noexcept
{
    for (const auto meta : midi)
    {
        const auto* d = meta.data;
        const int pos = meta.samplePosition;
        const int status = d[0] & 0xf0;

        switch (status)
        {
            case 0x90:
                if (meta.numBytes >= 3 && d[2] > 0) synth->noteOn (pos, d[1], d[2]);
                else if (meta.numBytes >= 3)        synth->noteOff (pos, d[1], 0);
                break;
            case 0x80: if (meta.numBytes >= 3) synth->noteOff (pos, d[1], d[2]); break;
            case 0xb0: if (meta.numBytes >= 3) synth->cc (pos, d[1], d[2]); break;
            case 0xc0: if (meta.numBytes >= 2) synth->programChange (pos, d[1]); break;
            case 0xd0: if (meta.numBytes >= 2) synth->channelAftertouch (pos, d[1]); break;
            case 0xa0: if (meta.numBytes >= 3) synth->polyAftertouch (pos, d[1], d[2]); break;
            case 0xe0:
                if (meta.numBytes >= 3)
                    synth->pitchWheel (pos, ((int) d[1] | ((int) d[2] << 7)) - 8192);
                break;
            default: break;
        }
    }

    float* channels[2] = { stereoOut.getWritePointer (0), stereoOut.getWritePointer (1) };
    synth->renderBlock (channels, (size_t) stereoOut.getNumSamples(), 1);
}

void SfzInstrument::allNotesOff() noexcept
{
    synth->allSoundOff();
}

#else // ------------------------------------------------------------------------

bool SfzInstrument::isAvailable() noexcept { return false; }
SfzInstrument::SfzInstrument() = default;
SfzInstrument::~SfzInstrument() = default;

bool SfzInstrument::load (const juce::File&, juce::String& error)
{
    error = "Deze build bevat geen SFZ-ondersteuning (KM_WITH_SFIZZ=OFF).";
    return false;
}

void SfzInstrument::prepare (double, int) {}
void SfzInstrument::process (juce::AudioBuffer<float>&, juce::MidiBuffer&) noexcept {}
void SfzInstrument::allNotesOff() noexcept {}

#endif

} // namespace km
