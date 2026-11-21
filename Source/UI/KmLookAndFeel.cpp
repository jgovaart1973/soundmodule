#include "UI/KmLookAndFeel.h"

namespace km
{
KmLookAndFeel::KmLookAndFeel()
{
    using namespace colours;
    setColourScheme ({ background, panel, panelLight, outline, text, accent, juce::Colours::black, panelLight, text });

    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::TextButton::buttonColourId, panelLight);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::ComboBox::backgroundColourId, panelLight);
    setColour (juce::ComboBox::outlineColourId, outline);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.35f));
    setColour (juce::Slider::thumbColourId, accent);
    setColour (juce::Slider::trackColourId, accent);
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, outline);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxBackgroundColourId, panelLight);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, text);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::TabbedButtonBar::tabTextColourId, textDim);
    setColour (juce::TabbedButtonBar::frontTextColourId, text);
    setColour (juce::ListBox::backgroundColourId, panel);
}

void KmLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                      float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float stroke = juce::jmax (2.5f, radius * 0.18f);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius - stroke * 0.5f, radius - stroke * 0.5f, 0.0f, startAngle, endAngle, true);
    g.setColour (colours::outline);
    g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bipolar sliders (pan) fill from the centre.
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float fromAngle = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius - stroke * 0.5f, radius - stroke * 0.5f, 0.0f,
                         juce::jmin (fromAngle, angle), juce::jmax (fromAngle, angle), true);
    g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId).withAlpha (slider.isEnabled() ? 1.0f : 0.4f));
    g.strokePath (value, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto tip = centre.getPointOnCircumference (radius - stroke * 1.6f, angle);
    g.setColour (colours::text);
    g.drawLine ({ centre, tip }, 2.0f);
}

void KmLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                      float minPos, float maxPos, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, pos, minPos, maxPos, style, slider);
        return;
    }

    const bool horizontal = style == juce::Slider::LinearHorizontal;
    auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
    const float thickness = 6.0f;
    auto track = horizontal ? area.withSizeKeepingCentre (area.getWidth(), thickness)
                            : area.withSizeKeepingCentre (thickness, area.getHeight());

    g.setColour (colours::outline);
    g.fillRoundedRectangle (track, thickness * 0.5f);

    auto filled = horizontal ? track.withRight (pos) : track.withTop (pos);
    g.setColour (slider.findColour (juce::Slider::trackColourId).withAlpha (slider.isEnabled() ? 1.0f : 0.4f));
    g.fillRoundedRectangle (filled, thickness * 0.5f);

    const float thumb = 14.0f;
    const auto thumbCentre = horizontal ? juce::Point<float> (pos, track.getCentreY()) : juce::Point<float> (track.getCentreX(), pos);
    g.setColour (colours::text);
    g.fillEllipse (juce::Rectangle<float> (thumb, thumb).withCentre (thumbCentre));
}

void KmLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& bg,
                                          bool over, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto colour = button.getToggleState() ? button.findColour (juce::TextButton::buttonOnColourId) : bg;
    if (down)      colour = colour.brighter (0.2f);
    else if (over) colour = colour.brighter (0.08f);
    if (! button.isEnabled())
        colour = colour.withMultipliedAlpha (0.5f);

    g.setColour (colour);
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (bounds, 5.0f, 1.0f);
}

juce::Font KmLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::Font (juce::FontOptions (juce::jmin (15.0f, (float) buttonHeight * 0.5f)));
}

} // namespace km
