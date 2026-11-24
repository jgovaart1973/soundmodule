#include "Engine/InstrumentPool.h"

#include "Engine/BuiltInDrums.h"
#include "Engine/BuiltInSynth.h"
#include "Engine/PluginInstrument.h"
#include "Engine/SfzInstrument.h"

namespace km
{
InstrumentPool::InstrumentPool (juce::AudioPluginFormatManager& f, juce::KnownPluginList& k)
    : formatManager (f), knownPlugins (k)
{
}

InstrumentPool::~InstrumentPool()
{
    loaders.removeAllJobs (true, 10000);
    // All instances should have been returned by now; destroy them regardless on exit.
    entries.clear();
}

void InstrumentPool::setAudioConfig (double sr, int bs)
{
    sampleRate = sr;
    blockSize = bs;
    for (auto& e : entries)
        e->instrument->prepare (sampleRate, blockSize);
}

InstrumentPool::Entry* InstrumentPool::findEntry (Instrument* inst) noexcept
{
    for (auto& e : entries)
        if (e->instrument.get() == inst)
            return e.get();
    return nullptr;
}

InstrumentPool::Entry* InstrumentPool::findIdle (const juce::String& poolKey) noexcept
{
    for (auto& e : entries)
        if (e->loans == 0 && e->poolKey == poolKey)
            return e.get();
    return nullptr;
}

Instrument* InstrumentPool::addLoaned (std::unique_ptr<Instrument> inst, const InstrumentKey& key, bool loaned)
{
    auto entry = std::make_unique<Entry>();
    inst->key = key;
    inst->prepare (sampleRate, blockSize);
    entry->instrument = std::move (inst);
    entry->poolKey = key.poolKey();
    entry->loans = loaned ? 1 : 0;
    entry->lastUsed = juce::Time::getMillisecondCounter();
    auto* raw = entry->instrument.get();
    entries.push_back (std::move (entry));
    return raw;
}

//==============================================================================
std::unique_ptr<Instrument> InstrumentPool::createSyncNoLoan (const InstrumentKey& key, juce::String& error)
{
    switch (key.type)
    {
        case InstrumentKey::Type::builtIn:
            if (key.ref.getIntValue() == BuiltInDrums::presetIndex)
                return std::make_unique<BuiltInDrums>();
            return std::make_unique<BuiltInSynth> (key.ref.getIntValue());

        case InstrumentKey::Type::sfz:
        {
            auto sfz = std::make_unique<SfzInstrument>();
            if (! sfz->load (juce::File (key.ref), error))
                return nullptr;
            return sfz;
        }

        case InstrumentKey::Type::plugin:
        {
            auto desc = knownPlugins.getTypeForIdentifierString (key.ref);
            if (desc == nullptr)
            {
                error = "Plug-in niet gevonden: " + key.displayName + " (scan je plug-ins opnieuw).";
                return nullptr;
            }
            auto instance = formatManager.createPluginInstance (*desc, sampleRate, blockSize, error);
            if (instance == nullptr)
                return nullptr;
            auto plugin = std::make_unique<PluginInstrument> (std::move (instance));
            plugin->setStateBase64 (key.pluginState);
            return plugin;
        }

        case InstrumentKey::Type::none:
            break;
    }

    error = "Geen klank gekozen.";
    return nullptr;
}

Instrument* InstrumentPool::acquireSync (const InstrumentKey& key, juce::String& error)
{
    if (auto* idle = findIdle (key.poolKey()))
    {
        idle->loans = 1;
        idle->lastUsed = juce::Time::getMillisecondCounter();
        return idle->instrument.get();
    }

    auto inst = createSyncNoLoan (key, error);
    return inst != nullptr ? addLoaned (std::move (inst), key, true) : nullptr;
}

void InstrumentPool::acquire (const InstrumentKey& key, Callback callback)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (auto* idle = findIdle (key.poolKey()))
    {
        idle->loans = 1;
        idle->lastUsed = juce::Time::getMillisecondCounter();
        callback (idle->instrument.get(), {});
        return;
    }

    createAsync (key, true, std::move (callback));
}

