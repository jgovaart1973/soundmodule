#include "UI/ArrangerComponent.h"

#include "Core/AppPaths.h"
#include "UI/KmLookAndFeel.h"

namespace km
{
namespace
{
    juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }

    juce::String formatPosition (const Song& s, double beat)
    {
        const double bar = juce::jmax (0.25, s.beatsPerBarAsQuarters());
        const int barNum = (int) std::floor (beat / bar + 1.0e-9) + 1;
        const double inBar = beat - (barNum - 1) * bar;
        const int beatNum = (int) std::floor (inBar + 1.0e-9) + 1;
        const double seconds = s.beatToSeconds (beat);
        const int minutes = (int) (seconds / 60.0);
        return juce::String::formatted ("Maat %d . %d     %d:%04.1f", barNum, beatNum, minutes, seconds - minutes * 60.0);
    }
} // namespace

//==============================================================================
class ArrangerComponent::TransportBar final : public juce::Component
{
public:
    explicit TransportBar (ArrangerComponent& o) : owner (o), song (o.song)
    {
        for (auto* b : { &toStart, &play, &stopButton, &recordButton, &loopButton, &clickButton, &countInButton,
                         &captureButton, &addTrackButton, &songButton, &undoButton, &redoButton })
            addAndMakeVisible (*b);
        undoButton.onClick = [this] { song.undo(); };
        redoButton.onClick = [this] { song.redo(); };

        toStart.setTooltip ("Naar het begin (Home)");
        play.setTooltip ("Afspelen / pauze (spatiebalk)");
        stopButton.setTooltip ("Stoppen");
        recordButton.setTooltip ("Opnemen (R). Zonder gekozen spoor neemt elke ingeschakelde part op een eigen spoor op.");
        captureButton.setTooltip ("Achteraf vastleggen: zet wat je net speelde (ook zonder opname) als clip op de tijdlijn");

        toStart.onClick = [this] { song.goToStart(); };
        play.onClick = [this] { song.togglePlay(); };
        stopButton.onClick = [this] { song.stop(); };
        recordButton.onClick = [this] { song.record(); };
        recordButton.setColour (juce::TextButton::buttonOnColourId, colours::danger);

        for (auto* t : { &loopButton, &clickButton, &countInButton })
            t->setClickingTogglesState (true);
        loopButton.onClick = [this]
        {
            const auto& s = song.getSong();
            song.setLoop (loopButton.getToggleState(), s.loopStart, s.loopEnd);
        };
        clickButton.onClick = [this] { song.setMetronome (clickButton.getToggleState()); };
        countInButton.onClick = [this] { song.setCountIn (countInButton.getToggleState()); };
        countInButton.setToggleState (song.isCountInOn(), juce::dontSendNotification);

        addAndMakeVisible (tempo);
        tempo.setSliderStyle (juce::Slider::IncDecButtons);
        tempo.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
        tempo.setRange (30.0, 300.0, 1.0);
        tempo.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 72, 24);
        tempo.textFromValueFunction = [] (double v) { return juce::String ((int) v) + " BPM"; };
        tempo.setTooltip ("Tempo op de afspeelkop. Tempowissels: dubbelklik in het tempospoor onder de liniaal.");
        tempo.onValueChange = [this]
        {
            // Change the tempo segment the playhead is in.
            const auto& s = song.getSong();
            const double pos = owner.controller.getEngine().sequencer.getPosition();
            double segment = 0.0;
            for (auto& tc : s.tempoChanges)
                if (tc.beat <= pos + 1.0e-9)
                    segment = tc.beat;
            song.setTempoAt (segment, tempo.getValue());
        };

        addAndMakeVisible (metre);
        const char* sigs[] = { "2/4", "3/4", "4/4", "5/4", "6/8", "7/8", "12/8" };
        for (int i = 0; i < 7; ++i)
            metre.addItem (sigs[i], i + 1);
        metre.onChange = [this]
        {
            const auto parts = juce::StringArray::fromTokens (metre.getText(), "/", {});
            song.setTimeSignature (parts[0].getIntValue(), parts[1].getIntValue());
        };

        addAndMakeVisible (position);
        position.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        position.setColour (juce::Label::textColourId, colours::accent);
        position.setColour (juce::Label::backgroundColourId, colours::background);

        addAndMakeVisible (songName);
        songName.setColour (juce::Label::textColourId, colours::textDim);
        songName.setJustificationType (juce::Justification::centredRight);

