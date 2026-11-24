#include "UI/PianoRoll.h"

#include "Model/DrumMap.h"
#include "UI/KmLookAndFeel.h"

namespace km
{
namespace
{
    constexpr int toolbarHeight = 40;
    constexpr int velocityHeight = 64;
    constexpr int ccHeight = 84;
    constexpr int scrollSize = 12;

    bool isBlackKey (int note)
    {
        const int pc = note % 12;
        return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    }

    juce::String noteName (int note) { return juce::MidiMessage::getMidiNoteName (note, true, true, 3); }
} // namespace

PianoRoll::PianoRoll (AppController& a, SongController& s) : app (a), song (s)
{
    setWantsKeyboardFocus (true);

    addAndMakeVisible (title);
    title.setFont (juce::FontOptions (14.0f, juce::Font::bold));

    addAndMakeVisible (gridBox);
    const auto& grids = edit::gridOptions();
    for (int i = 0; i < (int) grids.size(); ++i)
        gridBox.addItem (grids[(size_t) i].name, i + 1);
    gridBox.setSelectedId (3, juce::dontSendNotification); // 1/16
    gridBox.setTooltip ("Raster voor tekenen, verschuiven en quantize");
    gridBox.onChange = [this] { repaint(); };

    addAndMakeVisible (snapButton);
    snapButton.setToggleState (true, juce::dontSendNotification);
    snapButton.setTooltip ("Noten springen naar het raster (uit = vrij plaatsen)");

    addAndMakeVisible (drumsButton);
    drumsButton.setClickingTogglesState (true);
    drumsButton.setTooltip ("Drum-editor: een rij per drumklank (General MIDI), klik = slag");
    drumsButton.onClick = [this]
    {
        setDrumMode (drumsButton.getToggleState());
        song.setTrackDrums (track, drumMode);
    };

    addAndMakeVisible (quantizeButton);
    quantizeButton.setTooltip ("Gekozen noten (of alle) naar het raster trekken");
    quantizeButton.onClick = [this] { quantizeNow(); };

    for (auto* sl : { &strength, &swing })
    {
        addAndMakeVisible (*sl);
        sl->setSliderStyle (juce::Slider::LinearHorizontal);
        sl->setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
        sl->setRange (0.0, 100.0, 1.0);
        sl->textFromValueFunction = [] (double v) { return juce::String ((int) v) + "%"; };
    }
    strength.setValue (100.0, juce::dontSendNotification);
    strength.setTooltip ("Hoe ver noten naar het raster schuiven (minder = menselijker)");
    swing.setValue (0.0, juce::dontSendNotification);
    swing.setTooltip ("Elke tweede rasterpositie later (100% = triool-gevoel)");
    strength.updateText();
    swing.updateText();
    for (auto [label, text] : { std::pair<juce::Label*, const char*> { &strengthLabel, "Sterkte" }, { &swingLabel, "Swing" } })
    {
        addAndMakeVisible (*label);
        label->setText (text, juce::dontSendNotification);
        label->setColour (juce::Label::textColourId, colours::textDim);
    }
    addAndMakeVisible (lengthsButton);
    lengthsButton.setTooltip ("Ook het einde van noten op het raster zetten");

    for (auto [b, semis] : { std::pair<juce::TextButton*, int> { &octDown, -12 }, { &semiDown, -1 }, { &semiUp, 1 }, { &octUp, 12 } })
    {
        addAndMakeVisible (*b);
        b->setTooltip ("Gekozen noten (of alle) transponeren");
        const int s2 = semis;
        b->onClick = [this, s2] { transposeSelection (s2); };
    }

    addAndMakeVisible (laneBox);
    const auto& lanes = edit::controllerLanes();
    for (int i = 0; i < (int) lanes.size(); ++i)
        laneBox.addItem (lanes[(size_t) i].name, i + 1);
    laneBox.setSelectedId (1, juce::dontSendNotification);
    laneBox.setTooltip ("Welke controller je onderin tekent (rechtermuisknop = wissen)");
    laneBox.onChange = [this] { repaint(); };

    addAndMakeVisible (closeButton);
    closeButton.onClick = [this]
    {
        track = clipIndex = -1;
        if (onClose) onClose();
    };

    for (auto* sb : { &hScroll, &vScroll })
    {
        addAndMakeVisible (*sb);
        sb->addListener (this);
        sb->setAutoHide (false);
    }

    startTimerHz (30);
}

PianoRoll::~PianoRoll()
{
    hScroll.removeListener (this);
    vScroll.removeListener (this);
}

//==============================================================================
void PianoRoll::open (int t, int c)
{
    track = t;
    clipIndex = c;
    selection.clear();
    drag = Drag::none;
    reloadFromSong();
    if (! isOpen())
        return;

    const auto& tracks = song.getSong().tracks;
    setDrumMode (juce::isPositiveAndBelow (track, (int) tracks.size()) && tracks[(size_t) track].drums);
    drumsButton.setToggleState (drumMode, juce::dontSendNotification);

    // Centre the view on the notes (or around middle C when empty).
    scrollBeat = 0.0;
    if (! clip.notes.empty())
    {
        int lo = 127, hi = 0;
        for (auto& n : clip.notes) { lo = juce::jmin (lo, n.note); hi = juce::jmax (hi, n.note); }
        const int visible = juce::jmax (12, (int) (gridArea.getHeight() / noteHeight));
        topNote = juce::jlimit (visible - 1, 127, (lo + hi) / 2 + visible / 2);
    }
    else
    {
        topNote = 84;
    }

    if (gridArea.getWidth() > 0)
        pixelsPerBeat = juce::jlimit (20.0, 200.0, gridArea.getWidth() / juce::jmax (4.0, clip.length + 1.0));
    updateScrollBars();
    grabKeyboardFocus();
    repaint();
}

void PianoRoll::reloadFromSong()
{
    if (drag != Drag::none)
        return; // the gesture writes its own result when it ends

    const auto* c = song.getClip (track, clipIndex);
    if (c == nullptr)
    {
        if (isOpen())
        {
            track = clipIndex = -1;
            if (onClose) onClose();
        }
        return;
    }

    clip = *c;
    if (drumMode)
        rebuildDrumRows();
    for (auto it = selection.begin(); it != selection.end();)
        it = *it >= (int) clip.notes.size() ? selection.erase (it) : std::next (it);

    const auto& tr = song.getSong().tracks[(size_t) track];
    title.setText (tr.name + "  -  " + (clip.name.isNotEmpty() ? clip.name : juce::String ("Clip")), juce::dontSendNotification);
    updateScrollBars();
    repaint();
}

float PianoRoll::noteToY (int note) const noexcept
{
    if (drumMode)
        return gridArea.getY() + (float) (rowOf (note) - firstRow) * drumRowHeight;
    return gridArea.getY() + (float) (topNote - note) * noteHeight;
}

int PianoRoll::yToNote (float y) const noexcept
{
    if (drumMode)
        return rowNotes.empty() ? 36 : rowNotes[(size_t) rowAt (y)];
    return juce::jlimit (0, 127, topNote - (int) std::floor ((y - gridArea.getY()) / noteHeight));
}

int PianoRoll::rowOf (int note) const noexcept
{
    for (int i = 0; i < (int) rowNotes.size(); ++i)
        if (rowNotes[(size_t) i] == note)
            return i;
    return 0;
}

int PianoRoll::rowAt (float y) const noexcept
{
    return juce::jlimit (0, juce::jmax (0, (int) rowNotes.size() - 1),
                         firstRow + (int) std::floor ((y - gridArea.getY()) / drumRowHeight));
}

void PianoRoll::rebuildDrumRows()
{
    rowNotes.clear();
    for (auto& r : gmDrumRows())
        rowNotes.push_back (r.note);
    // Notes in the clip that are not in the map get their own rows at the bottom.
    for (auto& n : clip.notes)
        if (std::find (rowNotes.begin(), rowNotes.end(), n.note) == rowNotes.end())
            rowNotes.push_back (n.note);
}

void PianoRoll::setDrumMode (bool on)
{
    drumMode = on;
    firstRow = 0;
    rebuildDrumRows();
    if (on && gridBox.getSelectedId() == 3 && clip.notes.empty())
        gridBox.setSelectedId (3, juce::dontSendNotification); // 1/16 is the natural drum grid
    resized();
    repaint();
}

double PianoRoll::grid() const
{
    const auto& g = edit::gridOptions();
    return g[(size_t) juce::jlimit (0, (int) g.size() - 1, gridBox.getSelectedId() - 1)].beats;
}

const edit::ControllerLane& PianoRoll::lane() const
{
    const auto& l = edit::controllerLanes();
    return l[(size_t) juce::jlimit (0, (int) l.size() - 1, laneBox.getSelectedId() - 1)];
}

int PianoRoll::partOfTrack() const
{
    const auto& tracks = song.getSong().tracks;
    return juce::isPositiveAndBelow (track, (int) tracks.size()) ? tracks[(size_t) track].part : 0;
}

double PianoRoll::snapBeat (double beat) const
{
    return snapButton.getToggleState() ? edit::snapToGrid (beat, grid()) : beat;
}

juce::Rectangle<float> PianoRoll::noteRect (const SongNote& n) const
{
    const float x0 = beatToX (n.start);
    const float x1 = beatToX (n.start + n.length);
    return { x0, noteToY (n.note) + 1.0f, juce::jmax (drumMode ? 10.0f : 3.0f, x1 - x0), rowHeight() - 2.0f };
}

int PianoRoll::hitNote (juce::Point<float> p) const
{
    for (int i = (int) clip.notes.size() - 1; i >= 0; --i)
        if (noteRect (clip.notes[(size_t) i]).expanded (0.0f, 1.0f).contains (p))
            return i;
    return -1;
}

//==============================================================================
void PianoRoll::commitNotes (const juce::String& what)
{
    song.setClipNotes (track, clipIndex, clip.notes, what);
}

void PianoRoll::commitControls (const juce::String& what)
{
    song.setClipControls (track, clipIndex, clip.controls, what);
}

void PianoRoll::quantizeNow()
{
    if (! isOpen())
        return;
    edit::QuantizeSettings q;
    q.grid = grid();
    q.strength = strength.getValue() / 100.0;
    q.swing = swing.getValue() / 100.0;
    q.lengths = lengthsButton.getToggleState();
    song.quantizeClip (track, clipIndex, q, selection);
}

void PianoRoll::transposeSelection (int semitones)
{
    if (! isOpen() || clip.notes.empty())
        return;
    edit::transpose (clip.notes, selection, semitones);
    commitNotes ("Transponeren");
}

void PianoRoll::deleteSelection()
{
    if (selection.empty())
        return;
    std::vector<SongNote> kept;
    for (int i = 0; i < (int) clip.notes.size(); ++i)
        if (selection.count (i) == 0)
            kept.push_back (clip.notes[(size_t) i]);
    clip.notes = kept;
    selection.clear();
    commitNotes ("Noten verwijderen");
}

//==============================================================================
void PianoRoll::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (toolbarHeight).reduced (6, 7);
    auto place = [&bar] (juce::Component& c, int w) { c.setBounds (bar.removeFromLeft (w)); bar.removeFromLeft (6); };