void InstrumentPool::createAsync (const InstrumentKey& key, bool loaned, Callback callback)
{
    juce::WeakReference<InstrumentPool> weakThis (this);

    auto finish = [weakThis, key, loaned, callback] (std::unique_ptr<Instrument> inst, juce::String error)
    {
        if (weakThis == nullptr)
            return;

        auto& self = *weakThis;
        --self.numLoading;

        Instrument* raw = nullptr;
        if (inst != nullptr)
            raw = self.addLoaned (std::move (inst), key, loaned);

        if (callback)
            callback (raw, raw != nullptr ? juce::String() : error);
    };

    ++numLoading;

    switch (key.type)
    {
        case InstrumentKey::Type::sfz:
        {
            // Reading samples can take seconds: do it on a loader thread.
            loaders.addJob ([key, finish]
            {
                juce::String error;
                auto sfz = std::make_unique<SfzInstrument>();
                std::unique_ptr<Instrument> result;
                if (sfz->load (juce::File (key.ref), error))
                    result = std::move (sfz);

                // std::function needs a copyable callable, so pass ownership via a raw pointer.
                auto* rawResult = result.release();
                juce::MessageManager::callAsync ([finish, rawResult, error]
                {
                    finish (std::unique_ptr<Instrument> (rawResult), error);
                });
            });
            return;
        }

        case InstrumentKey::Type::plugin:
        {
            auto desc = knownPlugins.getTypeForIdentifierString (key.ref);
            if (desc == nullptr)
            {
                finish (nullptr, "Plug-in niet gevonden: " + key.displayName + " (scan je plug-ins opnieuw).");
                return;
            }

            formatManager.createPluginInstanceAsync (*desc, sampleRate, blockSize,
                [finish, key] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
                {
                    if (instance == nullptr)
                    {
                        finish (nullptr, error.isNotEmpty() ? error : juce::String ("Plug-in kon niet starten."));
                        return;
                    }
                    auto plugin = std::make_unique<PluginInstrument> (std::move (instance));
                    plugin->setStateBase64 (key.pluginState);
                    finish (std::move (plugin), {});
                });
            return;
        }

        case InstrumentKey::Type::builtIn:
        case InstrumentKey::Type::none:
        {
            juce::String error;
            auto inst = createSyncNoLoan (key, error);
            finish (std::move (inst), error);
            return;
        }
    }
}

//==============================================================================
void InstrumentPool::release (Instrument* inst)
{
    if (inst == nullptr)
        return;

    if (auto* e = findEntry (inst))
    {
        jassert (e->loans > 0);
        e->loans = juce::jmax (0, e->loans - 1);
        e->lastUsed = juce::Time::getMillisecondCounter();
    }
    else
    {
        jassertfalse; // not one of ours
    }
}

void InstrumentPool::preload (const juce::Array<InstrumentKey>& keys)
{
    for (auto& key : keys)
    {
        if (key.isEmpty() || findIdle (key.poolKey()) != nullptr)
            continue;
        createAsync (key, false, nullptr);
    }
}

void InstrumentPool::trimIdle (const juce::StringArray& keep, int maxIdle)
{
    // Destroy idle instances that no performance needs.
    entries.erase (std::remove_if (entries.begin(), entries.end(),
                                   [&] (const std::unique_ptr<Entry>& e)
                                   { return e->loans == 0 && ! keep.contains (e->poolKey); }),
                   entries.end());

    // Then cap the number of idle instances, oldest first.
    std::vector<Entry*> idle;
    for (auto& e : entries)
        if (e->loans == 0)
            idle.push_back (e.get());

    if ((int) idle.size() <= maxIdle)
        return;

    std::sort (idle.begin(), idle.end(), [] (Entry* a, Entry* b) { return a->lastUsed < b->lastUsed; });
    const auto toRemove = idle.size() - (size_t) maxIdle;
    for (size_t i = 0; i < toRemove; ++i)
    {
        auto* victim = idle[i];
        entries.erase (std::remove_if (entries.begin(), entries.end(),
                                       [victim] (const std::unique_ptr<Entry>& e) { return e.get() == victim; }),
                       entries.end());
    }
}

void InstrumentPool::updateKey (Instrument* inst, const InstrumentKey& newKey)
{
    if (auto* e = findEntry (inst))
    {
        inst->key = newKey;
        e->poolKey = newKey.poolKey();
    }
}

} // namespace km