        captureButton.onClick = [this]
        {
            if (! song.captureRecentPlaying() && song.onStatus)
                song.onStatus ("Niets om vast te leggen: speel eerst iets op je keyboard.");
        };
        addTrackButton.onClick = [this]
        {
            juce::PopupMenu m;
            m.addItem (1, "MIDI-spoor (speelt een part)");
            m.addItem (2, "Audiospoor (microfoon, gitaar, audiobestanden)");
            juce::Component::SafePointer<ArrangerComponent> safe (&owner);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addTrackButton), [safe] (int r)
            {
                if (safe == nullptr || r == 0)
                    return;
                if (r == 2)
                {
                    safe->selectedTrack = safe->song.addAudioTrack();
                    return;
                }
                int part = 0;
                const auto& perf = safe->controller.getPerformance();
                for (int p = 0; p < kNumParts; ++p)
                    if (perf.parts[(size_t) p].enabled) { part = p; break; }
                safe->selectedTrack = safe->song.addTrack (part);
            });
        };
        songButton.onClick = [this] { owner.showSongMenu(); };

        refresh();
    }

    void refresh()
    {
        const auto& s = song.getSong();
        if (tempo.getThumbBeingDragged() < 0 && ! tempo.isMouseButtonDown (true))
            tempo.setValue (s.tempoAt (owner.controller.getEngine().sequencer.getPosition()), juce::dontSendNotification);
        metre.setText (juce::String (s.beatsPerBar) + "/" + juce::String (s.beatUnit), juce::dontSendNotification);
        loopButton.setToggleState (s.loopEnabled, juce::dontSendNotification);
        clickButton.setToggleState (song.isMetronomeOn(), juce::dontSendNotification);
        songName.setText (s.name + (song.isDirty() ? u8 ("  \xe2\x97\x8f") : juce::String()), juce::dontSendNotification);
        undoButton.setEnabled (song.canUndo());
        redoButton.setEnabled (song.canRedo());
        undoButton.setTooltip (song.canUndo() ? "Ongedaan maken: " + song.getUndoDescription() + " (Ctrl+Z)" : juce::String ("Niets om ongedaan te maken"));
        redoButton.setTooltip (song.canRedo() ? "Opnieuw: " + song.getRedoDescription() + " (Ctrl+Y)" : juce::String ("Niets om opnieuw te doen"));
    }

    void tick()
    {
        auto& seq = owner.controller.getEngine().sequencer;
        const bool rolling = seq.isRolling();
        play.setButtonText (rolling ? u8 ("\xe2\x8f\xb8") : u8 ("\xe2\x96\xb6"));
        recordButton.setToggleState (seq.isRecording(), juce::dontSendNotification);

        juce::String text = seq.isCountingIn() ? "Aftellen  " + juce::String ((int) std::ceil (seq.getCountInRemaining()))
                                               : formatPosition (song.getSong(), seq.getPosition());
        position.setText (text, juce::dontSendNotification);
        refresh();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 8);
        auto place = [&r] (juce::Component& c, int w) { c.setBounds (r.removeFromLeft (w)); r.removeFromLeft (6); };
        place (toStart, 40);
        place (play, 52);
        place (stopButton, 44);
        place (recordButton, 44);
        r.removeFromLeft (8);
        place (position, 230);
        r.removeFromLeft (8);
        place (loopButton, 60);
        place (clickButton, 96);
        place (countInButton, 88);
        place (tempo, 140);
        place (metre, 70);
        r.removeFromLeft (8);
        place (captureButton, 110);
        place (addTrackButton, 86);
        r.removeFromLeft (8);
        place (undoButton, 40);
        place (redoButton, 40);
        songButton.setBounds (r.removeFromRight (110));
        r.removeFromRight (8);
        songName.setBounds (r);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::panel);
        g.setColour (colours::outline);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    }

private:
    ArrangerComponent& owner;
    SongController& song;
    juce::TextButton toStart { u8 ("|\xe2\x97\x80") }, play { u8 ("\xe2\x96\xb6") }, stopButton { u8 ("\xe2\x96\xa0") },
                     recordButton { u8 ("\xe2\x97\x8f") }, loopButton { "Loop" }, clickButton { "Metronoom" },
                     countInButton { "Aftellen" }, captureButton { "Vastleggen" }, addTrackButton { "+ Spoor" },
                     songButton { "Nummer..." }, undoButton { u8 ("\xe2\x86\xb6") }, redoButton { u8 ("\xe2\x86\xb7") };
    juce::Slider tempo;
    juce::ComboBox metre;
    juce::Label position, songName;
};

//==============================================================================
class ArrangerComponent::TrackHeader final : public juce::Component
{
public:
    TrackHeader (ArrangerComponent& o, int t) : owner (o), track (t)
    {
        addAndMakeVisible (name);
        name.setEditable (false, true, false);
        name.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        name.onTextChange = [this] { owner.song.setTrackName (track, name.getText()); };

        addAndMakeVisible (partBox);
        partBox.setTooltip ("Welke part (klank) dit spoor laat spelen");
        partBox.onChange = [this] { owner.song.setTrackPart (track, partBox.getSelectedId() - 1); };

        for (auto* b : { &mute, &solo, &arm, &remove })
            addAndMakeVisible (*b);
        for (auto* b : { &mute, &solo, &arm })
            b->setClickingTogglesState (true);
        mute.setColour (juce::TextButton::buttonOnColourId, colours::danger);
        solo.setColour (juce::TextButton::buttonOnColourId, colours::warn);
        arm.setColour (juce::TextButton::buttonOnColourId, colours::danger);
        arm.setTooltip ("Opnemen op dit spoor");
        remove.setTooltip ("Spoor verwijderen");
        mute.onClick = [this] { owner.song.setTrackMute (track, mute.getToggleState()); };
        solo.onClick = [this] { owner.song.setTrackSolo (track, solo.getToggleState()); };
        arm.onClick = [this] { owner.song.setTrackArmed (track, arm.getToggleState()); };

        // Audio track controls
        addChildComponent (inputBox);
        inputBox.addItem ("In 1+2", 1);
        inputBox.addItem ("In 1", 2);
        inputBox.addItem ("In 2", 3);
        inputBox.setTooltip ("Welke ingang van je audio-interface dit spoor opneemt (stereo of mono)");
        inputBox.onChange = [this] { owner.song.setTrackInput (track, inputBox.getSelectedId() - 1); };
        addChildComponent (monitor);
        monitor.setClickingTogglesState (true);
        monitor.setColour (juce::TextButton::buttonOnColourId, colours::good.darker (0.3f));
        monitor.setTooltip ("Meeluisteren: hoor de ingang terwijl het spoor op opnemen staat");
        monitor.onClick = [this] { owner.song.setTrackMonitor (track, monitor.getToggleState()); };
        addChildComponent (volume);
        volume.setSliderStyle (juce::Slider::LinearHorizontal);
        volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        volume.setRange (-60.0, 6.0, 0.1);
        volume.setSkewFactorFromMidPoint (-12.0);
        volume.setDoubleClickReturnValue (true, 0.0);
        volume.setPopupDisplayEnabled (true, true, nullptr);
        volume.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " dB"; };
        volume.onDragEnd = [this] { owner.song.setTrackVolume (track, (float) volume.getValue()); };
        volume.onValueChange = [this] { if (volume.getThumbBeingDragged() < 0) owner.song.setTrackVolume (track, (float) volume.getValue()); };
        addChildComponent (pan);
        pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        pan.setRange (-1.0, 1.0);
        pan.setDoubleClickReturnValue (true, 0.0);
        pan.setTooltip ("Pan");
        pan.onDragEnd = [this] { owner.song.setTrackPan (track, (float) pan.getValue()); };