    closeButton.setBounds (bar.removeFromRight (70));
    bar.removeFromRight (8);
    laneBox.setBounds (bar.removeFromRight (140));
    bar.removeFromRight (8);

    place (title, 130);
    place (drumsButton, 64);
    place (gridBox, 110);
    place (snapButton, 76);
    place (quantizeButton, 110);
    place (strengthLabel, 52);
    place (strength, 104);
    place (swingLabel, 42);
    place (swing, 104);
    place (lengthsButton, 100);
    place (octDown, 40);
    place (semiDown, 34);
    place (semiUp, 34);
    place (octUp, 40);

    hScroll.setBounds (r.removeFromBottom (scrollSize).withTrimmedLeft (kbWidth()).withTrimmedRight (scrollSize));
    auto cc = r.removeFromBottom (ccHeight);
    auto vel = r.removeFromBottom (velocityHeight);
    vScroll.setBounds (r.removeFromRight (scrollSize));
    cc.removeFromRight (scrollSize);
    vel.removeFromRight (scrollSize);

    keyboardArea = r.removeFromLeft (kbWidth()).toFloat();
    gridArea = r.toFloat();
    velocityArea = vel.withTrimmedLeft (kbWidth()).toFloat();
    ccArea = cc.withTrimmedLeft (kbWidth()).toFloat();
    updateScrollBars();
}

