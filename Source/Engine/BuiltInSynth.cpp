#include "Engine/BuiltInSynth.h"

#include <cmath>

namespace km
{
namespace
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    enum class Algo { subtractive, fm, organ };

    struct SynthPreset
    {
        const char* name;
        Algo algo;

        // Subtractive
        int numSaws;            ///< 1..3 detuned saws
        float detuneCents;
        float squareMix;        ///< 0..1
        float subMix;           ///< sine one octave down
        float cutoffHz;
        float resonance;        ///< 0..0.95
        float filterEnvOctaves;
        float keyTrack;         ///< 0..1
        float velToFilterOct;

        juce::ADSR::Parameters amp;
        juce::ADSR::Parameters filt;

        // FM (carrier + modulator, plus a fast "tine" modulator)
        float fmRatio, fmIndex, fmIndexDecaySec, tineRatio, tineIndex;

        // Organ drawbars at ratios 0.5, 1, 1.5, 2, 3, 4
        float drawbars[6];

        float vibratoDepthSemis; ///< permanent vibrato (mod wheel adds more)
        float gain;
    };

    // clang-format off
    const SynthPreset presets[] =
    {
        { "Electric Piano", Algo::fm,
          1, 0, 0, 0, 12000, 0.0f, 0, 0, 0,
          { 0.002f, 2.8f, 0.0f, 0.45f }, { 0.001f, 1.0f, 1.0f, 0.5f },
          1.0f, 1.9f, 1.1f, 14.0f, 0.55f,
          { 0, 0, 0, 0, 0, 0 }, 0.0f, 0.61f },

        { "Warm Pad", Algo::subtractive,
          3, 11.0f, 0.0f, 0.25f, 900, 0.2f, 1.6f, 0.4f, 0.5f,
          { 0.7f, 1.2f, 0.85f, 1.6f }, { 1.2f, 2.5f, 0.45f, 1.5f },
          0, 0, 0, 0, 0,
          { 0, 0, 0, 0, 0, 0 }, 0.04f, 0.91f },

        { "Drawbar Organ", Algo::organ,
          1, 0, 0, 0, 12000, 0.0f, 0, 0, 0,
          { 0.004f, 0.05f, 1.0f, 0.045f }, { 0.001f, 0.1f, 1.0f, 0.1f },
          0, 0, 0, 0, 0,
          { 0.55f, 0.8f, 0.45f, 0.55f, 0.3f, 0.25f }, 0.08f, 0.64f },

        { "Synth Bass", Algo::subtractive,
          1, 0.0f, 0.35f, 0.6f, 280, 0.35f, 3.2f, 0.3f, 1.0f,
          { 0.002f, 0.35f, 0.75f, 0.08f }, { 0.001f, 0.28f, 0.15f, 0.08f },
          0, 0, 0, 0, 0,
          { 0, 0, 0, 0, 0, 0 }, 0.0f, 0.78f },

        { "String Ensemble", Algo::subtractive,
          3, 14.0f, 0.0f, 0.0f, 2600, 0.1f, 0.8f, 0.6f, 0.6f,
          { 0.35f, 0.6f, 0.9f, 0.9f }, { 0.5f, 1.0f, 0.7f, 0.9f },
          0, 0, 0, 0, 0,
          { 0, 0, 0, 0, 0, 0 }, 0.08f, 0.98f },

        { "Brass", Algo::subtractive,
          2, 6.0f, 0.0f, 0.0f, 700, 0.15f, 2.6f, 0.5f, 1.2f,
          { 0.035f, 0.3f, 0.85f, 0.18f }, { 0.07f, 0.45f, 0.55f, 0.2f },
          0, 0, 0, 0, 0,
          { 0, 0, 0, 0, 0, 0 }, 0.05f, 0.84f },

        { "Bell", Algo::fm,
          1, 0, 0, 0, 12000, 0.0f, 0, 0, 0,
          { 0.001f, 3.5f, 0.0f, 1.8f }, { 0.001f, 1.0f, 1.0f, 0.5f },
          3.5f, 2.6f, 2.2f, 7.0f, 0.3f,
          { 0, 0, 0, 0, 0, 0 }, 0.0f, 0.38f },

        { "Square Lead", Algo::subtractive,
          2, 5.0f, 0.6f, 0.0f, 3200, 0.3f, 1.0f, 0.5f, 0.8f,
          { 0.005f, 0.2f, 0.9f, 0.15f }, { 0.002f, 0.3f, 0.6f, 0.2f },
          0, 0, 0, 0, 0,
          { 0, 0, 0, 0, 0, 0 }, 0.0f, 1.03f },
    };
    // clang-format on