        remove.onClick = [this]
        {
            juce::Component::SafePointer<ArrangerComponent> safe (&owner);
            const int trackToRemove = track;
            juce::MessageManager::callAsync ([safe, trackToRemove] { if (safe != nullptr) safe->song.removeTrack (trackToRemove); });
        };
        refresh();
    }

    void refresh()
    {
        const auto& tr = owner.song.getSong().tracks[(size_t) track];
        name.setText (tr.name, juce::dontSendNotification);
        partBox.clear (juce::dontSendNotification);
        const auto& perf = owner.controller.getPerformance();
        for (int p = 0; p < kNumParts; ++p)
        {
            const auto& key = perf.parts[(size_t) p].instrument;
            partBox.addItem ("Part " + juce::String (p + 1) + (key.isEmpty() ? juce::String() : u8 (" \xc2\xb7 ") + key.displayName), p + 1);
        }
        partBox.setSelectedId (tr.part + 1, juce::dontSendNotification);
        mute.setToggleState (tr.mute, juce::dontSendNotification);
        solo.setToggleState (tr.solo, juce::dontSendNotification);
        arm.setToggleState (tr.armed, juce::dontSendNotification);

        const bool audio = tr.isAudio();
        partBox.setVisible (! audio);
        for (juce::Component* c : { (juce::Component*) &inputBox, (juce::Component*) &monitor, (juce::Component*) &volume, (juce::Component*) &pan })
            c->setVisible (audio);
        inputBox.setSelectedId (tr.input + 1, juce::dontSendNotification);
        monitor.setToggleState (tr.monitor, juce::dontSendNotification);
        if (volume.getThumbBeingDragged() < 0)
            volume.setValue (tr.volumeDb, juce::dontSendNotification);
        if (pan.getThumbBeingDragged() < 0)
            pan.setValue (tr.pan, juce::dontSendNotification);
        arm.setTooltip (audio ? "Opnemen op dit audiospoor (via de gekozen ingang)" : "Opnemen op dit spoor");
        resized();
        repaint();
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        owner.selectedTrack = track;
        owner.selectedClip = -1;
        owner.repaint();
        for (auto* h : owner.headers)
            h->repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 5);
        auto top = r.removeFromTop (24);
        remove.setBounds (top.removeFromRight (24));
        if (volume.isVisible())
        {
            top.removeFromRight (4);
            volume.setBounds (top.removeFromRight (96));
        }
        name.setBounds (top);
        r.removeFromTop (4);
        auto bottom = r.removeFromTop (24);
        arm.setBounds (bottom.removeFromRight (28));
        bottom.removeFromRight (3);
        solo.setBounds (bottom.removeFromRight (28));
        bottom.removeFromRight (3);
        mute.setBounds (bottom.removeFromRight (28));
        bottom.removeFromRight (6);
        if (inputBox.isVisible())
        {
            pan.setBounds (bottom.removeFromRight (24));
            bottom.removeFromRight (3);
            monitor.setBounds (bottom.removeFromRight (40));
            bottom.removeFromRight (3);
            inputBox.setBounds (bottom);
        }
        else
        {
            partBox.setBounds (bottom);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (owner.selectedTrack == track ? colours::panelLight : colours::panel);
        g.setColour (colours::outline);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    }

private:
    ArrangerComponent& owner;
    const int track;
    juce::Label name;
    juce::ComboBox partBox, inputBox;
    juce::TextButton mute { "M" }, solo { "S" }, arm { u8 ("\xe2\x97\x8f") }, remove { u8 ("\xc3\x97") }, monitor { "Mon" };
    juce::Slider volume, pan;
};

//==============================================================================
class ArrangerComponent::Ruler final : public juce::Component
{
public:
    explicit Ruler (ArrangerComponent& o) : owner (o) {}

    void paint (juce::Graphics& g) override
    {
        const auto& s = owner.song.getSong();
        g.fillAll (colours::background);

        if (s.loopEnabled || dragging)
        {
            const double ls = dragging ? juce::jmin (dragStart, dragEnd) : s.loopStart;
            const double le = dragging ? juce::jmax (dragStart, dragEnd) : s.loopEnd;
            const float x0 = (float) owner.beatToX (ls), x1 = (float) owner.beatToX (le);
            g.setColour (colours::accent.withAlpha (s.loopEnabled || dragging ? 0.35f : 0.12f));
            g.fillRect (x0, 0.0f, x1 - x0, (float) getHeight());
        }

        const double bar = juce::jmax (0.25, s.beatsPerBarAsQuarters());
        const int step = owner.pixelsPerBeat * bar < 40 ? 4 : 1;
        g.setFont (juce::FontOptions (12.0f));
        const int firstBar = juce::jmax (0, (int) std::floor (owner.scrollBeat / bar));
        for (int b = firstBar;; ++b)
        {
            const double x = owner.beatToX (b * bar);
            if (x > getWidth())
                break;
            const bool labelled = b % step == 0;
            g.setColour (labelled ? colours::textDim : colours::outline);
            g.drawVerticalLine ((int) x, labelled ? 8.0f : 16.0f, (float) getHeight());
            if (labelled)
                g.drawText (juce::String (b + 1), (int) x + 3, 0, 40, getHeight() - 6, juce::Justification::centredLeft);
        }

        const float px = (float) owner.beatToX (owner.controller.getEngine().sequencer.getPosition());
        juce::Path head;
        head.addTriangle (px - 6, 0, px + 6, 0, px, 10);
        g.setColour (colours::text);
        g.fillPath (head);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto& s = owner.song.getSong();
        const double bar = juce::jmax (0.25, s.beatsPerBarAsQuarters());
        dragStart = dragEnd = std::round (owner.xToBeat (e.x) / bar) * bar;
        dragging = false;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (std::abs (e.getDistanceFromDragStartX()) < 6)
            return;
        const auto& s = owner.song.getSong();
        const double bar = juce::jmax (0.25, s.beatsPerBarAsQuarters());
        dragging = true;
        dragEnd = juce::jmax (0.0, std::round (owner.xToBeat (e.x) / bar) * bar);
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragging && std::abs (dragEnd - dragStart) >= 1.0)
            owner.song.setLoop (true, juce::jmin (dragStart, dragEnd), juce::jmax (dragStart, dragEnd));
        else if (! dragging)
            owner.song.locate (juce::jmax (0.0, std::floor (owner.xToBeat (e.x))));
        dragging = false;
        repaint();
    }