void PianoRoll::updateScrollBars()
{
    const double visibleBeats = gridArea.getWidth() / pixelsPerBeat;
    hScroll.setRangeLimits (0.0, juce::jmax (clip.length + 4.0, scrollBeat + visibleBeats), juce::dontSendNotification);
    hScroll.setCurrentRange (scrollBeat, visibleBeats, juce::dontSendNotification);

    if (drumMode)
    {
        vScroll.setRangeLimits (0.0, (double) juce::jmax (1, (int) rowNotes.size()), juce::dontSendNotification);
        vScroll.setCurrentRange (firstRow, gridArea.getHeight() / drumRowHeight, juce::dontSendNotification);
        return;
    }
    const double visibleNotes = gridArea.getHeight() / noteHeight;
    vScroll.setRangeLimits (0.0, 128.0, juce::dontSendNotification);
    vScroll.setCurrentRange (127.0 - topNote, visibleNotes, juce::dontSendNotification);
}

void PianoRoll::scrollBarMoved (juce::ScrollBar* bar, double start)
{
    if (bar == &hScroll)
        scrollBeat = juce::jmax (0.0, start);
    else if (drumMode)
        firstRow = juce::jlimit (0, juce::jmax (0, (int) rowNotes.size() - 1), (int) std::round (start));
    else
        topNote = juce::jlimit (0, 127, 127 - (int) std::round (start));
    repaint();
}

