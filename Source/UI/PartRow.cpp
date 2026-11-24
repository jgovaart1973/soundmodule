#include "UI/PartRow.h"

#include "Core/AppPaths.h"
#include "Engine/Arpeggiator.h"
#include "Engine/BuiltInDrums.h"
#include "Engine/BuiltInSynth.h"
#include "Engine/ExternalMidiInstrument.h"
#include "Engine/SfzInstrument.h"
#include "UI/KmLookAndFeel.h"

namespace km
{
//==============================================================================
void LevelMeter::setLevel (float peak)
{
    const float db = juce::Decibels::gainToDecibels (peak, -60.0f);
    const float target = juce::jmap (db, -60.0f, 0.0f, 0.0f, 1.0f);
    const float next = target > level ? target : level * 0.85f; // fast attack, smooth fall
    if (std::abs (next - level) > 0.003f)
    {
        level = next;
        repaint();
    }
}

void LevelMeter::paint (juce::Graphics& g)
{
    const bool vertical = getHeight() > getWidth();
    auto r = vertical ? getLocalBounds().toFloat().reduced ((float) getWidth() * 0.3f, 0.0f)
                      : getLocalBounds().toFloat().reduced (0.0f, (float) getHeight() * 0.3f);
    g.setColour (colours::outline);
    g.fillRoundedRectangle (r, 2.0f);
    auto fill = vertical ? r.withTop (r.getBottom() - r.getHeight() * level)
                         : r.withWidth (r.getWidth() * level);
    g.setColour (level > 0.95f ? colours::danger : level > 0.8f ? colours::warn : colours::good);
    g.fillRoundedRectangle (fill, 2.0f);
}

void ActivityLamp::update (uint32_t counter)
{
    if (counter != last)
    {
        last = counter;
        glow = 1.0f;
        repaint();
    }
    else if (glow > 0.0f)
    {
        glow = juce::jmax (0.0f, glow - 0.15f);
        repaint();
    }
}

void ActivityLamp::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().withSizeKeepingCentre (9.0f, 9.0f);
    g.setColour (colours::outline);
    g.fillEllipse (r);
    g.setColour (colours::good.withAlpha (glow));
    g.fillEllipse (r);
}

//==============================================================================
juce::Array<std::pair<juce::String, int>> PartRow::getColumns()
{
    juce::Array<std::pair<juce::String, int>> cols;
    for (auto& c : std::initializer_list<std::pair<const char*, int>> { { "", 30 }, { "#", 26 }, { "MIDI-ingang", 150 }, { "Kanaal", 74 }, { "Klank", 210 }, { "", 46 }, { "", 44 },
             { "Volume", 140 }, { "Pan", 40 }, { "Transp.", 64 }, { "Laag", 64 }, { "Hoog", 64 },
             { "Vel. min", 56 }, { "Vel. max", 56 }, { "Curve", 84 }, { "Galm", 40 }, { "Delay", 40 },
             { "", 30 }, { "", 30 }, { "", 70 } })
        cols.add ({ juce::String (c.first), c.second });
    return cols;
}

