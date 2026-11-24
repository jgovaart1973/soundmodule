# KlankModule

Een Windows x64-programma dat je pc verandert in een hoogwaardige **klankmodule voor MIDI-apparaten**.
Sluit je keyboard aan, start de app en speel. Klaar: **fase 1** (de klankmodule), **fase 2a**
(opnemen en arrangeren), **fase 2b** (pianorol, ongedaan maken), **fase 3a** (audiosporen en
tempowisselingen) en **fase 3b** (drumstel en drum-editor, arpeggiator, akkoordspoor).

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

## Plug-ins live bewerken
Elke part met een VST3-instrument heeft een knop **VST**. Die opent het originele venster van de plug-in,
of haalt het naar voren als het al open is. Klik nog eens terwijl het voorop staat en het sluit.
- Kies je een nieuwe plug-in in het klankmenu, dan opent het venster vanzelf zodra hij geladen is.
- Alles wat je in het venster verandert hoor je direct; de performance krijgt de markering *gewijzigd*.
- **Opslaan** bewaart de plug-in-instellingen in de performance (en in een nummer).
- In de werkbalk van het venster zet je *Altijd bovenop* aan of uit. Het venster onthoudt zijn plek.

## Opnemen en arrangeren (fase 2a)
Schakel bovenin naar **Arranger** (F2). Terug naar de parts met **Module** (F1).

| Wat | Hoe |
| --- | --- |
| Opnemen | Druk op ● of R. Zonder gekozen spoor krijgt elke ingeschakelde part een eigen spoor (een split neemt dus bas en piano apart op). Standaard eerst één maat aftellen |
| Afspelen / stoppen | Spatiebalk, of ▶ en ■. Home = naar het begin |
| Loop | Sleep over de liniaal om een loopbereik te kiezen. Klik op de liniaal om de afspeelkop te verplaatsen |
| Takes | Neem op met loop aan: elke ronde wordt een take. De laatste speelt; rechtsklik op de clip > *Take kiezen* |
| Achteraf vastleggen | Speel vrij (zonder opname) en klik **Vastleggen**: wat je net speelde komt als clip op de tijdlijn |
| Clips | Slepen om te verplaatsen (op de tel), Delete om te verwijderen, Ctrl + muiswiel om te zoomen |
| Tempo / maatsoort | In de transportbalk; metronoom aan/uit met **Metronoom** |
| Bestanden | **Nummer...**: nieuw, openen, opslaan (`.kmsong`, inclusief de klanken), MIDI importeren, exporteren als MIDI of als audio (WAV of FLAC, 24 bit) |

## Noten bewerken: de pianorol (fase 2b)
Dubbelklik op een clip (of kies *Bewerken in pianorol*). Dubbelklik op een leeg stuk van een spoor
maakt een nieuwe clip van één maat en opent die.

| Wat | Hoe |
| --- | --- |
| Noot toevoegen / verwijderen | Dubbelklik op een lege plek / op een noot |
| Selecteren | Klik; Shift+klik voegt toe; sleep over een lege plek voor een rechthoek; Ctrl+A = alles |
| Verplaatsen / verlengen | Sleep een noot (tijd en toonhoogte); sleep de rechterrand om te verlengen. Pijltjes ←/→ schuiven één rasterstap |
| Transponeren | ↑/↓ (Shift = octaaf) of de knoppen -12, -1, +1, +12 |
| Raster | 1/4 tot 1/32 en triolen; *Raster* uit = vrij plaatsen |
| Quantize | Knop of Q. *Sterkte* (hoe ver naar het raster), *Swing* (100% = triool-gevoel), *Ook lengtes*. Werkt op de selectie, of op alles als niets gekozen is |
| Velocity | Sleep in de baan onder de noten (met selectie: alleen gekozen noten) |
| Controllers | Kies rechtsboven de baan (Modulatie, Expressie, Volume, Sustain, Pitch bend, Aftertouch) en teken met de muis; rechtermuisknop wist |
| Beluisteren | Klik op een noot of op het toetsenbord links |
| Zoomen / scrollen | Ctrl + muiswiel / muiswiel (Shift = horizontaal) |
| Ongedaan maken | Ctrl+Z, opnieuw Ctrl+Y (of de pijlen in de transportbalk). Werkt voor alles in het nummer, ook opnames |

In de Arranger dupliceert Ctrl+D de gekozen clip; Enter opent hem in de pianorol.

## Audiosporen (fase 3a)
Kies **+ Spoor > Audiospoor**. Een audiospoor neemt op via je audio-interface (microfoon, gitaar, line-in).

| Wat | Hoe |
| --- | --- |
| Ingang kiezen | In het spoor: *In 1+2* (stereo), *In 1* of *In 2* (mono). Welke fysieke ingangen dat zijn, kies je in *Instellingen > Audio* |
| Opnemen | Zet ● van het spoor aan en druk op opnemen. Het bestand komt in `Documenten\KlankModule\Recordings` (WAV, 24 bit) |
| Meeluisteren | **Mon** aan: je hoort de ingang terwijl het spoor op opnemen staat (gebruik ASIO voor de kleinste vertraging) |
| Vertraging | Wordt automatisch gecorrigeerd: de opname staat precies gelijk met wat je hoorde |
| Takes | Opnieuw opnemen over dezelfde plek: de oude opname blijft bewaard (uit). Rechtsklik > *Deze take gebruiken* |
| Importeren | *Nummer... > Audiobestand importeren* (WAV, FLAC, AIFF, MP3). Komt op het gekozen audiospoor, op de maat van de afspeelkop |
| Mixen | Volume (schuif) en pan (knopje) per spoor; M en S zoals bij MIDI-sporen |
| Export | *Exporteren als audio* neemt audiosporen mee |