void PianoRoll::timerCallback()
{
    if (isOpen() && app.getEngine().sequencer.isRolling())
        repaint (gridArea.getUnion (velocityArea).getUnion (ccArea).toNearestInt());
}

//==============================================================================
void PianoRoll::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    g.setColour (colours::outline);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());
    g.drawHorizontalLine (toolbarHeight - 1, 0.0f, (float) getWidth());

    if (! isOpen())
        return;

    const double gridStep = grid();
    const double barLen = juce::jmax (0.25, song.getSong().beatsPerBarAsQuarters());

    // --- keyboard + note rows
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (keyboardArea.getUnion (gridArea).toNearestInt());
        if (drumMode)
        {
            for (int row = firstRow; row < (int) rowNotes.size(); ++row)
            {
                const float y = gridArea.getY() + (float) (row - firstRow) * drumRowHeight;
                if (y > gridArea.getBottom())
                    break;
                const int note = rowNotes[(size_t) row];
                g.setColour (row % 2 == 0 ? colours::background : colours::panelLight.darker (0.45f));
                g.fillRect (gridArea.getX(), y, gridArea.getWidth(), drumRowHeight);
                g.setColour (colours::panelLight);
                g.fillRect (keyboardArea.getX(), y, keyboardArea.getWidth() - 1.0f, drumRowHeight - 1.0f);
                g.setColour (colours::text);
                g.setFont (juce::FontOptions (11.0f));
                const char* name = gmDrumName (note);
                g.drawText (name != nullptr ? juce::String (name) : noteName (note),
                            keyboardArea.withY (y).withHeight (drumRowHeight).reduced (5, 0), juce::Justification::centredLeft);
            }
        }
        const int bottomNote = drumMode ? topNote + 1 : juce::jmax (0, topNote - (int) (gridArea.getHeight() / noteHeight) - 1);
        for (int n = topNote; n >= bottomNote; --n)
        {
            const float y = noteToY (n);
            const bool black = isBlackKey (n);
            g.setColour (black ? colours::background : colours::panelLight.darker (0.35f));
            g.fillRect (gridArea.getX(), y, gridArea.getWidth(), noteHeight);
            g.setColour (black ? juce::Colour (0xff22252c) : juce::Colour (0xffd9dde4));
            g.fillRect (keyboardArea.getX(), y, keyboardArea.getWidth() - (black ? 18.0f : 0.0f), noteHeight - 1.0f);
            if (n % 12 == 0)
            {
                g.setColour (juce::Colours::black);
                g.setFont (juce::FontOptions (10.0f));
                g.drawText (noteName (n), keyboardArea.withY (y).withHeight (noteHeight).reduced (3, 0), juce::Justification::centredRight);
                g.setColour (colours::outline);
                g.drawHorizontalLine ((int) (y + noteHeight), gridArea.getX(), gridArea.getRight());
            }
        }

        // Vertical grid lines
        const double firstBeat = std::floor (scrollBeat / gridStep) * gridStep;
        for (double b = firstBeat; beatToX (b) < gridArea.getRight(); b += gridStep)
        {
            const bool isBar = std::abs (std::fmod (b + 1e-9, barLen)) < 1e-6;
            const bool isBeat = std::abs (std::fmod (b + 1e-9, 1.0)) < 1e-6;
            g.setColour (colours::outline.withAlpha (isBar ? 1.0f : isBeat ? 0.6f : 0.25f));
            g.drawVerticalLine ((int) beatToX (b), gridArea.getY(), gridArea.getBottom());
        }

        // Beyond the end of the clip
        const float endX = beatToX (clip.length);
        if (endX < gridArea.getRight())
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRect (juce::Rectangle<float> (endX, gridArea.getY(), gridArea.getRight() - endX, gridArea.getHeight()));
        }

        // Notes
        for (int i = 0; i < (int) clip.notes.size(); ++i)
        {
            const auto& n = clip.notes[(size_t) i];
            const auto r = noteRect (n);
            const bool sel = selection.count (i) > 0;
            g.setColour (colours::accent.withAlpha (0.35f + 0.65f * (float) n.velocity / 127.0f));
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (sel ? colours::text : colours::accent.darker (0.5f));
            g.drawRoundedRectangle (r, 2.0f, sel ? 2.0f : 1.0f);
        }

        if (drag == Drag::rubber)
        {
            g.setColour (colours::text.withAlpha (0.15f));
            g.fillRect (rubber);
            g.setColour (colours::text.withAlpha (0.6f));
            g.drawRect (rubber, 1.0f);
        }

        // Playhead (song position relative to this clip)
        const double rel = app.getEngine().sequencer.getPosition() - clip.start;
        if (rel >= 0.0 && rel <= clip.length)
        {
            g.setColour (colours::text);
            g.drawVerticalLine ((int) beatToX (rel), gridArea.getY(), gridArea.getBottom());
        }
    }

    // --- velocity lane
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (velocityArea.toNearestInt());
        g.setColour (colours::background);
        g.fillRect (velocityArea);
        for (int i = 0; i < (int) clip.notes.size(); ++i)
        {
            const auto& n = clip.notes[(size_t) i];
            const float x = beatToX (n.start);
            const float h = (velocityArea.getHeight() - 6.0f) * (float) n.velocity / 127.0f;
            g.setColour (selection.count (i) > 0 ? colours::text : colours::accent);
            g.fillRect (x, velocityArea.getBottom() - h, 3.0f, h);
            g.fillEllipse (x - 2.0f, velocityArea.getBottom() - h - 3.0f, 7.0f, 7.0f);
        }
    }
    g.setColour (colours::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawFittedText ("Velocity", juce::Rectangle<float> (0.0f, velocityArea.getY(), (float) kbWidth(), velocityArea.getHeight()).toNearestInt().reduced (3),
                      juce::Justification::centred, 2);

    // --- controller lane
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (ccArea.toNearestInt());
        g.setColour (colours::background.darker (0.2f));
        g.fillRect (ccArea);
        g.setColour (colours::outline);
        g.drawHorizontalLine ((int) ccArea.getY(), ccArea.getX(), ccArea.getRight());

        const auto& l = lane();
        auto valueToY = [this] (double v) { return ccArea.getBottom() - 3.0f - (float) v * (ccArea.getHeight() - 6.0f); };
        if (l.kind == edit::ControllerLane::pitchBend)
        {
            g.setColour (colours::outline);
            g.drawHorizontalLine ((int) valueToY (0.5), ccArea.getX(), ccArea.getRight());
        }

        juce::Path path;
        double value = l.restValue();
        path.startNewSubPath (ccArea.getX(), valueToY (value));
        bool any = false;
        for (auto& c : clip.controls)
        {
            if (! l.matches (c))
                continue;
            any = true;
            const float x = beatToX (c.beat);
            path.lineTo (x, valueToY (value));   // step: hold the previous value
            value = l.valueOf (c);
            path.lineTo (x, valueToY (value));
        }
        path.lineTo (ccArea.getRight(), valueToY (value));
        g.setColour (any ? colours::learn : colours::outline);
        g.strokePath (path, juce::PathStrokeType (1.8f));
    }
    g.setColour (colours::textDim);
    g.drawFittedText (juce::String (lane().name).upToFirstOccurrenceOf (" ", false, false),
                      juce::Rectangle<float> (0.0f, ccArea.getY(), (float) kbWidth(), ccArea.getHeight()).toNearestInt().reduced (3),
                      juce::Justification::centred, 2);
}