PartRow::PartRow (AppController& c, int i)
    : controller (c), index (i),
      volume (c.getMidiLearn(), "part" + juce::String (i + 1) + ".volume"),
      pan (c.getMidiLearn(), "part" + juce::String (i + 1) + ".pan"),
      reverb (c.getMidiLearn(), "part" + juce::String (i + 1) + ".reverb"),
      delay (c.getMidiLearn(), "part" + juce::String (i + 1) + ".delay")
{
    auto& params = controller.partParams (index);

    addAndMakeVisible (enableButton);
    enableButton.setTooltip ("Part aan/uit");
    enableButton.onClick = [this] { controller.setPartEnabled (index, enableButton.getToggleState()); };

    addAndMakeVisible (numberLabel);
    numberLabel.setText (juce::String (index + 1), juce::dontSendNotification);
    numberLabel.setJustificationType (juce::Justification::centred);
    numberLabel.setColour (juce::Label::textColourId, colours::textDim);

    addAndMakeVisible (inputBox);
    inputBox.onChange = [this]
    {
        const int sel = inputBox.getSelectedItemIndex();
        if (sel == 0) controller.setPartInput (index, {}, {});
        else if (juce::isPositiveAndBelow (sel - 1, inputIds.size()))
            controller.setPartInput (index, inputIds[sel - 1], inputBox.getText().upToLastOccurrenceOf (" (niet", false, false));
    };

    addAndMakeVisible (channelBox);
    channelBox.addItem ("Omni", 1);
    for (int ch = 1; ch <= 16; ++ch)
        channelBox.addItem ("Kan " + juce::String (ch), ch + 1);
    channelBox.onChange = [this] { controller.setPartChannel (index, channelBox.getSelectedId() - 1); };

    addAndMakeVisible (instrumentButton);
    instrumentButton.setTooltip ("Klik om een klank te kiezen");
    instrumentButton.onClick = [this] { showInstrumentMenu(); };

    addAndMakeVisible (editorButton);
    editorButton.setTooltip ("Origineel plug-in-venster openen of naar voren halen om live te bewerken");
    editorButton.setColour (juce::TextButton::buttonOnColourId, colours::accent);
    editorButton.onClick = [this] { if (onOpenEditor) onOpenEditor (index); };

    addAndMakeVisible (arpButton);
    arpButton.setTooltip ("Arpeggiator: aan/uit en instellingen");
    arpButton.setColour (juce::TextButton::buttonOnColourId, colours::learn);
    arpButton.onClick = [this] { showArpPanel(); };

    auto setupKnob = [this] (juce::Slider& s, double lo, double hi, double def, const juce::String& tip)
    {
        addAndMakeVisible (s);
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRange (lo, hi, 0.0);
        s.setDoubleClickReturnValue (true, def);
        s.setPopupDisplayEnabled (true, true, this);
        s.setTooltip (tip);
    };

    addAndMakeVisible (volume);
    volume.setSliderStyle (juce::Slider::LinearHorizontal);
    volume.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
    volume.setRange (-60.0, 6.0, 0.1);
    volume.setSkewFactorFromMidPoint (-12.0);
    volume.setDoubleClickReturnValue (true, 0.0);
    volume.textFromValueFunction = [] (double v) { return v <= -59.9 ? juce::String ("-inf") : juce::String (v, 1); };
    volume.onValueChange = [this, &params]
    {
        params.volumeDb = volume.getValue() <= -59.9 ? -80.0f : (float) volume.getValue();
        controller.markDirty();
    };

    setupKnob (pan, -1.0, 1.0, 0.0, "Pan");
    pan.textFromValueFunction = [] (double v)
    {
        const int pct = juce::roundToInt (std::abs (v) * 100.0);
        return pct == 0 ? juce::String ("C") : juce::String (pct) + (v < 0 ? " L" : " R");
    };
    pan.onValueChange = [this, &params] { params.pan = (float) pan.getValue(); controller.markDirty(); };

    auto percent = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    setupKnob (reverb, 0.0, 1.0, 0.15, "Galm (send)");
    reverb.textFromValueFunction = percent;
    reverb.onValueChange = [this, &params] { params.reverbSend = (float) reverb.getValue(); controller.markDirty(); };
    setupKnob (delay, 0.0, 1.0, 0.0, "Delay (send)");
    delay.textFromValueFunction = percent;
    delay.onValueChange = [this, &params] { params.delaySend = (float) delay.getValue(); controller.markDirty(); };

    auto setupIncDec = [this] (juce::Slider& s, double lo, double hi, const juce::String& tip)
    {
        addAndMakeVisible (s);
        s.setSliderStyle (juce::Slider::IncDecButtons);
        s.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
        s.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 36, 20);
        s.setRange (lo, hi, 1.0);
        s.setTooltip (tip);
    };

    setupIncDec (transpose, -24, 24, "Transponeren (halve tonen)");
    transpose.textFromValueFunction = [] (double v) { return (v > 0 ? "+" : "") + juce::String ((int) v); };
    transpose.onValueChange = [this] { controller.setPartTranspose (index, (int) transpose.getValue()); };
    transpose.updateText();

    setupIncDec (keyLow, 0, 127, "Laagste toets (key split)");
    setupIncDec (keyHigh, 0, 127, "Hoogste toets (key split)");
    for (auto* s : { &keyLow, &keyHigh })
    {
        s->textFromValueFunction = [] (double v) { return noteName ((int) v); };
        s->valueFromTextFunction = [] (const juce::String& t)
        {
            for (int n = 0; n < 128; ++n)
                if (noteName (n).equalsIgnoreCase (t.trim()))
                    return (double) n;
            return t.getDoubleValue();
        };
        s->onValueChange = [this] { controller.setPartKeyRange (index, (int) keyLow.getValue(), (int) keyHigh.getValue()); };
        s->updateText();
    }

    setupIncDec (velLow, 1, 127, "Laagste aanslagsterkte (velocity layer)");
    setupIncDec (velHigh, 1, 127, "Hoogste aanslagsterkte (velocity layer)");
    for (auto* s : { &velLow, &velHigh })
    {
        s->setTextBoxStyle (juce::Slider::TextBoxLeft, false, 30, 20);
        s->onValueChange = [this] { controller.setPartVelocityRange (index, (int) velLow.getValue(), (int) velHigh.getValue()); };
    }

    addAndMakeVisible (curveBox);
    curveBox.addItem ("Lineair", 1);
    curveBox.addItem ("Licht", 2);
    curveBox.addItem ("Zwaar", 3);
    curveBox.addItem ("Vast", 4);
    curveBox.setTooltip ("Aanslagcurve: Licht = meer volume bij zacht spelen, Zwaar = meer kracht nodig");
    curveBox.onChange = [this] { controller.setPartVelocityCurve (index, (filter::VelocityCurve) (curveBox.getSelectedId() - 1)); };

    for (auto* b : { &muteButton, &soloButton })
    {
        addAndMakeVisible (*b);
        b->setClickingTogglesState (true);
    }
    muteButton.setColour (juce::TextButton::buttonOnColourId, colours::danger);
    soloButton.setColour (juce::TextButton::buttonOnColourId, colours::warn);
    muteButton.onClick = [this, &params] { params.mute = muteButton.getToggleState(); controller.markDirty(); };
    soloButton.onClick = [this, &params] { params.solo = soloButton.getToggleState(); controller.markDirty(); };

    addAndMakeVisible (meter);
    addAndMakeVisible (lamp);

    refreshFromModel();
}

