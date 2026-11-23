#pragma once

#include "App/AppController.h"
#include "UI/PianoRoll.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace km
{
/**
    The arranger (phase 2a): transport bar, tracks with their part/sound, and a timeline
    with clips, loop range and playhead.
*/
class ArrangerComponent final : public juce::Component, private juce::Timer, private juce::ScrollBar::Listener
{
public:
    explicit ArrangerComponent (AppController& controller);
    ~ArrangerComponent() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Rebuild after the song changed (tracks added, recording stored...). */
    void refresh();

    /** Open the piano roll for a clip. */
    void openEditor (int track, int clip);

private:
    class TransportBar;
    class TrackHeader;
    class Ruler;
    class Lanes;

    void timerCallback() override;
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void rebuildHeaders();
    void updateScrollRange();
    void showSongMenu();
    void chooseFile (const juce::String& title, const juce::String& pattern, bool save,
                     std::function<void (const juce::File&)> onChosen);

    // Shared view state for ruler and lanes
    double beatToX (double beat) const noexcept { return (beat - scrollBeat) * pixelsPerBeat; }
    double xToBeat (double x) const noexcept    { return x / pixelsPerBeat + scrollBeat; }
    double snap (double beat) const noexcept    { return std::round (beat); }

    AppController& controller;
    SongController& song;

    std::unique_ptr<TransportBar> transport;
    std::unique_ptr<Ruler> ruler;
    std::unique_ptr<Lanes> lanes;
    juce::Viewport bodyViewport;
    juce::Component body;
    juce::OwnedArray<TrackHeader> headers;
    juce::ScrollBar hScroll { false };
    std::unique_ptr<PianoRoll> pianoRoll;

    double pixelsPerBeat = 24.0;
    double scrollBeat = 0.0;
    double lastDrawnPosition = -1.0;
    int selectedTrack = -1, selectedClip = -1;
    std::unique_ptr<juce::FileChooser> fileChooser;

    static constexpr int headerWidth = 250;
    static constexpr int laneHeight = 64;
    static constexpr int rulerHeight = 26;
};

} // namespace km