//==============================================================================
void PianoRoll::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.position;
    if (gridArea.contains (p))
    {
        const int hit = hitNote (p);
        const bool edge = hit >= 0 && p.x > noteRect (clip.notes[(size_t) hit]).getRight() - 6.0f;
        setMouseCursor (edge ? juce::MouseCursor::LeftRightResizeCursor
                             : hit >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    }
    else
    {
        setMouseCursor (ccArea.contains (p) || velocityArea.contains (p) ? juce::MouseCursor::CrosshairCursor
                                                                         : juce::MouseCursor::NormalCursor);
    }
}

void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    if (! isOpen())
        return;
    grabKeyboardFocus();
    const auto p = e.position;
    changedInGesture = false;

    if (keyboardArea.contains (p))
    {
        drag = Drag::keyboard;
        lastPreviewNote = yToNote (p.y);
        app.previewNote (partOfTrack(), lastPreviewNote, 100);
        return;
    }

    if (gridArea.contains (p))
    {
        const int hit = hitNote (p);
        if (hit >= 0)
        {
            if (e.mods.isShiftDown() || e.mods.isCommandDown())
            {
                if (selection.count (hit) > 0) selection.erase (hit);
                else selection.insert (hit);
            }
            else if (selection.count (hit) == 0)
            {
                selection = { hit };
            }

            const auto& n = clip.notes[(size_t) hit];
            drag = (! drumMode && p.x > noteRect (n).getRight() - 6.0f) ? Drag::resize : Drag::move;
            toggleCandidate = drumMode && ! e.mods.isShiftDown() ? hit : -1;
            originals = clip.notes;
            dragAnchor = hit;
            dragStartBeat = xToBeat (p.x);
            dragStartNote = yToNote (p.y);
            lastVelocity = n.velocity;
            lastPreviewNote = n.note;
            app.previewNote (partOfTrack(), n.note, n.velocity, n.channel);
        }
        else if (drumMode && ! e.mods.isShiftDown())
        {
            // Drum grid: one click places a hit (Alt-click = soft, otherwise the last velocity).
            SongNote n;
            const double beat = juce::jmax (0.0, xToBeat (p.x));
            n.start = snapButton.getToggleState() ? edit::floorToGrid (beat, grid()) : beat;
            n.length = juce::jmax (0.05, grid() * 0.5);
            n.note = yToNote (p.y);
            n.velocity = e.mods.isAltDown() ? 60 : lastVelocity;
            n.channel = 10;
            clip.notes.push_back (n);
            selection = { (int) clip.notes.size() - 1 };
            app.previewNote (partOfTrack(), n.note, n.velocity, 10, 120);
            commitNotes ("Slag toevoegen");
            return;
        }
        else
        {
            if (! e.mods.isShiftDown())
                selection.clear();
            drag = Drag::rubber;
            rubber = { p, p };
        }
        repaint();
        return;
    }

    if (velocityArea.contains (p))
    {
        drag = Drag::velocity;
        mouseDrag (e);
        return;
    }

    if (ccArea.contains (p))
    {
        drag = e.mods.isPopupMenu() ? Drag::ccErase : Drag::ccDraw;
        ccLastBeat = juce::jmax (0.0, xToBeat (p.x));
        ccLastValue = juce::jlimit (0.0, 1.0, (double) (ccArea.getBottom() - 3.0f - p.y) / (ccArea.getHeight() - 6.0f));
        if (lane().kind == edit::ControllerLane::cc && lane().number == 64)
            ccLastValue = ccLastValue >= 0.5 ? 1.0 : 0.0;
        mouseDrag (e);
    }
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = e.position;
    switch (drag)
    {
        case Drag::keyboard:
        {
            const int n = yToNote (p.y);
            if (n != lastPreviewNote)
            {
                lastPreviewNote = n;
                app.previewNote (partOfTrack(), n, 100);
            }
            break;
        }

        case Drag::move:
        case Drag::resize:
        {
            double dBeat = xToBeat (p.x) - dragStartBeat;
            if (snapButton.getToggleState())
                dBeat = std::round (dBeat / grid()) * grid();
            const int dNote = drag == Drag::move && ! drumMode ? yToNote (p.y) - dragStartNote : 0;
            const int dRow = drag == Drag::move && drumMode ? rowAt (p.y) - rowOf (dragStartNote) : 0;

            clip.notes = originals;
            for (int i : selection)
            {
                if (! juce::isPositiveAndBelow (i, (int) clip.notes.size()))
                    continue;
                auto& n = clip.notes[(size_t) i];
                if (drag == Drag::move)
                {
                    n.start = juce::jmax (0.0, n.start + dBeat);
                    if (drumMode)
                        n.note = rowNotes[(size_t) juce::jlimit (0, (int) rowNotes.size() - 1, rowOf (n.note) + dRow)];
                    else
                        n.note = juce::jlimit (0, 127, n.note + dNote);
                }
                else
                {
                    const double minLen = snapButton.getToggleState() ? grid() * 0.5 : 0.02;
                    n.length = juce::jmax (minLen, n.length + dBeat);
                }
            }
            changedInGesture = std::abs (dBeat) > 1e-9 || dNote != 0 || dRow != 0;

            // Hear the new pitch while dragging up/down.
            if (drag == Drag::move && juce::isPositiveAndBelow (dragAnchor, (int) clip.notes.size())
                && clip.notes[(size_t) dragAnchor].note != lastPreviewNote)
            {
                lastPreviewNote = clip.notes[(size_t) dragAnchor].note;
                app.previewNote (partOfTrack(), lastPreviewNote, lastVelocity);
            }
            break;
        }

        case Drag::rubber:
        {
            rubber = juce::Rectangle<float> (e.mouseDownPosition, p);
            selection.clear();
            for (int i = 0; i < (int) clip.notes.size(); ++i)
                if (noteRect (clip.notes[(size_t) i]).intersects (rubber))
                    selection.insert (i);
            break;
        }

        case Drag::velocity:
        {
            const int v = juce::jlimit (1, 127, (int) std::lround (127.0 * (velocityArea.getBottom() - p.y) / (velocityArea.getHeight() - 6.0)));
            for (int i = 0; i < (int) clip.notes.size(); ++i)
            {
                auto& n = clip.notes[(size_t) i];
                const float x = beatToX (n.start);
                const bool under = p.x >= x - 4.0f && p.x <= juce::jmax (x + 6.0f, noteRect (n).getRight());
                const bool useSelection = ! selection.empty();
                if (under && (! useSelection || selection.count (i) > 0))
                {
                    n.velocity = v;
                    changedInGesture = true;
                }
            }
            break;
        }

        case Drag::ccDraw:
        case Drag::ccErase:
        {
            const double beat = juce::jlimit (0.0, clip.length, xToBeat (p.x));
            double value = juce::jlimit (0.0, 1.0, (double) (ccArea.getBottom() - 3.0f - p.y) / (ccArea.getHeight() - 6.0f));
            if (lane().kind == edit::ControllerLane::cc && lane().number == 64)
                value = value >= 0.5 ? 1.0 : 0.0;

            if (drag == Drag::ccErase)
                edit::eraseRange (clip.controls, lane(), ccLastBeat, beat);
            else
                edit::drawLine (clip.controls, lane(), 1, ccLastBeat, beat, ccLastValue, value, 1.0 / 32.0);
            ccLastBeat = beat;
            ccLastValue = value;
            changedInGesture = true;
            break;
        }

        case Drag::none:
            break;
    }
    repaint();
}

