#include "Engine/BuiltInDrums.h"

namespace km
{
namespace
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;
    // Inharmonic square-wave frequencies of the classic analogue hihat/cymbal circuit.
    constexpr float metalFreqs[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };

    float maxDuration (int kind)
    {
        switch (kind)
        {
            case 0: return 0.9f;   // kick
            case 7: return 1.0f;   // tom
            case 8: return 3.5f;   // crash
            case 9: return 2.5f;   // ride
            case 10: return 2.0f;  // bell
            case 6: return 1.4f;   // open hat
            default: return 0.6f;
        }
    }
} // namespace

BuiltInDrums::BuiltInDrums (int k) : kit (juce::jlimit (0, numKits - 1, k))
{
    key = InstrumentKey::builtIn (firstRef + kit, getName());
}

void BuiltInDrums::prepare (double sampleRate, int)
{
    sr = (float) sampleRate;
    dt = 1.0f / sr;
    allNotesOff();
}

void BuiltInDrums::allNotesOff() noexcept
{
    for (auto& v : voices)
        v.active = false;
}

float BuiltInDrums::noise() noexcept
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return (float) (rng & 0xffffff) / (float) 0x7fffff - 1.0f;
}

void BuiltInDrums::trigger (int note, int velocity) noexcept
{
    Kind kind = Kind::blip;
    float freq = 440.0f, pan = 0.0f;

    switch (note)
    {
        case 35: case 36: kind = Kind::kick; break;
        case 37:          kind = Kind::rim; pan = -0.1f; break;
        case 38: case 40: kind = Kind::snare; pan = -0.05f; break;
        case 39:          kind = Kind::clap; break;
        case 42:          kind = Kind::hatClosed; pan = 0.3f; break;
        case 44:          kind = Kind::hatPedal; pan = 0.3f; break;
        case 46:          kind = Kind::hatOpen; pan = 0.3f; break;
        case 41: kind = Kind::tom; freq = 62.0f;  pan = 0.45f; break;
        case 43: kind = Kind::tom; freq = 78.0f;  pan = 0.35f; break;
        case 45: kind = Kind::tom; freq = 98.0f;  pan = 0.15f; break;
        case 47: kind = Kind::tom; freq = 120.0f; pan = 0.0f;  break;
        case 48: kind = Kind::tom; freq = 145.0f; pan = -0.15f; break;
        case 50: kind = Kind::tom; freq = 175.0f; pan = -0.3f; break;
        case 49: case 52: case 55: case 57: kind = Kind::crash; pan = note == 57 ? 0.4f : -0.4f; break;
        case 51: case 59: kind = Kind::ride; pan = 0.45f; break;
        case 53:          kind = Kind::rideBell; pan = 0.45f; break;
        case 54:          kind = Kind::tambourine; pan = 0.2f; break;
        case 56:          kind = Kind::cowbell; pan = -0.2f; break;
        case 69: case 70: case 82: kind = Kind::shaker; pan = 0.25f; break;
        case 75: case 76: case 77: kind = Kind::clave; pan = -0.25f; break;
        default: freq = (float) juce::MidiMessage::getMidiNoteInHertz (note); break;
    }

    // Closed / pedal hihat choke a ringing open hihat.
    if (kind == Kind::hatClosed || kind == Kind::hatPedal)
        for (auto& v : voices)
            if (v.active && v.kind == Kind::hatOpen)
                v.choking = true;

    // Free voice, otherwise steal the oldest.
    Voice* target = nullptr;
    for (auto& v : voices)
        if (! v.active) { target = &v; break; }
    if (target == nullptr)
    {
        target = &voices[0];
        for (auto& v : voices)
            if (v.t > target->t) target = &v;
    }

    *target = Voice{};
    target->active = true;
    target->kind = kind;
    target->note = note;
    target->velocity = std::pow ((float) velocity / 127.0f, 1.3f);
    target->freq = freq;
    const float angle = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
    target->panL = std::cos (angle) * 1.41421356f;
    target->panR = std::sin (angle) * 1.41421356f;
}

