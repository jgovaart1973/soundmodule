#include "UI/ScoreView.h"

#include "UI/KmLookAndFeel.h"

namespace km
{
namespace
{
    const juce::Colour ink { 0xff15171c };
    const juce::Colour paperColour { 0xfffbfaf6 };

    juce::Font musicFont (float height)
    {
       #if JUCE_WINDOWS
        return juce::Font (juce::FontOptions ("Segoe UI Symbol", height, juce::Font::plain));
       #else
        return juce::Font (juce::FontOptions ("FreeSerif", height, juce::Font::plain));
       #endif
    }

    /** A glyph as a path, scaled to fit 'box' (keeps proportions). Empty if the font lacks it. */
    juce::Path glyph (const char* utf8, juce::Rectangle<float> box)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (musicFont (100.0f), juce::String::fromUTF8 (utf8), 0.0f, 0.0f);
        juce::Path p;
        ga.createPath (p);
        if (p.isEmpty() || p.getBounds().getHeight() < 1.0f)
            return {};
        p.applyTransform (p.getTransformToScaleToFit (box, true, juce::Justification::centred));
        return p;
    }

    float yOf (float staffTop, float sp, int position) { return staffTop + 4.0f * sp - (float) position * sp * 0.5f; }

    void drawHead (juce::Graphics& g, float cx, float cy, float sp, int denominator)
    {
        const bool whole = denominator == 1, hollow = denominator <= 2;
        const float w = (whole ? 1.75f : 1.3f) * sp, h = sp * 0.98f;
        juce::Path outer;
        outer.addEllipse (-w / 2, -h / 2, w, h);
        outer.applyTransform (juce::AffineTransform::rotation (whole ? 0.0f : -0.35f).translated (cx, cy));
        g.setColour (ink);
        g.fillPath (outer);
        if (hollow)
        {
            juce::Path inner;
            const float iw = whole ? w * 0.42f : w * 0.78f, ih = whole ? h * 0.62f : h * 0.42f;
            inner.addEllipse (-iw / 2, -ih / 2, iw, ih);
            inner.applyTransform (juce::AffineTransform::rotation (whole ? 0.9f : -0.5f).translated (cx, cy));
            g.setColour (paperColour);
            g.fillPath (inner);
            g.setColour (ink);
        }
    }