void PianoRoll::mouseUp (const juce::MouseEvent&)
{
    const auto finished = drag;
    drag = Drag::none;

    // Drum grid: clicking a hit without dragging removes it (like a step sequencer).
    if (drumMode && finished == Drag::move && ! changedInGesture && juce::isPositiveAndBelow (toggleCandidate, (int) clip.notes.size()))
    {
        clip.notes.erase (clip.notes.begin() + toggleCandidate);
        selection.clear();
        toggleCandidate = -1;
        commitNotes ("Slag verwijderen");
        return;
    }
    toggleCandidate = -1;

    if (changedInGesture)
    {
        switch (finished)
        {
            case Drag::move:     commitNotes ("Noten verplaatsen"); break;
            case Drag::resize:   commitNotes ("Noten verlengen"); break;
            case Drag::velocity: commitNotes ("Velocity"); break;
            case Drag::ccDraw:   commitControls ("Controller tekenen"); break;
            case Drag::ccErase:  commitControls ("Controller wissen"); break;
            case Drag::none:
            case Drag::rubber:
            case Drag::keyboard: break;
        }
    }
    changedInGesture = false;
    repaint();
}

void PianoRoll::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! isOpen() || ! gridArea.contains (e.position) || drumMode)
        return; // in the drum grid single clicks already add and remove hits

    const int hit = hitNote (e.position);
    if (hit >= 0)
    {
        clip.notes.erase (clip.notes.begin() + hit);
        selection.clear();
        commitNotes ("Noot verwijderen");
        return;
    }

    // Double-click on an empty spot: a new note, one grid step long.
    SongNote n;
    const double beat = juce::jmax (0.0, xToBeat (e.position.x));
    n.start = snapButton.getToggleState() ? edit::floorToGrid (beat, grid()) : beat;
    n.length = grid();
    n.note = yToNote (e.position.y);
    n.velocity = lastVelocity;
    clip.notes.push_back (n);
    selection = { (int) clip.notes.size() - 1 };
    app.previewNote (partOfTrack(), n.note, n.velocity);
    commitNotes ("Noot toevoegen");
}

