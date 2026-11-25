#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cmath>

/**
    Acoustic instrument models for the built-in synth (no samples needed):
      - PianoModel:   modal synthesis of a struck string (inharmonic partials, two-stage decay,
                      string-pair beating, hammer noise, damper)
      - PluckModel:   extended Karplus-Strong string (guitar, finger bass) with body resonances
      - WindModel:    breath-driven oscillator + formant filters (saxophones, flute)
      - ChoirModel:   ensemble of detuned voices through vowel formants
      - FretlessModel: bass with the typical "mwah" filter sweep and slide into the note
    All are allocation-free and run per sample on the audio thread.
*/
namespace km::models
{
constexpr float pi = juce::MathConstants<float>::pi;
constexpr float twoPi = juce::MathConstants<float>::twoPi;

/** Topology-preserving state variable filter with low / band / high outputs. */
struct Svf
{
    float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
    float low = 0, band = 0, high = 0;

    void set (float freq, float q, float sr) noexcept
    {
        freq = juce::jlimit (10.0f, sr * 0.45f, freq);
        g = std::tan (pi * freq / sr);
        k = 1.0f / juce::jmax (0.05f, q);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void process (float x) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        low = v2;
        band = v1;
        high = x - k * v1 - v2;
    }
    void reset() noexcept { ic1 = ic2 = low = band = high = 0.0f; }
};

struct Noise
{
    uint32_t state = 0x9e3779b9u;
    void seed (uint32_t s) noexcept { state = s != 0 ? s : 0x9e3779b9u; }
    float next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) (state & 0xffffff) / (float) 0x7fffff - 1.0f;
    }
};