private:
    ArrangerComponent& owner;
    double dragStart = 0, dragEnd = 0;
    bool dragging = false;
};

//==============================================================================
/** Tempo map under the ruler: drag a segment up/down, double-click to add a change, right-click to remove. */
class ArrangerComponent::TempoLane final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit TempoLane (ArrangerComponent& o) : owner (o)
    {
        setTooltip ("Tempo: sleep omhoog/omlaag, dubbelklik voor een tempowissel op die maat, rechtsklik op een wissel om te verwijderen");
    }

    void paint (juce::Graphics& g) override
    {
        const auto& s = owner.song.getSong();
        g.fillAll (colours::background.brighter (0.03f));
        g.setColour (colours::outline);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

        auto yFor = [this] (double bpm) { return (float) juce::jmap (juce::jlimit (40.0, 240.0, bpm), 40.0, 240.0, getHeight() - 4.0, 4.0); };

        std::vector<TempoChange> points { { 0.0, s.bpm } };
        points.insert (points.end(), s.tempoChanges.begin(), s.tempoChanges.end());

        g.setFont (juce::FontOptions (11.0f));
        for (size_t i = 0; i < points.size(); ++i)
        {
            const float x0 = (float) owner.beatToX (points[i].beat);
            const float x1 = i + 1 < points.size() ? (float) owner.beatToX (points[i + 1].beat) : (float) getWidth();
            const float y = yFor (points[i].bpm);
            g.setColour (colours::warn);
            g.drawLine (x0, y, x1, y, 1.5f);
            if (i + 1 < points.size())
                g.drawLine (x1, y, x1, yFor (points[i + 1].bpm), 1.0f);
            if (i > 0)
                g.fillEllipse (x0 - 3.0f, y - 3.0f, 6.0f, 6.0f);
            g.setColour (colours::text);
            g.drawText (juce::String (points[i].bpm, juce::exactlyEqual (points[i].bpm, std::floor (points[i].bpm)) ? 0 : 1),
                        (int) x0 + 4, 0, 60, getHeight(), juce::Justification::centredLeft, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto& s = owner.song.getSong();
        const double beat = juce::jmax (0.0, owner.xToBeat (e.x));

        // Right-click near a change marker: offer to remove it.
        if (e.mods.isPopupMenu())
        {
            for (auto& tc : s.tempoChanges)
                if (std::abs (owner.beatToX (tc.beat) - e.x) < 8.0)
                {
                    juce::PopupMenu m;
                    m.addItem (1, "Tempowissel verwijderen (" + juce::String (tc.bpm, 0) + " BPM)");
                    juce::Component::SafePointer<TempoLane> safe (this);
                    const double at = tc.beat;
                    m.showMenuAsync (juce::PopupMenu::Options(), [safe, at] (int r)
                    {
                        if (safe != nullptr && r == 1)
                            safe->owner.song.removeTempoChange (at);
                    });
                    return;
                }
            return;
        }

        segmentBeat = 0.0;
        for (auto& tc : s.tempoChanges)
            if (tc.beat <= beat + 1.0e-9)
                segmentBeat = tc.beat;
        startBpm = s.tempoAt (segmentBeat);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            return;
        const double bpm = std::round (juce::jlimit (30.0, 300.0, startBpm - e.getDistanceFromDragStartY() * 0.5));
        owner.song.setTempoAt (segmentBeat, bpm);   // quick repeats merge into one undo step
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto& s = owner.song.getSong();
        const double bar = juce::jmax (0.25, s.beatsPerBarAsQuarters());
        const double at = std::round (juce::jmax (0.0, owner.xToBeat (e.x)) / bar) * bar;
        if (at > 0.0)
            owner.song.setTempoAt (at, s.tempoAt (at));
    }

private:
    ArrangerComponent& owner;
    double segmentBeat = 0.0, startBpm = 120.0;
};

//==============================================================================
class ArrangerComponent::Lanes final : public juce::Component
{
public:
    explicit Lanes (ArrangerComponent& o) : owner (o) { setWantsKeyboardFocus (false); }

    void paint (juce::Graphics& g) override
    {
        const auto& s = owner.song.getSong();
        g.fillAll (colours::background);

        // Bar lines
        const double bar = juce::jmax (0.25, s.beatsPerBarAsQuarters());
        for (int b = juce::jmax (0, (int) std::floor (owner.scrollBeat / bar));; ++b)
        {
            const double x = owner.beatToX (b * bar);
            if (x > getWidth())
                break;
            g.setColour (colours::outline.withAlpha (0.5f));
            g.drawVerticalLine ((int) x, 0.0f, (float) getHeight());
        }

        if (s.loopEnabled)
        {
            g.setColour (colours::accent.withAlpha (0.06f));
            const float x0 = (float) owner.beatToX (s.loopStart), x1 = (float) owner.beatToX (s.loopEnd);
            g.fillRect (x0, 0.0f, x1 - x0, (float) getHeight());
        }

        for (int t = 0; t < (int) s.tracks.size(); ++t)
        {
            const int y = t * laneHeight;
            g.setColour (colours::outline.withAlpha (0.6f));
            g.drawHorizontalLine (y + laneHeight - 1, 0.0f, (float) getWidth());

            const auto& clips = s.tracks[(size_t) t].clips;
            for (int c = 0; c < (int) clips.size(); ++c)
                drawClip (g, clips[(size_t) c], t, c, y);

            const auto& audioClips = s.tracks[(size_t) t].audioClips;
            for (int c = 0; c < (int) audioClips.size(); ++c)
                drawAudioClip (g, s, audioClips[(size_t) c], t, c, y);
        }

        const float px = (float) owner.beatToX (owner.controller.getEngine().sequencer.getPosition());
        g.setColour (owner.controller.getEngine().sequencer.isRecording() ? colours::danger : colours::text);
        g.drawLine (px, 0.0f, px, (float) getHeight(), 1.5f);

        if (s.tracks.empty())
        {
            g.setColour (colours::textDim);
            g.setFont (juce::FontOptions (15.0f));
            g.drawFittedText (u8 ("Druk op \xe2\x97\x8f (of R) om op te nemen: elke ingeschakelde part krijgt een eigen spoor.\n"
                                  "Of speel eerst vrij en klik daarna op 'Vastleggen'. Importeer een MIDI-bestand via 'Nummer...'."),
                              getLocalBounds().reduced (40), juce::Justification::centred, 3);
        }
    }

    void drawClip (juce::Graphics& g, const Clip& clip, int t, int c, int y)
    {
        double start = clip.start;
        if (dragging && t == dragTrack && c == dragClip)
            start = juce::jmax (0.0, owner.snap (clip.start + dragDelta));

        const float x0 = (float) owner.beatToX (start);
        const float x1 = (float) owner.beatToX (start + clip.length);
        if (x1 < 0 || x0 > getWidth())
            return;

        auto r = juce::Rectangle<float> (x0, (float) y + 3, x1 - x0, (float) laneHeight - 7);
        const bool selected = t == owner.selectedTrack && c == owner.selectedClip;
        const auto base = clip.muted ? colours::outline : colours::accent.darker (0.6f);

        g.setColour (base.withAlpha (clip.muted ? 0.35f : 0.85f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (selected ? colours::text : (clip.muted ? colours::outline : colours::accent));
        g.drawRoundedRectangle (r, 4.0f, selected ? 2.0f : 1.0f);

        // Mini piano roll
        if (! clip.notes.empty())
        {
            int lo = 127, hi = 0;
            for (auto& n : clip.notes) { lo = juce::jmin (lo, n.note); hi = juce::jmax (hi, n.note); }
            const float range = (float) juce::jmax (12, hi - lo + 1);
            auto area = r.reduced (2.0f, 16.0f).withTrimmedBottom (-12.0f);
            g.setColour ((clip.muted ? colours::textDim : colours::text).withAlpha (0.8f));
            for (auto& n : clip.notes)
            {
                if (n.start >= clip.length)
                    continue;
                const float nx = x0 + (float) (n.start * owner.pixelsPerBeat);
                const float nw = juce::jmax (2.0f, (float) (juce::jmin (n.length, clip.length - n.start) * owner.pixelsPerBeat));
                const float ny = area.getBottom() - (float) (n.note - lo + 1) / range * area.getHeight();
                g.fillRect (nx, ny, nw, 2.5f);
            }
        }

        g.setColour (clip.muted ? colours::textDim : colours::text);
        g.setFont (juce::FontOptions (11.0f));
        // How many takes lie under this one (same range on the same track)?
        int takesHere = 0;
        for (auto& other : owner.song.getSong().tracks[(size_t) t].clips)
            if (other.start < clip.end() && clip.start < other.end())
                ++takesHere;
        auto label = clip.name.startsWith ("Take") ? clip.name : clip.name + (clip.take > 1 ? "  (take " + juce::String (clip.take) + ")" : juce::String());
        if (clip.muted)
            label << "  (uit)";
        if (takesHere > 1 && ! clip.muted)
            label << "   " << takesHere << " takes - rechtsklik om te kiezen";
        g.drawText (label, r.reduced (5.0f, 2.0f).removeFromTop (13.0f), juce::Justification::centredLeft, true);
    }

    void drawAudioClip (juce::Graphics& g, const Song& s, const AudioClip& clip, int t, int c, int y)
    {
        double start = clip.start;
        const int id = c | audioFlag;
        if (dragging && t == dragTrack && id == dragClip)
            start = juce::jmax (0.0, owner.snap (clip.start + dragDelta));

        const double startSec = s.beatToSeconds (start);
        const float x0 = (float) owner.beatToX (start);
        const float x1 = (float) owner.beatToX (s.secondsToBeat (startSec + clip.duration));
        if (x1 < 0 || x0 > getWidth())
            return;

        auto r = juce::Rectangle<float> (x0, (float) y + 3, juce::jmax (3.0f, x1 - x0), (float) laneHeight - 7);
        const bool selected = t == owner.selectedTrack && id == owner.selectedClip;
        const auto base = colours::good.darker (0.9f);
        g.setColour (base.withAlpha (clip.muted ? 0.3f : 0.85f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (selected ? colours::text : (clip.muted ? colours::outline : colours::good));
        g.drawRoundedRectangle (r, 4.0f, selected ? 2.0f : 1.0f);

        // Waveform from the cached overview (one column per pixel).
        if (auto data = owner.song.getAudioCache().get (juce::File (clip.file), owner.controller.getEngine().sequencer.getSampleRate()))
        {
            const auto wave = r.reduced (2.0f, 14.0f).withTrimmedBottom (-10.0f);
            const float mid = wave.getCentreY();
            g.setColour ((clip.muted ? colours::textDim : colours::good.brighter (0.4f)).withAlpha (0.9f));
            const double sr = data->sampleRate;
            const int firstPx = (int) juce::jmax (0.0f, x0), lastPx = (int) juce::jmin ((float) getWidth(), x1);
            for (int px = firstPx; px < lastPx; ++px)
            {
                const double secA = s.beatToSeconds (owner.xToBeat (px)) - startSec + clip.offset;
                const double secB = s.beatToSeconds (owner.xToBeat (px + 1)) - startSec + clip.offset;
                const int pa = juce::jmax (0, (int) (secA * sr) / AudioFileData::peakBlock);
                const int pb = juce::jmin ((int) data->peaks.size(), (int) (secB * sr) / AudioFileData::peakBlock + 1);
                float peak = 0.0f;
                for (int i = pa; i < pb; ++i)
                    peak = juce::jmax (peak, data->peaks[(size_t) i]);
                const float h = juce::jmin (1.0f, peak) * wave.getHeight() * 0.5f;
                g.drawVerticalLine (px, mid - h, mid + h + 1.0f);
            }
        }
        else
        {
            g.setColour (colours::danger);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText ("Bestand ontbreekt", r.reduced (5.0f), juce::Justification::centred, true);
        }

        g.setColour (clip.muted ? colours::textDim : colours::text);
        g.setFont (juce::FontOptions (11.0f));
        auto label = clip.name.isNotEmpty() ? clip.name : juce::File (clip.file).getFileNameWithoutExtension();
        if (clip.muted)
            label << "  (uit)";
        g.drawText (label, r.reduced (5.0f, 2.0f).removeFromTop (13.0f), juce::Justification::centredLeft, true);
    }

    bool hitClip (juce::Point<int> p, int& track, int& clip) const
    {
        const auto& s = owner.song.getSong();
        track = p.y / laneHeight;
        clip = -1;
        if (! juce::isPositiveAndBelow (track, (int) s.tracks.size()))
            return false;
        const double beat = owner.xToBeat (p.x);

        if (s.tracks[(size_t) track].isAudio())
        {
            const auto& audio = s.tracks[(size_t) track].audioClips;
            for (int pass = 0; pass < 2 && clip < 0; ++pass)
                for (int c = (int) audio.size() - 1; c >= 0; --c)
                {
                    const auto& a = audio[(size_t) c];
                    if (beat >= a.start && beat < s.audioClipEndBeat (a) && a.muted == (pass == 1))
                    {
                        clip = c | audioFlag;
                        break;
                    }
                }
            return clip >= 0;
        }

        const auto& clips = s.tracks[(size_t) track].clips;
        // Prefer audible clips over muted takes underneath them.
        for (int pass = 0; pass < 2 && clip < 0; ++pass)
            for (int c = (int) clips.size() - 1; c >= 0; --c)
                if (beat >= clips[(size_t) c].start && beat < clips[(size_t) c].end() && clips[(size_t) c].muted == (pass == 1))
                {
                    clip = c;
                    break;
                }
        return clip >= 0;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        int t = -1, c = -1;
        const bool hit = hitClip (e.getPosition(), t, c);
        owner.selectedTrack = juce::isPositiveAndBelow (t, (int) owner.song.getSong().tracks.size()) ? t : owner.selectedTrack;
        owner.selectedClip = hit ? c : -1;
        for (auto* h : owner.headers)
            h->repaint();

        if (e.mods.isPopupMenu() && hit)
        {
            showClipMenu (t, c);
            return;
        }

        if (! hit)
        {
            owner.song.locate (juce::jmax (0.0, std::floor (owner.xToBeat (e.x))));
            repaint();
            return;
        }

        dragTrack = t;
        dragClip = c;
        dragDelta = 0.0;
        dragging = false;
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragClip < 0 || std::abs (e.getDistanceFromDragStartX()) < 4)
            return;
        dragging = true;
        dragDelta = e.getDistanceFromDragStartX() / owner.pixelsPerBeat;
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging && dragClip >= audioFlag)
        {
            const int c = dragClip & ~audioFlag;
            const auto& clip = owner.song.getSong().tracks[(size_t) dragTrack].audioClips[(size_t) c];
            owner.song.moveAudioClip (dragTrack, c, juce::jmax (0.0, owner.snap (clip.start + dragDelta)));
        }
        else if (dragging && dragClip >= 0)
        {
            const auto& clip = owner.song.getSong().tracks[(size_t) dragTrack].clips[(size_t) dragClip];
            owner.song.moveClip (dragTrack, dragClip, juce::jmax (0.0, owner.snap (clip.start + dragDelta)));
        }
        dragging = false;
        dragClip = -1;
        repaint();
    }

    void showClipMenu (int t, int c)
    {
        if (c >= audioFlag)
        {
            const int ac = c & ~audioFlag;
            const auto& clip = owner.song.getSong().tracks[(size_t) t].audioClips[(size_t) ac];
            juce::PopupMenu m;
            m.addItem (1, "Deze take gebruiken", clip.muted);
            m.addItem (2, "Audioclip verwijderen");
            m.addItem (3, "Toon bestand in Verkenner");
            juce::Component::SafePointer<Lanes> safe (this);
            const auto file = juce::File (clip.file);
            m.showMenuAsync (juce::PopupMenu::Options(), [safe, t, ac, file] (int r)
            {
                if (safe == nullptr) return;
                if (r == 1) safe->owner.song.useAudioTake (t, ac);
                if (r == 2) { safe->owner.song.deleteAudioClip (t, ac); safe->owner.selectedClip = -1; }
                if (r == 3) file.revealToUser();
            });
            return;
        }

        const auto& clips = owner.song.getSong().tracks[(size_t) t].clips;
        const auto& clip = clips[(size_t) c];
        juce::PopupMenu m, takeMenu;

        // All takes recorded over this range, so an earlier one can be brought back.
        for (int i = 0; i < (int) clips.size(); ++i)
        {
            const auto& other = clips[(size_t) i];
            if (other.start < clip.end() && clip.start < other.end())
                takeMenu.addItem (100 + i, (other.name.isNotEmpty() ? other.name : "Clip " + juce::String (i + 1))
                                               + " (" + juce::String ((int) other.notes.size()) + " noten)",
                                  true, ! other.muted);
        }
        m.addItem (3, "Bewerken in pianorol (dubbelklik)");
        m.addItem (4, "Dupliceren");
        m.addSubMenu ("Take kiezen", takeMenu);
        m.addSeparator();
        m.addItem (2, "Clip verwijderen");

        juce::Component::SafePointer<Lanes> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [safe, t, c] (int r)
        {
            if (safe == nullptr) return;
            if (r >= 100) safe->owner.song.useTake (t, r - 100);
            if (r == 3) safe->owner.openEditor (t, c);
            if (r == 4) safe->owner.selectedClip = safe->owner.song.duplicateClip (t, c);
            if (r == 2) { safe->owner.song.deleteClip (t, c); safe->owner.selectedClip = -1; }
        });
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        int t = -1, c = -1;
        if (hitClip (e.getPosition(), t, c))
        {
            if (c < audioFlag)
                owner.openEditor (t, c);
            return;
        }
        if (juce::isPositiveAndBelow (t, (int) owner.song.getSong().tracks.size()) && owner.song.getSong().tracks[(size_t) t].isAudio())
            return; // audio tracks get their clips from recording or importing
        // Empty spot on a track: a new one-bar clip, opened straight away.
        if (juce::isPositiveAndBelow (t, (int) owner.song.getSong().tracks.size()))
        {
            const double bar = juce::jmax (0.25, owner.song.getSong().beatsPerBarAsQuarters());
            const double start = std::floor (juce::jmax (0.0, owner.xToBeat (e.x)) / bar) * bar;
            const int created = owner.song.createClip (t, start, bar);
            if (created >= 0)
                owner.openEditor (t, created);
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isCommandDown())
        {
            // Ctrl + wheel: zoom around the mouse.
            const double beatAtMouse = owner.xToBeat (e.x);
            owner.pixelsPerBeat = juce::jlimit (4.0, 200.0, owner.pixelsPerBeat * (w.deltaY > 0 ? 1.15 : 1.0 / 1.15));
            owner.scrollBeat = juce::jmax (0.0, beatAtMouse - e.x / owner.pixelsPerBeat);
            owner.updateScrollRange();
            owner.repaint();
        }
        else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
        {
            const double delta = (std::abs (w.deltaX) > 0 ? w.deltaX : w.deltaY) * -200.0 / owner.pixelsPerBeat;
            owner.scrollBeat = juce::jmax (0.0, owner.scrollBeat + delta);
            owner.updateScrollRange();
            owner.repaint();
        }
        else
        {
            Component::mouseWheelMove (e, w); // vertical scroll in the viewport
        }
    }

private:
    ArrangerComponent& owner;
    int dragTrack = -1, dragClip = -1;
    double dragDelta = 0.0;
    bool dragging = false;
};

//==============================================================================
ArrangerComponent::ArrangerComponent (AppController& c)
    : controller (c), song (c.getSongController())
{
    setWantsKeyboardFocus (true);
    transport = std::make_unique<TransportBar> (*this);
    ruler = std::make_unique<Ruler> (*this);
    tempoLane = std::make_unique<TempoLane> (*this);
    addAndMakeVisible (*tempoLane);
    lanes = std::make_unique<Lanes> (*this);

    addAndMakeVisible (*transport);
    addAndMakeVisible (*ruler);
    addAndMakeVisible (bodyViewport);
    addAndMakeVisible (hScroll);
    bodyViewport.setViewedComponent (&body, false);
    bodyViewport.setScrollBarsShown (true, false);
    body.addAndMakeVisible (*lanes);
    hScroll.addListener (this);
    hScroll.setAutoHide (false);

    pianoRoll = std::make_unique<PianoRoll> (controller, song);
    addChildComponent (*pianoRoll);
    pianoRoll->onClose = [this]
    {
        pianoRoll->setVisible (false);
        resized();
        grabKeyboardFocus();
    };

    rebuildHeaders();
    startTimerHz (30);
}

void ArrangerComponent::openEditor (int track, int clip)
{
    selectedTrack = track;
    selectedClip = clip;
    pianoRoll->setVisible (true);
    resized();
    pianoRoll->open (track, clip);
}

ArrangerComponent::~ArrangerComponent()
{
    hScroll.removeListener (this);
}

void ArrangerComponent::refresh()
{
    if (headers.size() != (int) song.getSong().tracks.size())
        rebuildHeaders();
    else
        for (auto* h : headers)
            h->refresh();

    if (! juce::isPositiveAndBelow (selectedTrack, (int) song.getSong().tracks.size()))
        selectedTrack = -1;
    transport->refresh();
    tempoLane->repaint();
    if (pianoRoll->isVisible())
        pianoRoll->reloadFromSong();
    updateScrollRange();
    resized();
    repaint();
}

void ArrangerComponent::rebuildHeaders()
{
    headers.clear();
    for (int t = 0; t < (int) song.getSong().tracks.size(); ++t)
        body.addAndMakeVisible (headers.add (new TrackHeader (*this, t)));
    resized();
}

void ArrangerComponent::updateScrollRange()
{
    const double visibleBeats = juce::jmax (1.0, (getWidth() - headerWidth) / pixelsPerBeat);
    const double total = juce::jmax (song.getSong().getEndBeat() + 32.0, scrollBeat + visibleBeats);
    hScroll.setRangeLimits (0.0, total, juce::dontSendNotification);
    hScroll.setCurrentRange (scrollBeat, visibleBeats, juce::dontSendNotification);
}

void ArrangerComponent::scrollBarMoved (juce::ScrollBar*, double start)
{
    scrollBeat = juce::jmax (0.0, start);
    tempoLane->repaint();
    ruler->repaint();
    lanes->repaint();
}

void ArrangerComponent::timerCallback()
{
    transport->tick();

    auto& seq = controller.getEngine().sequencer;
    if (seq.isRolling())
    {
        // Follow the playhead.
        const double visible = (getWidth() - headerWidth) / pixelsPerBeat;
        const double pos = seq.getPosition();
        if (pos > scrollBeat + visible * 0.9 || pos < scrollBeat)
        {
            scrollBeat = juce::jmax (0.0, pos - visible * 0.1);
            updateScrollRange();
        }
        ruler->repaint();
        lanes->repaint();
    }
    else if (std::abs (seq.getPosition() - lastDrawnPosition) > 1.0e-6)
    {
        ruler->repaint();
        lanes->repaint();
    }
    lastDrawnPosition = seq.getPosition();
}

void ArrangerComponent::resized()
{
    auto r = getLocalBounds();
    transport->setBounds (r.removeFromTop (54));
    if (pianoRoll->isVisible())
        pianoRoll->setBounds (r.removeFromBottom (juce::jmax (330, (int) (r.getHeight() * 0.55f))));
    hScroll.setBounds (r.removeFromBottom (14).withTrimmedLeft (headerWidth));
    ruler->setBounds (r.removeFromTop (rulerHeight).withTrimmedLeft (headerWidth));
    tempoLane->setBounds (r.removeFromTop (tempoHeight).withTrimmedLeft (headerWidth));
    bodyViewport.setBounds (r);

    const int lanesHeight = juce::jmax (r.getHeight(), (int) song.getSong().tracks.size() * laneHeight);
    const int width = r.getWidth() - (bodyViewport.isVerticalScrollBarShown() ? bodyViewport.getScrollBarThickness() : 0);
    body.setSize (juce::jmax (headerWidth + 100, width), lanesHeight);
    for (int i = 0; i < headers.size(); ++i)
        headers[i]->setBounds (0, i * laneHeight, headerWidth, laneHeight);
    lanes->setBounds (headerWidth, 0, body.getWidth() - headerWidth, lanesHeight);
    updateScrollRange();
}

void ArrangerComponent::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    g.setColour (colours::panel);
    g.fillRect (0, 54, headerWidth, rulerHeight + tempoHeight);
    g.setColour (colours::textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Tempo (BPM)", 10, 54 + rulerHeight, headerWidth - 20, tempoHeight, juce::Justification::centredLeft);
}

bool ArrangerComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)              { song.togglePlay(); return true; }
    if (key == juce::KeyPress ('r', 0, 'r') || key == juce::KeyPress ('R', 0, 'R')) { song.record(); return true; }
    if (key == juce::KeyPress::homeKey)               { song.goToStart(); return true; }
    if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'Z' || key.getKeyCode() == 'z'))
    {
        key.getModifiers().isShiftDown() ? song.redo() : song.undo();
        return true;
    }
    if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'Y' || key.getKeyCode() == 'y')) { song.redo(); return true; }
    if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'D' || key.getKeyCode() == 'd') && selectedClip >= 0 && selectedClip < audioFlag)
    {
        selectedClip = song.duplicateClip (selectedTrack, selectedClip);
        return true;
    }
    if (key == juce::KeyPress::returnKey && selectedClip >= 0 && selectedClip < audioFlag) { openEditor (selectedTrack, selectedClip); return true; }
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && selectedClip >= audioFlag)
    {
        song.deleteAudioClip (selectedTrack, selectedClip & ~audioFlag);
        selectedClip = -1;
        return true;
    }
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && selectedClip >= 0)
    {
        song.deleteClip (selectedTrack, selectedClip);
        selectedClip = -1;
        return true;
    }
    return false;
}