juce::String PartRow::noteName (int note)
{
    return juce::MidiMessage::getMidiNoteName (note, true, true, 3);
}

void PartRow::rebuildInputList()
{
    const auto& cfg = controller.getPerformance().parts[(size_t) index];
    inputBox.clear (juce::dontSendNotification);
    inputIds.clear();
    inputBox.addItem ("Alle MIDI-ingangen", 1);

    int selected = 1;
    for (auto& d : controller.getMidiRouter().getDevices())
    {
        inputIds.add (d.identifier);
        inputBox.addItem (d.name, inputIds.size() + 1);
        if (d.identifier == cfg.inputDeviceId)
            selected = inputIds.size() + 1;
    }

    if (cfg.inputDeviceId.isNotEmpty() && selected == 1)
    {
        // Saved device is unplugged right now: keep it visible.
        inputIds.add (cfg.inputDeviceId);
        inputBox.addItem (cfg.inputDeviceName + " (niet aangesloten)", inputIds.size() + 1);
        selected = inputIds.size() + 1;
    }
    inputBox.setSelectedId (selected, juce::dontSendNotification);
}

void PartRow::refreshFromModel()
{
    const auto& cfg = controller.getPerformance().parts[(size_t) index];
    auto& params = controller.partParams (index);

    enableButton.setToggleState (cfg.enabled, juce::dontSendNotification);
    rebuildInputList();
    channelBox.setSelectedId (cfg.channel + 1, juce::dontSendNotification);

    auto* inst = controller.getAssignedInstrument (index);
    juce::String name = controller.isPartLoading (index) ? "Laden..."
                      : cfg.instrument.isEmpty()          ? "- kies een klank -"
                      : cfg.instrument.displayName.isNotEmpty() ? cfg.instrument.displayName
                      : (inst != nullptr ? inst->getName() : juce::String ("?"));
    instrumentButton.setButtonText (name);
    const bool canEdit = inst != nullptr && inst->hasEditor() && ! controller.isPartLoading (index);
    editorButton.setEnabled (canEdit);
    editorButton.setTooltip (canEdit ? "Origineel plug-in-venster openen of naar voren halen om live te bewerken"
                                     : "Alleen VST3-plug-ins hebben een eigen venster");

    volume.setValue (params.volumeDb.load() < -59.9f ? -60.0 : params.volumeDb.load(), juce::dontSendNotification);
    pan.setValue (params.pan.load(), juce::dontSendNotification);
    reverb.setValue (params.reverbSend.load(), juce::dontSendNotification);
    delay.setValue (params.delaySend.load(), juce::dontSendNotification);
    transpose.setValue (cfg.transpose, juce::dontSendNotification);
    keyLow.setValue (cfg.keyLow, juce::dontSendNotification);
    keyHigh.setValue (cfg.keyHigh, juce::dontSendNotification);
    velLow.setValue (cfg.velLow, juce::dontSendNotification);
    velHigh.setValue (cfg.velHigh, juce::dontSendNotification);
    curveBox.setSelectedId ((int) cfg.velocityCurve + 1, juce::dontSendNotification);
    muteButton.setToggleState (params.mute.load(), juce::dontSendNotification);
    soloButton.setToggleState (params.solo.load(), juce::dontSendNotification);

    setAlpha (cfg.enabled ? 1.0f : 0.55f);
}

