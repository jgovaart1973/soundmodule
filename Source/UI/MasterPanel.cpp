#include "UI/MasterPanel.h"

#include "UI/KmLookAndFeel.h"

namespace km
{
MasterPanel::MasterPanel (AppController& c)
    : controller (c), masterVolume (c.getMidiLearn(), "master.volume")
{
    auto& m = controller.masterParams();

    addAndMakeVisible (masterLabel);
    masterLabel.setText ("MASTER", juce::dontSendNotification);
    masterLabel.setJustificationType (juce::Justification::centred);
    masterLabel.setFont (juce::FontOptions (16.0f, juce::Font::bold));

    addAndMakeVisible (masterVolume);
    masterVolume.setSliderStyle (juce::Slider::LinearVertical);
    masterVolume.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 20);
    masterVolume.setRange (-60.0, 6.0, 0.1);
    masterVolume.setSkewFactorFromMidPoint (-12.0);
    masterVolume.setDoubleClickReturnValue (true, 0.0);
    masterVolume.textFromValueFunction = [] (double v) { return v <= -59.9 ? juce::String ("-inf") : juce::String (v, 1) + " dB"; };
    masterVolume.updateText();
    masterVolume.onValueChange = [this, &m]
    {
        m.volumeDb = masterVolume.getValue() <= -59.9 ? -80.0f : (float) masterVolume.getValue();
        controller.markDirty();
    };

    addAndMakeVisible (meterL);

    auto knob = [&] (const juce::String& paramId) -> std::unique_ptr<juce::Slider>
    {
        if (paramId.isNotEmpty())
            return std::make_unique<LearnableSlider> (controller.getMidiLearn(), paramId);
        return std::make_unique<juce::Slider>();
    };

    auto percent = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };

    addKnob (reverbSize, knob ("fx.reverbSize"), "Galm ruimte");
    reverbSize.slider->textFromValueFunction = percent;
    reverbSize.slider->setRange (0.0, 1.0);
    reverbSize.slider->setDoubleClickReturnValue (true, 0.55);
    reverbSize.slider->onValueChange = [this, &m] { m.reverbSize = (float) reverbSize.slider->getValue(); controller.markDirty(); };

    addKnob (reverbDamping, knob ({}), "Galm demping");
    reverbDamping.slider->textFromValueFunction = percent;
    reverbDamping.slider->setRange (0.0, 1.0);
    reverbDamping.slider->setDoubleClickReturnValue (true, 0.4);
    reverbDamping.slider->onValueChange = [this, &m] { m.reverbDamping = (float) reverbDamping.slider->getValue(); controller.markDirty(); };

    addKnob (delayTime, knob ({}), "Delay tijd");
    delayTime.slider->setRange (20.0, 1500.0, 1.0);
    delayTime.slider->setSkewFactorFromMidPoint (400.0);
    delayTime.slider->textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + " ms"; };
    delayTime.slider->setDoubleClickReturnValue (true, 375.0);
    delayTime.slider->onValueChange = [this, &m] { m.delayMs = (float) delayTime.slider->getValue(); controller.markDirty(); };

    addKnob (delayFeedback, knob ("fx.delayFeedback"), "Herhalingen");
    delayFeedback.slider->textFromValueFunction = percent;
    delayFeedback.slider->setRange (0.0, 0.95);
    delayFeedback.slider->setDoubleClickReturnValue (true, 0.35);
    delayFeedback.slider->onValueChange = [this, &m] { m.delayFeedback = (float) delayFeedback.slider->getValue(); controller.markDirty(); };

    addKnob (limiter, knob ({}), "Limiter");
    limiter.slider->setRange (-12.0, 0.0, 0.1);
    limiter.slider->textFromValueFunction = [] (double v) { return juce::String (v, 1) + " dB"; };
    limiter.slider->setDoubleClickReturnValue (true, -1.0);
    limiter.slider->onValueChange = [this, &m] { m.limiterThresholdDb = (float) limiter.slider->getValue(); controller.markDirty(); };

    for (auto* k : { &reverbSize, &reverbDamping, &delayTime, &delayFeedback, &limiter })
        k->slider->updateText();

    addAndMakeVisible (limiterLabel);
    limiterLabel.setJustificationType (juce::Justification::centred);
    limiterLabel.setColour (juce::Label::textColourId, colours::textDim);

    refreshFromModel();
}

