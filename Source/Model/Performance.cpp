#include "Model/Performance.h"

namespace km
{
namespace
{
    juce::var get (const juce::var& v, const char* key, juce::var fallback)
    {
        if (auto* obj = v.getDynamicObject())
            if (obj->hasProperty (key))
                return obj->getProperty (key);
        return fallback;
    }

    float getF (const juce::var& v, const char* key, float fallback, float lo, float hi)
    {
        return juce::jlimit (lo, hi, (float) (double) get (v, key, fallback));
    }

    int getI (const juce::var& v, const char* key, int fallback, int lo, int hi)
    {
        return juce::jlimit (lo, hi, (int) get (v, key, fallback));
    }

    const char* curveName (filter::VelocityCurve c)
    {
        switch (c)
        {
            case filter::VelocityCurve::soft:  return "soft";
            case filter::VelocityCurve::hard:  return "hard";
            case filter::VelocityCurve::fixed: return "fixed";
            case filter::VelocityCurve::linear:
            default:                           return "linear";
        }
    }

    filter::VelocityCurve curveFromName (const juce::String& s)
    {
        if (s == "soft")  return filter::VelocityCurve::soft;
        if (s == "hard")  return filter::VelocityCurve::hard;
        if (s == "fixed") return filter::VelocityCurve::fixed;
        return filter::VelocityCurve::linear;
    }

    const char* typeName (InstrumentKey::Type t)
    {
        switch (t)
        {
            case InstrumentKey::Type::builtIn: return "builtin";
            case InstrumentKey::Type::sfz:     return "sfz";
            case InstrumentKey::Type::plugin:  return "plugin";
            case InstrumentKey::Type::midiOut: return "midiout";
            case InstrumentKey::Type::none:
            default:                           return "none";
        }
    }

    InstrumentKey::Type typeFromName (const juce::String& s)
    {
        if (s == "builtin") return InstrumentKey::Type::builtIn;
        if (s == "sfz")     return InstrumentKey::Type::sfz;
        if (s == "plugin")  return InstrumentKey::Type::plugin;
        if (s == "midiout") return InstrumentKey::Type::midiOut;
        return InstrumentKey::Type::none;
    }
} // namespace

//==============================================================================
juce::var InstrumentKey::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("type", typeName (type));
    o->setProperty ("ref", ref);
    o->setProperty ("name", displayName);
    if (pluginState.isNotEmpty())
        o->setProperty ("state", pluginState);
    return juce::var (o);
}

InstrumentKey InstrumentKey::fromVar (const juce::var& v)
{
    InstrumentKey k;
    k.type = typeFromName (get (v, "type", "none").toString());
    k.ref = get (v, "ref", "").toString();
    k.displayName = get (v, "name", "").toString();
    k.pluginState = get (v, "state", "").toString();
    if (k.ref.isEmpty())
        k.type = Type::none;
    if (k.type == Type::builtIn && k.ref == "8")   // before version 0.4: 8 = the (electronic) drum kit
    {
        k.ref = "101";
        k.displayName = "Drumstel Elektronisch";
    }
    return k;
}

//==============================================================================
juce::var PartConfig::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("enabled", enabled);
    o->setProperty ("inputDeviceId", inputDeviceId);
    o->setProperty ("inputDeviceName", inputDeviceName);
    o->setProperty ("channel", channel);
    o->setProperty ("instrument", instrument.toVar());
    o->setProperty ("volumeDb", volumeDb);
    o->setProperty ("pan", pan);
    o->setProperty ("transpose", transpose);
    o->setProperty ("keyLow", keyLow);
    o->setProperty ("keyHigh", keyHigh);
    o->setProperty ("velLow", velLow);
    o->setProperty ("velHigh", velHigh);
    o->setProperty ("velocityCurve", curveName (velocityCurve));
    o->setProperty ("fixedVelocity", fixedVelocity);
    o->setProperty ("reverbSend", reverbSend);
    o->setProperty ("delaySend", delaySend);
    o->setProperty ("mute", mute);
    o->setProperty ("solo", solo);
    auto* arp = new juce::DynamicObject();
    arp->setProperty ("on", arpOn);
    arp->setProperty ("mode", arpMode);
    arp->setProperty ("rate", arpRate);
    arp->setProperty ("octaves", arpOctaves);
    arp->setProperty ("gate", arpGate);
    arp->setProperty ("latch", arpLatch);
    o->setProperty ("arpeggiator", juce::var (arp));
    o->setProperty ("mpe", mpe);
    return juce::var (o);
}

