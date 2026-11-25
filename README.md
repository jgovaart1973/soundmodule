# KlankModule

Een Windows x64-programma dat je pc verandert in een hoogwaardige **klankmodule voor MIDI-apparaten**.
Sluit je keyboard aan, start de app en speel. Klaar: **fase 1** (de klankmodule), **fase 2a**
(opnemen en arrangeren), **fase 2b** (pianorol, ongedaan maken), **fase 3a** (audiosporen en
tempowisselingen), **fase 3b** (drumstel en drum-editor, arpeggiator, akkoordspoor), **fase 3c**
(notenbalk, hardware-synths via MIDI-uit, MIDI-clock, SysEx), **fase 3d** (MPE, MIDI 2.0, CLAP-plug-ins)
**versie 0.4** (akoestische instrumenten, twee stemmen per notenbalk, triolen, installatieprogramma) en
**versie 0.5** (transponeren per spoor, maten kopieren met Shift + slepen, terug- en vooruitspoelen,
meerdere akkoorden per maat, geen tik meer bij het aanslaan van de piano).

## Installeren
Download `KlankModule-<versie>-setup-x64.exe` (GitHub > Actions > laatste build > *KlankModule-setup*) en start het.
Een nieuwere versie installeer je gewoon over de oude heen. Je hebt geen
beheerdersrechten nodig: standaard installeert het alleen voor jou (of kies *voor alle gebruikers*).
Het installatieprogramma maakt een snelkoppeling in het Startmenu (en desgewenst op het bureaublad),
koppelt `.kmsong`- en `.kmperf`-bestanden aan KlankModule (dubbelklik opent ze) en kan meteen de gratis
concertvleugel downloaden. Verwijderen gaat via *Instellingen > Apps*; je eigen performances, nummers
en instellingen blijven dan bewaard.

Windows SmartScreen kan waarschuwen omdat het programma (nog) niet digitaal ondertekend is:
kies *Meer informatie > Toch uitvoeren*.

## Wat het nu kan (fase 1)

| Functie | Details |
| --- | --- |
| 16 multitimbrale parts | Per part: MIDI-ingang (of alle), kanaal (of omni), klank, volume, pan, transpose, key split, velocity layer en aanslagcurve |
| Klanken | 8 ingebouwde klanken + drumstel, SFZ-bibliotheken via sfizz (bijv. Salamander Grand Piano), VST3- en CLAP-instrumenten, hardware-synths via MIDI-uit |
| Mixer | 16 kanalen met mute/solo, meters, 2 effect-sends (galm, ping-pong delay), transparante master-limiter |
| Performances | Alle instellingen opslaan als `.kmperf` (leesbare JSON). Wisselen zonder hangende noten: lopende noten klinken uit op de oude klank |
| Snel wisselen | Klanken van alle opgeslagen performances worden vooraf geladen; gelijke klanken worden hergebruikt |
| MIDI-bediening | Program Change + Bank Select per part, performance kiezen via Program Change op een controlekanaal (standaard 16), General MIDI-modus |
| MIDI Learn | Rechtsklik op een schuif of knop > MIDI Learn. Ook voor volgende/vorige performance en Panic (voetschakelaar) |
| Veiligheid | Panic-knop (Esc), automatisch herstel na audio-uitval, alle noten uit bij het loskoppelen van een keyboard, autosave elke 2 minuten, crashlog |
| Plug-ins scannen | Elke plug-in wordt in een apart proces gescand: een crashende plug-in neemt de app niet mee |

## Plug-ins live bewerken
Elke part met een VST3- of CLAP-instrument heeft een knop **VST**. Die opent het originele venster van de plug-in,
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

## Arrangeren in versie 0.5
- **Transponeren per spoor**: in de kop van elk MIDI-spoor staat *Tr 0*. Klik op + / - (of sleep
  omhoog/omlaag, dubbelklik = 0) om het spoor bij het afspelen in halve tonen te verschuiven; de noten
  zelf blijven staan. Rechtsklik op de spoorkop voor snelle keuzes (octaaf, kwint ...) en *Transpositie
  in de noten zetten*. Drumsporen worden nooit getransponeerd. MIDI- en audio-export klinken getransponeerd.
- **Maten kopieren met Shift + slepen**:
  - Klik op een clip: de maat onder de muis wordt geselecteerd (geel kader).
  - Sleep over een lege plek, of Shift+klik op een andere maat of ander spoor: een **groep maten**
    (ook over meerdere sporen).
  - **Shift + slepen** vanuit de selectie (of vanuit een willekeurige maat) kopieert die maat of groep
    naar de plek waar je loslaat, ook naar een ander spoor. Een groen kader toont waar de kopie komt.
    Wat daar stond wordt vervangen. De kopie blijft geselecteerd, dus nog eens Shift + slepen gaat verder.
  - **Ctrl+D** zet de selectie er direct achter; rechtsklik op de selectie herhaalt hem 1, 2 of 4 keer.
  - Gewoon slepen van een clip verplaatst hem, zoals voorheen.
