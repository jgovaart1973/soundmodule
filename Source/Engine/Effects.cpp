#include "Engine/Effects.h"

namespace km
{
//==============================================================================
void ReverbFx::prepare (double sampleRate)
{
    reverb.setSampleRate (sampleRate);
    reverb.reset();
    lastSize = lastDamping = -1.0f;
}

void ReverbFx::process (juce::AudioBuffer<float>& bus, int n, float size, float damping) noexcept
{
    if (! juce::exactlyEqual (size, lastSize) || ! juce::exactlyEqual (damping, lastDamping))
    {
        juce::Reverb::Parameters p;
        p.roomSize = juce::jlimit (0.0f, 1.0f, size);
        p.damping = juce::jlimit (0.0f, 1.0f, damping);
        p.wetLevel = 0.6f;
        p.dryLevel = 0.0f;
        p.width = 1.0f;
        reverb.setParameters (p);
        lastSize = size;
        lastDamping = damping;
    }
    reverb.processStereo (bus.getWritePointer (0), bus.getWritePointer (1), n);
}

//==============================================================================
void DelayFx::prepare (double sampleRate)
{
    sr = sampleRate;
    line.setSize (2, (int) (sampleRate * 2.1) + 1);
    line.clear();
    writePos = 0;
    delaySamples.reset (sampleRate, 0.15);
    delaySamples.setCurrentAndTargetValue ((float) (0.375 * sampleRate));
    lowpassState[0] = lowpassState[1] = 0.0f;
}

void DelayFx::reset() noexcept
{
    line.clear();
    lowpassState[0] = lowpassState[1] = 0.0f;
}

void DelayFx::process (juce::AudioBuffer<float>& bus, int n, float timeMs, float feedback) noexcept
{
    const int size = line.getNumSamples();
    if (size < 4)
        return;

    delaySamples.setTargetValue (juce::jlimit (1.0f, (float) size - 2.0f, timeMs * 0.001f * (float) sr));
    feedback = juce::jlimit (0.0f, 0.95f, feedback);

    auto* inL = bus.getWritePointer (0);
    auto* inR = bus.getWritePointer (1);
    auto* bufL = line.getWritePointer (0);
    auto* bufR = line.getWritePointer (1);

    for (int i = 0; i < n; ++i)
    {
        const float d = delaySamples.getNextValue();
        float readPos = (float) writePos - d;
        if (readPos < 0.0f)
            readPos += (float) size;

        const int i0 = (int) readPos;
        const int i1 = (i0 + 1) % size;
        const float frac = readPos - (float) i0;

        const float outL = bufL[i0] + frac * (bufL[i1] - bufL[i0]);
        const float outR = bufR[i0] + frac * (bufR[i1] - bufR[i0]);

        // Darken the repeats a little (analog-style).
        lowpassState[0] += 0.35f * (outL - lowpassState[0]);
        lowpassState[1] += 0.35f * (outR - lowpassState[1]);

        bufL[writePos] = inL[i] + lowpassState[1] * feedback; // ping-pong
        bufR[writePos] = inR[i] + lowpassState[0] * feedback;

        inL[i] = outL;
        inR[i] = outR;

        if (++writePos >= size)
            writePos = 0;
    }
}

//==============================================================================
void MasterFx::prepare (double sampleRate, int)
{
    gain.reset (sampleRate, 0.03);
    gain.setCurrentAndTargetValue (1.0f);
    envelope = 0.0f;
    releaseCoef = std::exp (-1.0f / (0.08f * (float) sampleRate));
}

void MasterFx::process (juce::AudioBuffer<float>& bus, int n, float volumeDb, float thresholdDb) noexcept
{
    gain.setTargetValue (juce::Decibels::decibelsToGain (volumeDb, -80.0f));
    const float threshold = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 0.0f, thresholdDb));

    auto* l = bus.getWritePointer (0);
    auto* r = bus.getWritePointer (1);
    float maxReduction = 1.0f;

    for (int i = 0; i < n; ++i)
    {
        const float g = gain.getNextValue();
        float sl = l[i] * g;
        float sr = r[i] * g;
        if (! std::isfinite (sl)) sl = 0.0f; // a misbehaving plug-in must not poison the limiter
        if (! std::isfinite (sr)) sr = 0.0f;

        const float level = juce::jmax (std::abs (sl), std::abs (sr));
        envelope = level > envelope ? level : envelope * releaseCoef + level * (1.0f - releaseCoef);

        if (envelope > threshold)
        {
            const float reduction = threshold / envelope;
            sl *= reduction;
            sr *= reduction;
            maxReduction = juce::jmin (maxReduction, reduction);
        }

        // Final safety: never exceed the threshold.
        l[i] = juce::jlimit (-threshold, threshold, sl);
        r[i] = juce::jlimit (-threshold, threshold, sr);
    }

    reductionDb.store (juce::Decibels::gainToDecibels (maxReduction, -60.0f), std::memory_order_relaxed);
}

} // namespace km
