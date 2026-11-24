// Tiny CLAP instrument used by the tests: a sine per note, one "Gain" parameter, state.
// Two plug-ins in one file: one takes CLAP note events, the other raw MIDI.
#include <clap/clap.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace
{
constexpr clap_id gainId = 7;

const char* const features[] = { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, nullptr };

const clap_plugin_descriptor descriptors[2] = {
    { CLAP_VERSION_INIT, "com.klankmodule.test.clapsynth", "KM Test CLAP Synth", "KlankModule", "", "", "", "1.0", "Test sine (CLAP notes)", features },
    { CLAP_VERSION_INIT, "com.klankmodule.test.midisynth", "KM Test CLAP MIDI", "KlankModule", "", "", "", "1.0", "Test sine (MIDI)", features },
};

struct Synth
{
    clap_plugin plugin {};
    const clap_host* host = nullptr;
    bool midiDialect = false;
    double sampleRate = 48000.0;
    double gain = 0.5;
    struct Voice { bool on = false; int key = 0; double phase = 0.0; } voices[16];

    void noteOn (int key)
    {
        for (auto& v : voices)
            if (! v.on) { v.on = true; v.key = key; v.phase = 0.0; return; }
    }
    void noteOff (int key)
    {
        for (auto& v : voices)
            if (v.on && v.key == key) v.on = false;
    }
    void handle (const clap_event_header* e)
    {
        if (e->space_id != CLAP_CORE_EVENT_SPACE_ID)
            return;
        switch (e->type)
        {
            case CLAP_EVENT_NOTE_ON:  noteOn (((const clap_event_note*) e)->key); break;
            case CLAP_EVENT_NOTE_OFF: noteOff (((const clap_event_note*) e)->key); break;
            case CLAP_EVENT_MIDI:
            {
                auto* m = (const clap_event_midi*) e;
                const int type = m->data[0] & 0xf0;
                if (type == 0x90 && m->data[2] > 0) noteOn (m->data[1]);
                else if (type == 0x80 || type == 0x90) noteOff (m->data[1]);
                break;
            }
            case CLAP_EVENT_PARAM_VALUE:
            {
                auto* p = (const clap_event_param_value*) e;
                if (p->param_id == gainId) gain = p->value;
                break;
            }
            default: break;
        }
    }
};

Synth* self (const clap_plugin* p) { return static_cast<Synth*> (p->plugin_data); }

// --- extensions ------------------------------------------------------------
uint32_t portsCount (const clap_plugin*, bool isInput) { return isInput ? 0 : 1; }
bool portsGet (const clap_plugin*, uint32_t index, bool isInput, clap_audio_port_info* info)
{
    if (isInput || index != 0) return false;
    info->id = 0;
    std::strcpy (info->name, "Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports audioPorts { portsCount, portsGet };

uint32_t notePortsCount (const clap_plugin*, bool isInput) { return isInput ? 1 : 0; }
bool notePortsGet (const clap_plugin* p, uint32_t index, bool isInput, clap_note_port_info* info)
{
    if (! isInput || index != 0) return false;
    info->id = 0;
    const uint32_t d = self (p)->midiDialect ? CLAP_NOTE_DIALECT_MIDI : CLAP_NOTE_DIALECT_CLAP;
    info->supported_dialects = d;
    info->preferred_dialect = d;
    std::strcpy (info->name, "Notes");
    return true;
}
const clap_plugin_note_ports notePorts { notePortsCount, notePortsGet };

uint32_t paramsCount (const clap_plugin*) { return 1; }
bool paramsInfo (const clap_plugin*, uint32_t index, clap_param_info* info)
{
    if (index != 0) return false;
    std::memset (info, 0, sizeof (*info));
    info->id = gainId;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strcpy (info->name, "Gain");
    info->min_value = 0.0;
    info->max_value = 1.0;
    info->default_value = 0.5;
    return true;
}
bool paramsValue (const clap_plugin* p, clap_id id, double* v) { if (id != gainId) return false; *v = self (p)->gain; return true; }
bool paramsToText (const clap_plugin*, clap_id, double v, char* out, uint32_t size)
{
    std::snprintf (out, size, "%d %%", (int) std::lround (v * 100.0));
    return true;
}
bool paramsFromText (const clap_plugin*, clap_id, const char* text, double* v) { *v = std::atof (text) / 100.0; return true; }
void paramsFlush (const clap_plugin* p, const clap_input_events* in, const clap_output_events*)
{
    for (uint32_t i = 0; i < in->size (in); ++i)
        self (p)->handle (in->get (in, i));
}
const clap_plugin_params params { paramsCount, paramsInfo, paramsValue, paramsToText, paramsFromText, paramsFlush };

bool stateSave (const clap_plugin* p, const clap_ostream* s)
{
    const double g = self (p)->gain;
    return s->write (s, &g, sizeof (g)) == (int64_t) sizeof (g);
}
bool stateLoad (const clap_plugin* p, const clap_istream* s)
{
    double g = 0.0;
    if (s->read (s, &g, sizeof (g)) != (int64_t) sizeof (g)) return false;
    self (p)->gain = g;
    return true;
}
const clap_plugin_state state { stateSave, stateLoad };

// --- plugin ------------------------------------------------------------------
bool init (const clap_plugin*) { return true; }
void destroy (const clap_plugin* p) { delete self (p); }
bool activate (const clap_plugin* p, double sr, uint32_t, uint32_t) { self (p)->sampleRate = sr; return true; }
void deactivate (const clap_plugin*) {}
bool startProcessing (const clap_plugin*) { return true; }
void stopProcessing (const clap_plugin*) {}
void reset (const clap_plugin*) {}

clap_process_status process (const clap_plugin* p, const clap_process* proc)
{
    auto* s = self (p);
    const uint32_t n = proc->frames_count;
    const uint32_t numEvents = proc->in_events->size (proc->in_events);
    uint32_t next = 0;
    float* l = proc->audio_outputs[0].data32[0];
    float* r = proc->audio_outputs[0].data32[1];
    for (uint32_t i = 0; i < n; ++i)
    {
        while (next < numEvents)
        {
            auto* e = proc->in_events->get (proc->in_events, next);
            if (e->time > i) break;
            s->handle (e);
            ++next;
        }
        double out = 0.0;
        for (auto& v : s->voices)
        {
            if (! v.on) continue;
            const double f = 440.0 * std::pow (2.0, (v.key - 69) / 12.0);
            out += 0.2 * std::sin (v.phase);
            v.phase += 2.0 * 3.141592653589793 * f / s->sampleRate;
        }
        l[i] = r[i] = (float) (out * s->gain);
    }
    while (next < numEvents)
        s->handle (proc->in_events->get (proc->in_events, next++));
    return CLAP_PROCESS_CONTINUE;
}

const void* getExtension (const clap_plugin*, const char* id)
{
    if (! std::strcmp (id, CLAP_EXT_AUDIO_PORTS)) return &audioPorts;
    if (! std::strcmp (id, CLAP_EXT_NOTE_PORTS))  return &notePorts;
    if (! std::strcmp (id, CLAP_EXT_PARAMS))      return &params;
    if (! std::strcmp (id, CLAP_EXT_STATE))       return &state;
    return nullptr;
}
void onMainThread (const clap_plugin*) {}

// --- factory -----------------------------------------------------------------
uint32_t factoryCount (const clap_plugin_factory*) { return 2; }
const clap_plugin_descriptor* factoryDescriptor (const clap_plugin_factory*, uint32_t index)
{
    return index < 2 ? &descriptors[index] : nullptr;
}
const clap_plugin* factoryCreate (const clap_plugin_factory*, const clap_host* host, const char* id)
{
    for (int k = 0; k < 2; ++k)
    {
        if (std::strcmp (id, descriptors[k].id) != 0) continue;
        auto* s = new (std::nothrow) Synth();
        if (s == nullptr) return nullptr;
        s->host = host;
        s->midiDialect = k == 1;
        s->plugin.desc = &descriptors[k];
        s->plugin.plugin_data = s;
        s->plugin.init = init;
        s->plugin.destroy = destroy;
        s->plugin.activate = activate;
        s->plugin.deactivate = deactivate;
        s->plugin.start_processing = startProcessing;
        s->plugin.stop_processing = stopProcessing;
        s->plugin.reset = reset;
        s->plugin.process = process;
        s->plugin.get_extension = getExtension;
        s->plugin.on_main_thread = onMainThread;
        return &s->plugin;
    }
    return nullptr;
}
const clap_plugin_factory factory { factoryCount, factoryDescriptor, factoryCreate };

bool entryInit (const char*) { return true; }
void entryDeinit() {}
const void* entryFactory (const char* id) { return std::strcmp (id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; }
} // namespace

extern "C"
#if defined(_WIN32)
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
const clap_plugin_entry clap_entry { CLAP_VERSION_INIT, entryInit, entryDeinit, entryFactory };
