#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace km
{
namespace colours
{
    const juce::Colour background { 0xff15171c };
    const juce::Colour panel      { 0xff1f232b };
    const juce::Colour panelLight { 0xff2a2f39 };
    const juce::Colour outline    { 0xff363c48 };
    const juce::Colour text       { 0xffe4e7ec };
    const juce::Colour textDim    { 0xff8a93a3 };
    const juce::Colour accent     { 0xff4fb3ff };
    const juce::Colour learn      { 0xffffb347 };
    const juce::Colour good       { 0xff5ad17a };
    const juce::Colour warn       { 0xffffc24f };
    const juce::Colour danger     { 0xffe5534b };
} // namespace colours

/** Dark, high-contrast look that stays readable on stage and works with touch. */
class KmLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    KmLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
};

} // namespace km