    constexpr int numPresets = (int) (sizeof (presets) / sizeof (presets[0]));

    /** PolyBLEP correction for naive saw/square discontinuities. */
    inline float polyBlep (float t, float dt) noexcept
    {
        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0f;
        }
        if (t > 1.0f - dt)
        {
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }
        return 0.0f;
    }

    inline void wrap (float& phase) noexcept
    {
        if (phase >= 1.0f)
            phase -= std::floor (phase);
    }

    /** Zero-delay-feedback state-variable low-pass (Simper / Cytomic). */
    struct SvfLowpass
    {
        float ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0;

        void setCoefficients (float cutoff, float resonance, float sampleRate) noexcept
        {
            cutoff = juce::jlimit (20.0f, sampleRate * 0.45f, cutoff);
            const float g = std::tan (juce::MathConstants<float>::pi * cutoff / sampleRate);
            const float k = 2.0f - 1.9f * juce::jlimit (0.0f, 0.95f, resonance);
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        float process (float x) noexcept
        {
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return v2;
        }

        void reset() noexcept { ic1 = ic2 = 0.0f; }
    };

    struct KmSound final : public juce::SynthesiserSound
    {
        bool appliesToNote (int) override    { return true; }
        bool appliesToChannel (int) override { return true; }
    };

    //==========================================================================
    class KmVoice final : public juce::SynthesiserVoice
    {
    public:
        KmVoice (const std::atomic<int>& presetRef, const BuiltInSynth::Expression& ex) : presetIndex (presetRef), expr (ex) {}

        bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<KmSound*> (s) != nullptr; }

        void startNote (int note, float velocity, juce::SynthesiserSound*, int pitchWheel) override
        {
            p = &presets[juce::jlimit (0, numPresets - 1, presetIndex.load())];
            channel = 0;
            for (int c = 1; c <= 16; ++c)
                if (isPlayingChannel (c))
                    channel = c - 1;
            sr = (float) getSampleRate();
            midiNote = note;
            vel = velocity;
            velGain = 0.2f + 0.8f * std::pow (velocity, 1.4f);
            baseFreq = (float) juce::MidiMessage::getMidiNoteInHertz (note);
            pitchWheelMoved (pitchWheel);

            // Gentle keyboard stereo spread, like a sampled piano.
            const float spread = juce::jlimit (-0.35f, 0.35f, (note - 64) / 90.0f);
            const float angle = (spread + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            gainL = std::cos (angle) * 1.41421356f;
            gainR = std::sin (angle) * 1.41421356f;

            for (auto& ph : phases)
                ph = rng.nextFloat();
            subPhase = carrierPhase = modPhase = tinePhase = 0.0f;
            for (auto& ph : organPhases)
                ph = 0.0f;
            lfoPhase = rng.nextFloat();
            samplesSinceStart = 0;
            silentAfter = (int) ((p->amp.attack + p->amp.decay) * sr);

            ampEnv.setSampleRate (sr);
            filtEnv.setSampleRate (sr);
            ampEnv.setParameters (p->amp);
            filtEnv.setParameters (p->filt);
            ampEnv.noteOn();
            filtEnv.noteOn();

            fmIndexEnv = 1.0f;
            fmIndexDecay = p->fmIndexDecaySec > 0 ? std::exp (-1.0f / (p->fmIndexDecaySec * sr)) : 1.0f;
            tineEnv = 1.0f;
            tineDecay = std::exp (-1.0f / (0.06f * sr));
            clickSamples = (int) (0.004f * sr);

            filter.reset();
            coefCountdown = 0;
        }

        void stopNote (float, bool allowTailOff) override
        {
            if (allowTailOff)
            {
                ampEnv.noteOff();
                filtEnv.noteOff();
            }
            else
            {
                ampEnv.reset();
                filtEnv.reset();
                clearCurrentNote();
            }
        }

        void pitchWheelMoved (int value) override
        {
            bendSemis = ((float) value - 8192.0f) / 8192.0f * expr.bendRange[(size_t) channel];
        }

        void controllerMoved (int cc, int value) override
        {
            if (cc == 1)
                modWheel = (float) value / 127.0f;
        }

        void channelPressureChanged (int value) override { pressure = (float) value / 127.0f; }
        void aftertouchChanged (int value) override      { pressure = (float) value / 127.0f; }

        void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
        {
            if (! isVoiceActive() || p == nullptr)
                return;

            auto* left = out.getWritePointer (0);
            auto* right = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;
            const float lfoInc = 5.5f / sr;

            for (int i = start; i < start + num; ++i)
            {
                // Pitch: bend + vibrato (preset depth + mod wheel + pressure)
                lfoPhase += lfoInc;
                wrap (lfoPhase);
                // MPE: pressure and timbre come per note (member channel); otherwise pressure adds vibrato.
                const bool mpe = expr.mpe;
                const float notePressure = mpe ? expr.pressure[(size_t) channel] : pressure;
                const float vibDepth = p->vibratoDepthSemis + modWheel * 0.35f + (mpe ? 0.0f : pressure * 0.15f);
                const float semis = bendSemis + (mpe ? expr.managerBend : 0.0f) + vibDepth * std::sin (twoPi * lfoPhase);
                expressionGain = mpe ? 0.75f + 0.5f * notePressure : 1.0f;
                brightness = (expr.timbre[(size_t) channel] - 0.5f) * 3.0f + (mpe ? notePressure * 1.5f : 0.0f);
                const float freq = baseFreq * std::exp2 (semis / 12.0f);

                const float a = ampEnv.getNextSample();
                const float f = filtEnv.getNextSample();
                float sample = 0.0f;

                switch (p->algo)
                {
                    case Algo::subtractive: sample = renderSubtractive (freq, f); break;
                    case Algo::fm:          sample = renderFm (freq);             break;
                    case Algo::organ:       sample = renderOrgan (freq);          break;
                }

                sample *= a * velGain * p->gain * 0.25f * expressionGain;
                left[i] += sample * gainL;
                if (right != nullptr)
                    right[i] += sample * gainR;

                // Percussive presets (sustain 0) free their voice once fully decayed.
                const bool decayedAway = p->amp.sustain <= 0.0f && ++samplesSinceStart > silentAfter && a < 1.0e-4f;

                if (! ampEnv.isActive() || decayedAway)
                {
                    clearCurrentNote();
                    break;
                }
            }
        }

    private:
        float renderSubtractive (float freq, float filterEnvValue) noexcept
        {
            float osc = 0.0f;
            const int n = juce::jlimit (1, 3, p->numSaws);
            static constexpr float detuneSpread[3] = { 0.0f, 1.0f, -1.0f };

            for (int s = 0; s < n; ++s)
            {
                const float fr = freq * std::exp2 (detuneSpread[s] * p->detuneCents / 1200.0f);
                const float dt = fr / sr;
                phases[(size_t) s] += dt;
                wrap (phases[(size_t) s]);
                const float t = phases[(size_t) s];

                const float saw = 2.0f * t - 1.0f - polyBlep (t, dt);
                float sq = t < 0.5f ? 1.0f : -1.0f;
                sq += polyBlep (t, dt);
                float t2 = t + 0.5f;
                if (t2 >= 1.0f)
                    t2 -= 1.0f;
                sq -= polyBlep (t2, dt);

                osc += saw * (1.0f - p->squareMix) + sq * p->squareMix;
            }
            osc /= (float) n;

            subPhase += 0.5f * freq / sr;
            wrap (subPhase);
            osc += p->subMix * std::sin (twoPi * subPhase);

            if (--coefCountdown <= 0)
            {
                coefCountdown = 16;
                const float octaves = p->filterEnvOctaves * filterEnvValue
                                    + p->keyTrack * (float) (midiNote - 60) / 12.0f
                                    + p->velToFilterOct * (vel - 0.5f)
                                    + (expr.mpe ? 0.0f : pressure * 1.5f) + modWheel * 0.5f + brightness;
                filter.setCoefficients (p->cutoffHz * std::exp2 (octaves), p->resonance, sr);
            }
            return filter.process (osc);
        }

        float renderFm (float freq) noexcept
        {
            // Fade FM brightness out towards Nyquist so high notes do not alias.
            const float nyquistGuard = juce::jlimit (0.0f, 1.0f, 1.0f - freq * p->fmRatio * 4.0f / (sr * 0.45f));
            const float tineGuard = juce::jlimit (0.0f, 1.0f, 1.0f - freq * p->tineRatio * 1.5f / (sr * 0.45f));
            const float idx = p->fmIndex * (0.35f + 0.65f * vel) * fmIndexEnv * (0.3f + 0.7f * nyquistGuard);
            const float tIdx = p->tineIndex * vel * tineEnv * tineGuard;
            fmIndexEnv *= fmIndexDecay;
            tineEnv *= tineDecay;

            modPhase += freq * p->fmRatio / sr;
            wrap (modPhase);
            tinePhase += freq * p->tineRatio / sr;
            wrap (tinePhase);
            carrierPhase += freq / sr;
            wrap (carrierPhase);

            const float mod = idx * std::sin (twoPi * modPhase) + tIdx * std::sin (twoPi * tinePhase);
            return std::sin (twoPi * carrierPhase + mod);
        }

        float renderOrgan (float freq) noexcept
        {
            static constexpr float ratios[6] = { 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f };
            float s = 0.0f;
            for (size_t h = 0; h < 6; ++h)
            {
                const float hf = freq * ratios[h];
                if (hf > sr * 0.45f)
                    continue;
                organPhases[h] += hf / sr;
                wrap (organPhases[h]);
                s += p->drawbars[h] * std::sin (twoPi * organPhases[h]);
            }
            if (clickSamples > 0)
            {
                --clickSamples;
                s += 0.15f * (rng.nextFloat() * 2.0f - 1.0f);
            }
            return s * 0.45f;
        }

        const std::atomic<int>& presetIndex;
        const BuiltInSynth::Expression& expr;
        const SynthPreset* p = nullptr;
        int channel = 0;
        float expressionGain = 1.0f, brightness = 0.0f;

        float sr = 44100.0f;
        int midiNote = 60;
        float vel = 1.0f, velGain = 1.0f, baseFreq = 440.0f;
        float bendSemis = 0.0f, modWheel = 0.0f, pressure = 0.0f;
        float gainL = 1.0f, gainR = 1.0f;

        std::array<float, 3> phases {};
        std::array<float, 6> organPhases {};
        float subPhase = 0, carrierPhase = 0, modPhase = 0, tinePhase = 0, lfoPhase = 0;
        float fmIndexEnv = 1, fmIndexDecay = 1, tineEnv = 1, tineDecay = 1;
        int clickSamples = 0;
        int coefCountdown = 0;
        int samplesSinceStart = 0, silentAfter = 0;
        juce::Random rng;

        juce::ADSR ampEnv, filtEnv;
        SvfLowpass filter;
    };
} // namespace

