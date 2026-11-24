#pragma once

#include "Model/Notation.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace km
{
/**
    Notation view (phase 3c): shows a clip on a grand staff (treble + bass) with key signature,
    time signature, note values, ties, beams, accidentals and rests. Read-only: edit in the piano
    roll, the score follows. Can be saved as a PNG for printing.
*/
class ScoreView final : public juce::Component, private juce::ScrollBar::Listener
{
public:
    ScoreView();

    /** Notes are clip-relative. keySetting = Song::autoKey to guess the key. */
    void setContent (const std::vector<SongNote>& notes, double clipLength, int beatsPerBar, int beatUnit,
                     int keySetting, const juce::String& title);
    void setPlayhead (double clipBeat);   ///< < 0 = hidden

    std::function<void (int keySetting)> onKeyChosen;

    /** Render the whole score at the given width (for saving / printing). */
    juce::Image renderImage (int width, float scale = 2.0f);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct System { int firstBar = 0, numBars = 0; float y = 0.0f; };
    struct Geometry
    {
        float sp = 8.0f;                ///< staff line distance
        float pxPerQuarter = 64.0f;
        float left = 16.0f, width = 800.0f;
        float prefix = 90.0f;           ///< clef + key + time
        float systemHeight = 200.0f;
        std::vector<System> systems;
        std::vector<float> barX, barWidth;
    };

    void scrollBarMoved (juce::ScrollBar*, double) override;
    void rebuild();
    Geometry computeGeometry (float width) const;
    void drawScore (juce::Graphics&, const Geometry&, float yOffset, bool showPlayhead) const;
    void drawSystem (juce::Graphics&, const Geometry&, const System&, float y, bool first) const;
    void drawStaffItems (juce::Graphics&, const Geometry&, const notation::Bar&, int staff, float barX, float staffTop) const;
    void saveImage();

    std::vector<SongNote> notes;
    double clipLength = 4.0;
    int beatsPerBar = 4, beatUnit = 4, keySetting = -99;
    juce::String title;
    notation::Layout layout;
    double playhead = -1.0;
    float zoom = 1.0f;

    juce::Rectangle<int> paper;
    juce::Label keyLabel;
    juce::ComboBox keyBox, gridBox, voicesBox;
    juce::TextButton saveButton { "Opslaan als afbeelding..." };
    juce::ScrollBar vScroll { true };
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace km