void PartRow::tick()
{
    auto& part = controller.getEngine().getPart (index);
    auto& params = part.params;

    meter.setLevel (part.getPeakAndReset());
    lamp.update (part.getActivityCount());

    // Reflect changes made by MIDI Learn controllers (unless the user is dragging).
    auto sync = [] (juce::Slider& s, double v)
    {
        if (s.getThumbBeingDragged() < 0 && std::abs (s.getValue() - v) > 1.0e-3)
            s.setValue (v, juce::dontSendNotification);
    };
    sync (volume, params.volumeDb.load() < -59.9f ? -60.0 : params.volumeDb.load());
    sync (pan, params.pan.load());
    sync (reverb, params.reverbSend.load());
    sync (delay, params.delaySend.load());
    muteButton.setToggleState (params.mute.load(), juce::dontSendNotification);
    editorButton.setToggleState (isEditorOpen != nullptr && isEditorOpen (index), juce::dontSendNotification);
    arpButton.setToggleState (params.arpOn.load(), juce::dontSendNotification);

    for (auto* s : { &volume, &pan, &reverb, &delay })
        s->refreshLearnState();
}

//==============================================================================
namespace
{
    /** Arpeggiator settings of one part, shown in a call-out bubble under the Arp button. */
    class ArpPanel final : public juce::Component
    {
    public:
        ArpPanel (AppController& c, int part) : controller (c), params (c.partParams (part))
        {
            addAndMakeVisible (onButton);
            onButton.setButtonText ("Arpeggiator aan");
            onButton.setToggleState (params.arpOn.load(), juce::dontSendNotification);
            onButton.onClick = [this] { params.arpOn = onButton.getToggleState(); controller.markDirty(); };

            addAndMakeVisible (mode);
            mode.addItemList (Arpeggiator::modeNames(), 1);
            mode.setSelectedId (params.arpMode.load() + 1, juce::dontSendNotification);
            mode.onChange = [this] { params.arpMode = mode.getSelectedId() - 1; controller.markDirty(); };

            addAndMakeVisible (rate);
            for (int i = 0; i < (int) Arpeggiator::rates().size(); ++i)
                rate.addItem (Arpeggiator::rates()[(size_t) i].name, i + 1);
            rate.setSelectedId (params.arpRate.load() + 1, juce::dontSendNotification);
            rate.onChange = [this] { params.arpRate = rate.getSelectedId() - 1; controller.markDirty(); };

            addAndMakeVisible (octaves);
            octaves.setSliderStyle (juce::Slider::IncDecButtons);
            octaves.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 40, 22);
            octaves.setRange (1, 4, 1);
            octaves.setValue (params.arpOctaves.load(), juce::dontSendNotification);
            octaves.onValueChange = [this] { params.arpOctaves = (int) octaves.getValue(); controller.markDirty(); };