void MasterPanel::addKnob (Knob& k, std::unique_ptr<juce::Slider> slider, const juce::String& name)
{
    k.slider = std::move (slider);
    k.slider->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);
    addAndMakeVisible (*k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (13.0f));
    k.label.setColour (juce::Label::textColourId, colours::textDim);
    addAndMakeVisible (k.label);
}

void MasterPanel::refreshFromModel()
{
    auto& m = controller.masterParams();
    masterVolume.setValue (m.volumeDb.load() < -59.9f ? -60.0 : m.volumeDb.load(), juce::dontSendNotification);
    reverbSize.slider->setValue (m.reverbSize.load(), juce::dontSendNotification);
    reverbDamping.slider->setValue (m.reverbDamping.load(), juce::dontSendNotification);
    delayTime.slider->setValue (m.delayMs.load(), juce::dontSendNotification);
    delayFeedback.slider->setValue (m.delayFeedback.load(), juce::dontSendNotification);
    limiter.slider->setValue (m.limiterThresholdDb.load(), juce::dontSendNotification);
}

void MasterPanel::tick()
{
    auto& engine = controller.getEngine();
    meterL.setLevel (engine.getMasterPeakAndReset());

    const float gr = engine.getLimiterReductionDb();
    limiterLabel.setText (gr < -0.1f ? "Begrenzing " + juce::String (gr, 1) + " dB" : juce::String ("Geen begrenzing"),
                          juce::dontSendNotification);
    limiterLabel.setColour (juce::Label::textColourId, gr < -3.0f ? colours::warn : colours::textDim);

    auto& m = controller.masterParams();
    auto sync = [] (juce::Slider& s, double v)
    {
        if (s.getThumbBeingDragged() < 0 && std::abs (s.getValue() - v) > 1.0e-3)
            s.setValue (v, juce::dontSendNotification);
    };
    sync (masterVolume, m.volumeDb.load() < -59.9f ? -60.0 : m.volumeDb.load());
    sync (*reverbSize.slider, m.reverbSize.load());
    sync (*delayFeedback.slider, m.delayFeedback.load());

    masterVolume.refreshLearnState();
    for (auto* k : { &reverbSize, &delayFeedback })
        if (auto* ls = dynamic_cast<LearnableSlider*> (k->slider.get()))
            ls->refreshLearnState();
}

void MasterPanel::resized()
{
    auto area = getLocalBounds().reduced (10);
    masterLabel.setBounds (area.removeFromTop (24));

    auto top = area.removeFromTop (juce::jmin (300, area.getHeight() / 2));
    meterL.setBounds (top.removeFromRight (36).reduced (4, 10));
    masterVolume.setBounds (top);

    area.removeFromTop (8);
    auto row = [&area] (int h) { auto r = area.removeFromTop (h); area.removeFromTop (4); return r; };

    auto placeKnob = [] (Knob& k, juce::Rectangle<int> cell)
    {
        k.label.setBounds (cell.removeFromTop (16));
        k.slider->setBounds (cell);
    };

    auto r1 = row (96);
    placeKnob (reverbSize, r1.removeFromLeft (r1.getWidth() / 2));
    placeKnob (reverbDamping, r1);
    auto r2 = row (96);
    placeKnob (delayTime, r2.removeFromLeft (r2.getWidth() / 2));
    placeKnob (delayFeedback, r2);
    auto r3 = row (96);
    placeKnob (limiter, r3.removeFromLeft (r3.getWidth() / 2));
    limiterLabel.setBounds (r3);
}

void MasterPanel::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRect (getLocalBounds());
    g.setColour (colours::outline);
    g.drawVerticalLine (0, 0.0f, (float) getHeight());
}

} // namespace km
