#include "UI/LearnableSlider.h"

#include "UI/KmLookAndFeel.h"

namespace km
{
LearnableSlider::LearnableSlider (MidiLearn& l, juce::String id) : learn (l), paramId (std::move (id))
{
    refreshLearnState();
}

void LearnableSlider::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && paramId.isNotEmpty())
    {
        showLearnMenu();
        return;
    }
    Slider::mouseDown (e);
}

void LearnableSlider::showLearnMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader (learn.getDisplayName (paramId));
    menu.addItem (1, armed ? "MIDI Learn annuleren" : "MIDI Learn (beweeg daarna een knop)");
    menu.addItem (2, "Koppeling verwijderen" + (mapped ? " (" + learn.describeMapping (paramId) + ")" : juce::String()), mapped);
    menu.addItem (3, "Standaardwaarde");

    juce::Component::SafePointer<LearnableSlider> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int result)
    {
        if (safe == nullptr)
            return;
        auto& s = *safe;
        if (result == 1)
        {
            if (s.armed) s.learn.cancelArm();
            else         s.learn.arm (s.paramId);
        }
        else if (result == 2)
        {
            s.learn.removeMapping (s.paramId);
        }
        else if (result == 3 && s.isDoubleClickReturnEnabled())
        {
            s.setValue (s.getDoubleClickReturnValue(), juce::sendNotificationSync);
        }
        s.refreshLearnState();
    });
}

void LearnableSlider::refreshLearnState()
{
    const bool nowArmed = paramId.isNotEmpty() && learn.getArmedParameter() == paramId;
    const bool nowMapped = paramId.isNotEmpty() && learn.hasMapping (paramId);

    if (nowArmed != armed || nowMapped != mapped)
    {
        armed = nowArmed;
        mapped = nowMapped;
        setTooltip (mapped ? "MIDI: " + learn.describeMapping (paramId) + "  (rechtsklik voor MIDI Learn)"
                           : "Rechtsklik voor MIDI Learn");
        repaint();
    }
}

void LearnableSlider::paint (juce::Graphics& g)
{
    Slider::paint (g);

    if (armed)
    {
        g.setColour (colours::learn);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f, 2.0f);
    }
    else if (mapped)
    {
        g.setColour (colours::learn);
        g.fillEllipse (2.0f, 2.0f, 5.0f, 5.0f);
    }
}

} // namespace km
