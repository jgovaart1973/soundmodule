#include "Plugins/ClapHosting.h"

#include "Core/SpscQueue.h"

#include <clap/clap.h>

#include <map>
#include <unordered_map>

#if JUCE_WINDOWS
 #include <juce_gui_extra/juce_gui_extra.h>
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace km
{
namespace
{
    //==========================================================================
    /** A loaded .clap file. Shared by every instance from that file; deinit when the last one goes. */
    struct ClapModule
    {
        juce::DynamicLibrary lib;
        const clap_plugin_entry* entry = nullptr;
        const clap_plugin_factory* factory = nullptr;
        juce::String path;

        ~ClapModule()
        {
            if (entry != nullptr)
                entry->deinit();
            lib.close();
        }

        static juce::File binaryFor (const juce::File& f)
        {
           #if JUCE_MAC
            if (f.isDirectory())
                return f.getChildFile ("Contents/MacOS").getChildFile (f.getFileNameWithoutExtension());
           #endif
            return f;
        }

        static std::shared_ptr<ClapModule> open (const juce::String& path, juce::String& error)
        {
            JUCE_ASSERT_MESSAGE_THREAD
            static std::map<juce::String, std::weak_ptr<ClapModule>> registry;
            if (auto existing = registry[path].lock())
                return existing;

            auto m = std::make_shared<ClapModule>();
            m->path = path;
            if (! m->lib.open (binaryFor (juce::File (path)).getFullPathName()))
            {
                error = "CLAP-bestand kan niet geladen worden: " + path;
                return nullptr;
            }
            auto* entry = reinterpret_cast<const clap_plugin_entry*> (m->lib.getFunction ("clap_entry"));
            if (entry == nullptr || ! clap_version_is_compatible (entry->clap_version))
            {
                error = "Geen (geschikte) CLAP-plug-in: " + path;
                return nullptr;
            }
            if (! entry->init (path.toRawUTF8()))
            {
                error = "CLAP-plug-in weigert te starten: " + path;
                return nullptr;
            }
            m->entry = entry;
            m->factory = static_cast<const clap_plugin_factory*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
            if (m->factory == nullptr)
            {
                error = "CLAP-bestand bevat geen plug-ins: " + path;
                return nullptr;
            }
            registry[path] = m;
            return m;
        }
    };

    int uidFor (const char* clapId) { return juce::String (clapId).hashCode(); }

    juce::PluginDescription describe (const clap_plugin_descriptor& d, const juce::File& file, int countInFile)
    {
        juce::PluginDescription p;
        p.name = juce::String::fromUTF8 (d.name);
        p.descriptiveName = d.description != nullptr ? juce::String::fromUTF8 (d.description) : p.name;
        p.pluginFormatName = ClapPluginFormat::formatName;
        p.manufacturerName = d.vendor != nullptr ? juce::String::fromUTF8 (d.vendor) : juce::String();
        p.version = d.version != nullptr ? juce::String (d.version) : juce::String();
        p.fileOrIdentifier = file.getFullPathName();
        p.lastFileModTime = file.getLastModificationTime();
        p.lastInfoUpdateTime = juce::Time::getCurrentTime();
        p.uniqueId = p.deprecatedUid = uidFor (d.id);
        p.hasSharedContainer = countInFile > 1;
        p.numInputChannels = 0;
        p.numOutputChannels = 2;

        juce::StringArray features;
        if (d.features != nullptr)
            for (auto f = d.features; *f != nullptr; ++f)
                features.add (*f);
        p.isInstrument = features.contains (CLAP_PLUGIN_FEATURE_INSTRUMENT);
        p.category = p.isInstrument ? "Instrument" : features.joinIntoString ("|");
        return p;
    }

    //==========================================================================
    union ClapEvent
    {
        clap_event_header_t header;
        clap_event_note_t note;
        clap_event_midi_t midi;
        clap_event_param_value_t param;
    };

    struct ParamChange { clap_id id; void* cookie; double value; };

    class ClapInstance;

    //==========================================================================
    class ClapParameter final : public juce::HostedAudioProcessorParameter
    {
    public:
        ClapParameter (ClapInstance& o, const clap_param_info& i, double current)
            : owner (o), info (i), value (current) {}

        float getValue() const override             { return toNormal (value.load()); }
        void setValue (float normal) override;
        float getDefaultValue() const override      { return toNormal (info.default_value); }
        juce::String getName (int maxLen) const override { return juce::String::fromUTF8 (info.name).substring (0, maxLen); }
        juce::String getLabel() const override      { return {}; }
        juce::String getText (float normal, int maxLen) const override;
        float getValueForText (const juce::String& text) const override;
        bool isDiscrete() const override            { return (info.flags & CLAP_PARAM_IS_STEPPED) != 0; }
        bool isBoolean() const override             { return isDiscrete() && info.max_value - info.min_value <= 1.0 + 1.0e-9; }
        int getNumSteps() const override
        {
            return isDiscrete() ? juce::jmax (2, (int) (info.max_value - info.min_value) + 1) : AudioProcessorParameter::getDefaultNumParameterSteps();
        }
        bool isAutomatable() const override         { return (info.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0; }
        juce::String getParameterID() const override { return juce::String (info.id); }

        /** From the plug-in (audio or main thread). */
        void updateFromPlugin (double v)
        {
            value.store (v);
            sendValueChangedMessageToListeners (toNormal (v));
        }

        float toNormal (double v) const noexcept
        {
            const double range = info.max_value - info.min_value;
            return range > 0.0 ? (float) juce::jlimit (0.0, 1.0, (v - info.min_value) / range) : 0.0f;
        }
        double fromNormal (float n) const noexcept
        {
            double v = info.min_value + (double) n * (info.max_value - info.min_value);
            if (isDiscrete())
                v = std::round (v);
            return v;
        }

        ClapInstance& owner;
        const clap_param_info info;
        std::atomic<double> value;
    };

    //==========================================================================
    class ClapInstance final : public juce::AudioPluginInstance
    {
    public:
        ClapInstance (std::shared_ptr<ClapModule> m, const clap_plugin_descriptor& d, const juce::PluginDescription& pd)
            : AudioPluginInstance (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
              module (std::move (m)), description (pd), clapId (d.id)
        {
            alive = std::make_shared<bool> (true);

            host.clap_version = CLAP_VERSION;
            host.host_data = this;
            host.name = "KlankModule";
            host.vendor = "KlankModule";
            host.url = "https://github.com/jgovaart1973/soundmodule";
            host.version = "1.0";
            host.get_extension = [] (const clap_host* h, const char* id) -> const void* { return self (h)->hostExtension (id); };
            host.request_restart = [] (const clap_host*) {};
            host.request_process = [] (const clap_host*) {};
            host.request_callback = [] (const clap_host* h) { self (h)->requestCallback(); };

            hostLog.log = [] (const clap_host*, clap_log_severity severity, const char* msg)
            {
                if (severity >= CLAP_LOG_ERROR)
                    juce::Logger::writeToLog ("CLAP: " + juce::String::fromUTF8 (msg));
            };
            hostThreadCheck.is_main_thread = [] (const clap_host*) { return juce::MessageManager::existsAndIsCurrentThread(); };
            hostThreadCheck.is_audio_thread = [] (const clap_host* h) { return self (h)->inProcess.load(); };
            hostParams.rescan = [] (const clap_host* h, clap_param_rescan_flags flags)
            {
                if ((flags & CLAP_PARAM_RESCAN_VALUES) != 0)
                    self (h)->refreshParameterValues();
            };
            hostParams.clear = [] (const clap_host*, clap_id, clap_param_clear_flags) {};
            hostParams.request_flush = [] (const clap_host* h) { self (h)->requestCallback(); };
            hostState.mark_dirty = [] (const clap_host* h)
            {
                self (h)->updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
            };
            hostLatency.changed = [] (const clap_host*) {};
            hostGui.resize_hints_changed = [] (const clap_host*) {};
            hostGui.request_resize = [] (const clap_host* h, uint32_t w, uint32_t hh) { return self (h)->requestResize ((int) w, (int) hh); };
            hostGui.request_show = [] (const clap_host*) { return true; };
            hostGui.request_hide = [] (const clap_host*) { return true; };
            hostGui.closed = [] (const clap_host*, bool) {};
            hostNotePorts.supported_dialects = [] (const clap_host*) -> uint32_t { return CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_MIDI_MPE; };
            hostNotePorts.rescan = [] (const clap_host*, uint32_t) {};
            hostAudioPorts.is_rescan_flag_supported = [] (const clap_host*, uint32_t) { return false; };
            hostAudioPorts.rescan = [] (const clap_host*, uint32_t) {};

            inList.ctx = this;
            inList.size = [] (const clap_input_events* l) { return (uint32_t) static_cast<ClapInstance*> (l->ctx)->numInEvents; };
            inList.get = [] (const clap_input_events* l, uint32_t i) -> const clap_event_header_t*
            {
                auto* s = static_cast<ClapInstance*> (l->ctx);
                return i < (uint32_t) s->numInEvents ? &s->inEvents[i].header : nullptr;
            };
            outList.ctx = this;
            outList.try_push = [] (const clap_output_events* l, const clap_event_header_t* e)
            {
                static_cast<ClapInstance*> (l->ctx)->handleOutputEvent (*e);
                return true;
            };
            inEvents.resize (4096);
        }

        bool initialise (juce::String& error)
        {
            plugin = module->factory->create_plugin (module->factory, &host, clapId.toRawUTF8());
            if (plugin == nullptr || ! plugin->init (plugin))
            {
                if (plugin != nullptr)
                    plugin->destroy (plugin);
                plugin = nullptr;
                error = "CLAP-plug-in kon niet starten: " + description.name;
                return false;
            }
            params = static_cast<const clap_plugin_params*> (plugin->get_extension (plugin, CLAP_EXT_PARAMS));
            state = static_cast<const clap_plugin_state*> (plugin->get_extension (plugin, CLAP_EXT_STATE));
            gui = static_cast<const clap_plugin_gui*> (plugin->get_extension (plugin, CLAP_EXT_GUI));
            audioPorts = static_cast<const clap_plugin_audio_ports*> (plugin->get_extension (plugin, CLAP_EXT_AUDIO_PORTS));
            notePorts = static_cast<const clap_plugin_note_ports*> (plugin->get_extension (plugin, CLAP_EXT_NOTE_PORTS));

            if (notePorts != nullptr && notePorts->count (plugin, true) > 0)
            {
                clap_note_port_info info {};
                if (notePorts->get (plugin, 0, true, &info))
                {
                    // Prefer MIDI when the plug-in takes it: CCs, pressure, bend and MPE pass unchanged.
                    useMidiDialect = (info.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0;
                    noteDialectClap = (info.supported_dialects & CLAP_NOTE_DIALECT_CLAP) != 0;
                }
            }
            else
            {
                useMidiDialect = false;
                noteDialectClap = false;
            }

            buildParameters();
            return true;
        }

        ~ClapInstance() override
        {
            *alive = false;
            if (plugin != nullptr)
            {
                deactivate();
                plugin->destroy (plugin);
            }
        }

        static ClapInstance* self (const clap_host* h) { return static_cast<ClapInstance*> (h->host_data); }

        //== AudioPluginInstance ===============================================
        void fillInPluginDescription (juce::PluginDescription& d) const override { d = description; }
        const juce::String getName() const override { return description.name; }

        bool isBusesLayoutSupported (const BusesLayout& l) const override
        {
            return l.getMainInputChannelSet().isDisabled()
                && (l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
                    || l.getMainOutputChannelSet() == juce::AudioChannelSet::mono());
        }

        void prepareToPlay (double sampleRate, int maxBlock) override
        {
            JUCE_ASSERT_MESSAGE_THREAD
            deactivate();
            preparePorts (maxBlock);
            if (plugin->activate (plugin, sampleRate, 1, (uint32_t) juce::jmax (1, maxBlock)))
            {
                activated = true;
                processing = false;
            }
        }

        void releaseResources() override { deactivate(); }

        using AudioPluginInstance::processBlock;

        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
        {
            const int n = buffer.getNumSamples();
            inProcess = true;
            if (! activated || n <= 0 || n > maxFrames)
            {
                buffer.clear();
                inProcess = false;
                return;
            }
            if (! processing)
            {
                processing = plugin->start_processing == nullptr || plugin->start_processing (plugin);
                if (! processing)
                {
                    buffer.clear();
                    inProcess = false;
                    return;
                }
            }

            // Events: parameter changes from the host first, then MIDI in time order.
            numInEvents = 0;
            while (auto c = paramQueue.pop())
            {
                if (numInEvents >= (int) inEvents.size())
                    break;
                auto& e = inEvents[(size_t) numInEvents++].param;
                e = {};
                e.header = { sizeof (clap_event_param_value_t), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 };
                e.param_id = c->id;
                e.cookie = c->cookie;
                e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
                e.value = c->value;
            }
            for (const auto meta : midi)
            {
                if (numInEvents >= (int) inEvents.size())
                    break;
                addMidiEvent (meta.data, meta.numBytes, (uint32_t) juce::jlimit (0, n - 1, meta.samplePosition));
            }

            // Audio: main output -> our buffer, everything else -> scratch; inputs silent.
            for (size_t p = 0; p < outPorts.size(); ++p)
            {
                auto& port = outPorts[p];
                for (uint32_t ch = 0; ch < port.buffer.channel_count; ++ch)
                {
                    float* dest = nullptr;
                    if (p == 0 && (int) ch < buffer.getNumChannels())
                        dest = buffer.getWritePointer ((int) ch);
                    port.pointers[ch] = dest != nullptr ? dest : scratch.getWritePointer ((int) port.scratchStart + (int) ch);
                }
                port.buffer.data32 = port.pointers.data();
            }
            for (auto& port : inPorts)
            {
                for (uint32_t ch = 0; ch < port.buffer.channel_count; ++ch)
                {
                    auto* d = scratch.getWritePointer ((int) port.scratchStart + (int) ch);
                    juce::FloatVectorOperations::clear (d, n);
                    port.pointers[ch] = d;
                }
                port.buffer.data32 = port.pointers.data();
                port.buffer.constant_mask = ~0ull;
            }
            outBuffers.clear();
            for (auto& port : outPorts) outBuffers.push_back (port.buffer);
            inBuffers.clear();
            for (auto& port : inPorts) inBuffers.push_back (port.buffer);

            clap_process proc {};
            proc.steady_time = steadyTime;
            proc.frames_count = (uint32_t) n;
            proc.transport = nullptr;
            proc.audio_inputs = inBuffers.data();
            proc.audio_inputs_count = (uint32_t) inBuffers.size();
            proc.audio_outputs = outBuffers.data();
            proc.audio_outputs_count = (uint32_t) outBuffers.size();
            proc.in_events = &inList;
            proc.out_events = &outList;

            const auto status = plugin->process (plugin, &proc);
            steadyTime += n;
            if (status == CLAP_PROCESS_ERROR)
                buffer.clear();
            else if (! outPorts.empty() && outPorts[0].buffer.channel_count == 1 && buffer.getNumChannels() > 1)
                buffer.copyFrom (1, 0, buffer, 0, 0, n);
            else if (outPorts.empty())
                buffer.clear();
            inProcess = false;
        }

        double getTailLengthSeconds() const override { return 0.0; }
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return false; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& dest) override
        {
            JUCE_ASSERT_MESSAGE_THREAD
            if (state == nullptr)
                return;
            juce::MemoryOutputStream out (dest, false);
            clap_ostream os { &out, [] (const clap_ostream* s, const void* data, uint64_t size) -> int64_t
            {
                return static_cast<juce::MemoryOutputStream*> (s->ctx)->write (data, (size_t) size) ? (int64_t) size : -1;
            } };
            state->save (plugin, &os);
        }

        void setStateInformation (const void* data, int size) override
        {
            JUCE_ASSERT_MESSAGE_THREAD
            if (state == nullptr || size <= 0)
                return;
            juce::MemoryInputStream in (data, (size_t) size, false);
            clap_istream is { &in, [] (const clap_istream* s, void* buffer, uint64_t bytes) -> int64_t
            {
                return static_cast<juce::MemoryInputStream*> (s->ctx)->read (buffer, (int) bytes);
            } };
            if (state->load (plugin, &is))
                refreshParameterValues();
        }

        bool hasEditor() const override { return true; }
        juce::AudioProcessorEditor* createEditor() override;

        //== parameters ========================================================
        void queueParamChange (clap_id id, void* cookie, double v)
        {
            const juce::SpinLock::ScopedLockType sl (paramProducerLock);
            paramQueue.push ({ id, cookie, v });
            if (! activated && params != nullptr && params->flush != nullptr && juce::MessageManager::existsAndIsCurrentThread())
                flushParams();
        }

        juce::String valueToText (clap_id id, double v) const
        {
            char text[256] {};
            if (params != nullptr && juce::MessageManager::existsAndIsCurrentThread() && params->value_to_text (plugin, id, v, text, sizeof (text)))
                return juce::String::fromUTF8 (text);
            return juce::String (v, 2);
        }

        double textToValue (clap_id id, const juce::String& t, double fallback) const
        {
            double v = fallback;
            if (params != nullptr && juce::MessageManager::existsAndIsCurrentThread() && params->text_to_value (plugin, id, t.toRawUTF8(), &v))
                return v;
            return t.getDoubleValue();
        }

        //== GUI support (used by the editor) ==================================
        const clap_plugin* getPlugin() const noexcept   { return plugin; }
        const clap_plugin_gui* getGui() const noexcept  { return gui; }
        std::function<void (int, int)> onResizeRequest;

    private:
        struct Port
        {
            clap_audio_buffer buffer {};
            std::vector<float*> pointers;
            uint32_t scratchStart = 0;
        };

        const void* hostExtension (const char* id)
        {
            const juce::String e (id);
            if (e == CLAP_EXT_LOG)          return &hostLog;
            if (e == CLAP_EXT_THREAD_CHECK) return &hostThreadCheck;
            if (e == CLAP_EXT_PARAMS)       return &hostParams;
            if (e == CLAP_EXT_STATE)        return &hostState;
            if (e == CLAP_EXT_LATENCY)      return &hostLatency;
            if (e == CLAP_EXT_GUI)          return &hostGui;
            if (e == CLAP_EXT_NOTE_PORTS)   return &hostNotePorts;
            if (e == CLAP_EXT_AUDIO_PORTS)  return &hostAudioPorts;
            return nullptr;
        }

        void requestCallback()
        {
            std::weak_ptr<bool> weak = alive;
            juce::MessageManager::callAsync ([this, weak]
            {
                if (auto a = weak.lock(); a != nullptr && *a && plugin != nullptr)
                {
                    plugin->on_main_thread (plugin);
                    if (! activated)
                        flushParams();
                }
            });
        }

        bool requestResize (int w, int h)
        {
            std::weak_ptr<bool> weak = alive;
            juce::MessageManager::callAsync ([this, weak, w, h]
            {
                if (auto a = weak.lock(); a != nullptr && *a && onResizeRequest)
                    onResizeRequest (w, h);
            });
            return true;
        }

        void buildParameters()
        {
            if (params == nullptr)
                return;
            juce::AudioProcessorParameterGroup group;
            const auto count = params->count (plugin);
            for (uint32_t i = 0; i < count; ++i)
            {
                clap_param_info info {};
                if (! params->get_info (plugin, i, &info) || (info.flags & CLAP_PARAM_IS_HIDDEN) != 0)
                    continue;
                double v = info.default_value;
                params->get_value (plugin, info.id, &v);
                auto p = std::make_unique<ClapParameter> (*this, info, v);
                byId[info.id] = p.get();
                group.addChild (std::move (p));
            }
            setHostedParameterTree (std::move (group));
        }

        void refreshParameterValues()
        {
            if (params == nullptr || ! juce::MessageManager::existsAndIsCurrentThread())
                return;
            for (auto& [id, p] : byId)
            {
                double v = 0.0;
                if (params->get_value (plugin, id, &v))
                    p->updateFromPlugin (v);
            }
        }

        void flushParams()
        {
            if (params == nullptr || params->flush == nullptr || activated)
                return;
            numInEvents = 0;
            while (auto c = paramQueue.pop())
            {
                if (numInEvents >= (int) inEvents.size())
                    break;
                auto& e = inEvents[(size_t) numInEvents++].param;
                e = {};
                e.header = { sizeof (clap_event_param_value_t), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 };
                e.param_id = c->id; e.cookie = c->cookie;
                e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
                e.value = c->value;
            }
            params->flush (plugin, &inList, &outList);
            numInEvents = 0;
        }

        void handleOutputEvent (const clap_event_header_t& e)
        {
            if (e.space_id != CLAP_CORE_EVENT_SPACE_ID || e.type != CLAP_EVENT_PARAM_VALUE)
                return;
            const auto& pv = reinterpret_cast<const clap_event_param_value_t&> (e);
            if (auto it = byId.find (pv.param_id); it != byId.end())
                it->second->updateFromPlugin (pv.value);
        }

        void addMidiEvent (const juce::uint8* d, int size, uint32_t time)
        {
            if (size < 1)
                return;
            const int type = d[0] & 0xf0, ch = d[0] & 0x0f;
            auto& slot = inEvents[(size_t) numInEvents];

            const bool isNote = (type == 0x90 || type == 0x80) && size >= 3;
            if (isNote && (noteDialectClap && ! useMidiDialect))
            {
                const bool on = type == 0x90 && d[2] > 0;
                slot.note = {};
                slot.note.header = { sizeof (clap_event_note_t), time, CLAP_CORE_EVENT_SPACE_ID,
                                     (uint16_t) (on ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF), 0 };
                slot.note.note_id = -1;
                slot.note.port_index = 0;
                slot.note.channel = (int16_t) ch;
                slot.note.key = (int16_t) d[1];
                slot.note.velocity = d[2] / 127.0;
                ++numInEvents;
                return;
            }
            if (! useMidiDialect || size > 3 || d[0] >= 0xf0)
                return; // a CLAP-dialect-only plug-in gets notes; MIDI-only events are dropped

            slot.midi = {};
            slot.midi.header = { sizeof (clap_event_midi_t), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0 };
            slot.midi.port_index = 0;
            for (int i = 0; i < size; ++i)
                slot.midi.data[i] = d[i];
            ++numInEvents;
        }

        void preparePorts (int maxBlock)
        {
            maxFrames = juce::jmax (1, maxBlock);
            outPorts.clear();
            inPorts.clear();
            uint32_t scratchChannels = 0;
            auto add = [&] (std::vector<Port>& list, bool input)
            {
                const uint32_t count = audioPorts != nullptr ? audioPorts->count (plugin, input) : 0;
                for (uint32_t i = 0; i < count; ++i)
                {
                    clap_audio_port_info info {};
                    if (! audioPorts->get (plugin, i, input, &info))
                        continue;
                    Port p;
                    p.buffer.channel_count = info.channel_count;
                    p.pointers.resize (info.channel_count);
                    p.scratchStart = scratchChannels;
                    scratchChannels += info.channel_count;
                    list.push_back (std::move (p));
                }
            };
            add (outPorts, false);
            add (inPorts, true);
            scratch.setSize ((int) juce::jmax (1u, scratchChannels), maxFrames, false, true, false);
            outBuffers.reserve (outPorts.size());
            inBuffers.reserve (inPorts.size());
        }

        void deactivate()
        {
            if (! activated)
                return;
            if (processing && plugin->stop_processing != nullptr)
                plugin->stop_processing (plugin); // audio is stopped when the pool prepares / releases
            processing = false;
            plugin->deactivate (plugin);
            activated = false;
        }

        std::shared_ptr<ClapModule> module;
        juce::PluginDescription description;
        juce::String clapId;
        std::shared_ptr<bool> alive;

        clap_host host {};
        clap_host_log hostLog {};
        clap_host_thread_check hostThreadCheck {};
        clap_host_params hostParams {};
        clap_host_state hostState {};
        clap_host_latency hostLatency {};
        clap_host_gui hostGui {};
        clap_host_note_ports hostNotePorts {};
        clap_host_audio_ports hostAudioPorts {};

        const clap_plugin* plugin = nullptr;
        const clap_plugin_params* params = nullptr;
        const clap_plugin_state* state = nullptr;
        const clap_plugin_gui* gui = nullptr;
        const clap_plugin_audio_ports* audioPorts = nullptr;
        const clap_plugin_note_ports* notePorts = nullptr;
        bool useMidiDialect = true, noteDialectClap = false;

        std::unordered_map<clap_id, ClapParameter*> byId;
        juce::SpinLock paramProducerLock;
        SpscQueue<ParamChange, 1024> paramQueue;

        std::vector<ClapEvent> inEvents;
        int numInEvents = 0;
        clap_input_events inList {};
        clap_output_events outList {};

        std::vector<Port> outPorts, inPorts;
        std::vector<clap_audio_buffer> outBuffers, inBuffers;
        juce::AudioBuffer<float> scratch;
        int maxFrames = 0;
        int64_t steadyTime = 0;
        bool activated = false, processing = false;
        std::atomic<bool> inProcess { false };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClapInstance)
    };

    void ClapParameter::setValue (float normal)
    {
        const double v = fromNormal (normal);
        value.store (v);
        owner.queueParamChange (info.id, info.cookie, v);
    }

    juce::String ClapParameter::getText (float normal, int maxLen) const
    {
        return owner.valueToText (info.id, fromNormal (normal)).substring (0, maxLen);
    }

    float ClapParameter::getValueForText (const juce::String& text) const
    {
        return toNormal (owner.textToValue (info.id, text, value.load()));
    }

    //==========================================================================
   #if JUCE_WINDOWS
    /** The plug-in's own window, embedded as a child win32 window. */
    class ClapGuiEditor final : public juce::AudioProcessorEditor
    {
    public:
        explicit ClapGuiEditor (ClapInstance& p) : AudioProcessorEditor (p), owner (p)
        {
            auto* gui = owner.getGui();
            created = gui->create (owner.getPlugin(), CLAP_WINDOW_API_WIN32, false);
            uint32_t w = 600, h = 400;
            if (created)
                gui->get_size (owner.getPlugin(), &w, &h);
            setSize ((int) w, (int) h);
            setResizable (created && gui->can_resize (owner.getPlugin()), false);
            addAndMakeVisible (holder);
            owner.onResizeRequest = [this] (int nw, int nh)
            {
                const double s = scale();
                setSize (juce::roundToInt (nw / s), juce::roundToInt (nh / s));
            };
        }

        ~ClapGuiEditor() override
        {
            owner.onResizeRequest = nullptr;
            if (created)
            {
                owner.getGui()->hide (owner.getPlugin());
                owner.getGui()->destroy (owner.getPlugin());
            }
            holder.setHWND (nullptr);
            if (child != nullptr)
                DestroyWindow (child);
        }

        void parentHierarchyChanged() override { attachIfPossible(); }
        void visibilityChanged() override      { attachIfPossible(); }

        void resized() override
        {
            holder.setBounds (getLocalBounds());
            if (attached && getGuiResizable())
            {
                const double s = scale();
                uint32_t w = (uint32_t) juce::roundToInt (getWidth() * s), h = (uint32_t) juce::roundToInt (getHeight() * s);
                auto* gui = owner.getGui();
                if (gui->adjust_size (owner.getPlugin(), &w, &h))
                    gui->set_size (owner.getPlugin(), w, h);
            }
        }

        void paint (juce::Graphics& g) override
        {
            if (! created)
            {
                g.fillAll (juce::Colours::black);
                g.setColour (juce::Colours::white);
                g.drawText ("Deze CLAP-plug-in heeft geen eigen venster.", getLocalBounds(), juce::Justification::centred);
            }
        }

    private:
        bool getGuiResizable() const { return created && owner.getGui()->can_resize (owner.getPlugin()); }

        double scale() const
        {
            if (auto* peer = getPeer())
                return peer->getPlatformScaleFactor();
            return 1.0;
        }

        void attachIfPossible()
        {
            if (attached || ! created || getPeer() == nullptr || ! isShowing())
                return;
            auto parent = static_cast<HWND> (getPeer()->getNativeHandle());
            const double s = scale();
            owner.getGui()->set_scale (owner.getPlugin(), s);
            uint32_t w = 0, h = 0;
            if (owner.getGui()->get_size (owner.getPlugin(), &w, &h) && w > 0 && h > 0)
                setSize (juce::roundToInt (w / s), juce::roundToInt (h / s));

            child = CreateWindowExW (0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                     0, 0, (int) juce::jmax (1u, w), (int) juce::jmax (1u, h), parent, nullptr,
                                     GetModuleHandleW (nullptr), nullptr);
            if (child == nullptr)
                return;
            holder.setHWND (child);
            holder.setBounds (getLocalBounds());

            clap_window win {};
            win.api = CLAP_WINDOW_API_WIN32;
            win.win32 = child;
            if (owner.getGui()->set_parent (owner.getPlugin(), &win))
            {
                owner.getGui()->show (owner.getPlugin());
                attached = true;
            }
        }

        ClapInstance& owner;
        juce::HWNDComponent holder;
        HWND child = nullptr;
        bool created = false, attached = false;
    };
   #endif

    juce::AudioProcessorEditor* ClapInstance::createEditor()
    {
       #if JUCE_WINDOWS
        if (gui != nullptr && gui->is_api_supported (plugin, CLAP_WINDOW_API_WIN32, false))
            return new ClapGuiEditor (*this);
       #endif
        return new juce::GenericAudioProcessorEditor (*this);
    }
} // namespace

//==============================================================================
void ClapPluginFormat::findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results, const juce::String& path)
{
    if (! fileMightContainThisPluginType (path))
        return;
    juce::String error;
    auto m = ClapModule::open (path, error);
    if (m == nullptr)
        return;
    const auto count = m->factory->get_plugin_count (m->factory);
    for (uint32_t i = 0; i < count; ++i)
        if (auto* d = m->factory->get_plugin_descriptor (m->factory, i))
            results.add (new juce::PluginDescription (describe (*d, juce::File (path), (int) count)));
}

bool ClapPluginFormat::fileMightContainThisPluginType (const juce::String& f)
{
    const juce::File file (f);
    return juce::File::isAbsolutePath (f) && file.exists() && file.hasFileExtension (".clap");
}

juce::String ClapPluginFormat::getNameOfPluginFromIdentifier (const juce::String& f)
{
    return juce::File (f).getFileNameWithoutExtension();
}

bool ClapPluginFormat::pluginNeedsRescanning (const juce::PluginDescription& d)
{
    return juce::File (d.fileOrIdentifier).getLastModificationTime() != d.lastFileModTime;
}

bool ClapPluginFormat::doesPluginStillExist (const juce::PluginDescription& d)
{
    return juce::File (d.fileOrIdentifier).exists();
}

juce::StringArray ClapPluginFormat::searchPathsForPlugins (const juce::FileSearchPath& paths, bool recursive, bool)
{
    juce::StringArray found;
    for (int i = 0; i < paths.getNumPaths(); ++i)
    {
        const auto dir = paths[i];
        if (! dir.isDirectory())
            continue;
        for (const auto& entry : juce::RangedDirectoryIterator (dir, recursive, "*.clap",
                                                                juce::File::findFilesAndDirectories))
            found.addIfNotAlreadyThere (entry.getFile().getFullPathName());
    }
    return found;
}

juce::FileSearchPath ClapPluginFormat::getDefaultLocationsToSearch()
{
    juce::FileSearchPath p;
   #if JUCE_WINDOWS
    p.add (juce::File (juce::SystemStats::getEnvironmentVariable ("COMMONPROGRAMFILES", "C:\\Program Files\\Common Files")).getChildFile ("CLAP"));
    p.add (juce::File (juce::SystemStats::getEnvironmentVariable ("LOCALAPPDATA", {})).getChildFile ("Programs\\Common\\CLAP"));
   #elif JUCE_MAC
    p.add (juce::File ("/Library/Audio/Plug-Ins/CLAP"));
    p.add (juce::File ("~/Library/Audio/Plug-Ins/CLAP"));
   #else
    p.add (juce::File ("~/.clap"));
    p.add (juce::File ("/usr/lib/clap"));
   #endif
    // CLAP_PATH: extra folders, like other hosts
    for (auto& extra : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("CLAP_PATH", {}),
                                                     juce::File::getSeparatorChar() == '\\' ? ";" : ":", {}))
        if (extra.isNotEmpty())
            p.add (juce::File (extra));
    return p;
}