//==============================================================================
int BuiltInSynth::getNumPresets() noexcept { return numPresets; }

juce::String BuiltInSynth::getPresetName (int index)
{
    return presets[juce::jlimit (0, numPresets - 1, index)].name;
}

BuiltInSynth::BuiltInSynth (int presetIndex)
{
    setPreset (presetIndex);
    for (int i = 0; i < 32; ++i)
        synth.addVoice (new KmVoice (preset, expression));
    synth.addSound (new KmSound());
    synth.setNoteStealingEnabled (true);
    key = InstrumentKey::builtIn (getPreset(), getPresetName (getPreset()));
}

void BuiltInSynth::setPreset (int index) noexcept
{
    preset.store (juce::jlimit (0, numPresets - 1, index));
}

void BuiltInSynth::prepare (double sampleRate, int)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);
    filtered.ensureSize (4096);
}

void BuiltInSynth::preprocess (juce::MidiBuffer& midi) noexcept
{
    filtered.clear();
    for (const auto meta : midi)
    {
        const auto* d = meta.data;
        if (meta.numBytes < 2)
        {
            filtered.addEvent (d, meta.numBytes, meta.samplePosition);
            continue;
        }
        const int ch = d[0] & 0x0f, type = d[0] & 0xf0;

        if (type == 0xb0 && meta.numBytes >= 3)
        {
            const int cc = d[1], v = d[2];
            if (cc == 101) { rpnMsb[(size_t) ch] = v; continue; }
            if (cc == 100) { rpnLsb[(size_t) ch] = v; continue; }
            if (cc == 6 || cc == 38)
            {
                const int rpn = rpnMsb[(size_t) ch] * 128 + rpnLsb[(size_t) ch];
                if (cc == 6 && rpn == 0)
                {
                    expression.bendRange[(size_t) ch] = (float) juce::jlimit (0, 96, v);
                }
                else if (cc == 6 && rpn == 6 && ch == 0)
                {
                    // MPE Configuration Message, lower zone: v member channels (2..v+1)
                    expression.mpe = v > 0;
                    expression.bendRange[0] = 2.0f;
                    for (int c = 1; c < 16; ++c)
                        expression.bendRange[(size_t) c] = (v > 0 && c <= v) ? 48.0f : 2.0f;
                    expression.managerBend = 0.0f;
                }
                continue;
            }
            if (cc == 74)
                expression.timbre[(size_t) ch] = (float) v / 127.0f;
            if (expression.mpe && ch == 0 && cc == 64)
            {
                for (int c = 1; c <= 16; ++c)       // manager channel sustain holds every note
                    synth.handleSustainPedal (c, v >= 64);
                continue;
            }
        }
        else if (type == 0xd0)
        {
            expression.pressure[(size_t) ch] = (float) d[1] / 127.0f;
        }
        else if (type == 0xe0 && meta.numBytes >= 3 && expression.mpe && ch == 0)
        {
            const int value = d[1] | (d[2] << 7);
            expression.managerBend = ((float) value - 8192.0f) / 8192.0f * expression.bendRange[0];
            continue;
        }
        else if (type == 0x90 && meta.numBytes >= 3 && d[2] > 0 && expression.mpe)
        {
            // A new MPE note starts without pressure until the controller says otherwise.
            expression.pressure[(size_t) ch] = 0.0f;
        }
        filtered.addEvent (d, meta.numBytes, meta.samplePosition);
    }
}

void BuiltInSynth::process (juce::AudioBuffer<float>& stereoOut, juce::MidiBuffer& midi) noexcept
{
    preprocess (midi);
    synth.renderNextBlock (stereoOut, filtered, 0, stereoOut.getNumSamples());
}

void BuiltInSynth::allNotesOff() noexcept
{
    synth.allNotesOff (0, false);
}

} // namespace km
