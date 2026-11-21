#include "App/AppController.h"

#include "Core/AppPaths.h"
#include "Engine/BuiltInSynth.h"
#include "Plugins/PluginScanner.h"

namespace km
{
namespace
{
    float normToDb (float norm) noexcept
    {
        return norm <= 0.001f ? -80.0f : juce::jmap (std::sqrt (norm), -60.0f, 6.0f);
    }

    constexpr int autosaveIntervalMs = 2 * 60 * 1000;
} // namespace

AppController::AppController()
{
    juce::addDefaultFormatsToManager (formatManager);

    juce::PropertiesFile::Options opts;
    opts.applicationName = "KlankModule";
    opts.filenameSuffix = ".xml";
    opts.storageFormat = juce::PropertiesFile::storeAsXML;
    settings = std::make_unique<juce::PropertiesFile> (paths::settingsFile(), opts);
}

AppController::~AppController()
{
    shutdown();
}

//==============================================================================
void AppController::initialise()
{
    JUCE_ASSERT_MESSAGE_THREAD

    // Saved state
    if (auto xml = juce::parseXML (paths::pluginListFile()))
        knownPlugins.recreateFromXml (*xml);
    knownPlugins.setCustomScanner (std::make_unique<OutOfProcessScanner>());

    learn.fromJson (paths::midiLearnFile().loadFileAsString());
    programMap.fromJson (paths::programMapFile().loadFileAsString());

    engine.controlChannel.store (settings->getIntValue ("controlChannel", 16));
    gmMode = settings->getBoolValue ("gmMode", false);
    preloadAll = settings->getBoolValue ("preloadAll", true);

    registerLearnParameters();
    learn.onLearned = [this] (const juce::String& id)
    {
        paths::midiLearnFile().replaceWithText (learn.toJson());
        setStatus ("MIDI Learn: " + learn.getDisplayName (id) + " gekoppeld aan " + learn.describeMapping (id));
        if (onMidiLearned)
            onMidiLearned (id);
    };

    // MIDI
    router.setDisabledDevices (juce::StringArray::fromLines (settings->getValue ("disabledMidiInputs")));
    router.onDevicesChanged = [this]
    {
        resolveInputSlots();
        if (onDevicesChanged)
            onDevicesChanged();
    };
    router.onDeviceRemoved = [this]
    {
        engine.requestPanic();
        setStatus ("MIDI-apparaat losgekoppeld: alle noten uit.");
    };
    router.start();

    // Audio
    engine.onAboutToStart = [this] (double sr, int bs) { pool.setAudioConfig (sr, bs); };
    engine.onDeviceError = [this] (const juce::String& message)
    {
        deviceErrorPending = true;
        juce::MessageManager::callAsync ([weak = juce::WeakReference<AppController> (this), message]
        {
            if (weak != nullptr)
                weak->setStatus ("Audiofout: " + message + " (probeer opnieuw te starten...)");
        });
    };
    initialiseAudio();

    // Performance: restore the last session (autosave) so nothing is ever lost.
    rescanPerformanceFolder();
    currentFile = juce::File (settings->getValue ("currentFile"));
    Performance restored;
    juce::String error;
    if (restored.loadFrom (paths::autosaveFile(), error))
    {
        applyPerformance (restored);
        dirty = settings->getBoolValue ("dirty", false);
    }
    else if (currentFile.existsAsFile() && restored.loadFrom (currentFile, error))
    {
        applyPerformance (restored);
    }
    else
    {
        currentFile = juce::File();
        applyPerformance (Performance::makeDefault());
    }

    initialised = true;
    lastAutosave = juce::Time::getMillisecondCounter();
    startTimerHz (30);
}

void AppController::initialiseAudio()
{
    std::unique_ptr<juce::XmlElement> saved (settings->getXmlValue ("audioDevice"));

    juce::AudioDeviceManager::AudioDeviceSetup preferred;
    preferred.bufferSize = 128;
    preferred.sampleRate = 48000.0;

    juce::String preferredType;
    if (saved == nullptr)
    {
        // First start: prefer ASIO when a driver is installed, otherwise Windows Audio.
        for (auto* type : deviceManager.getAvailableDeviceTypes())
        {
            type->scanForDevices();
            if (type->getTypeName() == "ASIO" && type->getDeviceNames().size() > 0)
                preferredType = "ASIO";
        }
    }

    const auto error = deviceManager.initialise (0, 2, saved.get(), true, preferredType, &preferred);
    if (error.isNotEmpty())
        setStatus ("Audio kon niet starten: " + error);

    deviceManager.addAudioCallback (&engine);
    deviceManager.addChangeListener (this);
}

void AppController::shutdown()
{
    if (! initialised)
        return;
    initialised = false;
    stopTimer();

    writeAutosave();
    settings->setValue ("disabledMidiInputs", router.getDisabledDevices().joinIntoString ("\n"));
    if (auto xml = knownPlugins.createXml())
        xml->writeTo (paths::pluginListFile());
    paths::midiLearnFile().replaceWithText (learn.toJson());
    settings->saveIfNeeded();

    router.stop();
    deviceManager.removeChangeListener (this);
    deviceManager.removeAudioCallback (&engine);
    deviceManager.closeAudioDevice();

    // Audio is stopped: take every instrument back from the parts.
    juce::Array<Instrument*> back;
    for (int i = 0; i < kNumParts; ++i)
        engine.getPart (i).reclaimAll (back);
    while (auto released = engine.releaseQueue.pop())
        back.add (*released);
    for (auto* inst : back)
        pool.release (inst);
    assigned.fill (nullptr);
}

//==============================================================================
void AppController::registerLearnParameters()
{
    for (int i = 0; i < kNumParts; ++i)
    {
        const auto prefix = "part" + juce::String (i + 1) + ".";
        const auto label = "Part " + juce::String (i + 1) + " ";
        auto& p = engine.getPart (i).params;

        learn.registerParameter (prefix + "volume", label + "volume",
                                 [this, &p] (float v) { p.volumeDb = normToDb (v); learnChangedSomething = true; });
        learn.registerParameter (prefix + "pan", label + "pan",
                                 [this, &p] (float v) { p.pan = v * 2.0f - 1.0f; learnChangedSomething = true; });
        learn.registerParameter (prefix + "reverb", label + "galm",
                                 [this, &p] (float v) { p.reverbSend = v; learnChangedSomething = true; });
        learn.registerParameter (prefix + "delay", label + "delay",
                                 [this, &p] (float v) { p.delaySend = v; learnChangedSomething = true; });
        learn.registerParameter (prefix + "mute", label + "mute",
                                 [this, &p] (float) { p.mute = ! p.mute.load(); learnChangedSomething = true; }, true);
    }

    auto& m = engine.master;
    learn.registerParameter ("master.volume", "Master volume",
                             [this, &m] (float v) { m.volumeDb = normToDb (v); learnChangedSomething = true; });
    learn.registerParameter ("fx.reverbSize", "Galm: ruimte",
                             [this, &m] (float v) { m.reverbSize = v; learnChangedSomething = true; });
    learn.registerParameter ("fx.delayFeedback", "Delay: herhalingen",
                             [this, &m] (float v) { m.delayFeedback = v * 0.95f; learnChangedSomething = true; });

    auto weak = juce::WeakReference<AppController> (this);
    learn.registerParameter ("action.nextPerformance", "Volgende performance",
                             [weak] (float) { juce::MessageManager::callAsync ([weak] { if (weak) weak->nextPerformance(); }); }, true);
    learn.registerParameter ("action.previousPerformance", "Vorige performance",
                             [weak] (float) { juce::MessageManager::callAsync ([weak] { if (weak) weak->previousPerformance(); }); }, true);
    learn.registerParameter ("action.panic", "Panic",
                             [this] (float) { engine.requestPanic(); }, true);
}

//==============================================================================
void AppController::timerCallback()
{
    // Instruments the audio thread no longer uses.
    while (auto released = engine.releaseQueue.pop())
        pool.release (*released);

    // Program changes that need a decision.
    while (auto req = engine.programRequests.pop())
        handleProgramRequest (*req);

    if (learnChangedSomething.exchange (false))
        dirty = true;

    const auto now = juce::Time::getMillisecondCounter();

    // Recover from audio dropouts / unplugged interfaces.
    if (deviceErrorPending || ! isAudioRunning())
    {
        if (now - lastRestartAttempt > 3000 && deviceManager.getCurrentAudioDevice() != nullptr)
        {
            lastRestartAttempt = now;
            deviceErrorPending = false;
            deviceManager.restartLastAudioDevice();
            if (isAudioRunning())
                setStatus ("Audio hersteld.");
        }
    }

    if (now - lastAutosave > (juce::uint32) autosaveIntervalMs)
    {
        lastAutosave = now;
        writeAutosave();
    }
}

void AppController::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Audio device settings changed: remember them.
    if (auto xml = deviceManager.createStateXml())
        settings->setValue ("audioDevice", xml.get());
    settings->saveIfNeeded();
}

