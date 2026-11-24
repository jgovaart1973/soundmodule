#pragma once

#include "App/AppController.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <set>

namespace km
{
/**
    Piano roll for one clip (phase 2b): draw, move, resize and delete notes, quantize with
    strength/swing, transpose, edit velocities and draw controller curves. Every finished
    gesture is one undo step in the SongController.
*/
class PianoRoll final : public juce::Component, private juce::Timer, private juce::ScrollBar::Listener
{
public:
    PianoRoll (AppController& app, SongController& song);
    ~PianoRoll() override;

    void open (int track, int clip);
    void reloadFromSong();             ///< the song changed (undo, recording, ...)
    bool isOpen() const noexcept       { return track >= 0; }
    int getTrack() const noexcept      { return track; }
    int getClip() const noexcept       { return clipIndex; }

    std::function<void()> onClose;

    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum class Drag { none, move, resize, rubber, velocity, ccDraw, ccErase, keyboard };

    void timerCallback() override;
    void scrollBarMoved (juce::ScrollBar*, double) override;
    void updateScrollBars();
    void commitNotes (const juce::String& what);
    void commitControls (const juce::String& what);
    void quantizeNow();
    void transposeSelection (int semitones);
    void deleteSelection();
    double grid() const;
    const edit::ControllerLane& lane() const;
    int partOfTrack() const;

    // Geometry
    float beatToX (double beat) const noexcept { return gridArea.getX() + (float) ((beat - scrollBeat) * pixelsPerBeat); }
    double xToBeat (float x) const noexcept    { return (x - gridArea.getX()) / pixelsPerBeat + scrollBeat; }
    float noteToY (int note) const noexcept;
    int yToNote (float y) const noexcept;
    int kbWidth() const noexcept               { return drumMode ? 112 : 56; }
    float rowHeight() const noexcept           { return drumMode ? drumRowHeight : noteHeight; }

    // Drum mode: one row per drum sound (General MIDI map) instead of a chromatic keyboard
    void setDrumMode (bool on);
    void rebuildDrumRows();
    int rowOf (int note) const noexcept;
    int rowAt (float y) const noexcept;
    bool drumMode = false;
    std::vector<int> rowNotes;
    int firstRow = 0;
    float drumRowHeight = 18.0f;
    int toggleCandidate = -1;
    juce::Rectangle<float> noteRect (const SongNote& n) const;
    int hitNote (juce::Point<float> p) const;
    double snapBeat (double beat) const;

    AppController& app;
    SongController& song;
    int track = -1, clipIndex = -1;
    Clip clip;                        ///< working copy, written back on every finished gesture
    std::set<int> selection;

    // View
    double pixelsPerBeat = 80.0, scrollBeat = 0.0;
    float noteHeight = 12.0f;
    int topNote = 84;
    juce::Rectangle<float> keyboardArea, gridArea, velocityArea, ccArea;

    // Gesture state
    Drag drag = Drag::none;
    std::vector<SongNote> originals;
    double dragStartBeat = 0.0, ccLastBeat = 0.0, ccLastValue = 0.0;
    int dragStartNote = 60, dragAnchor = -1, lastPreviewNote = -1;
    juce::Rectangle<float> rubber;
    bool changedInGesture = false;
    int lastVelocity = 100;

    // Toolbar
    juce::Label title;
    juce::ComboBox gridBox, laneBox;
    juce::ToggleButton snapButton { "Raster" }, lengthsButton { "Ook lengtes" };
    juce::Slider strength, swing;
    juce::Label strengthLabel, swingLabel;
    juce::TextButton drumsButton { "Drums" };
    juce::TextButton quantizeButton { "Quantize (Q)" }, octDown { "-12" }, semiDown { "-1" }, semiUp { "+1" },
                     octUp { "+12" }, closeButton { "Sluiten" };
    juce::ScrollBar hScroll { false }, vScroll { true };
};

} // namespace km