Audio speelt op zijn eigen snelheid: een tempowissel verandert de plaats van een audioclip niet in seconden.
Opnemen met loop aan stopt de audio-opname bij het einde van de loop (één take).

## Tempowisselingen (fase 3a)
Onder de liniaal staat het **tempospoor**.
- Sleep een stuk omhoog of omlaag om het tempo daar te veranderen.
- Dubbelklik op een maat om daar een tempowissel te maken; rechtsklik op het bolletje om hem te verwijderen.
- Het tempoveld in de transportbalk toont en verandert het tempo op de afspeelkop.
- Afspelen, opnemen, metronoom, export en MIDI-bestanden (import en export) volgen de tempowisselingen.

Een geïmporteerd MIDI-bestand krijgt per kanaal een spoor op de part met hetzelfde nummer.
Parts zonder klank krijgen automatisch een passende ingebouwde klank.

## Drums (fase 3b)
- Nieuwe ingebouwde klank **Drumstel (GM)**: alle General MIDI-drumnoten (35-81) zijn gesynthetiseerd:
  kick, snare, rim, clap, hihats (een dichte hihat kapt de open af), toms, crash, ride, bell, percussie.
- In de pianorol zet de knop **Drums** de **drum-editor** aan: per rij een instrument met naam
  (Kick, Snare, Closed hat ...). Klik = slag zetten, klik op een slag = weghalen, Alt+klik = zachte slag.
  Een spoor met het drumstel (of MIDI-kanaal 10) opent automatisch in drum-modus.
- Een MIDI-bestand met kanaal 10 krijgt bij importeren automatisch een drumspoor met het drumstel.
- Programmawissels op een part met het drumstel worden genegeerd (het drumstel blijft staan).

## Arpeggiator (fase 3b)
Elke part heeft een knop **Arp**. Daar kies je:
- **Modus**: op, neer, op-neer, willekeurig, zoals gespeeld, akkoord (herhaald).
- **Snelheid** 1/4, 1/8, 1/8 triool, 1/16, 1/16 triool, 1/32 - loopt synchroon met het tempo als het
  nummer speelt, anders op het eigen tempo.
- **Octaven** (1-4), **gate** (nootlengte) en **latch** (blijft doorlopen na loslaten; een nieuw akkoord vervangt het).
- Opnemen legt de noten vast die de arpeggiator speelt. Instellingen worden in de performance bewaard.

## Akkoordspoor (fase 3b)
Onder het tempospoor staat het **akkoordspoor**.
- Dubbelklik of rechtsklik op een maat om een akkoord te kiezen (12 grondtonen x 14 soorten:
  majeur, mineur, 7, maj7, m7, dim, aug, sus2, sus4, 6, m6, add9, m7b5, dim7). Rechtsklik op een akkoord
  om de lengte te veranderen of het te verwijderen.
- Rechtsklik op een MIDI-clip > **Akkoorden herkennen**: KlankModule bepaalt per maat het akkoord
  (de basnoot telt mee) en zet ze op het akkoordspoor.
- **Begeleiding maken** / **Baslijn maken** (rechtsklik op het akkoordspoor) zet een nieuwe clip met
  akkoorden en/of grondtonen op het gekozen MIDI-spoor - handig als startpunt, daarna vrij te bewerken.

## Bouwen op Windows

**Eenmalig installeren**
1. [Visual Studio Community (2022 of nieuwer)](https://visualstudio.microsoft.com/vs/community/) met de workload *Desktop development with C++* (bevat CMake).
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
| F1 / F2 | Module / Arranger |
| Spatiebalk | Afspelen / stoppen |
| R | Opnemen (in de Arranger) |
| Ctrl+Z / Ctrl+Y | Ongedaan maken / opnieuw (nummer) |
| Q | Quantize (in de pianorol) |
| Page Up / Page Down | Vorige / volgende performance |
| Ctrl+S | Opslaan |
| Ctrl+ + / Ctrl+ - | Interface groter / kleiner |

## Waar staat wat
| Wat | Map |
| --- | --- |
| Performances | `Documenten\KlankModule\Performances` |
| Nummers | `Documenten\KlankModule\Songs` |
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
- General MIDI-modus kiest per GM-familie een ingebouwde klank; kanaal 10 gebruikt het ingebouwde drumstel.
- MIDI loopt via de standaard Windows-MIDI-API. Op Windows 11 gaat dat automatisch via Windows MIDI
  Services (meerdere apps tegelijk op één keyboard). Echte MIDI 2.0-functies volgen in fase 3.
- MIDI-uitgang naar hardware-synths, SysEx en MIDI-clock volgen in fase 3.
- Audioclips inkorten (trimmen) en tijdrekken volgen later. Geleidelijke tempoveranderingen (accelerando) ook.
- Akkoordherkenning werkt per maat; twee akkoorden binnen een maat worden nog niet apart herkend.
- Nog te doen in fase 3: notenbalk en MIDI-uitgang naar hardware (3c), MIDI 2.0 en CLAP-plug-ins (3d).
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