    void drawRest (juce::Graphics& g, float x, float staffTop, float sp, int denominator)
    {
        g.setColour (ink);
        const float mid = yOf (staffTop, sp, 4);
        switch (denominator)
        {
            case 1: g.fillRect (x - 0.6f * sp, yOf (staffTop, sp, 6), 1.2f * sp, 0.5f * sp); break;
            case 2: g.fillRect (x - 0.6f * sp, mid - 0.5f * sp, 1.2f * sp, 0.5f * sp); break;
            case 4:
            {
                juce::Path z;
                z.startNewSubPath (x - 0.3f * sp, mid - 1.5f * sp);
                z.lineTo (x + 0.4f * sp, mid - 0.6f * sp);
                z.lineTo (x - 0.3f * sp, mid + 0.2f * sp);
                z.lineTo (x + 0.4f * sp, mid + 0.9f * sp);
                z.quadraticTo (x - 0.6f * sp, mid + 0.6f * sp, x + 0.1f * sp, mid + 1.6f * sp);
                g.strokePath (z, juce::PathStrokeType (0.32f * sp, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
                break;
            }
            default:
            {
                // 8th / 16th / 32nd: a slanted stem with one, two or three hooks
                const int hooks = denominator >= 32 ? 3 : denominator >= 16 ? 2 : 1;
                const float top = mid - 0.6f * sp;
                const float bottom = top + (1.5f + (float) hooks) * sp;
                g.drawLine (x + 0.5f * sp, top, x - 0.1f * sp, bottom, 0.16f * sp);
                for (int k = 0; k < hooks; ++k)
                {
                    const float hy = top + (float) k * sp;
                    const float hx = x + 0.5f * sp - (float) k * 0.25f * sp;
                    g.fillEllipse (hx - 1.05f * sp, hy - 0.05f * sp, 0.5f * sp, 0.5f * sp);
                    juce::Path hook;
                    hook.startNewSubPath (hx - 0.8f * sp, hy + 0.3f * sp);
                    hook.quadraticTo (hx - 0.2f * sp, hy + 0.5f * sp, hx, hy);
                    g.strokePath (hook, juce::PathStrokeType (0.14f * sp));
                }
                break;
            }
        }
    }

    void drawAccidental (juce::Graphics& g, int acc, float x, float y, float sp)
    {
        juce::Path p;
        if (acc > 0)       p = glyph ("\xe2\x99\xaf", { x - 0.45f * sp, y - 1.35f * sp, 0.9f * sp, 2.7f * sp });   // sharp
        else if (acc < 0)  p = glyph ("\xe2\x99\xad", { x - 0.45f * sp, y - 1.75f * sp, 0.9f * sp, 2.35f * sp });  // flat
        else               p = glyph ("\xe2\x99\xae", { x - 0.4f * sp, y - 1.35f * sp, 0.8f * sp, 2.7f * sp });    // natural
        g.setColour (ink);
        if (! p.isEmpty())
            g.fillPath (p);
        else
            g.drawText (acc > 0 ? "#" : acc < 0 ? "b" : "n", juce::Rectangle<float> (x - sp, y - sp, 2 * sp, 2 * sp),
                        juce::Justification::centred);
    }

    void drawFlags (juce::Graphics& g, float sx, float sy, bool up, int denominator, float sp)
    {
        const int flags = denominator >= 32 ? 3 : denominator >= 16 ? 2 : denominator >= 8 ? 1 : 0;
        for (int k = 0; k < flags; ++k)
        {
            const float dir = up ? 1.0f : -1.0f;
            const float y0 = sy + dir * (float) k * 0.85f * sp;
            juce::Path f;
            f.startNewSubPath (sx, y0);
            f.cubicTo (sx + 0.1f * sp, y0 + dir * 1.0f * sp, sx + 1.3f * sp, y0 + dir * 1.3f * sp, sx + 0.9f * sp, y0 + dir * 2.9f * sp);
            f.cubicTo (sx + 1.0f * sp, y0 + dir * 1.7f * sp, sx + 0.4f * sp, y0 + dir * 1.4f * sp, sx, y0 + dir * 1.2f * sp);
            f.closeSubPath();
            g.fillPath (f);
        }
    }

    constexpr float padLeftSp = 2.6f, padRightSp = 1.2f;
} // namespace

//==============================================================================
ScoreView::ScoreView()
{
    addAndMakeVisible (keyLabel);
    keyLabel.setText ("Toonsoort:", juce::dontSendNotification);
    keyLabel.setColour (juce::Label::textColourId, colours::text);

    addAndMakeVisible (keyBox);
    keyBox.addItem ("Automatisch", 1);
    for (int f = -7; f <= 7; ++f)
        keyBox.addItem (notation::keyName (f), f + 100);
    keyBox.onChange = [this]
    {
        const int id = keyBox.getSelectedId();
        keySetting = id == 1 ? Song::autoKey : id - 100;
        rebuild();
        if (onKeyChosen)
            onKeyChosen (keySetting);
    };

    addAndMakeVisible (gridBox);
    gridBox.addItem ("Weergave in 1/8", 1);
    gridBox.addItem ("Weergave in 1/16", 2);
    gridBox.addItem ("Weergave in 1/32", 3);
    gridBox.setSelectedId (2, juce::dontSendNotification);
    gridBox.setTooltip ("Afronding voor rechte noten; triolen worden per tel vanzelf herkend");
    gridBox.onChange = [this] { rebuild(); };

    addAndMakeVisible (voicesBox);
    voicesBox.addItem ("1 stem per balk", 1);
    voicesBox.addItem ("2 stemmen per balk", 2);
    voicesBox.setSelectedId (2, juce::dontSendNotification);
    voicesBox.setTooltip ("Twee stemmen: een bovenstem (stokken omhoog) en een onderstem (stokken omlaag) per notenbalk");
    voicesBox.onChange = [this] { rebuild(); };

    addAndMakeVisible (saveButton);
    saveButton.onClick = [this] { saveImage(); };

    addAndMakeVisible (vScroll);
    vScroll.addListener (this);
    vScroll.setAutoHide (false);
}

void ScoreView::setContent (const std::vector<SongNote>& n, double length, int bpb, int unit, int key, const juce::String& t)
{
    notes = n;
    clipLength = length;
    beatsPerBar = juce::jmax (1, bpb);
    beatUnit = juce::jmax (1, unit);
    keySetting = key;
    title = t;
    keyBox.setSelectedId (key == Song::autoKey ? 1 : key + 100, juce::dontSendNotification);
    rebuild();
}

void ScoreView::setPlayhead (double clipBeat)
{
    if (std::abs (clipBeat - playhead) > 1.0e-3)
    {
        playhead = clipBeat;
        repaint();
    }
}

void ScoreView::rebuild()
{
    const double barLength = beatsPerBar * 4.0 / beatUnit;
    // Compound meters (6/8, 9/8, 12/8) count in dotted quarters; other x/8 meters in eighths.
    const double beatLength = beatUnit == 8 && beatsPerBar % 3 == 0 && beatsPerBar > 3 ? 1.5 : 4.0 / beatUnit;
    const int key = keySetting == Song::autoKey ? notation::guessKey (notes) : keySetting;
    const double grid = gridBox.getSelectedId() == 1 ? 0.5 : gridBox.getSelectedId() == 3 ? 0.125 : 0.25;
    layout = notation::layout (notes, clipLength, barLength, beatLength, key, grid, 60, voicesBox.getSelectedId() == 1 ? 1 : 2);
    keyBox.changeItemText (1, "Automatisch (" + notation::keyName (key).upToFirstOccurrenceOf (" /", false, false) + ")");
    resized();
    repaint();
}

//==============================================================================
ScoreView::Geometry ScoreView::computeGeometry (float width) const
{
    Geometry G;
    G.sp = 8.0f * zoom;
    G.pxPerQuarter = 58.0f * zoom;
    G.left = 2.0f * G.sp;
    G.width = juce::jmax (200.0f, width - 4.0f * G.sp);
    const float keyWidth = (float) std::abs (layout.keyFifths) * 1.0f * G.sp;
    G.prefix = 4.5f * G.sp + keyWidth + 1.0f * G.sp;
    G.systemHeight = 27.0f * G.sp;

    const int numBars = (int) layout.bars.size();
    const float natural = (float) layout.barLength * G.pxPerQuarter + (padLeftSp + padRightSp) * G.sp;
    G.barX.assign ((size_t) numBars, 0.0f);
    G.barWidth.assign ((size_t) numBars, natural);

    int bar = 0;
    float y = 4.0f * G.sp;
    while (bar < numBars)
    {
        System s;
        s.firstBar = bar;
        s.y = y;
        const float timeSig = bar == 0 ? 3.0f * G.sp : 0.0f;
        const float avail = G.width - G.prefix - timeSig;
        const int fit = juce::jmax (1, (int) (avail / natural));
        s.numBars = juce::jmin (fit, numBars - bar);
        // Justify full systems; the last one keeps its natural width.
        const float w = (s.numBars == fit && bar + s.numBars < numBars) ? avail / (float) s.numBars : natural;
        float x = G.left + G.prefix + timeSig;
        for (int b = bar; b < bar + s.numBars; ++b)
        {
            G.barX[(size_t) b] = x;
            G.barWidth[(size_t) b] = w;
            x += w;
        }
        G.systems.push_back (s);
        bar += s.numBars;
        y += G.systemHeight;
    }
    return G;
}

void ScoreView::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (36).reduced (6, 4);
    keyLabel.setBounds (bar.removeFromLeft (80));
    keyBox.setBounds (bar.removeFromLeft (230));
    bar.removeFromLeft (8);
    gridBox.setBounds (bar.removeFromLeft (170));
    bar.removeFromLeft (8);
    voicesBox.setBounds (bar.removeFromLeft (170));
    bar.removeFromLeft (8);
    saveButton.setBounds (bar.removeFromLeft (200));
    vScroll.setBounds (r.removeFromRight (14));
    paper = r;

    const auto G = computeGeometry ((float) paper.getWidth());
    const double total = G.systems.size() * (double) G.systemHeight + 8.0 * G.sp + 30.0;
    vScroll.setRangeLimits (0.0, juce::jmax (total, (double) paper.getHeight()));
    vScroll.setCurrentRange (vScroll.getCurrentRangeStart(), paper.getHeight());
}

void ScoreView::scrollBarMoved (juce::ScrollBar*, double) { repaint(); }

void ScoreView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        zoom = juce::jlimit (0.6f, 2.5f, zoom * (w.deltaY > 0 ? 1.1f : 1.0f / 1.1f));
        resized();
        repaint();
        return;
    }
    vScroll.setCurrentRangeStart (vScroll.getCurrentRangeStart() - w.deltaY * 300.0);
}