bool AppController::isAudioRunning() const
{
    auto* device = deviceManager.getCurrentAudioDevice();
    return device != nullptr && device->isPlaying();
}

int AppController::getXRunCount() const
{
    auto* device = deviceManager.getCurrentAudioDevice();
    return device != nullptr ? device->getXRunCount() : 0;
}

void AppController::setStatus (const juce::String& message)
{
    statusMessage = message;
    juce::Logger::writeToLog (message);
    if (onStatus)
        onStatus (message);
}

//==============================================================================
void AppController::applyPartRealtime (int i)
{
    auto& c = performance.parts[(size_t) i];
    auto& p = engine.getPart (i).params;
    p.enabled = c.enabled;
    p.channel = c.channel;
    p.volumeDb = c.volumeDb;
    p.pan = c.pan;
    p.transpose = c.transpose;
    p.keyLow = c.keyLow;
    p.keyHigh = c.keyHigh;
    p.velLow = c.velLow;
    p.velHigh = c.velHigh;
    p.velocityCurve = (int) c.velocityCurve;
    p.fixedVelocity = c.fixedVelocity;
    p.reverbSend = c.reverbSend;
    p.delaySend = c.delaySend;
    p.mute = c.mute;
    p.solo = c.solo;
}

void AppController::captureRealtimeIntoModel()
{
    for (int i = 0; i < kNumParts; ++i)
    {
        auto& c = performance.parts[(size_t) i];
        auto& p = engine.getPart (i).params;
        c.volumeDb = p.volumeDb;
        c.pan = p.pan;
        c.reverbSend = p.reverbSend;
        c.delaySend = p.delaySend;
        c.mute = p.mute;
        c.solo = p.solo;

        // Plug-in sound settings live inside the plug-in: capture them.
        if (auto* inst = assigned[(size_t) i]; inst != nullptr && inst->key.type == InstrumentKey::Type::plugin)
        {
            auto key = inst->key;
            key.pluginState = inst->getStateBase64();
            pool.updateKey (inst, key);
            c.instrument = key;
        }
    }

    auto& m = engine.master;
    performance.master.volumeDb = m.volumeDb;
    performance.master.reverbSize = m.reverbSize;
    performance.master.reverbDamping = m.reverbDamping;
    performance.master.delayMs = m.delayMs;
    performance.master.delayFeedback = m.delayFeedback;
    performance.master.limiterThresholdDb = m.limiterThresholdDb;
}

