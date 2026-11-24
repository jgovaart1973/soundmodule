#include "Engine/ModuleEngine.h"

namespace km
{
ModuleEngine::ModuleEngine (MidiEventQueue* liveInput) : liveQueue (liveInput)
{
    for (int i = 0; i < kNumParts; ++i)
    {
        parts[(size_t) i] = std::make_unique<Part> (i, releaseQueue, programRequests);
        parts[(size_t) i]->setRecorder (&sequencer);
    }

    liveEvents.resize (2048);
    livePositions.resize (2048);
    seqEvents.resize (8192);
    seqPositions.resize (8192);
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

    sequencer.prepare (sampleRate, maxBlock);
    reverb.prepare (sampleRate);
    delay.prepare (sampleRate);
    masterFx.prepare (sampleRate, maxBlock);
}

//==============================================================================
void ModuleEngine::process (juce::AudioBuffer<float>& out, int n,
                            const MidiEvent* events, const int* positions, int numEvents,
                            const float* const* inputs, int numInputs) noexcept
{
    juce::ScopedNoDenormals noDenormals;
    jassert (n <= maxBlock && out.getNumChannels() >= 2);
    n = juce::jmin (n, maxBlock);

    for (auto& p : parts)
        p->beginBlock();

    if (midiOut != nullptr)
        midiOut->beginBlock (sampleRate, n);

    // Song playback for this block (also handles play/stop/locate/loop).
    const int numSeq = sequencer.generate (n, seqEvents.data(), seqPositions.data(), (int) seqEvents.size());

    if (midiOut != nullptr)
        midiOut->processClock (sequencer.isBlockRolling(), sequencer.getBlockStartBeat(), sequencer.getTempoBeatsPerSample(), n);

    if (panicRequested.exchange (false))
    {
        sequencer.resetSounding();
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

    for (int e = 0; e < numSeq; ++e)
        getPart (seqEvents[(size_t) e].targetPart).handleEvent (seqEvents[(size_t) e], seqPositions[(size_t) e]);

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

    sequencer.processAudio (mainView, inputs, numInputs, n);   // audio tracks, monitoring, recording
    sequencer.renderClicks (mainView, n);
    masterFx.process (mainView, n, master.volumeDb.load(), master.limiterThresholdDb.load());

    const float peakNow = mainView.getMagnitude (0, n);
    if (peakNow > masterPeak.load (std::memory_order_relaxed))
        masterPeak.store (peakNow, std::memory_order_relaxed);

    out.copyFrom (0, 0, mainView, 0, 0, n);
    out.copyFrom (1, 0, mainView, 1, 0, n);
    sequencer.advance (n);
}

//==============================================================================
void ModuleEngine::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
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
        const float* chunkInputs[8] {};
        const int ins = juce::jmin (8, numInputs);
        for (int ch = 0; ch < ins; ++ch)
            chunkInputs[ch] = inputs[ch] != nullptr ? inputs[ch] + done : nullptr;
        process (view, chunk, liveEvents.data(), livePositions.data(), first ? numEvents : 0, chunkInputs, ins);
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

    if (midiOut != nullptr && sr > 0.0)
        midiOut->setOutputLatencyMs ((bs + device->getOutputLatencyInSamples()) * 1000.0 / sr);

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