//==============================================================================
void ScoreView::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    g.setColour (paperColour);
    g.fillRect (paper);
    g.saveState();
    g.reduceClipRegion (paper);
    g.setOrigin (paper.getPosition());
    const auto G = computeGeometry ((float) paper.getWidth());
    drawScore (g, G, -(float) vScroll.getCurrentRangeStart(), true);
    g.restoreState();
}

void ScoreView::drawScore (juce::Graphics& g, const Geometry& G, float yOffset, bool showPlayhead) const
{
    g.setColour (ink);
    g.setFont (juce::FontOptions (2.2f * G.sp, juce::Font::bold));
    g.drawText (title, juce::Rectangle<float> (G.left, yOffset + 0.5f * G.sp, G.width, 2.6f * G.sp), juce::Justification::centred);

    for (size_t i = 0; i < G.systems.size(); ++i)
        drawSystem (g, G, G.systems[i], G.systems[i].y + yOffset + 2.0f * G.sp, i == 0);

    if (showPlayhead && playhead >= 0.0 && ! layout.bars.empty())
    {
        const int b = juce::jlimit (0, (int) layout.bars.size() - 1, (int) (playhead / layout.barLength));
        for (auto& s : G.systems)
            if (b >= s.firstBar && b < s.firstBar + s.numBars)
            {
                const float inner = G.barWidth[(size_t) b] - (padLeftSp + padRightSp) * G.sp;
                const float x = G.barX[(size_t) b] + padLeftSp * G.sp
                              + (float) ((playhead - layout.bars[(size_t) b].start) / layout.barLength) * inner;
                const float top = s.y + yOffset + 2.0f * G.sp + 4.0f * G.sp;
                g.setColour (colours::accent.withAlpha (0.8f));
                g.fillRect (x - 1.0f, top - 2.0f * G.sp, 2.0f, 18.0f * G.sp);
            }
    }
}