void AppController::applyPerformance (const Performance& p)
{
    performance = p;

    for (int i = 0; i < kNumParts; ++i)
    {
        applyPartRealtime (i);
        const auto& key = performance.parts[(size_t) i].instrument;

        // Same sound already playing on this part: keep it (no reload, held notes continue).
        auto* current = assigned[(size_t) i];
        if (current != nullptr && ! key.isEmpty() && current->key.poolKey() == key.poolKey())
            continue;

        setPartInstrument (i, key);
    }

    auto& m = engine.master;
    m.volumeDb = performance.master.volumeDb;
    m.reverbSize = performance.master.reverbSize;
    m.reverbDamping = performance.master.reverbDamping;
    m.delayMs = performance.master.delayMs;
    m.delayFeedback = performance.master.delayFeedback;
    m.limiterThresholdDb = performance.master.limiterThresholdDb;

    resolveInputSlots();
    dirty = false;
    updatePreloads();

    if (onPerformanceChanged)
        onPerformanceChanged();
}

void AppController::resolveInputSlots()
{
    for (int i = 0; i < kNumParts; ++i)
    {
        const auto& id = performance.parts[(size_t) i].inputDeviceId;
        engine.getPart (i).params.inputSlot = id.isEmpty() ? -1 : router.getSlotFor (id);
    }
}

//==============================================================================
void AppController::lendToPart (int part, Instrument* inst)
{
    if (auto* notAdopted = engine.getPart (part).lend (inst))
        pool.release (notAdopted);
    // The previously adopted instrument (if any) comes back through engine.releaseQueue.
    assigned[(size_t) part] = inst;
}