            addAndMakeVisible (gate);
            gate.setSliderStyle (juce::Slider::LinearHorizontal);
            gate.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 22);
            gate.setRange (5.0, 100.0, 1.0);
            gate.textFromValueFunction = [] (double v) { return juce::String ((int) v) + "%"; };
            gate.setValue (params.arpGate.load() * 100.0, juce::dontSendNotification);
            gate.updateText();
            gate.onValueChange = [this] { params.arpGate = (float) gate.getValue() / 100.0f; controller.markDirty(); };

            addAndMakeVisible (latch);
            latch.setButtonText ("Latch (blijft spelen na loslaten)");
            latch.setToggleState (params.arpLatch.load(), juce::dontSendNotification);
            latch.onClick = [this] { params.arpLatch = latch.getToggleState(); controller.markDirty(); };

            for (auto [label, text] : { std::pair<juce::Label*, const char*> { &modeLabel, "Volgorde" }, { &rateLabel, "Snelheid" },
                                        { &octLabel, "Octaven" }, { &gateLabel, "Lengte" } })
            {
                addAndMakeVisible (*label);
                label->setText (text, juce::dontSendNotification);
                label->setColour (juce::Label::textColourId, colours::textDim);
            }
            setSize (300, 210);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (10);
            onButton.setBounds (r.removeFromTop (26));
            r.removeFromTop (6);
            auto row = [&r] (juce::Label& l, juce::Component& c)
            {
                auto line = r.removeFromTop (28);
                l.setBounds (line.removeFromLeft (80));
                c.setBounds (line);
                r.removeFromTop (4);
            };
            row (modeLabel, mode);
            row (rateLabel, rate);
            row (octLabel, octaves);
            row (gateLabel, gate);
            latch.setBounds (r.removeFromTop (26));
        }

    private:
        AppController& controller;
        PartParams& params;
        juce::ToggleButton onButton, latch;
        juce::ComboBox mode, rate;
        juce::Slider octaves, gate;
        juce::Label modeLabel, rateLabel, octLabel, gateLabel;
    };
} // namespace

void PartRow::showArpPanel()
{
    juce::CallOutBox::launchAsynchronously (std::make_unique<ArpPanel> (controller, index), arpButton.getScreenBounds(), nullptr);
}