void ScoreView::drawSystem (juce::Graphics& g, const Geometry& G, const System& s, float y, bool first) const
{
    const float sp = G.sp;
    const float trebleTop = y + 4.0f * sp, bassTop = trebleTop + 10.0f * sp;
    const float x0 = G.left, x1 = G.barX[(size_t) (s.firstBar + s.numBars - 1)] + G.barWidth[(size_t) (s.firstBar + s.numBars - 1)];

    g.setColour (ink);
    for (float top : { trebleTop, bassTop })
        for (int i = 0; i < 5; ++i)
            g.drawLine (x0, top + (float) i * sp, x1, top + (float) i * sp, juce::jmax (1.0f, 0.1f * sp));

    // Brace-like bracket and system line
    g.fillRect (x0 - 0.15f * sp, trebleTop, 0.3f * sp, 14.0f * sp);
    g.fillRect (x0 - 0.9f * sp, trebleTop - 0.2f * sp, 0.45f * sp, 14.4f * sp);

    // Clefs
    if (auto p = glyph ("\xf0\x9d\x84\x9e", { x0 + 0.4f * sp, trebleTop - 1.6f * sp, 3.0f * sp, 7.6f * sp }); ! p.isEmpty())
        g.fillPath (p);
    else
        g.drawText ("G", juce::Rectangle<float> (x0, trebleTop, 3 * sp, 4 * sp), juce::Justification::centred);
    if (auto p = glyph ("\xf0\x9d\x84\xa2", { x0 + 0.5f * sp, bassTop - 0.05f * sp, 3.0f * sp, 3.3f * sp }); ! p.isEmpty())
        g.fillPath (p);
    else
        g.drawText ("F", juce::Rectangle<float> (x0, bassTop, 3 * sp, 4 * sp), juce::Justification::centred);

    // Key signature
    static constexpr int sharpSteps[7] = { 38, 35, 39, 36, 33, 37, 34 };   // treble F5 C5 G5 D5 A4 E5 B4
    static constexpr int flatSteps[7] = { 34, 37, 33, 36, 32, 35, 31 };    // treble B4 E5 A4 D5 G4 C5 F4
    const int n = std::abs (layout.keyFifths);
    for (int k = 0; k < juce::jmin (7, n); ++k)
    {
        const int step = layout.keyFifths > 0 ? sharpSteps[k] : flatSteps[k];
        const float kx = x0 + 4.4f * sp + (float) k * 1.0f * sp;
        drawAccidental (g, layout.keyFifths > 0 ? 1 : -1, kx, yOf (trebleTop, sp, notation::staffPosition (step, 0)), sp);
        drawAccidental (g, layout.keyFifths > 0 ? 1 : -1, kx, yOf (bassTop, sp, notation::staffPosition (step - 14, 1)), sp);
    }

    // Time signature on the first system
    if (first)
    {
        const float tx = x0 + G.prefix + 0.2f * sp;
        g.setFont (juce::FontOptions (2.9f * sp, juce::Font::bold));
        for (float top : { trebleTop, bassTop })
        {
            g.drawText (juce::String (beatsPerBar), juce::Rectangle<float> (tx, top - 0.35f * sp, 2.4f * sp, 2.6f * sp), juce::Justification::centred);
            g.drawText (juce::String (beatUnit), juce::Rectangle<float> (tx, top + 1.75f * sp, 2.4f * sp, 2.6f * sp), juce::Justification::centred);
        }
    }

    // Bars
    for (int b = s.firstBar; b < s.firstBar + s.numBars; ++b)
    {
        const float bx = G.barX[(size_t) b], bw = G.barWidth[(size_t) b];
        const bool last = b == (int) layout.bars.size() - 1;
        g.setColour (ink);
        if (last)
        {
            g.fillRect (bx + bw - 0.9f * sp, trebleTop, 0.15f * sp, 14.0f * sp);
            g.fillRect (bx + bw - 0.5f * sp, trebleTop, 0.5f * sp, 14.0f * sp);
        }
        else
        {
            g.fillRect (bx + bw - 0.08f * sp, trebleTop, juce::jmax (1.0f, 0.15f * sp), 14.0f * sp);
        }
        if (b == s.firstBar)
        {
            g.setFont (juce::FontOptions (1.4f * sp, juce::Font::italic));
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (x0, trebleTop - 3.6f * sp, 3.0f * sp, 1.6f * sp), juce::Justification::centredLeft);
        }
        drawStaffItems (g, G, layout.bars[(size_t) b], 0, bx, trebleTop);
        drawStaffItems (g, G, layout.bars[(size_t) b], 1, bx, bassTop);
    }
}