void AppController::setPartInstrument (int part, const InstrumentKey& key)
{
    performance.parts[(size_t) part].instrument = key;
    const int ticket = ++loadTicket[(size_t) part];
    dirty = true;

    if (key.isEmpty())
    {
        loading[(size_t) part] = false;
        lendToPart (part, nullptr);
        if (onPartChanged) onPartChanged (part);
        return;
    }

    loading[(size_t) part] = true;
    if (onPartChanged) onPartChanged (part);

    auto weak = juce::WeakReference<AppController> (this);
    pool.acquire (key, [weak, part, ticket, key] (Instrument* inst, const juce::String& error)
    {
        if (weak == nullptr)
            return;
        auto& self = *weak;

        if (ticket != self.loadTicket[(size_t) part])
        {
            // The user picked something else meanwhile.
            self.pool.release (inst);
            return;
        }

        self.loading[(size_t) part] = false;
        if (inst == nullptr)
        {
            self.setStatus ("Part " + juce::String (part + 1) + ": " + error);
        }
        else
        {
            self.lendToPart (part, inst);
            // Keep the user's chosen name if the instrument has none of its own.
            if (self.performance.parts[(size_t) part].instrument.displayName.isEmpty())
                self.performance.parts[(size_t) part].instrument.displayName = inst->getName();
        }

        if (self.onPartChanged)
            self.onPartChanged (part);
    });
}

void AppController::setPartEnabled (int part, bool enabled)
{
    performance.parts[(size_t) part].enabled = enabled;
    engine.getPart (part).params.enabled = enabled;
    dirty = true;
    if (onPartChanged) onPartChanged (part);
}

void AppController::setPartInput (int part, const juce::String& deviceId, const juce::String& deviceName)
{
    performance.parts[(size_t) part].inputDeviceId = deviceId;
    performance.parts[(size_t) part].inputDeviceName = deviceName;
    resolveInputSlots();
    dirty = true;
    if (onPartChanged) onPartChanged (part);
}

void AppController::setPartChannel (int part, int channel)
{
    performance.parts[(size_t) part].channel = juce::jlimit (0, 16, channel);
    engine.getPart (part).params.channel = performance.parts[(size_t) part].channel;
    dirty = true;
}

void AppController::setPartTranspose (int part, int semitones)
{
    performance.parts[(size_t) part].transpose = juce::jlimit (-48, 48, semitones);
    engine.getPart (part).params.transpose = performance.parts[(size_t) part].transpose;
    dirty = true;
}

void AppController::setPartKeyRange (int part, int low, int high)
{
    auto& c = performance.parts[(size_t) part];
    c.keyLow = juce::jlimit (0, 127, low);
    c.keyHigh = juce::jlimit (0, 127, high);
    engine.getPart (part).params.keyLow = c.keyLow;
    engine.getPart (part).params.keyHigh = c.keyHigh;
    dirty = true;
}

void AppController::setPartVelocityRange (int part, int low, int high)
{
    auto& c = performance.parts[(size_t) part];
    c.velLow = juce::jlimit (1, 127, low);
    c.velHigh = juce::jlimit (1, 127, high);
    engine.getPart (part).params.velLow = c.velLow;
    engine.getPart (part).params.velHigh = c.velHigh;
    dirty = true;
}

void AppController::setPartVelocityCurve (int part, filter::VelocityCurve curve)
{
    performance.parts[(size_t) part].velocityCurve = curve;
    engine.getPart (part).params.velocityCurve = (int) curve;
    dirty = true;
}

//==============================================================================
void AppController::handleProgramRequest (const ProgramRequest& req)
{
    if (req.kind == ProgramRequest::performanceSelect)
    {
        selectPerformance (req.program);
        return;
    }

    const int part = req.part;
    if (auto* mapped = programMap.find (req.bank, req.program))
    {
        setPartInstrument (part, *mapped);
        return;
    }

    auto* inst = assigned[(size_t) part];
    if (inst != nullptr && inst->key.type != InstrumentKey::Type::builtIn)
        return; // plug-ins / SFZ handle Program Change themselves

    const int preset = gmMode ? ProgramMap::gmFamilyToBuiltInPreset (req.program)
                              : req.program % BuiltInSynth::getNumPresets();
    const auto key = InstrumentKey::builtIn (preset, BuiltInSynth::getPresetName (preset));

    if (auto* synth = dynamic_cast<BuiltInSynth*> (inst))
    {
        synth->setPreset (preset); // instant, applies to the next note
        pool.updateKey (synth, key);
        performance.parts[(size_t) part].instrument = key;
        dirty = true;
        if (onPartChanged) onPartChanged (part);
    }
    else
    {
        setPartInstrument (part, key);
    }
}

