# Architectuur

## Componenten en threads

```mermaid
flowchart LR
    subgraph MIDI["MIDI-threads (driver)"]
        R[MidiRouter<br/>per apparaat een slot] --> L{MidiLearn<br/>CC gekoppeld?}
    end
    L -- ja --> P[(atomics:<br/>volume, pan, sends)]
    L -- nee --> Q[[MidiEventQueue<br/>lock-free]]

    subgraph AUDIO["Audio-thread (realtime)"]
        Q --> E[ModuleEngine]
        E --> PA[16 x Part<br/>split, layers, transpose]
        PA --> I[Instrument<br/>BuiltInSynth / SFZ / VST3]
        I --> B[Main / Galm / Delay bus]
        B --> M[MasterFx<br/>volume + limiter]
        M --> OUT((ASIO / WASAPI))
    end

    subgraph UI["Message-thread (UI)"]
        C[AppController] --> POOL[InstrumentPool<br/>laden, cache, preload]
        C --> PERF[Performance<br/>JSON]
        UIC[MainComponent<br/>PartRow, MasterPanel] --> C
    end

    POOL -- "lend()" --> PA
    PA -- ReleaseQueue --> POOL
    E -- ProgramRequestQueue --> C
```

## Realtime-regels
De audio-thread wacht nooit en alloceert niets:
- MIDI komt binnen via een lock-free wachtrij (`SpscQueue`), parameters zijn `std::atomic`.
- Een nieuwe klank wordt **uitgeleend** (`Part::lend`) via één atomaire pointer. De audio-thread neemt hem
  aan het begin van het volgende blok over.
- De oude klank wordt de **tail**: noten die erop gestart zijn krijgen hun note-off en sustain daar
  (dus geen hangende noten). Na 3 s stilte gaat hij via `ReleaseQueue` terug naar de pool.
- Program Change die een beslissing vraagt (patchlijst, performance kiezen) gaat via
  `ProgramRequestQueue` naar de message-thread.
- NaN/inf uit een plug-in wordt per part weggefilterd, zodat de gedeelde effecten nooit vastlopen.

## Bestanden
| Map | Inhoud |
| --- | --- |
| `Source/Engine` | `ModuleEngine`, `Part`, instrumenten, effecten, `InstrumentPool` |
| `Source/Midi` | `MidiRouter` (apparaten, hot-plug), `MidiLearn`, `MidiEvent` + wachtrij |
| `Source/Model` | `Performance` (JSON met versienummer), `ProgramMap` |
| `Source/App` | `AppController`: verbindt alles, autosave, herstel na audio-uitval |
| `Source/Plugins` | Plug-in-scan in een apart proces |
| `Source/UI` | Interface (JUCE Components) |
| `Source/Render` | Offline renderen: MIDI-bestand naar WAV |
| `Tests` | Catch2-tests: filters, wachtrij, JSON, engine, MIDI Learn, sessie opslaan/herstellen |

## Uitbreiden in fase 2
- Sequencer als extra bron van `MidiEvent`s die de engine al begrijpt (`source = -1`).
- Opnemen: een ringbuffer achter de MIDI-router ("retrospective record") en de audio-uitgang.
- Offline export bestaat al als `OfflineRenderer`.