void ScoreView::drawStaffItems (juce::Graphics& g, const Geometry& G, const notation::Bar& bar, int staff, float barX, float staffTop) const
{
    const float sp = G.sp;
    const auto& items = bar.staves[(size_t) staff];
    const size_t b = (size_t) std::lround (bar.start / layout.barLength);
    const float barW = b < G.barWidth.size() ? G.barWidth[b] : 100.0f;
    const float inner = barW - (padLeftSp + padRightSp) * sp;
    const float headW = 1.3f * sp;
    auto xOf = [&] (const notation::Item& it)
    {
        return barX + padLeftSp * sp + (float) ((it.start - bar.start) / layout.barLength) * inner;
    };

    struct StemInfo { float x = 0, yHead = 0, yEnd = 0; bool up = true; };
    std::vector<StemInfo> stems (items.size());

    // Stem direction: per beam group, otherwise per chord.
    std::vector<bool> stemUp (items.size(), true);
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].rest)
            continue;
        double sum = 0.0;
        int count = 0;
        for (size_t j = 0; j < items.size(); ++j)
            if (j == i || (items[i].beamGroup >= 0 && items[j].beamGroup == items[i].beamGroup))
                for (auto& h : items[j].heads)
                {
                    sum += notation::staffPosition (h.step, staff);
                    ++count;
                }
        stemUp[i] = count == 0 || sum / count < 4.0;
        // Two voices on one staff: upper voice stems up, lower voice stems down.
        if (bar.twoVoices[(size_t) staff])
            stemUp[i] = items[i].voice == 0;
    }

    g.setColour (ink);
    for (size_t i = 0; i < items.size(); ++i)
    {
        const auto& it = items[i];
        const float x = xOf (it);

        if (it.rest)
        {
            const bool wholeBar = it.value.denominator == 1;
            // With two voices the rests move out of each other's way: upper voice higher, lower voice lower.
            const float shift = bar.twoVoices[(size_t) staff] ? (it.voice == 0 ? -2.0f * sp : 2.0f * sp) : 0.0f;
            drawRest (g, wholeBar ? barX + barW * 0.5f - 0.3f * sp : x + 0.5f * headW, staffTop + shift, sp, it.value.denominator);
            if (it.value.dotted)
                g.fillEllipse (x + headW + 0.5f * sp, yOf (staffTop + shift, sp, 5) - 0.2f * sp, 0.4f * sp, 0.4f * sp);
            continue;
        }

        const bool up = stemUp[i];
        const float cx = x + headW * 0.5f;
        float lowY = -1.0e9f, highY = 1.0e9f;
        int accColumn = 0;
        int prevStep = -1000;
        bool prevShifted = false;
        for (size_t k = 0; k < it.heads.size(); ++k)
        {
            const auto& h = it.heads[k];
            const int pos = notation::staffPosition (h.step, staff);
            const float y = yOf (staffTop, sp, pos);

            // Seconds in a chord: put one head on the other side of the stem.
            const bool shifted = (h.step - prevStep == 1) && ! prevShifted;
            prevShifted = shifted;
            prevStep = h.step;
            const float hx = cx + (shifted ? (up ? headW - 0.1f * sp : -(headW - 0.1f * sp)) : 0.0f);

            // Ledger lines
            for (int l = -2; l >= pos; l -= 2)
                g.drawLine (hx - 0.95f * headW, yOf (staffTop, sp, l), hx + 0.95f * headW, yOf (staffTop, sp, l), juce::jmax (1.0f, 0.12f * sp));
            for (int l = 10; l <= pos; l += 2)
                g.drawLine (hx - 0.95f * headW, yOf (staffTop, sp, l), hx + 0.95f * headW, yOf (staffTop, sp, l), juce::jmax (1.0f, 0.12f * sp));

            drawHead (g, hx, y, sp, it.value.denominator);

            if (h.accidental != notation::noAccidental)
                drawAccidental (g, h.accidental, cx - headW * 0.5f - 1.0f * sp - (float) (accColumn++ % 2) * 1.0f * sp, y, sp);

            if (it.value.dotted)
            {
                const float dy = (pos % 2 == 0) ? -0.5f * sp : 0.0f;
                g.fillEllipse (cx + headW * 0.5f + (up && shifted ? headW : 0.0f) + 0.35f * sp, y + dy - 0.2f * sp, 0.4f * sp, 0.4f * sp);
            }

            if (h.tieToNext)
            {
                float endX = barX + barW + padLeftSp * sp * 0.6f;
                for (size_t j = i + 1; j < items.size(); ++j)
                    if (! items[j].rest && items[j].voice == it.voice)
                    {
                        endX = xOf (items[j]) + headW * 0.2f;
                        break;
                    }
                const float dir = up ? 1.0f : -1.0f;
                juce::Path tie;
                const float ty = y + dir * 0.7f * sp;
                tie.startNewSubPath (cx + headW * 0.4f, ty);
                tie.quadraticTo ((cx + endX) * 0.5f, ty + dir * 1.1f * sp, endX, ty);
                tie.quadraticTo ((cx + endX) * 0.5f, ty + dir * 0.75f * sp, cx + headW * 0.4f, ty);
                tie.closeSubPath();
                g.fillPath (tie);
            }

            lowY = juce::jmax (lowY, y);
            highY = juce::jmin (highY, y);
        }

        if (it.value.denominator >= 2)
        {
            auto& st = stems[i];
            st.up = up;
            st.x = up ? cx + headW * 0.5f - 0.06f * sp : cx - headW * 0.5f + 0.06f * sp;
            st.yHead = up ? lowY : highY;
            st.yEnd = up ? highY - 3.5f * sp : lowY + 3.5f * sp;
            // Notes far from the staff: stems reach the middle line.
            const float middle = yOf (staffTop, sp, 4);
            st.yEnd = up ? juce::jmin (st.yEnd, middle) : juce::jmax (st.yEnd, middle);
        }
    }

    // Beams: straight line between the outer stems, moved so no stem gets too short.
    std::map<int, std::vector<size_t>> groups;
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].beamGroup >= 0)
            groups[items[i].beamGroup].push_back (i);

    for (auto& [id, members] : groups)
    {
        const auto& a = stems[members.front()];
        const auto& z = stems[members.back()];
        const bool up = a.up;
        const float dx = juce::jmax (1.0f, z.x - a.x);
        float slope = juce::jlimit (-0.12f, 0.12f, (z.yEnd - a.yEnd) / dx);
        float y0 = a.yEnd;
        for (auto m : members)
        {
            const float lineY = y0 + slope * (stems[m].x - a.x);
            const float natural = stems[m].yEnd;
            if (up && lineY > natural) y0 -= lineY - natural;
            if (! up && lineY < natural) y0 += natural - lineY;
        }
        for (auto m : members)
            stems[m].yEnd = y0 + slope * (stems[m].x - a.x);

        const float thick = 0.48f * sp, gap = 0.78f * sp;
        auto beamAt = [&] (float xa, float xb, int level)
        {
            const float off = (up ? 1.0f : -1.0f) * (float) level * gap;
            juce::Path p;
            const float ya = y0 + slope * (xa - a.x) + off, yb = y0 + slope * (xb - a.x) + off;
            const float t = up ? thick : -thick;
            p.startNewSubPath (xa, ya);
            p.lineTo (xb, yb);
            p.lineTo (xb, yb + t);
            p.lineTo (xa, ya + t);
            p.closeSubPath();
            g.fillPath (p);
        };
        beamAt (a.x, z.x, 0);
        for (int level = 1; level <= 2; ++level)
        {
            const int need = level == 1 ? 16 : 32;
            for (size_t k = 0; k < members.size(); ++k)
            {
                if (items[members[k]].value.denominator < need)
                    continue;
                const bool nextToo = k + 1 < members.size() && items[members[k + 1]].value.denominator >= need;
                const bool prevToo = k > 0 && items[members[k - 1]].value.denominator >= need;
                if (nextToo)
                    beamAt (stems[members[k]].x, stems[members[k + 1]].x, level);
                else if (! prevToo)
                {
                    const float stub = 1.1f * sp;
                    if (k + 1 < members.size())
                        beamAt (stems[members[k]].x, stems[members[k]].x + stub, level);
                    else
                        beamAt (stems[members[k]].x - stub, stems[members[k]].x, level);
                }
            }
        }
    }

    for (size_t i = 0; i < items.size(); ++i)
    {
        const auto& st = stems[i];
        if (items[i].rest || items[i].value.denominator < 2)
            continue;
        g.drawLine (st.x, st.yHead, st.x, st.yEnd, juce::jmax (1.0f, 0.13f * sp));
        if (items[i].beamGroup < 0)
            drawFlags (g, st.x, st.yEnd, st.up, items[i].value.denominator, sp);
    }

    // Triplets: a "3" (with a bracket unless the notes share one beam) on the stem side.
    const auto& tuplets = bar.tuplets[(size_t) staff];
    for (int t = 0; t < (int) tuplets.size(); ++t)
    {
        float x0 = 1.0e9f, x1 = -1.0e9f, top = staffTop - sp, bottom = staffTop + 5.0f * sp;
        int upVotes = 0, count = 0, beam = -2;
        bool oneBeam = true;
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (items[i].tuplet != t)
                continue;
            const float x = xOf (items[i]);
            x0 = juce::jmin (x0, x);
            x1 = juce::jmax (x1, x + headW);
            ++count;
            if (items[i].rest)
            {
                oneBeam = false;
                continue;
            }
            upVotes += stemUp[i] ? 1 : -1;
            top = juce::jmin (top, juce::jmin (stems[i].yEnd, stems[i].yHead - sp));
            bottom = juce::jmax (bottom, juce::jmax (stems[i].yEnd, stems[i].yHead + sp));
            if (beam == -2)
                beam = items[i].beamGroup;
            oneBeam = oneBeam && items[i].beamGroup >= 0 && items[i].beamGroup == beam;
        }
        if (count == 0)
            continue;
        const bool above = tuplets[(size_t) t].voice == 0 ? upVotes >= 0 : false;
        const float y = above ? top - 1.4f * sp : bottom + 1.4f * sp;
        const float mid = 0.5f * (x0 + x1);
        g.setFont (juce::FontOptions (1.6f * sp, juce::Font::bold | juce::Font::italic));
        const juce::Rectangle<float> label (mid - 0.8f * sp, y - 0.9f * sp, 1.6f * sp, 1.8f * sp);
        if (! oneBeam)
        {
            const float hook = above ? 0.6f * sp : -0.6f * sp;
            const float lw = juce::jmax (1.0f, 0.1f * sp);
            g.drawLine (x0, y + hook, x0, y, lw);
            g.drawLine (x0, y, label.getX() - 0.1f * sp, y, lw);
            g.drawLine (label.getRight() + 0.1f * sp, y, x1, y, lw);
            g.drawLine (x1, y, x1, y + hook, lw);
        }
        g.drawText ("3", label, juce::Justification::centred);
    }
}

//==============================================================================
juce::Image ScoreView::renderImage (int width, float scale)
{
    const auto G = computeGeometry ((float) width);
    const int height = (int) (G.systems.size() * G.systemHeight + 8.0f * G.sp + 20.0f);
    juce::Image img (juce::Image::RGB, (int) ((float) width * scale), (int) ((float) height * scale), true);
    juce::Graphics g (img);
    g.addTransform (juce::AffineTransform::scale (scale));
    g.fillAll (paperColour);
    drawScore (g, G, 0.0f, false);
    return img;
}

void ScoreView::saveImage()
{
    chooser = std::make_unique<juce::FileChooser> ("Notenbalk opslaan als afbeelding",
                                                    juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                        .getChildFile ((title.isEmpty() ? juce::String ("Notenbalk") : title) + ".png"),
                                                    "*.png");
    juce::Component::SafePointer<ScoreView> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (safe == nullptr || file == juce::File())
                return;
            file = file.withFileExtension ("png");
            const auto img = safe->renderImage (1100, 2.0f);
            juce::FileOutputStream out (file);
            if (out.openedOk())
            {
                out.setPosition (0);
                out.truncate();
                juce::PNGImageFormat().writeImageToStream (img, out);
            }
        });
}

} // namespace km
