#include "UI/TopBar.h"

#include "UI/KmLookAndFeel.h"

namespace km
{
TopBar::TopBar (AppController& c) : controller (c)
{
    for (auto* b : { &prevButton, &nextButton, &newButton, &saveButton, &saveAsButton, &panicButton, &settingsButton, &zoomOut, &zoomIn })
        addAndMakeVisible (*b);

    prevButton.setTooltip ("Vorige performance (Page Up)");
    nextButton.setTooltip ("Volgende performance (Page Down)");
    prevButton.onClick = [this] { controller.previousPerformance(); };
    nextButton.onClick = [this] { controller.nextPerformance(); };
    newButton.onClick = [this] { controller.newPerformance(); };
    saveButton.setTooltip ("Opslaan (Ctrl+S)");
    saveButton.onClick = [this]
    {
        if (! controller.savePerformance() && onSaveAs)
            onSaveAs();
    };
    saveAsButton.onClick = [this] { if (onSaveAs) onSaveAs(); };

    panicButton.setTooltip ("Alle noten uit (Esc)");
    panicButton.setColour (juce::TextButton::buttonColourId, colours::danger.darker (0.3f));
    panicButton.onClick = [this] { controller.panic(); };

    settingsButton.onClick = [this] { if (onSettings) onSettings(); };

    for (auto* b : { &moduleView, &arrangerView })
    {
        addAndMakeVisible (*b);
        b->setRadioGroupId (101);
        b->setClickingTogglesState (true);
    }
    moduleView.setToggleState (true, juce::dontSendNotification);
    moduleView.setTooltip ("Klankmodule: parts, klanken en mixer (F1)");
    arrangerView.setTooltip ("Opnemen en arrangeren (F2)");
    moduleView.onClick = [this] { if (moduleView.getToggleState() && onView) onView (false); };
    arrangerView.onClick = [this] { if (arrangerView.getToggleState() && onView) onView (true); };
    zoomOut.onClick = [this] { if (onZoom) onZoom (-0.1f); };
    zoomIn.onClick = [this] { if (onZoom) onZoom (0.1f); };

    addAndMakeVisible (performanceBox);
    performanceBox.setTextWhenNothingSelected ("(niet opgeslagen)");
    performanceBox.onChange = [this]
    {
        const int idx = performanceBox.getSelectedItemIndex();
        if (idx >= 0 && idx != controller.getCurrentPerformanceIndex())
            controller.selectPerformance (idx);
    };

    addAndMakeVisible (dirtyLabel);
    dirtyLabel.setColour (juce::Label::textColourId, colours::warn);
    dirtyLabel.setTooltip ("Niet-opgeslagen wijzigingen (worden wel elke 2 minuten automatisch bewaard)");

    addAndMakeVisible (cpuLabel);
    cpuLabel.setJustificationType (juce::Justification::centredRight);
    cpuLabel.setColour (juce::Label::textColourId, colours::textDim);

    refreshFromModel();
    refreshDevices();
}

void TopBar::refreshFromModel()
{
    performanceBox.clear (juce::dontSendNotification);
    const auto files = controller.getPerformanceFiles();
    for (int i = 0; i < files.size(); ++i)
        performanceBox.addItem (juce::String (i) + "  " + files[i].getFileNameWithoutExtension(), i + 1);

    const int current = controller.getCurrentPerformanceIndex();
    if (current >= 0)
        performanceBox.setSelectedItemIndex (current, juce::dontSendNotification);
    else
        performanceBox.setText (controller.getPerformance().name + " (niet opgeslagen)", juce::dontSendNotification);
}

void TopBar::refreshDevices()
{
    for (auto& d : deviceLamps)
    {
        removeChildComponent (d.lamp.get());
        removeChildComponent (d.name.get());
    }
    deviceLamps.clear();

    for (auto& dev : controller.getMidiRouter().getDevices())
    {
        if (! dev.enabled)
            continue;
        DeviceLamp d;
        d.slot = dev.slot;
        d.lamp = std::make_unique<ActivityLamp>();
        d.name = std::make_unique<juce::Label> (juce::String(), dev.name);
        d.name->setFont (juce::FontOptions (12.0f));
        d.name->setColour (juce::Label::textColourId, colours::textDim);
        d.name->setTooltip (dev.name);
        addAndMakeVisible (*d.lamp);
        addAndMakeVisible (*d.name);
        deviceLamps.push_back (std::move (d));
    }
    resized();
}

void TopBar::tick()
{
    for (auto& d : deviceLamps)
        d.lamp->update (controller.getMidiRouter().getActivity (d.slot));

    dirtyLabel.setText (controller.isDirty() ? juce::String::fromUTF8 ("\xe2\x97\x8f gewijzigd") : juce::String(),
                        juce::dontSendNotification);

    auto& dm = controller.getDeviceManager();
    juce::String status;
    if (auto* dev = dm.getCurrentAudioDevice(); dev != nullptr && dev->isPlaying())
    {
        const double sr = dev->getCurrentSampleRate();
        const double latencyMs = 1000.0 * (dev->getCurrentBufferSizeSamples() + dev->getOutputLatencyInSamples()) / sr;
        status << dev->getTypeName() << "  " << juce::String (sr / 1000.0, 1) << " kHz  "
               << juce::String (latencyMs, 1) << " ms  CPU " << juce::roundToInt (dm.getCpuUsage() * 100.0) << "%";
        const int xruns = controller.getXRunCount();
        if (xruns > 0)
            status << "  haperingen: " << xruns;
        cpuLabel.setColour (juce::Label::textColourId, dm.getCpuUsage() > 0.8 || xruns > 0 ? colours::warn : colours::textDim);
    }
    else
    {
        status = "Geen audio - kies een uitgang in Instellingen";
        cpuLabel.setColour (juce::Label::textColourId, colours::danger);
    }
    cpuLabel.setText (status, juce::dontSendNotification);
}

void TopBar::setView (bool arranger)
{
    (arranger ? arrangerView : moduleView).setToggleState (true, juce::dontSendNotification);
}

void TopBar::resized()
{
    auto area = getLocalBounds().reduced (8, 8);
    auto place = [&area] (juce::Component& c, int w) { c.setBounds (area.removeFromLeft (w)); area.removeFromLeft (6); };

    moduleView.setBounds (area.removeFromLeft (80));
    arrangerView.setBounds (area.removeFromLeft (86));
    area.removeFromLeft (14);

    place (prevButton, 36);
    place (performanceBox, 260);
    place (nextButton, 36);
    place (dirtyLabel, 90);
    place (newButton, 70);
    place (saveButton, 80);
    place (saveAsButton, 110);
    area.removeFromLeft (10);
    place (panicButton, 80);
    area.removeFromLeft (10);

    auto right = area.removeFromRight (40);
    zoomIn.setBounds (right);
    area.removeFromRight (4);
    zoomOut.setBounds (area.removeFromRight (40));
    area.removeFromRight (6);
    settingsButton.setBounds (area.removeFromRight (110));
    area.removeFromRight (10);
    cpuLabel.setBounds (area.removeFromRight (juce::jmin (380, area.getWidth() / 2)));

    // MIDI device lamps fill what is left.
    for (auto& d : deviceLamps)
    {
        if (area.getWidth() < 40)
        {
            d.lamp->setBounds ({});
            d.name->setBounds ({});
            continue;
        }
        d.lamp->setBounds (area.removeFromLeft (14));
        d.name->setBounds (area.removeFromLeft (juce::jmin (130, area.getWidth())));
        area.removeFromLeft (6);
    }
}

void TopBar::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRect (getLocalBounds());
    g.setColour (colours::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

} // namespace km