//==============================================================================
void PartRow::showInstrumentMenu()
{
    juce::PopupMenu menu, builtIn, plugins, recent;

    for (int p = 0; p < BuiltInSynth::getNumPresets(); ++p)
        builtIn.addItem (100 + p, BuiltInSynth::getPresetName (p));
    builtIn.addSeparator();
    builtIn.addItem (100 + BuiltInDrums::presetIndex, "Drumstel (GM)");
    menu.addSubMenu ("Ingebouwde klanken", builtIn);

    menu.addItem (2, "SFZ-bibliotheek laden...", SfzInstrument::isAvailable());
    const auto recentFiles = juce::StringArray::fromLines (controller.getSettings().getValue ("recentSfz"));
    for (int r = 0; r < recentFiles.size(); ++r)
        recent.addItem (200 + r, juce::File (recentFiles[r]).getFileNameWithoutExtension(), SfzInstrument::isAvailable());
    if (! recentFiles.isEmpty())
        menu.addSubMenu ("Recente SFZ", recent);

    // Plug-in instruments grouped by manufacturer
    juce::Array<juce::PluginDescription> instruments;
    for (auto& d : controller.getKnownPlugins().getTypes())
        if (d.isInstrument)
            instruments.add (d);

    std::map<juce::String, juce::PopupMenu> byMaker;
    for (int k = 0; k < instruments.size(); ++k)
        byMaker[instruments[k].manufacturerName.isEmpty() ? juce::String ("Overig") : instruments[k].manufacturerName]
            .addItem (1000 + k, instruments[k].name + (instruments[k].pluginFormatName == "CLAP" ? " (CLAP)" : ""));
    for (auto& [maker, sub] : byMaker)
        plugins.addSubMenu (maker, sub);
    if (instruments.isEmpty())
        plugins.addItem (3, "Geen plug-ins gevonden - scan via Instellingen > Plug-ins (VST3 en CLAP)", false);
    menu.addSubMenu ("Plug-in-instrumenten (VST3 / CLAP)", plugins);

    // Hardware synths on a MIDI output
    juce::PopupMenu hardware;
    const auto outputs = juce::MidiOutput::getAvailableDevices();
    for (int d = 0; d < juce::jmin (40, outputs.size()); ++d)
    {
        juce::PopupMenu channels;
        channels.addItem (5000 + d * 20, "Kanaal zoals binnenkomt (multitimbraal)");
        channels.addSeparator();
        for (int ch = 1; ch <= 16; ++ch)
            channels.addItem (5000 + d * 20 + ch, "Kanaal " + juce::String (ch));
        hardware.addSubMenu (outputs[d].name, channels);
    }
    if (outputs.isEmpty())
        hardware.addItem (7, "Geen MIDI-uitgangen gevonden", false);
    menu.addSubMenu ("Hardware-synth (MIDI-uitgang)", hardware);

    menu.addSeparator();
    auto* inst = controller.getAssignedInstrument (index);
    const auto& partKey = controller.getPerformance().parts[(size_t) index].instrument;
    if (partKey.type == InstrumentKey::Type::midiOut)
        menu.addItem (6, "Programma en bank voor de hardware...");
    menu.addItem (8, "MPE / per-noot-expressie (Seaboard, Osmose, LinnStrument, MIDI 2.0)", true,
                  controller.getEngine().getPart (index).params.mpe.load());
    menu.addItem (4, "Origineel plug-in-venster (live bewerken)", inst != nullptr && inst->hasEditor());
    menu.addItem (5, "Klank verwijderen", inst != nullptr);

    juce::Component::SafePointer<PartRow> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&instrumentButton),
        [safe, instruments, recentFiles, outputs] (int result)
        {
            if (safe == nullptr || result == 0)
                return;
            auto& self = *safe;

            if (result >= 1000 && result - 1000 < instruments.size())
            {
                const auto& d = instruments.getReference (result - 1000);
                if (self.onPluginChosen)
                    self.onPluginChosen (self.index); // opens its window once it has loaded
                self.controller.setPartInstrument (self.index, { InstrumentKey::Type::plugin, d.createIdentifierString(), d.name, {} });
                if (! self.controller.getPerformance().parts[(size_t) self.index].enabled)
                    self.controller.setPartEnabled (self.index, true);
            }
            else if (result >= 200 && result < 1000)
            {
                self.controller.setPartInstrument (self.index, InstrumentKey::sfz (juce::File (recentFiles[result - 200])));
                if (! self.controller.getPerformance().parts[(size_t) self.index].enabled)
                    self.controller.setPartEnabled (self.index, true);
            }
            else if (result >= 100 && result < 200)
            {
                const int preset = result - 100;
                self.controller.setPartInstrument (self.index, InstrumentKey::builtIn (preset, preset == BuiltInDrums::presetIndex
                                                                                                  ? juce::String ("Drumstel (GM)")
                                                                                                  : BuiltInSynth::getPresetName (preset)));
                if (! self.controller.getPerformance().parts[(size_t) self.index].enabled)
                    self.controller.setPartEnabled (self.index, true);
            }
            else if (result >= 5000 && result < 5000 + 20 * outputs.size())
            {
                const auto& dev = outputs.getReference ((result - 5000) / 20);
                ExternalMidiInstrument::Settings s;
                const auto& old = self.controller.getPerformance().parts[(size_t) self.index].instrument;
                if (old.type == InstrumentKey::Type::midiOut)
                    s = ExternalMidiInstrument::Settings::fromString (old.pluginState); // keep program / bank
                s.channel = (result - 5000) % 20;
                self.controller.setPartInstrument (self.index, ExternalMidiInstrument::makeKey (dev.identifier, dev.name, s));
                if (! self.controller.getPerformance().parts[(size_t) self.index].enabled)
                    self.controller.setPartEnabled (self.index, true);
            }
            else if (result == 6) self.showHardwareProgramDialog();
            else if (result == 8)
            {
                auto& mpe = self.controller.getEngine().getPart (self.index).params.mpe;
                mpe = ! mpe.load();
                self.controller.markDirty();
                self.instrumentButton.setButtonText (self.instrumentButton.getButtonText());
            }
            else if (result == 2) self.chooseSfzFile();
            else if (result == 4) { if (self.onOpenEditor) self.onOpenEditor (self.index); }
            else if (result == 5) self.controller.setPartInstrument (self.index, {});
        });
}

