#pragma once

#include "Engine/Instrument.h"

#include <functional>
#include <memory>
#include <vector>

namespace km
{
/**
    Creates, caches and owns all instrument instances (message thread only).

    An instance is either idle (in the pool, may be pre-loaded for a quick performance
    switch) or loaned to exactly one part. Instances are never destroyed while loaned.
    SFZ files load on background threads; plug-ins are created asynchronously.
*/
class InstrumentPool
{
public:
    using Callback = std::function<void (Instrument* instrument, const juce::String& error)>;

    InstrumentPool (juce::AudioPluginFormatManager& formats, juce::KnownPluginList& knownPlugins);
    ~InstrumentPool();

    /** Prepare every instance for a new sample rate / block size. Audio must be stopped. */
    void setAudioConfig (double sampleRate, int blockSize);

    /** Get an instance for key, marked as loaned. Callback runs on the message thread
        (immediately if a matching idle instance exists). */
    void acquire (const InstrumentKey& key, Callback callback);

    /** Blocking variant for offline rendering / tests. Instance is marked as loaned. */
    Instrument* acquireSync (const InstrumentKey& key, juce::String& error);

    /** Give a loaned instance back (it stays cached as idle). */
    void release (Instrument* instrument);

    /** Make sure an idle instance exists for each key (background loading). */
    void preload (const juce::Array<InstrumentKey>& keys);

    /** Destroy idle instances whose key is not in keep, oldest first, until at most maxIdle remain. */
    void trimIdle (const juce::StringArray& keepPoolKeys, int maxIdle);

    /** Call after changing a live instance (preset change, plug-in state captured). */
    void updateKey (Instrument* instrument, const InstrumentKey& newKey);

    int getNumLoading() const noexcept { return numLoading; }
    int getNumInstances() const noexcept { return (int) entries.size(); }

private:
    struct Entry
    {
        std::unique_ptr<Instrument> instrument;
        juce::String poolKey;
        int loans = 0;
        juce::uint32 lastUsed = 0;
    };

    Entry* findEntry (Instrument*) noexcept;
    Entry* findIdle (const juce::String& poolKey) noexcept;
    Instrument* addLoaned (std::unique_ptr<Instrument> instrument, const InstrumentKey& key, bool loaned);
    std::unique_ptr<Instrument> createSyncNoLoan (const InstrumentKey& key, juce::String& error);
    void createAsync (const InstrumentKey& key, bool loaned, Callback callback);

    juce::AudioPluginFormatManager& formatManager;
    juce::KnownPluginList& knownPlugins;
    std::vector<std::unique_ptr<Entry>> entries;
    juce::ThreadPool loaders { juce::ThreadPoolOptions{}.withNumberOfThreads (2).withThreadName ("KM loader") };

    double sampleRate = 48000.0;
    int blockSize = 512;
    int numLoading = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (InstrumentPool)
    JUCE_DECLARE_NON_COPYABLE (InstrumentPool)
};

} // namespace km