PartConfig PartConfig::fromVar (const juce::var& v)
{
    PartConfig p;
    p.enabled = (bool) get (v, "enabled", false);
    p.inputDeviceId = get (v, "inputDeviceId", "").toString();
    p.inputDeviceName = get (v, "inputDeviceName", "").toString();
    p.channel = getI (v, "channel", 0, 0, 16);
    p.instrument = InstrumentKey::fromVar (get (v, "instrument", {}));
    p.volumeDb = getF (v, "volumeDb", 0.0f, -100.0f, 12.0f);
    p.pan = getF (v, "pan", 0.0f, -1.0f, 1.0f);
    p.transpose = getI (v, "transpose", 0, -48, 48);
    p.keyLow = getI (v, "keyLow", 0, 0, 127);
    p.keyHigh = getI (v, "keyHigh", 127, 0, 127);
    p.velLow = getI (v, "velLow", 1, 1, 127);
    p.velHigh = getI (v, "velHigh", 127, 1, 127);
    p.velocityCurve = curveFromName (get (v, "velocityCurve", "linear").toString());
    p.fixedVelocity = getI (v, "fixedVelocity", 100, 1, 127);
    p.reverbSend = getF (v, "reverbSend", 0.15f, 0.0f, 1.0f);
    p.delaySend = getF (v, "delaySend", 0.0f, 0.0f, 1.0f);
    p.mute = (bool) get (v, "mute", false);
    p.solo = (bool) get (v, "solo", false);
    const auto arp = get (v, "arpeggiator", {});
    p.arpOn = (bool) get (arp, "on", false);
    p.arpMode = getI (arp, "mode", 0, 0, 5);
    p.arpRate = getI (arp, "rate", 1, 0, 5);
    p.arpOctaves = getI (arp, "octaves", 1, 1, 4);
    p.arpGate = getF (arp, "gate", 0.6f, 0.05f, 1.0f);
    p.arpLatch = (bool) get (arp, "latch", false);
    p.mpe = (bool) get (v, "mpe", false);
    return p;
}

//==============================================================================
juce::var MasterConfig::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("volumeDb", volumeDb);
    o->setProperty ("reverbSize", reverbSize);
    o->setProperty ("reverbDamping", reverbDamping);
    o->setProperty ("delayMs", delayMs);
    o->setProperty ("delayFeedback", delayFeedback);
    o->setProperty ("limiterThresholdDb", limiterThresholdDb);
    return juce::var (o);
}

MasterConfig MasterConfig::fromVar (const juce::var& v)
{
    MasterConfig m;
    m.volumeDb = getF (v, "volumeDb", 0.0f, -100.0f, 12.0f);
    m.reverbSize = getF (v, "reverbSize", 0.55f, 0.0f, 1.0f);
    m.reverbDamping = getF (v, "reverbDamping", 0.4f, 0.0f, 1.0f);
    m.delayMs = getF (v, "delayMs", 375.0f, 10.0f, 2000.0f);
    m.delayFeedback = getF (v, "delayFeedback", 0.35f, 0.0f, 0.95f);
    m.limiterThresholdDb = getF (v, "limiterThresholdDb", -1.0f, -24.0f, 0.0f);
    return m;
}

//==============================================================================
Performance Performance::makeDefault()
{
    Performance p;
    p.name = "Start";
    p.parts[0].enabled = true;
    p.parts[0].instrument = InstrumentKey::builtIn (0, "Electric Piano");
    return p;
}

juce::String Performance::toJson() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("format", "KlankModule.Performance");
    o->setProperty ("version", currentVersion);
    o->setProperty ("name", name);
    o->setProperty ("master", master.toVar());

    juce::Array<juce::var> partsArray;
    for (auto& p : parts)
        partsArray.add (p.toVar());
    o->setProperty ("parts", partsArray);

    return juce::JSON::toString (juce::var (o), false);
}

bool Performance::fromJson (const juce::String& json, juce::String& error)
{
    juce::var root;
    const auto result = juce::JSON::parse (json, root);

    if (result.failed())
    {
        error = "Ongeldige JSON: " + result.getErrorMessage();
        return false;
    }

    if (get (root, "format", "").toString() != "KlankModule.Performance")
    {
        error = "Dit is geen KlankModule-performance.";
        return false;
    }

    const int version = (int) get (root, "version", 0);
    if (version > currentVersion)
    {
        DBG ("Performance written by a newer version (" << version << "); loading what we understand.");
    }

    // Version migrations go here, e.g. if (version < 2) { ... }

    Performance loaded;
    loaded.name = get (root, "name", "Naamloos").toString();
    loaded.master = MasterConfig::fromVar (get (root, "master", {}));

    if (auto* arr = get (root, "parts", {}).getArray())
        for (int i = 0; i < juce::jmin (kNumParts, arr->size()); ++i)
            loaded.parts[(size_t) i] = PartConfig::fromVar (arr->getReference (i));

    *this = loaded;
    return true;
}

bool Performance::saveTo (const juce::File& file) const
{
    file.getParentDirectory().createDirectory();
    // Write to a temp file first so a crash mid-write never destroys the old file.
    juce::TemporaryFile temp (file);
    if (! temp.getFile().replaceWithText (toJson()))
        return false;
    return temp.overwriteTargetFileWithTemporary();
}

bool Performance::loadFrom (const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "Bestand niet gevonden: " + file.getFullPathName();
        return false;
    }
    return fromJson (file.loadFileAsString(), error);
}

} // namespace km