void PianoRoll::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown())
    {
        const double beatAtMouse = xToBeat (e.position.x);
        pixelsPerBeat = juce::jlimit (10.0, 600.0, pixelsPerBeat * (w.deltaY > 0 ? 1.15 : 1.0 / 1.15));
        scrollBeat = juce::jmax (0.0, beatAtMouse - (e.position.x - gridArea.getX()) / pixelsPerBeat);
    }
    else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const double d = (std::abs (w.deltaX) > 0 ? w.deltaX : w.deltaY) * -150.0 / pixelsPerBeat;
        scrollBeat = juce::jmax (0.0, scrollBeat + d);
    }
    else if (drumMode)
    {
        firstRow = juce::jlimit (0, juce::jmax (0, (int) rowNotes.size() - 1), firstRow + (w.deltaY > 0 ? -2 : 2));
    }
    else
    {
        topNote = juce::jlimit ((int) (gridArea.getHeight() / noteHeight), 127, topNote + (w.deltaY > 0 ? 3 : -3));
    }
    updateScrollBars();
    repaint();
}

bool PianoRoll::keyPressed (const juce::KeyPress& key)
{
    if (! isOpen())
        return false;

    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();

    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { deleteSelection(); return true; }
    if (mods.isCommandDown() && (code == 'A' || code == 'a'))
    {
        selection.clear();
        for (int i = 0; i < (int) clip.notes.size(); ++i)
            selection.insert (i);
        repaint();
        return true;
    }
    if (mods.isCommandDown() && (code == 'Z' || code == 'z'))
    {
        mods.isShiftDown() ? song.redo() : song.undo();
        return true;
    }
    if (mods.isCommandDown() && (code == 'Y' || code == 'y')) { song.redo(); return true; }
    if (! mods.isCommandDown() && (code == 'Q' || code == 'q')) { quantizeNow(); return true; }
    if (code == juce::KeyPress::upKey)   { transposeSelection (mods.isShiftDown() ? 12 : 1); return true; }
    if (code == juce::KeyPress::downKey) { transposeSelection (mods.isShiftDown() ? -12 : -1); return true; }
    if ((code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey) && ! clip.notes.empty())
    {
        const double d = code == juce::KeyPress::leftKey ? -grid() : grid();
        for (int i = 0; i < (int) clip.notes.size(); ++i)
            if (selection.empty() || selection.count (i) > 0)
                clip.notes[(size_t) i].start = juce::jmax (0.0, clip.notes[(size_t) i].start + d);
        commitNotes ("Noten verschuiven");
        return true;
    }
    return false;
}

} // namespace km