inline float polyBlep (float t, float dt) noexcept
{
    if (t < dt)       { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

/** Band-limited sawtooth oscillator. */
struct Saw
{
    float phase = 0.0f;
    float next (float freq, float sr) noexcept
    {
        const float dt = juce::jlimit (0.0f, 0.5f, freq / sr);
        phase += dt;
        if (phase >= 1.0f) phase -= 1.0f;
        return 2.0f * phase - 1.0f - polyBlep (phase, dt);
    }
};

inline float decayPerSample (float t60, float sr) noexcept
{
    return std::exp (-6.9078f / (juce::jmax (0.001f, t60) * sr));
}

//==============================================================================
class PianoModel
{
public:
    /** brightness ~1, decayScale ~1 (preset parameters). */
    void start (int note, float velocity, float sampleRate, float brightness, float decayScale) noexcept
    {
        sr = sampleRate;
        midiNote = note;
        vel = juce::jlimit (0.01f, 1.0f, velocity);
        released = false;
        age = 0;
        const float f0 = (float) juce::MidiMessage::getMidiNoteInHertz (note);

        // Inharmonicity grows towards the treble; wound bass strings stay fairly harmonic.
        const float B = juce::jlimit (0.00004f, 0.02f, 0.00025f * std::exp2 ((float) (note - 48) / 16.0f));
        // Fundamental decay: long in the bass, short in the treble.
        const float t60 = juce::jlimit (0.7f, 22.0f, 20.0f * std::exp2 (-(float) (note - 21) / 17.0f)) * decayScale;
        // Harder strikes are brighter.
        const float tilt = (1.95f - 1.15f * vel) / juce::jmax (0.3f, brightness);

        numPartials = 0;
        float total = 0.0f;
        for (int k = 1; k <= maxPartials; ++k)
        {
            const float fk = (float) k * f0 * std::sqrt (1.0f + B * (float) (k * k));
            if (fk > sr * 0.42f)
                break;
            const auto i = (size_t) numPartials;
            freq[i] = fk;
            const float strike = std::abs (std::sin (pi * (float) k / 7.5f)) + 0.06f;   // hammer at ~1/7.5 of the string
            amp[i] = std::pow ((float) k, -tilt) * strike;
            const float tk = t60 / (1.0f + 0.32f * (float) (k - 1) + 0.0006f * fk);
            d1[i] = decayPerSample (tk * 0.38f, sr);
            d2[i] = decayPerSample (tk * 1.7f, sr);
            e1[i] = 0.72f;
            e2[i] = 0.28f;
            // Spread the starting phases: partials that all start at phase 0 add up to a sawtooth-like
            // step, which is heard as a click at every note.
            const float ph = twoPi * (float) ((note * 131 + k * 977) % 1000) / 1000.0f;
            re[i] = std::cos (ph); im[i] = std::sin (ph);
            total += amp[i];
            ++numPartials;
        }
        for (int i = 0; i < numPartials; ++i)
            amp[(size_t) i] /= juce::jmax (0.001f, total);

        // Two strings slightly out of tune: beating in the low partials.
        numBeating = note > 32 ? juce::jmin (numPartials, maxBeating) : 0;
        for (int i = 0; i < numBeating; ++i)
        {
            detune[(size_t) i] = (0.45f + 0.12f * (float) (i + 1)) * 0.0005776f;
            re2[(size_t) i] = re[(size_t) i];
            im2[(size_t) i] = im[(size_t) i];
        }
        // The hammer stays on the string for a few milliseconds (longer for soft notes): a smooth onset.
        attackLength = juce::jmax (8, (int) ((0.0012f + 0.003f * (1.0f - vel)) * sr));
        attackPos = 0;

        ratio = -1.0f;
        setRatio (1.0f);
        loudness = 0.55f + 0.45f * vel;
        hammer = 0.5f * vel;
        hammerDecay = decayPerSample (0.012f, sr);
        hammerLp.set (900.0f + 2500.0f * vel, 0.7f, sr);
        hammerLp.reset();
        noise.seed ((uint32_t) (note * 7919 + 17));
    }

    void setRatio (float r) noexcept
    {
        if (std::abs (r - ratio) < 1.0e-5f)
            return;
        ratio = r;
        for (int i = 0; i < numPartials; ++i)
        {
            const float w = twoPi * freq[(size_t) i] * ratio / sr;
            cr[(size_t) i] = std::cos (w);
            ci[(size_t) i] = std::sin (w);
        }
        for (int i = 0; i < numBeating; ++i)
        {
            const float w2 = twoPi * freq[(size_t) i] * ratio * (1.0f + detune[(size_t) i]) / sr;
            cr2[(size_t) i] = std::cos (w2);
            ci2[(size_t) i] = std::sin (w2);
        }
    }

    void release() noexcept
    {
        if (released)
            return;
        released = true;
        if (midiNote >= 89)
            return; // the top octave has no dampers
        const float damp = decayPerSample (0.11f, sr);
        for (int i = 0; i < numPartials; ++i)
        {
            d1[(size_t) i] = juce::jmin (d1[(size_t) i], damp);
            d2[(size_t) i] = juce::jmin (d2[(size_t) i], damp);
        }
    }

    float render() noexcept
    {
        // Struct-of-arrays loops so the compiler can vectorise them.
        const int n = numPartials;
        float out = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float r = re[(size_t) i] * cr[(size_t) i] - im[(size_t) i] * ci[(size_t) i];
            const float m = re[(size_t) i] * ci[(size_t) i] + im[(size_t) i] * cr[(size_t) i];
            re[(size_t) i] = r;
            im[(size_t) i] = m;
            e1[(size_t) i] *= d1[(size_t) i];
            e2[(size_t) i] *= d2[(size_t) i];
            env[(size_t) i] = amp[(size_t) i] * (e1[(size_t) i] + e2[(size_t) i]);
            out += m * env[(size_t) i];
        }
        float beat = 0.0f;
        for (int i = 0; i < numBeating; ++i)
        {
            const float r = re2[(size_t) i] * cr2[(size_t) i] - im2[(size_t) i] * ci2[(size_t) i];
            const float m = re2[(size_t) i] * ci2[(size_t) i] + im2[(size_t) i] * cr2[(size_t) i];
            re2[(size_t) i] = r;
            im2[(size_t) i] = m;
            beat += (m - im[(size_t) i]) * env[(size_t) i];
        }
        out += 0.5f * beat;   // the beating partials are the average of both strings

        if ((++age & 63) == 0)
        {
            // Keep the rotating phasors on the unit circle and drop partials that died away.
            for (int i = 0; i < n; ++i)
            {
                const float k = 1.5f - 0.5f * (re[(size_t) i] * re[(size_t) i] + im[(size_t) i] * im[(size_t) i]);
                re[(size_t) i] *= k; im[(size_t) i] *= k;
            }
            for (int i = 0; i < numBeating; ++i)
            {
                const float k = 1.5f - 0.5f * (re2[(size_t) i] * re2[(size_t) i] + im2[(size_t) i] * im2[(size_t) i]);
                re2[(size_t) i] *= k; im2[(size_t) i] *= k;
            }
            float level = 0.0f;
            for (int i = 0; i < n; ++i)
                level += env[(size_t) i];
            currentLevel = level + hammer;
            while (numPartials > 1 && env[(size_t) (numPartials - 1)] < 1.0e-6f)
                --numPartials;
            numBeating = juce::jmin (numBeating, numPartials);
        }

        if (hammer > 1.0e-5f)
        {
            hammerLp.process (noise.next());
            out += hammer * hammerLp.low;
            hammer *= hammerDecay;
        }
        if (attackPos < attackLength)
        {
            const float x = (float) attackPos++ / (float) attackLength;
            out *= 0.5f - 0.5f * std::cos (pi * x);
        }
        return out * loudness;
    }

    bool isDone() const noexcept { return age > (int) (0.05f * sr) && currentLevel < 2.0e-4f; }

private:
    static constexpr int maxPartials = 32, maxBeating = 8;
    alignas (16) std::array<float, maxPartials> freq {}, amp {}, d1 {}, d2 {}, e1 {}, e2 {}, env {}, re {}, im {}, cr {}, ci {};
    alignas (16) std::array<float, maxBeating> detune {}, re2 {}, im2 {}, cr2 {}, ci2 {};
    int numPartials = 0, numBeating = 0, midiNote = 60, age = 0, attackLength = 64, attackPos = 0;
    float sr = 48000.0f, vel = 1.0f, ratio = -1.0f, loudness = 1.0f, hammer = 0.0f, hammerDecay = 0.0f, currentLevel = 1.0f;
    bool released = false;
    Svf hammerLp;
    Noise noise;
};

//==============================================================================
/** Extended Karplus-Strong plucked string. */
class PluckModel
{
public:
    struct Params
    {
        float brightness = 0.5f;   ///< 0 (dull, nylon / thumb) .. 1 (bright, steel / pick)
        float decay = 3.0f;        ///< T60 of the fundamental at C3, seconds
        float pluckPos = 0.13f;    ///< 0..0.5 along the string
        float body = 0.5f;         ///< acoustic body resonance mix
        float tone = 6000.0f;      ///< output low-pass
    };

    void start (int note, float velocity, float sampleRate, const Params& p) noexcept
    {
        sr = sampleRate;
        params = p;
        released = false;
        age = 0;
        f0 = (float) juce::MidiMessage::getMidiNoteInHertz (note);
        // Loop filter: one-zero low-pass mixing current and previous sample; S = amount of the previous.
        S = juce::jlimit (0.02f, 0.5f, 0.5f - 0.45f * p.brightness * (0.6f + 0.4f * velocity));
        const float t60 = p.decay * std::exp2 (-(float) (note - 48) / 24.0f);
        setDecay (t60);
        setRatio (1.0f);

        buffer.fill (0.0f);
        write = 0;
        prev = 0.0f;
        noise.seed ((uint32_t) (note * 104729 + (int) (velocity * 1000)));

        // Excitation: noise, softened for soft plucks, with the comb of the pluck position.
        const int len = juce::jlimit (2, (int) buffer.size() - 2, (int) (sr / f0));
        Svf soft;
        soft.set (f0 * (2.0f + 30.0f * velocity * (0.3f + p.brightness)), 0.6f, sr);
        std::array<float, 4096> tmp {};
        for (int i = 0; i < len; ++i)
        {
            soft.process (noise.next());
            tmp[(size_t) i] = soft.low;
        }
        const int P = juce::jmax (1, (int) (p.pluckPos * (float) len));
        float mean = 0.0f;
        for (int i = 0; i < len; ++i)
        {
            const float v = tmp[(size_t) i] - (i >= P ? tmp[(size_t) (i - P)] : 0.0f);
            buffer[(size_t) i] = v;
            mean += v;
        }
        mean /= (float) len;
        float peak = 0.0f;
        for (int i = 0; i < len; ++i)
        {
            buffer[(size_t) i] -= mean;
            peak = juce::jmax (peak, std::abs (buffer[(size_t) i]));
        }
        const float scale = (0.35f + 0.65f * velocity) / juce::jmax (0.001f, peak);
        for (int i = 0; i < len; ++i)
            buffer[(size_t) i] *= scale;
        write = len;

        body1.set (98.0f, 3.0f, sr);   body1.reset();
        body2.set (205.0f, 4.0f, sr);  body2.reset();
        body3.set (420.0f, 3.5f, sr);  body3.reset();
        toneLp.set (p.tone, 0.6f, sr); toneLp.reset();
        dcIn = dcOut = 0.0f;
        level = 1.0f;
    }

    void setRatio (float r) noexcept
    {
        delay = juce::jlimit (2.0f, (float) buffer.size() - 4.0f, sr / (f0 * r) - S);
    }

    void release() noexcept
    {
        if (released)
            return;
        released = true;
        setDecay (0.12f);
    }

    float render() noexcept
    {
        const int size = (int) buffer.size();
        float readPos = (float) write - delay;
        while (readPos < 0.0f)
            readPos += (float) size;
        const int i0 = (int) readPos;
        const float frac = readPos - (float) i0;
        const float a = buffer[(size_t) (i0 % size)], b = buffer[(size_t) ((i0 + 1) % size)];
        const float y = a + frac * (b - a);

        const float filtered = rho * ((1.0f - S) * y + S * prev);
        prev = y;
        buffer[(size_t) write] = filtered;
        write = (write + 1) % size;

        // DC blocker + body + tone
        const float hp = y - dcIn + 0.995f * dcOut;
        dcIn = y;
        dcOut = hp;
        float out = hp;
        if (params.body > 0.0f)
        {
            body1.process (hp); body2.process (hp); body3.process (hp);
            out += params.body * (0.9f * body1.band + 0.6f * body2.band + 0.35f * body3.band);
        }
        toneLp.process (out);
        level = 0.999f * level + 0.001f * std::abs (toneLp.low);
        ++age;
        // ~0.7 ms fade-in: the pluck stays crisp but the first sample is no hard step.
        const float fade = age < 32 ? (float) age / 32.0f : 1.0f;
        return toneLp.low * fade;
    }

    bool isDone() const noexcept { return age > (int) (0.1f * sr) && level < 1.0e-4f; }

private:
    void setDecay (float t60) noexcept
    {
        rho = juce::jlimit (0.5f, 0.99999f, std::pow (10.0f, -3.0f / (f0 * juce::jmax (0.02f, t60))));
    }

    std::array<float, 4096> buffer {};
    int write = 0, age = 0;
    float sr = 48000.0f, f0 = 110.0f, S = 0.3f, rho = 0.999f, delay = 100.0f, prev = 0.0f;
    float dcIn = 0.0f, dcOut = 0.0f, level = 0.0f;
    bool released = false;
    Params params;
    Svf body1, body2, body3, toneLp;
    Noise noise;
};

//==============================================================================
/** Breath-driven reed / flute model with formants. Amplitude envelope comes from the voice. */
class WindModel
{
public:
    struct Params
    {
        float f1 = 500, f2 = 1500, f3 = 2600;   ///< formants (Hz)
        float formantMix = 0.8f;                 ///< 0 = plain source
        float breath = 0.05f;                    ///< breath noise
        float scoopCents = 35.0f;                ///< pitch scoop into the note
        float vibratoCents = 16.0f;              ///< delayed vibrato depth
        bool sineSource = false;                 ///< flute-like source instead of a reed
        float brightness = 1.0f;
    };

    void start (int note, float velocity, float sampleRate, const Params& p) noexcept
    {
        sr = sampleRate;
        params = p;
        vel = juce::jlimit (0.05f, 1.0f, velocity);
        f0 = (float) juce::MidiMessage::getMidiNoteInHertz (note);
        t = 0.0f;
        saw.phase = 0.0f;
        sinePhase = 0.0f;
        vibPhase = 0.0f;
        noise.seed ((uint32_t) note * 2654435761u);
        fm1.set (p.f1, 5.0f, sr); fm1.reset();
        fm2.set (p.f2, 7.0f, sr); fm2.reset();
        fm3.set (p.f3, 8.0f, sr); fm3.reset();
        breathBp.set (p.sineSource ? f0 * 2.0f : 2400.0f, 1.2f, sr); breathBp.reset();
        bright.reset();
        countdown = 0;
        pitchCountdown = 0;
    }

    /** ratio = pitch bend, envelope = current amplitude envelope (drives the timbre). */
    float render (float ratio, float envelope, float extraVibrato) noexcept
    {
        t += 1.0f / sr;
        if (--pitchCountdown <= 0)
        {
            // Scoop up into the note, then a vibrato that starts after a moment (updated every 8 samples).
            pitchCountdown = 8;
            const float scoop = -params.scoopCents * std::exp (-t / 0.05f);
            const float vibAmount = juce::jlimit (0.0f, 1.0f, (t - 0.35f) / 0.45f) * params.vibratoCents + extraVibrato * 40.0f;
            vibPhase += 8.0f * 5.1f / sr;
            if (vibPhase >= 1.0f) vibPhase -= 1.0f;
            const float cents = scoop + vibAmount * std::sin (twoPi * vibPhase);
            freq = f0 * ratio * std::exp2 (cents / 1200.0f);
        }

        const float breath = envelope * (0.6f + 0.4f * vel);
        const float turbulence = 1.0f + 0.03f * noise.next();
        float src;
        if (params.sineSource)
        {
            sinePhase += freq / sr;
            if (sinePhase >= 1.0f) sinePhase -= 1.0f;
            src = std::sin (twoPi * sinePhase) + 0.22f * breath * std::sin (2.0f * twoPi * sinePhase)
                + 0.08f * breath * std::sin (3.0f * twoPi * sinePhase);
        }
        else
        {
            src = saw.next (freq, sr);
        }

        // Louder playing = more upper harmonics.
        if (--countdown <= 0)
        {
            countdown = 16;
            const float cutoff = freq * (1.5f + 14.0f * breath * vel * params.brightness);
            bright.set (cutoff, 0.6f, sr);
        }
        bright.process (src * turbulence);
        float s = bright.low;

        if (params.formantMix > 0.0f)
        {
            fm1.process (s); fm2.process (s); fm3.process (s);
            const float formants = fm1.band + 0.7f * fm2.band + 0.45f * fm3.band;
            s = (1.0f - params.formantMix) * s + params.formantMix * 2.2f * formants;
        }

        breathBp.process (noise.next());
        s += params.breath * breathBp.band * (0.4f + breath);
        return s;
    }

private:
    Params params;
    float sr = 48000.0f, vel = 1.0f, f0 = 440.0f, t = 0.0f, sinePhase = 0.0f, vibPhase = 0.0f;
    int countdown = 0, pitchCountdown = 0;
    float freq = 440.0f;
    Saw saw;
    Svf fm1, fm2, fm3, breathBp, bright;
    Noise noise;
};

//==============================================================================
/** A small ensemble of singers on one note, through vowel formants. */
class ChoirModel
{
public:
    struct Params
    {
        float f1 = 750, f2 = 1150, f3 = 2800;   ///< vowel formants
        float breath = 0.04f;
    };

    void start (int note, float velocity, float sampleRate, const Params& p) noexcept
    {
        sr = sampleRate;
        params = p;
        f0 = (float) juce::MidiMessage::getMidiNoteInHertz (note);
        vel = velocity;
        noise.seed ((uint32_t) (note * 40503 + 7));
        for (int i = 0; i < numSingers; ++i)
        {
            auto& s = singers[(size_t) i];
            s.saw.phase = 0.5f + 0.5f * noise.next();
            s.detune = 9.0f * noise.next();                 // cents
            s.driftPhase = 0.5f + 0.5f * noise.next();
            s.driftRate = 0.25f + 0.2f * (0.5f + 0.5f * noise.next());
            s.vibPhase = 0.5f + 0.5f * noise.next();
            s.vibRate = 4.8f + 0.8f * (0.5f + 0.5f * noise.next());
        }
        // Formants move a little with pitch (higher voices open up).
        const float shift = juce::jlimit (0.85f, 1.25f, std::exp2 ((float) (note - 60) / 48.0f));
        fm1.set (p.f1 * shift, 6.0f, sr); fm1.reset();
        fm2.set (p.f2 * shift, 8.0f, sr); fm2.reset();
        fm3.set (p.f3, 10.0f, sr);        fm3.reset();
        air.set (3000.0f, 0.8f, sr);      air.reset();
        lp.set (juce::jmin (9000.0f, f0 * 24.0f), 0.6f, sr); lp.reset();
        countdown = 0;
    }

    float render (float ratio, float envelope) noexcept
    {
        // Pitch modulation is slow: update it every 16 samples.
        if (--countdown <= 0)
        {
            countdown = 16;
            for (auto& s : singers)
            {
                s.driftPhase += 16.0f * s.driftRate / sr;
                if (s.driftPhase >= 1.0f) s.driftPhase -= 1.0f;
                s.vibPhase += 16.0f * s.vibRate / sr;
                if (s.vibPhase >= 1.0f) s.vibPhase -= 1.0f;
                const float cents = s.detune + 4.0f * std::sin (twoPi * s.driftPhase) + 11.0f * std::sin (twoPi * s.vibPhase);
                s.freq = f0 * ratio * std::exp2 (cents / 1200.0f);
            }
        }
        float sum = 0.0f;
        for (auto& s : singers)
            sum += s.saw.next (s.freq, sr);
        sum *= 1.0f / (float) numSingers;
        lp.process (sum);
        const float x = lp.low;
        fm1.process (x); fm2.process (x); fm3.process (x);
        float out = 2.4f * (fm1.band + 0.55f * fm2.band + 0.3f * fm3.band) + 0.12f * x;
        air.process (noise.next());
        out += params.breath * air.band * envelope;
        return out * (0.7f + 0.3f * vel);
    }

private:
    static constexpr int numSingers = 4;
    struct Singer { Saw saw; float detune = 0, driftPhase = 0, driftRate = 0.3f, vibPhase = 0, vibRate = 5.0f, freq = 220.0f; };
    int countdown = 0;
    std::array<Singer, numSingers> singers {};
    Params params;
    float sr = 48000.0f, f0 = 220.0f, vel = 1.0f;
    Svf fm1, fm2, fm3, air, lp;
    Noise noise;
};

//==============================================================================
/** Fretless bass: slide into the note and the "mwah" of the string against the fingerboard. */
class FretlessModel
{
public:
    void start (int note, float velocity, float sampleRate) noexcept
    {
        sr = sampleRate;
        vel = velocity;
        f0 = (float) juce::MidiMessage::getMidiNoteInHertz (note);
        t = 0.0f;
        saw.phase = 0.5f;     // start at the zero crossing of the sawtooth
        subPhase = 0.0f;
        filter.reset();
        countdown = 0;
    }

    float render (float ratio) noexcept
    {
        t += 1.0f / sr;
        const float slide = -28.0f * std::exp (-t / 0.06f);   // cents
        const float freq = f0 * ratio * std::exp2 (slide / 1200.0f);
        const float s = 0.55f * saw.next (freq, sr);
        subPhase += freq / sr;
        if (subPhase >= 1.0f) subPhase -= 1.0f;
        const float sub = std::sin (twoPi * subPhase);

        if (--countdown <= 0)
        {
            countdown = 16;
            // mwah: the filter opens after the attack and slowly closes again
            const float swell = (1.0f - std::exp (-t / 0.07f)) * (0.35f + 0.65f * std::exp (-t / 0.9f));
            const float cutoff = freq * (1.2f + (3.0f + 9.0f * vel) * swell);
            filter.set (cutoff, 2.2f, sr);
        }
        filter.process (s);
        return filter.low * 0.9f + 0.55f * sub;
    }

private:
    float sr = 48000.0f, vel = 1.0f, f0 = 55.0f, t = 0.0f, subPhase = 0.0f;
    int countdown = 0;
    Saw saw;
    Svf filter;
};

} // namespace km::models
