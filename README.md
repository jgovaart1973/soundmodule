# KlankModule

Een Windows x64-programma dat je pc verandert in een hoogwaardige **klankmodule voor MIDI-apparaten**.
Sluit je keyboard aan, start de app en speel. Dit is **fase 1** uit het bouwplan: de klankmodule.
Opnemen en arrangeren (GarageBand-niveau) volgen in fase 2.

## Wat het nu kan (fase 1)

| Functie | Details |
| --- | --- |
| 16 multitimbrale parts | Per part: MIDI-ingang (of alle), kanaal (of omni), klank, volume, pan, transpose, key split, velocity layer en aanslagcurve |
| Klanken | 8 ingebouwde klanken (direct speelbaar), SFZ-bibliotheken via sfizz (bijv. Salamander Grand Piano), VST3-instrumenten |
| Mixer | 16 kanalen met mute/solo, meters, 2 effect-sends (galm, ping-pong delay), transparante master-limiter |
| Performances | Alle instellingen opslaan als `.kmperf` (leesbare JSON). Wisselen zonder hangende noten: lopende noten klinken uit op de oude klank |
| Snel wisselen | Klanken van alle opgeslagen performances worden vooraf geladen; gelijke klanken worden hergebruikt |
| MIDI-bediening | Program Change + Bank Select per part, performance kiezen via Program Change op een controlekanaal (standaard 16), General MIDI-modus |
| MIDI Learn | Rechtsklik op een schuif of knop > MIDI Learn. Ook voor volgende/vorige performance en Panic (voetschakelaar) |
| Veiligheid | Panic-knop (Esc), automatisch herstel na audio-uitval, alle noten uit bij het loskoppelen van een keyboard, autosave elke 2 minuten, crashlog |
| Plug-ins scannen | Elke plug-in wordt in een apart proces gescand: een crashende plug-in neemt de app niet mee |

## Bouwen op Windows

**Eenmalig installeren**
1. [Visual Studio 2022 Community](https://visualstudio.microsoft.com/vs/community/) met de workload *Desktop development with C++* (bevat CMake).
2. [Git voor Windows](https://git-scm.com/download/win).

**Bouwen**
```bat
Scripts\build_windows.bat
```
De eerste keer downloadt CMake automatisch JUCE 8, sfizz en Catch2 (een paar minuten). Daarna bouwt het
de app, draait de tests en meldt waar `KlankModule.exe` staat:
`build\KlankModule_artefacts\Release\KlankModule.exe`

**Zonder Visual Studio: laat GitHub bouwen.** Zet deze map in een (privé) GitHub-repository. De workflow
`.github/workflows/windows.yml` bouwt en test automatisch op Windows en levert een zip met de `.exe` onder
*Actions > laatste run > Artifacts*.

**Installer maken (optioneel).** Open `installer\KlankModule.iss` in [Inno Setup 6](https://jrsoftware.org/isinfo.php)
en kies *Compile*. Installeert per gebruiker, zonder beheerdersrechten.

### ASIO (aanbevolen voor de laagste vertraging)
Zonder ASIO gebruikt de app WASAPI (*Windows Audio* of *Exclusive Mode*). Dat werkt met elke geluidskaart.
Heeft je audio-interface een ASIO-driver, bouw dan met ASIO:
1. Download de ASIO SDK van [steinberg.net/developers](https://www.steinberg.net/developers/).
2. Pak uit naar `ThirdParty\asiosdk`, zodat `ThirdParty\asiosdk\common\iasiodrv.h` bestaat.
3. Draai `Scripts\build_windows.bat` opnieuw. Het script ziet de SDK en zet ASIO aan.

Licentie: voor eigen gebruik is dit vrij. Wil je de app verspreiden, kies dan de gratis propriëtaire
ASIO-licentie van Steinberg (app mag gesloten blijven) of de GPLv3-optie (app wordt open source).

## Eerste keer gebruiken
1. Start `KlankModule.exe`. Part 1 speelt meteen de ingebouwde Electric Piano op alle MIDI-ingangen.
2. **Instellingen > Audio**: kies je uitgang. ASIO of *Windows Audio (Exclusive Mode)* met 128 samples geeft
   ongeveer 3 ms vertraging. Klik *Testtoon* om te controleren.
3. Speel. In de bovenbalk knippert een lampje per MIDI-apparaat.
4. Betere piano: voer `Scripts\download_sounds.ps1` uit (Salamander Grand Piano, ~700 MB, CC-BY 3.0).
   Kies daarna bij een part *Klank > SFZ-bibliotheek laden...*.
5. VST3-instrumenten: *Instellingen > Plug-ins > Options > Scan for new or updated VST3 plug-ins*.

### Voorbeeld: split met bas links en piano rechts
| Part | Klank | Laag | Hoog |
| --- | --- | --- | --- |
| 1 | Salamander Grand Piano | C3 | G8 |
| 2 | Synth Bass | C-2 | B2 |

Sla op met **Opslaan als...**. De nummers in de performancelijst (0, 1, 2, ...) zijn ook de
Program Change-nummers op het controlekanaal.

## Sneltoetsen
| Toets | Actie |
| --- | --- |
| Esc | Panic: alle noten uit |
| Page Up / Page Down | Vorige / volgende performance |
| Ctrl+S | Opslaan |
| Ctrl+ + / Ctrl+ - | Interface groter / kleiner |

## Waar staat wat
| Wat | Map |
| --- | --- |
| Performances | `Documenten\KlankModule\Performances` |
| Klankbibliotheken | `Documenten\KlankModule\Sounds` |
| Instellingen, MIDI Learn, plug-inlijst, autosave | `%APPDATA%\KlankModule` |
| Logboek en crashlog | `%LOCALAPPDATA%\KlankModule\logs` |
| Patchlijst Program Change (bewerkbaar) | `%APPDATA%\KlankModule\program-map.json` |

## Testen zonder geluidskaart
`KlankModuleRender.exe` speelt een MIDI-bestand door de complete engine en schrijft een WAV:
```bat
KlankModuleRender.exe --performance mijn.kmperf --midi liedje.mid --out test.wav
```
Zonder `--midi` speelt het een korte demo. Handig om klanken te vergelijken of fouten te reproduceren.

## Bekende beperkingen fase 1
- Ingebouwde klanken zijn synthesizers; voor een echte piano gebruik je een SFZ-bibliotheek of een VST3.
- General MIDI-modus kiest per GM-familie een ingebouwde klank; er is nog geen GM-drumkit.
- MIDI loopt via de standaard Windows-MIDI-API. Op Windows 11 gaat dat automatisch via Windows MIDI
  Services (meerdere apps tegelijk op één keyboard). Echte MIDI 2.0-functies volgen in fase 3.
- MIDI-uitgang naar hardware-synths, SysEx en MIDI-clock volgen in fase 3.
- Niet-opgeslagen wijzigingen van een performance vervallen als je naar een andere performance wisselt
  (wel bewaard bij afsluiten via de autosave). Sla op voor je wisselt.

## Projectstructuur
Zie [Docs/ARCHITECTUUR.md](Docs/ARCHITECTUUR.md).

## Licenties van onderdelen
| Onderdeel | Licentie |
| --- | --- |
| JUCE 8 | AGPLv3 of commercieel (gratis Starter-licentie tot een omzetgrens) |
| sfizz | BSD-2-Clause |
| VST3 SDK (via JUCE) | MIT |
| ASIO SDK (optioneel) | Propriëtair (gratis) of GPLv3 |
| Catch2 (alleen tests) | BSL-1.0 |
| Salamander Grand Piano (download) | CC-BY 3.0, Alexander Holm |