void PartRow::showHardwareProgramDialog()
{
    const auto key = controller.getPerformance().parts[(size_t) index].instrument;
    if (key.type != InstrumentKey::Type::midiOut)
        return;
    const auto s = ExternalMidiInstrument::Settings::fromString (key.pluginState);

    auto* w = new juce::AlertWindow ("Hardware-synth: programma",
                                     "Welke klank moet de hardware kiezen als deze performance of dit nummer start?\n"
                                     "Leeg laten = niets sturen.",
                                     juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("program", s.program >= 0 ? juce::String (s.program + 1) : juce::String(), "Programma (1-128):");
    w->addTextEditor ("msb", s.bank >= 0 ? juce::String (s.bank >> 7) : juce::String(), "Bank MSB (CC 0, 0-127):");
    w->addTextEditor ("lsb", s.bank >= 0 ? juce::String (s.bank & 0x7f) : juce::String(), "Bank LSB (CC 32, 0-127):");
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Annuleren", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<PartRow> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, key] (int r)
    {
        std::unique_ptr<juce::AlertWindow> owner (w);
        if (safe == nullptr || r != 1)
            return;
        auto st = ExternalMidiInstrument::Settings::fromString (key.pluginState);
        const auto prog = w->getTextEditorContents ("program").trim();
        const auto msb = w->getTextEditorContents ("msb").trim(), lsb = w->getTextEditorContents ("lsb").trim();
        st.program = prog.isEmpty() ? -1 : juce::jlimit (1, 128, prog.getIntValue()) - 1;
        st.bank = (msb.isEmpty() && lsb.isEmpty()) ? -1
                                                   : juce::jlimit (0, 127, msb.getIntValue()) * 128 + juce::jlimit (0, 127, lsb.getIntValue());
        const auto deviceName = key.displayName.upToFirstOccurrenceOf (" kan ", false, false)
                                               .upToFirstOccurrenceOf (" (kanaal", false, false);
        safe->controller.setPartInstrument (safe->index, ExternalMidiInstrument::makeKey (key.ref, deviceName, st));
    }), false);
}

void PartRow::chooseSfzFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Kies een SFZ-bibliotheek", paths::soundsDir(), "*.sfz");
    juce::Component::SafePointer<PartRow> safe (this);

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe] (const juce::FileChooser& fc)
        {
            if (safe == nullptr || fc.getResult() == juce::File())
                return;
            auto& self = *safe;
            const auto file = fc.getResult();

            auto recent = juce::StringArray::fromLines (self.controller.getSettings().getValue ("recentSfz"));
            recent.removeString (file.getFullPathName());
            recent.insert (0, file.getFullPathName());
            recent.removeEmptyStrings();
            while (recent.size() > 10)
                recent.remove (recent.size() - 1);
            self.controller.getSettings().setValue ("recentSfz", recent.joinIntoString ("\n"));

            self.controller.setPartInstrument (self.index, InstrumentKey::sfz (file));
            if (! self.controller.getPerformance().parts[(size_t) self.index].enabled)
                self.controller.setPartEnabled (self.index, true);
        });
}

//==============================================================================
void PartRow::resized()
{
    auto area = getLocalBounds().reduced (4, 6);
    juce::Component* comps[] = { &enableButton, &numberLabel, &inputBox, &channelBox, &instrumentButton, &editorButton, &arpButton,
                                 &volume, &pan, &transpose, &keyLow, &keyHigh, &velLow, &velHigh, &curveBox,
                                 &reverb, &delay, &muteButton, &soloButton, &meter };
    const auto columns = getColumns();

    for (int c = 0; c < columns.size(); ++c)
    {
        auto cell = area.removeFromLeft (columns[c].second);
        area.removeFromLeft (4);
        comps[c]->setBounds (cell);
    }

    lamp.setBounds (numberLabel.getBounds().removeFromTop (12).withX (numberLabel.getRight() - 10).withWidth (10));
}

void PartRow::paint (juce::Graphics& g)
{
    g.setColour (index % 2 == 0 ? colours::panel : colours::panel.brighter (0.03f));
    g.fillRect (getLocalBounds());
    g.setColour (colours::outline.withAlpha (0.5f));
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

} // namespace km