//==============================================================================
void AppController::rescanPerformanceFolder()
{
    preloadsDirty = true;
    performanceFiles = paths::performancesDir().findChildFiles (juce::File::findFiles, false, "*.kmperf");
    std::sort (performanceFiles.begin(), performanceFiles.end(),
               [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
}

int AppController::getCurrentPerformanceIndex() const
{
    return performanceFiles.indexOf (currentFile);
}

void AppController::newPerformance()
{
    currentFile = juce::File();
    applyPerformance (Performance::makeDefault());
    performance.name = "Nieuwe performance";
    dirty = true;
    if (onPerformanceChanged) onPerformanceChanged();
}

bool AppController::savePerformance()
{
    if (currentFile == juce::File())
        return false;
    return savePerformanceAs (currentFile, performance.name);
}

bool AppController::savePerformanceAs (const juce::File& file, const juce::String& name)
{
    captureRealtimeIntoModel();
    performance.name = name.isNotEmpty() ? name : file.getFileNameWithoutExtension();

    if (! performance.saveTo (file))
    {
        setStatus ("Opslaan mislukt: " + file.getFullPathName());
        return false;
    }

    currentFile = file;
    dirty = false;
    settings->setValue ("currentFile", currentFile.getFullPathName());
    rescanPerformanceFolder();
    preloadsDirty = true;
    updatePreloads();
    writeAutosave();
    setStatus ("Opgeslagen: " + performance.name);
    if (onPerformanceChanged) onPerformanceChanged();
    return true;
}

bool AppController::loadPerformance (const juce::File& file)
{
    Performance p;
    juce::String error;
    if (! p.loadFrom (file, error))
    {
        setStatus (error);
        return false;
    }

    currentFile = file;
    settings->setValue ("currentFile", currentFile.getFullPathName());
    applyPerformance (p);
    setStatus ("Performance: " + performance.name);
    return true;
}

void AppController::selectPerformance (int index)
{
    if (juce::isPositiveAndBelow (index, performanceFiles.size()))
        loadPerformance (performanceFiles[index]);
}

void AppController::nextPerformance()
{
    if (performanceFiles.isEmpty()) return;
    const int i = getCurrentPerformanceIndex();
    selectPerformance (i < 0 ? 0 : (i + 1) % performanceFiles.size());
}

void AppController::previousPerformance()
{
    if (performanceFiles.isEmpty()) return;
    const int i = getCurrentPerformanceIndex();
    selectPerformance (i <= 0 ? performanceFiles.size() - 1 : i - 1);
}

void AppController::updatePreloads()
{
    // With "pre-load all", the set only changes when performances are saved or the folder changes.
    // Skipping it keeps live performance switching fast.
    if (preloadAll && ! preloadsDirty)
        return;
    preloadsDirty = false;

    // Pre-load the sounds of every saved performance, so switching takes milliseconds.
    juce::StringArray keep;
    juce::Array<InstrumentKey> wanted;

    for (auto& part : performance.parts)
        if (! part.instrument.isEmpty())
            keep.add (part.instrument.poolKey());

    if (preloadAll)
    {
        for (auto& f : performanceFiles)
        {
            Performance p;
            juce::String err;
            if (! p.loadFrom (f, err))
                continue;
            for (auto& part : p.parts)
                if (part.enabled && ! part.instrument.isEmpty())
                {
                    keep.add (part.instrument.poolKey());
                    wanted.add (part.instrument);
                }
        }
    }

    pool.trimIdle (keep, preloadAll ? 64 : 4);
    pool.preload (wanted);
}

void AppController::writeAutosave()
{
    captureRealtimeIntoModel();
    performance.saveTo (paths::autosaveFile());
    settings->setValue ("dirty", dirty);
    settings->setValue ("currentFile", currentFile.getFullPathName());
    settings->saveIfNeeded();
}

//==============================================================================
void AppController::panic()
{
    engine.requestPanic();
    setStatus ("Panic: alle noten uit.");
}

void AppController::setControlChannel (int channel)
{
    engine.controlChannel = juce::jlimit (0, 16, channel);
    settings->setValue ("controlChannel", engine.controlChannel.load());
}

void AppController::setGeneralMidiMode (bool on)
{
    gmMode = on;
    settings->setValue ("gmMode", on);
}

void AppController::setPreloadAll (bool on)
{
    preloadAll = on;
    settings->setValue ("preloadAll", on);
    preloadsDirty = true;
    updatePreloads();
}

} // namespace km