- **Terug- en vooruitspoelen**: de knoppen ◀◀ en ▶▶ (ingedrukt houden spoelt door), of de toetsen
  , en . (ook pijl links/rechts en - / + op het numerieke deel): een maat terug of verder.

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

## Ingebouwde instrumenten (versie 0.4)
Naast de 8 synthesizerklanken zijn er nu akoestische instrumenten. Ze gebruiken geen samples maar
natuurkundige modellen, dus ze laden direct en kosten bijna geen geheugen:

| Instrument | Hoe het werkt |
| --- | --- |
| Acoustic Grand Piano, Bright Piano | Snaarmodel: boventonen die iets te hoog liggen zoals bij een echte piano, twee snaren per toon (zweving), hamergeluid, demper bij loslaten (bovenste octaaf zonder demper), sustainpedaal |
| Nylon Guitar, Steel Guitar, Clean Electric Guitar | Getokkelde snaar (Karplus-Strong) met plukplaats, klankkast (akoestisch) en demping bij loslaten |
| Fretless Bass, Finger Bass | Fretless: glijdt de toon in met de typische "mwah"; Finger Bass: getokkelde snaar |
| Soprano Sax, Alto Sax, Tenor Sax, Flute | Blaasmodel: ademruis, formanten per sax, harder spelen = helderder, toon die even onder de noot begint, vibrato dat na een moment invalt (modulatiewiel = meer vibrato) |
| Choir Aahs, Choir Oohs | Vier zangers per toon, licht verstemd, op de klinkers "aa" en "oe" |
| Drumstel Akoestisch, Drumstel Elektronisch | Akoestisch: vel-modellen, snarenmat, bekkens; Elektronisch: het analoge 808-achtige stel |

General MIDI kiest deze instrumenten nu ook (piano 0-3, gitaren 24-28, bassen 32-35, koor 52-53,
saxen 64-67, fluiten 72-79); een Program Change 25 op kanaal 10 kiest het elektronische drumstel.
Oudere performances met "Drumstel (GM)" houden hun (elektronische) klank.

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
- Dubbelklik of rechtsklik op een **tel** om daar een akkoord te kiezen (12 grondtonen x 14 soorten:
  majeur, mineur, 7, maj7, m7, dim, aug, sus2, sus4, 6, m6, add9, m7b5, dim7). **Meerdere akkoorden per
  maat**: klik op tel 3 midden in een akkoord en kies *Akkoord vanaf maat x, tel 3*; het bestaande
  akkoord wordt dan gesplitst. Rechtsklik op een akkoord om de lengte te veranderen (twee maten tot
  een tel), het hele akkoord te wijzigen of het te verwijderen.
- Rechtsklik op een MIDI-clip > **Akkoorden herkennen**: KlankModule bepaalt per maat het akkoord
  (de basnoot telt mee), en twee akkoorden per maat als de harmonie halverwege duidelijk wisselt.
- **Begeleiding maken** / **Baslijn maken** (rechtsklik op het akkoordspoor) zet een nieuwe clip met
  akkoorden en/of grondtonen op het gekozen MIDI-spoor - handig als startpunt, daarna vrij te bewerken.

## Notenbalk (fase 3c)
In de pianorol zet de knop **Notenbalk** de clip om in bladmuziek op een pianosysteem (G- en F-sleutel).
- Toonsoort: *Automatisch* raadt hem uit de noten, of kies er zelf een (wordt in het nummer bewaard).
- Weergave in 1/8, 1/16 of 1/32: alleen de weergave wordt afgerond, het nummer blijft zoals het is.
- Notenwaarden met punten en overbindingen, waardestrepen per tel (6/8 in groepjes van drie), voortekens
  die per maat blijven gelden, rusten, hulplijnen. Noten vanaf C4 staan in de G-sleutel.
- **Twee stemmen per notenbalk**: een aangehouden noot boven een bewegende lijn (of andersom) wordt
  netjes als bovenstem (stokken omhoog) en onderstem (stokken omlaag) geschreven, met de rusten uit
  elkaars weg. Een akkoord blijft één akkoord. Kies *1 stem per balk* voor de eenvoudige weergave.
- **Triolen** worden per tel vanzelf herkend: achtste- en zestiende-triolen, en kwart-triolen over
  twee tellen, ook als ze niet precies gespeeld zijn. Ze krijgen een **3** (met haak als ze geen
  gezamenlijke waardestreep hebben). Rechte noten blijven recht.
- Triolen invoeren: kies in de pianorol het raster *1/4 triool*, *1/8 triool* of *1/16 triool* om
  te tekenen en te quantizen; de arpeggiator heeft 1/8- en 1/16-triolen.
