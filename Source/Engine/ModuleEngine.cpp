#include "Engine/ModuleEngine.h"

namespace km
{
ModuleEngine::ModuleEngine (MidiEventQueue* liveInput) : liveQueue (liveInput)
{
    for (int i = 0; i < kNumParts; ++i)
        parts[(size_t) i] = std::make_unique<Part> (i, releaseQueue, programRequests);

    liveEvents.resize (2048);
    livePositions.resize (2048);
    prepare (48000.0, 512);
}

ModuleEngine::~ModuleEngine() = default;

void ModuleEngine::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate > 0 ? newSampleRate : 48000.0;
    maxBlock = juce::jmax (16, maxBlockSize);

    for (auto& p : parts)
        p->prepare (sampleRate, maxBlock);

    for (auto* b : { &mainBus, &reverbBus, &delayBus, &deviceScratch })
        b->setSize (2, maxBlock, false, true, false);

    reverb.prepare (sampleRate);
    delay.prepare (sampleRate);
    masterFx.prepare (sampleRate, maxBlock);
}

//==============================================================================
void ModuleEngine::process (juce::AudioBuffer<float>& out, int n,
                            const MidiEvent* events, const int* positions, int numEvents) noexcept
{
    juce::ScopedNoDenormals noDenormals;
    jassert (n <= maxBlock && out.getNumChannels() >= 2);
    n = juce::jmin (n, maxBlock);

    for (auto& p : parts)
        p->beginBlock();

    if (panicRequested.exchange (false))
    {
        for (auto& p : parts)
            p->panic();
        reverb.reset();
        delay.reset();
    }

    // Route MIDI
    const int control = controlChannel.load (std::memory_order_relaxed);
    for (int e = 0; e < numEvents; ++e)
    {
        const auto& ev = events[e];
        const int pos = juce::jlimit (0, n - 1, positions[e]);

        if (ev.isProgramChange() && control != 0 && ev.channel() == control)
        {
            ProgramRequest req;
            req.kind = ProgramRequest::performanceSelect;
            req.program = ev.data[1] & 0x7f;
            programRequests.push (req);
            continue;
        }

        for (auto& p : parts)
            p->handleEvent (ev, pos);
    }

    // Render parts into the buses
    juce::AudioBuffer<float> mainView (mainBus.getArrayOfWritePointers(), 2, n);
    juce::AudioBuffer<float> revView (reverbBus.getArrayOfWritePointers(), 2, n);
    juce::AudioBuffer<float> dlyView (delayBus.getArrayOfWritePointers(), 2, n);
    mainView.clear();
    revView.clear();
    dlyView.clear();

    bool anySolo = false;
    for (auto& p : parts)
        anySolo = anySolo || (p->params.solo.load() && p->params.enabled.load());

    for (auto& p : parts)
        p->render (mainView, revView, dlyView, n, anySolo);

    // Send effects
    reverb.process (revView, n, master.reverbSize.load(), master.reverbDamping.load());
    delay.process (dlyView, n, master.delayMs.load(), master.delayFeedback.load());
    for (int ch = 0; ch < 2; ++ch)
    {
        mainView.addFrom (ch, 0, revView, ch, 0, n);
        mainView.addFrom (ch, 0, dlyView, ch, 0, n);
    }

    // Test tone (440 Hz, -12 dBFS) for checking the audio output
    if (int remaining = testToneRemaining.load (std::memory_order_relaxed); remaining > 0)
    {
        const double inc = juce::MathConstants<double>::twoPi * 440.0 / sampleRate;
        const int count = juce::jmin (n, remaining);
        for (int i = 0; i < count; ++i)
        {
            const float fade = juce::jmin (1.0f, (float) (remaining - i) / 2000.0f);
            const float s = 0.25f * fade * (float) std::sin (testTonePhase);
            testTonePhase += inc;
            mainView.addSample (0, i, s);
            mainView.addSample (1, i, s);
        }
        testTonePhase = std::fmod (testTonePhase, juce::MathConstants<double>::twoPi);
        testToneRemaining.store (remaining - count, std::memory_order_relaxed);
    }

    masterFx.process (mainView, n, master.volumeDb.load(), master.limiterThresholdDb.load());

    const float peakNow = mainView.getMagnitude (0, n);
    if (peakNow > masterPeak.load (std::memory_order_relaxed))
        masterPeak.store (peakNow, std::memory_order_relaxed);

    out.copyFrom (0, 0, mainView, 0, 0, n);
    out.copyFrom (1, 0, mainView, 1, 0, n);
}

//==============================================================================
void ModuleEngine::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                     float* const* outputs, int numOutputs, int numSamples,
                                                     const juce::AudioIODeviceCallbackContext&)
{
    // Drain live MIDI. Offset 0 = lowest possible latency (jitter <= one buffer).
    int numEvents = 0;
    if (liveQueue != nullptr)
    {
        while (numEvents < (int) liveEvents.size())
        {
            auto e = liveQueue->pop();
            if (! e.has_value())
                break;
            liveEvents[(size_t) numEvents] = *e;
            livePositions[(size_t) numEvents] = 0;
            ++numEvents;
        }
    }

    // The device may deliver more samples than we prepared for: process in chunks.
    int done = 0;
    bool first = true;
    while (done < numSamples)
    {
        const int chunk = juce::jmin (maxBlock, numSamples - done);
        juce::AudioBuffer<float> view (deviceScratch.getArrayOfWritePointers(), 2, chunk);
        process (view, chunk, liveEvents.data(), livePositions.data(), first ? numEvents : 0);
        first = false;

        for (int ch = 0; ch < numOutputs; ++ch)
        {
            if (outputs[ch] == nullptr)
                continue;
            if (ch < 2)
                juce::FloatVectorOperations::copy (outputs[ch] + done, view.getReadPointer (ch), chunk);
            else
                juce::FloatVectorOperations::clear (outputs[ch] + done, chunk);
        }
        if (numOutputs == 1 && outputs[0] != nullptr) // mono device: mix down
        {
            juce::FloatVectorOperations::add (outputs[0] + done, view.getReadPointer (1), chunk);
            juce::FloatVectorOperations::multiply (outputs[0] + done, 0.5f, chunk);
        }

        done += chunk;
    }
}

void ModuleEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const double sr = device->getCurrentSampleRate();
    const int bs = device->getCurrentBufferSizeSamples();
    prepare (sr, bs);

    if (onAboutToStart)
        onAboutToStart (sr, bs);
}

void ModuleEngine::audioDeviceStopped() {}

void ModuleEngine::audioDeviceError (const juce::String& message)
{
    if (onDeviceError)
        onDeviceError (message);
}

} // namespace km
