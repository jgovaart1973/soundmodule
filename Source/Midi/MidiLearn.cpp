#include "Midi/MidiLearn.h"

namespace km
{
void MidiLearn::registerParameter (const juce::String& id, const juce::String& displayName, Setter setter, bool isAction)
{
    const juce::ScopedLock sl (lock);
    params[id] = Param { displayName, std::move (setter), isAction };
}

void MidiLearn::arm (const juce::String& paramId)
{
    const juce::ScopedLock sl (lock);
    armed = paramId;
}

void MidiLearn::cancelArm()
{
    const juce::ScopedLock sl (lock);
    armed.clear();
}

juce::String MidiLearn::getArmedParameter() const
{
    const juce::ScopedLock sl (lock);
    return armed;
}

void MidiLearn::removeMapping (const juce::String& paramId)
{
    const juce::ScopedLock sl (lock);
    mappings.removeIf ([&] (const Mapping& m) { return m.paramId == paramId; });
    actionLatched.clear();
}

bool MidiLearn::hasMapping (const juce::String& paramId) const
{
    const juce::ScopedLock sl (lock);
    for (auto& m : mappings)
        if (m.paramId == paramId)
            return true;
    return false;
}

juce::String MidiLearn::describeMapping (const juce::String& paramId) const
{
    const juce::ScopedLock sl (lock);
    for (auto& m : mappings)
        if (m.paramId == paramId)
            return "CC " + juce::String (m.cc) + (m.channel > 0 ? ", kanaal " + juce::String (m.channel) : juce::String());
    return {};
}

juce::String MidiLearn::getDisplayName (const juce::String& paramId) const
{
    const juce::ScopedLock sl (lock);
    auto it = params.find (paramId);
    return it != params.end() ? it->second.displayName : paramId;
}

bool MidiLearn::handle (const MidiEvent& e)
{
    if (! e.isController())
        return false;

    const int cc = e.data[1] & 0x7f;
    const int value = e.data[2] & 0x7f;

    Setter setter;
    float normalised = (float) value / 127.0f;

    {
        const juce::ScopedLock sl (lock);

        if (armed.isNotEmpty())
        {
            // Bank select must keep working, so never learn CC 0/32.
            if (cc == 0 || cc == 32)
                return false;

            const auto learnedId = armed;
            mappings.removeIf ([&] (const Mapping& m)
                               { return m.paramId == learnedId || (m.cc == cc && m.channel == e.channel()); });
            // Mappings listen to any device (device slots are only valid for one session).
            mappings.add ({ -1, e.channel(), cc, learnedId });
            actionLatched.clear();
            armed.clear();

            auto callback = onLearned;
            juce::MessageManager::callAsync ([callback, learnedId] { if (callback) callback (learnedId); });
            return true;
        }

        for (int i = 0; i < mappings.size(); ++i)
        {
            const auto& m = mappings.getReference (i);
            if (m.cc != cc || (m.channel != 0 && m.channel != e.channel()) || (m.source != -1 && m.source != e.source))
                continue;

            auto it = params.find (m.paramId);
            if (it == params.end())
                return true; // mapped to something that no longer exists: swallow

            if (it->second.isAction)
            {
                const bool pressed = value >= 64;
                const bool wasPressed = actionLatched[i];
                actionLatched[i] = pressed;
                if (! pressed || wasPressed)
                    return true;
                normalised = 1.0f;
            }

            setter = it->second.setter;
            break;
        }
    }

    if (setter)
    {
        setter (normalised);
        return true;
    }
    return false;
}

juce::String MidiLearn::toJson() const
{
    const juce::ScopedLock sl (lock);
    juce::Array<juce::var> list;
    for (auto& m : mappings)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("param", m.paramId);
        o->setProperty ("cc", m.cc);
        o->setProperty ("channel", m.channel);
        o->setProperty ("anyDevice", m.source < 0);
        list.add (juce::var (o));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "KlankModule.MidiLearn");
    root->setProperty ("version", 1);
    root->setProperty ("mappings", list);
    return juce::JSON::toString (juce::var (root), false);
}

void MidiLearn::fromJson (const juce::String& json)
{
    juce::var root;
    if (juce::JSON::parse (json, root).failed() || root["format"].toString() != "KlankModule.MidiLearn")
        return;

    const juce::ScopedLock sl (lock);
    mappings.clear();
    actionLatched.clear();
    if (auto* arr = root["mappings"].getArray())
        for (auto& v : *arr)
        {
            Mapping m;
            m.paramId = v["param"].toString();
            m.cc = juce::jlimit (0, 127, (int) v["cc"]);
            m.channel = juce::jlimit (0, 16, (int) v["channel"]);
            // Device slots are per session, so saved mappings listen to any device.
            m.source = -1;
            if (m.paramId.isNotEmpty())
                mappings.add (m);
        }
}

} // namespace km