float BuiltInDrums::renderVoice (Voice& v) noexcept
{
    const float t = v.t;
    auto hp = [&v] (float x, float coef)
    {
        const float y = coef * (v.hpState + x - v.hpPrev);
        v.hpPrev = x;
        v.hpState = y;
        return y;
    };
    auto bandpass = [&v, this] (float x, float centre, float q)
    {
        const float f = 2.0f * std::sin (juce::MathConstants<float>::pi * juce::jmin (centre, sr * 0.2f) / sr);
        v.bpLow += f * v.bpBand;
        const float high = x - v.bpLow - q * v.bpBand;
        v.bpBand += f * high;
        return v.bpBand;
    };
    auto metal = [&v, this] (float scale)
    {
        float s = 0.0f;
        for (int i = 0; i < 6; ++i)
        {
            v.metalPhase[i] += metalFreqs[i] * scale * dt;
            if (v.metalPhase[i] >= 1.0f) v.metalPhase[i] -= 1.0f;
            s += v.metalPhase[i] < 0.5f ? 1.0f : -1.0f;
        }
        return s / 6.0f;
    };
    auto env = [t] (float tau) { return std::exp (-t / tau); };

    float s = 0.0f;
    if (kit == 0 && renderAcoustic (v, s))
    {
        // acoustic kit handled the sound
    }
    else switch (v.kind)
    {
        case Kind::kick:
        {
            const float pitch = 45.0f + 110.0f * env (0.035f);
            v.phase += pitch * dt;
            s = std::sin (twoPi * v.phase) * env (0.32f) + 0.3f * noise() * env (0.004f);
            break;
        }
        case Kind::snare:
            s = 0.5f * std::sin (twoPi * 185.0f * t) * env (0.09f) + 0.3f * std::sin (twoPi * 330.0f * t) * env (0.06f)
              + 0.8f * hp (noise(), 0.85f) * env (0.16f);
            break;
        case Kind::rim:
            s = 0.6f * std::sin (twoPi * 1700.0f * t) * env (0.012f) + 0.5f * bandpass (noise(), 2500.0f, 0.4f) * env (0.02f);
            break;
        case Kind::clap:
        {
            // Three quick slaps and a short room tail.
            float e = 0.0f;
            for (float start : { 0.0f, 0.011f, 0.022f })
                if (t >= start) e = juce::jmax (e, std::exp (-(t - start) / 0.006f));
            if (t >= 0.03f) e = juce::jmax (e, 0.7f * std::exp (-(t - 0.03f) / 0.14f));
            s = 1.6f * bandpass (noise(), 1100.0f, 0.5f) * e;
            break;
        }
        case Kind::hatClosed:  s = 0.9f * hp (metal (1.0f) + 0.3f * noise(), 0.7f) * env (0.045f); break;
        case Kind::hatPedal:   s = 0.7f * hp (metal (1.0f) + 0.3f * noise(), 0.7f) * env (0.09f); break;
        case Kind::hatOpen:    s = 0.8f * hp (metal (1.0f) + 0.3f * noise(), 0.7f) * env (0.45f); break;
        case Kind::tom:
        {
            const float pitch = v.freq * (1.0f + 0.6f * env (0.05f));
            v.phase += pitch * dt;
            s = 0.65f * std::sin (twoPi * v.phase) * env (0.28f) + 0.06f * noise() * env (0.01f);
            break;
        }
        case Kind::crash:      s = 0.6f * hp (0.6f * metal (1.35f) + 0.6f * noise(), 0.75f) * env (1.3f); break;
        case Kind::ride:       s = 0.4f * hp (metal (1.6f) + 0.2f * noise(), 0.8f) * env (0.9f); break;
        case Kind::rideBell:   s = 0.35f * (std::sin (twoPi * 740.0f * t) + 0.6f * std::sin (twoPi * 1113.0f * t)) * env (0.7f)
                                 + 0.25f * hp (metal (1.6f), 0.8f) * env (0.5f); break;
        case Kind::tambourine: s = 0.6f * hp (metal (2.2f) * noise(), 0.6f) * env (0.22f); break;
        case Kind::cowbell:
        {
            const float sq = (std::fmod (540.0f * t, 1.0f) < 0.5f ? 0.5f : -0.5f) + (std::fmod (800.0f * t, 1.0f) < 0.5f ? 0.5f : -0.5f);
            s = 0.5f * bandpass (sq, 700.0f, 0.3f) * (0.6f * env (0.02f) + 0.4f * env (0.12f));
            break;
        }
        case Kind::shaker:     s = 0.6f * hp (noise(), 0.5f) * juce::jmin (1.0f, t / 0.015f) * env (0.07f); break;
        case Kind::clave:      s = 0.6f * std::sin (twoPi * 2500.0f * t) * env (0.025f); break;
        case Kind::blip:       s = 0.4f * std::sin (twoPi * v.freq * t) * env (0.06f); break;
    }

    if (v.choking)
    {
        v.choke -= dt / 0.008f;   // 8 ms fade
        if (v.choke <= 0.0f)
            v.active = false;
    }

    v.t += dt;
    if (v.t > maxDuration ((int) v.kind))
        v.active = false;

    return s * v.velocity * juce::jmax (0.0f, v.choke) * 0.55f;
}

