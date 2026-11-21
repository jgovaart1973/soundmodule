#pragma once

#include "App/AppController.h"
#include "UI/LearnableSlider.h"
#include "UI/PartRow.h"

namespace km
{
/** Right-hand panel: master volume + meter, reverb, delay and limiter. */
class MasterPanel final : public juce::Component
{
public:
    explicit MasterPanel (AppController& controller);

    void refreshFromModel();
    void tick();

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    struct Knob
    {
        std::unique_ptr<juce::Slider> slider;
        juce::Label label;
    };

    void addKnob (Knob& k, std::unique_ptr<juce::Slider> slider, const juce::String& name);

    AppController& controller;
    LearnableSlider masterVolume;
    LevelMeter meterL;
    juce::Label masterLabel, limiterLabel;
    Knob reverbSize, reverbDamping, delayTime, delayFeedback, limiter;
};

} // namespace km