//==============================================================================
void ArrangerComponent::chooseFile (const juce::String& title, const juce::String& pattern, bool save,
                                    std::function<void (const juce::File&)> onChosen)
{
    auto start = song.getSongFile() != juce::File() ? song.getSongFile().getParentDirectory() : paths::songsDir();
    if (save)
        start = start.getChildFile (juce::File::createLegalFileName (song.getSong().name) + pattern.fromLastOccurrenceOf ("*", false, false).upToFirstOccurrenceOf (";", false, false));

    fileChooser = std::make_unique<juce::FileChooser> (title, start, pattern);
    const int chooserFlags = save ? (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting)
                           : (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles);
    fileChooser->launchAsync (chooserFlags, [onChosen] (const juce::FileChooser& fc)
    {
        if (fc.getResult() != juce::File())
            onChosen (fc.getResult());
    });
}

void ArrangerComponent::showSongMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Nieuw nummer");
    m.addItem (2, "Openen...");
    m.addItem (3, "Opslaan", song.getSongFile() != juce::File());
    m.addItem (4, "Opslaan als...");
    m.addSeparator();
    m.addItem (5, "MIDI-bestand importeren...");
    m.addItem (8, "Audiobestand importeren (WAV, FLAC, AIFF, MP3)...");
    m.addItem (6, "Exporteren als MIDI-bestand...");
    m.addItem (7, "Exporteren als audio (WAV of FLAC)...");

    juce::Component::SafePointer<ArrangerComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe] (int r)
    {
        if (safe == nullptr || r == 0)
            return;
        auto& self = *safe;
        auto& s = self.song;

        switch (r)
        {
            case 1: s.newSong(); break;
            case 2: self.chooseFile ("Nummer openen", "*.kmsong", false, [&s] (const juce::File& f) { s.loadSong (f); }); break;
            case 3: s.saveSong(); break;
            case 4: self.chooseFile ("Nummer opslaan", "*.kmsong", true,
                                     [&s] (const juce::File& f) { s.saveSongAs (f.withFileExtension ("kmsong")); });
                    break;
            case 5: self.chooseFile ("MIDI-bestand importeren", "*.mid;*.midi", false, [&s] (const juce::File& f)
                    {
                        juce::String error;
                        s.importMidi (f, error);
                        if (error.isNotEmpty() && s.onStatus) s.onStatus (error);
                    });
                    break;
            case 6: self.chooseFile ("Exporteren als MIDI", "*.mid", true,
                                     [&s] (const juce::File& f) { s.exportMidi (f.withFileExtension ("mid")); });
                    break;
            case 7: self.chooseFile ("Exporteren als audio", "*.wav;*.flac", true, [&s] (const juce::File& f)
                    {
                        const auto file = f.hasFileExtension ("flac") ? f : f.withFileExtension ("wav");
                        if (s.onStatus) s.onStatus ("Bezig met exporteren...");
                        // Let the status line repaint before the (blocking) render starts.
                        juce::Timer::callAfterDelay (60, [&s, file]
                        {
                            juce::String error;
                            if (! s.exportAudio (file, error) && s.onStatus)
                                s.onStatus ("Export mislukt: " + error);
                        });
                    });
                    break;
            case 8: self.chooseFile ("Audiobestand importeren", "*.wav;*.flac;*.aif;*.aiff;*.mp3;*.ogg", false, [safe] (const juce::File& f)
                    {
                        if (safe == nullptr) return;
                        auto& me = *safe;
                        const auto& current = me.song.getSong();
                        const double bar = juce::jmax (0.25, current.beatsPerBarAsQuarters());
                        const double at = std::floor (me.controller.getEngine().sequencer.getPosition() / bar) * bar;
                        const int target = juce::isPositiveAndBelow (me.selectedTrack, (int) current.tracks.size())
                                           && current.tracks[(size_t) me.selectedTrack].isAudio() ? me.selectedTrack : -1;
                        juce::String error;
                        if (! me.song.importAudio (f, target, at, error) && me.song.onStatus)
                            me.song.onStatus (error);
                    });
                    break;
            default: break;
        }
    });
}

} // namespace km