bool BuiltInDrums::renderAcoustic (Voice& v, float& s) noexcept
{
    const float t = v.t;
    auto env = [t] (float tau) { return std::exp (-t / tau); };
    auto attack = [t] (float ms) { return juce::jmin (1.0f, t / (ms * 0.001f)); };
    // Two independent resonators (state variable band-pass) for drum heads and snare wires.
    auto bp = [this] (float& low, float& band, float x, float centre, float damping)
    {
        const float f = 2.0f * std::sin (juce::MathConstants<float>::pi * juce::jmin (centre, sr * 0.2f) / sr);
        low += f * band;
        const float high = x - low - damping * band;
        band += f * high;
        return band;
    };
    auto lowpass = [this] (float& state, float x, float hz)
    {
        state += (1.0f - std::exp (-juce::MathConstants<float>::twoPi * hz / sr)) * (x - state);
        return state;
    };
    const float vel = v.velocity;

    switch (v.kind)
    {
        case Kind::kick:
        {
            // Felt beater on a big head: pitch drops from ~100 to ~52 Hz, a soft click and a woofy body.
            const float pitch = 52.0f + 50.0f * env (0.028f);
            v.phase += pitch * dt;
            if (v.phase >= 1.0f) v.phase -= 1.0f;
            const float body = std::sin (twoPi * v.phase) * (0.75f * env (0.18f) + 0.25f * env (0.45f));
            const float click = lowpass (v.lpState, noise(), 2500.0f + 3500.0f * vel) * env (0.0035f);
            const float thump = bp (v.bpLow, v.bpBand, noise(), 85.0f, 0.4f) * env (0.05f);
            s = 0.95f * body + (0.5f + 0.6f * vel) * click + 0.9f * thump;
            return true;
        }
        case Kind::snare:
        {
            // Two head modes, a crack and the snare wires (brighter for harder hits).
            v.phase += 182.0f * dt;  if (v.phase >= 1.0f) v.phase -= 1.0f;
            v.phase2 += 336.0f * dt; if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;
            const float heads = 0.45f * std::sin (twoPi * v.phase) * env (0.07f) + 0.25f * std::sin (twoPi * v.phase2) * env (0.05f);
            const float n = noise();
            const float wires = bp (v.bpLow, v.bpBand, n, 3800.0f + 2500.0f * vel, 0.9f) * (0.6f * env (0.11f) + 0.4f * env (0.26f));
            const float crack = (n - lowpass (v.lpState, n, 1500.0f)) * env (0.006f);
            s = heads * attack (0.4f) + (0.9f + 0.4f * vel) * wires + 0.5f * vel * crack;
            return true;
        }
        case Kind::tom:
        {
            const float pitch = v.freq * 1.15f * (1.0f + 0.12f * env (0.06f));
            v.phase += pitch * dt;          if (v.phase >= 1.0f) v.phase -= 1.0f;
            v.phase2 += pitch * 1.58f * dt; if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;
            const float heads = 0.6f * std::sin (twoPi * v.phase) * env (0.38f) + 0.18f * std::sin (twoPi * v.phase2) * env (0.16f);
            const float stick = bp (v.bpLow, v.bpBand, noise(), 900.0f, 0.7f) * env (0.012f);
            s = heads * attack (0.6f) + 0.5f * stick;
            return true;
        }
        case Kind::hatClosed:
        case Kind::hatPedal:
        case Kind::hatOpen:
        {
            // Two thin cymbals: bright noise shaped by band-passes, plus a little metal.
            const float n = noise();
            const float sizzle = bp (v.bpLow, v.bpBand, n, 8500.0f, 0.6f) + 0.6f * bp (v.bp2Low, v.bp2Band, n, 11500.0f, 0.8f);
            const float tau = v.kind == Kind::hatOpen ? 0.42f : v.kind == Kind::hatPedal ? 0.06f : 0.035f;
            const float chick = v.kind == Kind::hatPedal ? 0.4f * bp (v.lp2, v.lpState, n, 1200.0f, 0.8f) * env (0.01f) : 0.0f;
            s = (0.28f * sizzle + 0.12f * (std::fmod (6970.0f * t, 1.0f) - 0.5f)) * env (tau) * (0.6f + 0.4f * vel) + chick;
            return true;
        }
        case Kind::crash:
        {
            const float n = noise();
            const float wash = bp (v.bpLow, v.bpBand, n, 5200.0f, 0.5f) + 0.7f * bp (v.bp2Low, v.bp2Band, n, 9000.0f, 0.6f);
            const float low = lowpass (v.lpState, n, 900.0f) * env (0.25f);
            s = (0.38f * wash * (0.7f * env (0.9f) + 0.3f * env (2.2f)) + 0.6f * low) * attack (2.0f);
            return true;
        }
        case Kind::ride:
        {
            // Ping (bell-like partials) over a quiet wash.
            const float ping = 0.16f * (std::sin (twoPi * 2950.0f * t) + 0.7f * std::sin (twoPi * 4470.0f * t)
                                        + 0.5f * std::sin (twoPi * 5600.0f * t)) * env (0.25f);
            const float wash = bp (v.bpLow, v.bpBand, noise(), 6800.0f, 0.5f) * (0.5f * env (0.6f) + 0.5f * env (1.6f));
            s = ping + 0.22f * wash;
            return true;
        }
        case Kind::rim: case Kind::clap: case Kind::rideBell: case Kind::tambourine: case Kind::cowbell:
        case Kind::shaker: case Kind::clave: case Kind::blip:
            return false;
    }
    return false;
}

void BuiltInDrums::process (juce::AudioBuffer<float>& out, juce::MidiBuffer& midi) noexcept
{
    auto* l = out.getWritePointer (0);
    auto* r = out.getWritePointer (1);
    const int n = out.getNumSamples();

    auto renderRange = [&] (int from, int to)
    {
        for (auto& v : voices)
        {
            if (! v.active)
                continue;
            for (int i = from; i < to && v.active; ++i)
            {
                const float s = renderVoice (v);
                l[i] += s * v.panL;
                r[i] += s * v.panR;
            }
        }
    };

    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, n, meta.samplePosition);
        renderRange (pos, at);
        pos = at;
        const auto* d = meta.data;
        if (meta.numBytes >= 3 && (d[0] & 0xf0) == 0x90 && d[2] > 0)
            trigger (d[1], d[2]);
        else if (meta.numBytes >= 3 && (d[0] & 0xf0) == 0xb0 && (d[1] == 120 || d[1] == 123))
            allNotesOff();
        // Note-offs are ignored: drums are one-shots.
    }
    renderRange (pos, n);
}

} // namespace km