- **Opslaan als afbeelding...** maakt een PNG om te printen. Ctrl+muiswiel zoomt.
- Bewerken doe je in de pianorol of de drum-editor: de notenbalk volgt direct.

## Hardware-synths, MIDI-clock en SysEx (fase 3c)
- Kies bij een part *Klank > Hardware-synth (MIDI-uitgang) > apparaat > kanaal*. Live spelen, het nummer,
  de arpeggiator en het akkoordspoor sturen die part dan naar de hardware. *Kanaal zoals binnenkomt*
  laat een multitimbrale synth alle kanalen ontvangen.
- *Programma en bank voor de hardware...* (in hetzelfde menu): KlankModule kiest die klank op de synth
  zodra de performance of het nummer start.
- Wisselen, panic en stoppen sturen note-offs: er blijven geen noten hangen op de hardware.
- De timing loopt gelijk met de geluidskaart (de vertraging van de audio wordt meegerekend).
  Fijnregelen kan in *Instellingen > MIDI-uit*.
- Daar kies je ook de apparaten die **MIDI-clock** krijgen (24 ppq + start/stop/continue + songpositie),
  zodat drummachines en sequencers meelopen, en stuur je een **SysEx-bestand** (.syx) naar een apparaat.
- Het geluid van de hardware neem je op via een audiospoor (fase 3a).

## MPE en MIDI 2.0 (fase 3d)
- **MPE** (per-noot-expressie, bijvoorbeeld ROLI Seaboard, Expressive E Osmose, LinnStrument):
  zet bij een part *Klank > MPE* aan. De part stuurt dan de MPE-configuratie (lagere zone, 15 kanalen,
  buiging ±48) naar de klank. De ingebouwde klanken reageren per noot op buiging, druk (luider en
  helderder) en timbre (CC74); VST3- en CLAP-plug-ins met MPE-ondersteuning krijgen hetzelfde.
  Een hardware-synth op MIDI-uit krijgt de configuratie ook.
- **MIDI 2.0**: *Instellingen > MIDI > MIDI 2.0* opent de ingangen als Universal MIDI Packet-apparaten.
  Waarden met hoge resolutie worden netjes omgerekend, en per-noot-buiging, -druk en -controllers
  worden omgezet naar MPE (zet dan ook MPE aan op de part). Apparaten die alleen MIDI 1.0 kennen,
  werken gewoon door. Zie de beperkingen hieronder voor Windows.

## CLAP-plug-ins (fase 3d)
CLAP-instrumenten werken net als VST3: scannen (in een apart proces), kiezen in het klankmenu
(met *(CLAP)* achter de naam), het eigen venster openen en live bewerken, en de instellingen worden
in de performance en het nummer bewaard. Zoekmappen: `C:\Program Files\Common Files\CLAP`,
`%LOCALAPPDATA%\Programs\Common\CLAP` en de mappen in de omgevingsvariabele `CLAP_PATH`.
Heeft een plug-in geen eigen venster, dan krijg je schuiven voor alle parameters.

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
5. Plug-ins: *Instellingen > Plug-ins > Options > Scan for new or updated VST3 plug-ins* (en idem voor *CLAP*).

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
| , / . (of pijl links/rechts) | Een maat terug / verder (in de Arranger) |
| Ctrl+D | Geselecteerde maten (of clip) dupliceren (in de Arranger) |
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
  Services (meerdere apps tegelijk op één keyboard).
- MIDI 2.0 op Windows: deze build gebruikt nog niet de Windows MIDI Services-SDK (die is nog in preview).
  Windows levert MIDI 2.0-apparaten daardoor als MIDI 1.0 aan: ze werken, maar zonder hoge resolutie en
  per-noot-expressie. Zodra de SDK stabiel is, kan de build hem aanzetten; de rest van de app is er klaar voor.
  MPE-controllers werken wel volledig.
- De notenbalk is alleen-lezen, heeft maximaal twee stemmen per balk, alleen triolen (geen kwintolen e.d.)
  en nog geen slagwerknotatie voor drumsporen.
- De akoestische instrumenten zijn modellen, geen opnames: een goede SFZ-bibliotheek of plug-in klinkt
  realistischer (zie *Gratis piano downloaden*).
- MIDI-clock wordt alleen verstuurd, niet ontvangen (KlankModule volgt nog geen externe clock).
- CLAP: geen polyfone modulatie en geen note-expressies van de plug-in terug; wel MPE via MIDI.
- Audioclips inkorten (trimmen) en tijdrekken volgen later. Geleidelijke tempoveranderingen (accelerando) ook.
- Akkoordherkenning werkt per maat; twee akkoorden binnen een maat worden nog niet apart herkend.
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
| CLAP SDK | MIT |
| ASIO SDK (optioneel) | Propriëtair (gratis) of GPLv3 |
| Catch2 (alleen tests) | BSL-1.0 |
| Salamander Grand Piano (download) | CC-BY 3.0, Alexander Holm |
