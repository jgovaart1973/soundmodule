#pragma once

#include "Engine/InstrumentPool.h"
#include "Engine/ModuleEngine.h"
#include "Midi/MidiLearn.h"
#include "Midi/MidiOutHub.h"
#include "Midi/MidiRouter.h"
#include "Model/Performance.h"
#include "Model/ProgramMap.h"
#include "App/SongController.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace km
{
/**
    Message-thread "brain" of the application. Owns the audio device, MIDI router,
    engine, instrument pool and the current performance, and keeps them in sync.
    The UI only talks to this class.
*/
class AppController final : private juce::Timer,
                            private juce::ChangeListener
{
public:
    AppController();
    ~AppController() override;

    void initialise();
    void shutdown();

    //== parts =================================================================
    const Performance& getPerformance() const noexcept { return performance; }
    PartParams& partParams (int part) noexcept         { return engine.getPart (part).params; }
    MasterParams& masterParams() noexcept              { return engine.master; }
    Instrument* getAssignedInstrument (int part) const noexcept { return assigned[(size_t) part]; }
    bool isPartLoading (int part) const noexcept       { return loading[(size_t) part]; }

    void setPartEnabled (int part, bool enabled);
    void setPartInput (int part, const juce::String& deviceId, const juce::String& deviceName);
    void setPartChannel (int part, int channel);
    void setPartInstrument (int part, const InstrumentKey& key);
    void setPartTranspose (int part, int semitones);
    void setPartKeyRange (int part, int low, int high);
    void setPartVelocityRange (int part, int low, int high);
    void setPartVelocityCurve (int part, filter::VelocityCurve curve);

    /** Called by the UI after it changed a realtime parameter (volume, pan, sends, mute, solo, master). */
    void markDirty() noexcept { dirty = true; }
    bool isDirty() const noexcept { return dirty; }

    //== performances ==========================================================
    void newPerformance();
    bool savePerformance();                       ///< to the current file (asks UI for a name if none)
    bool savePerformanceAs (const juce::File& file, const juce::String& name);
    bool loadPerformance (const juce::File& file);
    void selectPerformance (int indexInFolder);
    void nextPerformance();
    void previousPerformance();
    juce::Array<juce::File> getPerformanceFiles() const { return performanceFiles; }
    int getCurrentPerformanceIndex() const;
    juce::File getCurrentFile() const { return currentFile; }
    void rescanPerformanceFolder();

    /** Current sounds incl. live knob positions and plug-in states (for saving into songs). */
    Performance capturePerformance();
    /** Loading a song: use its sounds, detached from any .kmperf file. */
    void applyPerformanceFromSong (const Performance& p);
    SongController& getSongController() noexcept { return *songController; }

    //== global ================================================================
    void panic();
    void playTestTone() { engine.startTestTone(); }

    /** Play a note on a part for a moment (piano roll preview). */
    void previewNote (int part, int note, int velocity = 100, int channel = 1, int durationMs = 350);

    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    MidiRouter& getMidiRouter() noexcept                  { return router; }
    MidiLearn& getMidiLearn() noexcept                    { return learn; }
    ModuleEngine& getEngine() noexcept                    { return engine; }
    juce::AudioPluginFormatManager& getFormatManager() noexcept { return formatManager; }
    juce::KnownPluginList& getKnownPlugins() noexcept     { return knownPlugins; }
    juce::PropertiesFile& getSettings() noexcept          { return *settings; }
    InstrumentPool& getPool() noexcept                    { return pool; }
    MidiOutHub& getMidiOutHub() noexcept                  { return midiOutHub; }

    /** MIDI clock / transport to these output devices (identifiers); stored in the settings. */
    void setMidiClockOutputs (const juce::StringArray& identifiers);
    /** Extra delay for all hardware MIDI output, in ms (can be negative). */
    void setMidiOutOffsetMs (double ms);
    /** MIDI 2.0 input (UMP) and per-note expression -> MPE; stored in the settings. */
    void setMidi2Input (bool useUmp, bool perNoteAsMpe);
    /** Send a .syx file to an output device. @returns false with an error message. */
    bool sendSysExFile (const juce::String& identifier, const juce::File& file, juce::String& error);

    int getControlChannel() const noexcept { return engine.controlChannel.load(); }
    void setControlChannel (int channel);
    bool isGeneralMidiMode() const noexcept { return gmMode; }
    void setGeneralMidiMode (bool on);
    bool isPreloadAll() const noexcept { return preloadAll; }
    void setPreloadAll (bool on);

    /** Last status / error message for the status bar. */
    juce::String getStatusMessage() const { return statusMessage; }
    bool isAudioRunning() const;
    /** Input + output latency of the audio device, used to line up audio recordings. */
    int getRoundTripLatencySamples() const;
    int getXRunCount() const;

    //== notifications (message thread) =======================================
    std::function<void()> onPerformanceChanged;   ///< whole performance replaced / renamed / saved
    std::function<void (int part)> onPartChanged; ///< a part's structure changed (instrument, input...)
    std::function<void()> onDevicesChanged;
    std::function<void (const juce::String&)> onStatus;
    std::function<void (const juce::String& paramId)> onMidiLearned;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void applyPerformance (const Performance& p);
    void applyPartRealtime (int part);
    void captureRealtimeIntoModel();
    void lendToPart (int part, Instrument* instrument);
    void handleProgramRequest (const ProgramRequest& request);
    void resolveInputSlots();
    void registerLearnParameters();
    void setStatus (const juce::String& message);
    void writeAutosave();
    void updatePreloads();
    void initialiseAudio();

    // Order matters: the engine must outlive the device manager callback, and the pool
    // must outlive the parts that borrow from it.
    MidiEventQueue midiQueue;
    MidiLearn learn;
    MidiRouter router { midiQueue, learn };
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    MidiOutHub midiOutHub;   ///< outlives the pool (hardware "sounds" point to it) and the engine
    InstrumentPool pool { formatManager, knownPlugins };
    ModuleEngine engine { &midiQueue };
    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<juce::PropertiesFile> settings;
    ProgramMap programMap;
    std::unique_ptr<SongController> songController;

    Performance performance;
    juce::File currentFile;
    juce::Array<juce::File> performanceFiles;
    bool dirty = false;
    bool gmMode = false;
    bool preloadAll = true;
    bool preloadsDirty = true;   ///< saved performances changed: recompute what to pre-load

    std::array<Instrument*, kNumParts> assigned {};
    std::array<int, kNumParts> loadTicket {};
    std::array<bool, kNumParts> loading {};

    std::atomic<bool> learnChangedSomething { false };
    std::atomic<bool> deviceErrorPending { false };
    juce::uint32 lastAutosave = 0, lastRestartAttempt = 0;
    juce::String statusMessage;
    bool initialised = false;

    JUCE_DECLARE_WEAK_REFERENCEABLE (AppController)
    JUCE_DECLARE_NON_COPYABLE (AppController)
};

} // namespace km