std::unique_ptr<juce::AudioPluginInstance> ClapPluginFormat::createInstance (const juce::PluginDescription& desc,
                                                                             double sampleRate, int blockSize, juce::String& error)
{
    JUCE_ASSERT_MESSAGE_THREAD
    auto m = ClapModule::open (desc.fileOrIdentifier, error);
    if (m == nullptr)
        return nullptr;

    const auto count = m->factory->get_plugin_count (m->factory);
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto* d = m->factory->get_plugin_descriptor (m->factory, i);
        if (d == nullptr || (uidFor (d->id) != desc.uniqueId && (count > 1 || desc.uniqueId != 0)))
            continue;
        auto pd = describe (*d, juce::File (desc.fileOrIdentifier), (int) count);
        auto instance = std::make_unique<ClapInstance> (m, *d, pd);
        if (! instance->initialise (error))
            return nullptr;
        instance->setRateAndBufferSizeDetails (sampleRate, blockSize);
        return instance;
    }
    error = "CLAP-plug-in niet gevonden in " + desc.fileOrIdentifier + " (opnieuw scannen?)";
    return nullptr;
}

void ClapPluginFormat::createPluginInstance (const juce::PluginDescription& desc, double sampleRate, int blockSize,
                                             PluginCreationCallback callback)
{
    juce::String error;
    auto instance = createInstance (desc, sampleRate, blockSize, error);
    callback (std::move (instance), error);
}

void addPluginFormats (juce::AudioPluginFormatManager& manager)
{
    juce::addDefaultFormatsToManager (manager);
    manager.addFormat (std::make_unique<ClapPluginFormat>());
}

} // namespace km
