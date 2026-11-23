#include "UI/SettingsComponent.h"

#include "Core/AppPaths.h"
#include "UI/KmLookAndFeel.h"
#include "UI/PartRow.h"

namespace km
{
namespace
{
    juce::Label* makeInfo (juce::Component& parent, juce::OwnedArray<juce::Label>& store, const juce::String& text)
    {
        auto* l = store.add (new juce::Label ({}, text));
        l->setColour (juce::Label::textColourId, colours::textDim);
        l->setJustificationType (juce::Justification::topLeft);
        l->setMinimumHorizontalScale (1.0f);
        parent.addAndMakeVisible (l);
        return l;
    }
} // namespace

//==============================================================================
class SettingsComponent::AudioPage final : public juce::Component
{
public:
    explicit AudioPage (AppController& c)
        : selector (c.getDeviceManager(), 0, 2, 2, 2, false, false, true, false)
    {
        addAndMakeVisible (selector);
        addAndMakeVisible (testTone);
        testTone.onClick = [&c] { c.playTestTone(); };
        info = makeInfo (*this, labels,
            "Tip: kies ASIO als je audio-interface een ASIO-driver heeft (laagste vertraging). Zonder ASIO: "
            "'Windows Audio (Exclusive Mode)'. Een buffer van 128 samples bij 48 kHz geeft ongeveer 3 ms vertraging.");
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        auto bottom = r.removeFromBottom (70);
        testTone.setBounds (bottom.removeFromLeft (140).removeFromTop (32));
        bottom.removeFromLeft (12);
        info->setBounds (bottom);
        selector.setBounds (r);
    }

private:
    juce::AudioDeviceSelectorComponent selector;
    juce::TextButton testTone { "Testtoon" };
    juce::OwnedArray<juce::Label> labels;
    juce::Label* info = nullptr;
};

//==============================================================================
class SettingsComponent::MidiPage final : public juce::Component, private juce::Timer
{
public:
    explicit MidiPage (AppController& c) : controller (c)
    {
        addAndMakeVisible (devicesTitle);
        devicesTitle.setText ("MIDI-ingangen", juce::dontSendNotification);
        devicesTitle.setFont (juce::FontOptions (16.0f, juce::Font::bold));

        addAndMakeVisible (controlLabel);
        controlLabel.setText ("Performance kiezen via Program Change op kanaal:", juce::dontSendNotification);
        addAndMakeVisible (controlChannel);
        controlChannel.addItem ("Uit", 1);
        for (int ch = 1; ch <= 16; ++ch)
            controlChannel.addItem ("Kanaal " + juce::String (ch), ch + 1);
        controlChannel.setSelectedId (controller.getControlChannel() + 1, juce::dontSendNotification);
        controlChannel.onChange = [this] { controller.setControlChannel (controlChannel.getSelectedId() - 1); };

        addAndMakeVisible (gmMode);
        gmMode.setButtonText ("General MIDI-modus (Program Change kiest een ingebouwde klank per GM-familie)");
        gmMode.setToggleState (controller.isGeneralMidiMode(), juce::dontSendNotification);
        gmMode.onClick = [this] { controller.setGeneralMidiMode (gmMode.getToggleState()); };

        addAndMakeVisible (learnTitle);
        learnTitle.setText ("MIDI Learn", juce::dontSendNotification);
        learnTitle.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        learnInfo = makeInfo (*this, labels,
            "Rechtsklik op een schuif of knop en kies 'MIDI Learn', beweeg daarna een knop op je keyboard. "
            "Volgende/vorige performance en Panic koppel je hieronder aan een knop of voetschakelaar.");

        for (auto [id, text] : { std::pair<const char*, const char*> { "action.previousPerformance", "Vorige performance" },
                                 { "action.nextPerformance", "Volgende performance" },
                                 { "action.panic", "Panic" } })
        {
            auto* b = actionButtons.add (new juce::TextButton (juce::String (text) + ": leren"));
            const juce::String paramId (id);
            b->onClick = [this, paramId] { controller.getMidiLearn().arm (paramId); };
            addAndMakeVisible (b);
            actionIds.add (paramId);
        }

        refresh();
        startTimerHz (15);
    }

    void refresh()
    {
        rows.clear();
        for (auto& d : controller.getMidiRouter().getDevices())
        {
            auto* row = rows.add (new Row());
            row->identifier = d.identifier;
            row->slot = d.slot;
            row->toggle.setButtonText (d.name);
            row->toggle.setToggleState (d.enabled, juce::dontSendNotification);
            const auto id = d.identifier;
            row->toggle.onClick = [this, id, t = &row->toggle] { controller.getMidiRouter().setDeviceEnabled (id, t->getToggleState()); };
            addAndMakeVisible (row->toggle);
            addAndMakeVisible (row->lamp);
        }
        if (rows.isEmpty() && noDevices == nullptr)
            noDevices = makeInfo (*this, labels, "Geen MIDI-apparaten gevonden. Sluit je keyboard aan (USB) - het verschijnt hier vanzelf.");
        else if (noDevices != nullptr)
            noDevices->setVisible (false);
        resized();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        devicesTitle.setBounds (r.removeFromTop (26));
        if (rows.isEmpty() && noDevices != nullptr)
        {
            noDevices->setVisible (true);
            noDevices->setBounds (r.removeFromTop (28));
        }
        for (auto* row : rows)
        {
            auto line = r.removeFromTop (28);
            row->lamp.setBounds (line.removeFromLeft (20));
            row->toggle.setBounds (line);
        }
        r.removeFromTop (16);
        auto cc = r.removeFromTop (28);
        controlLabel.setBounds (cc.removeFromLeft (360));
        controlChannel.setBounds (cc.removeFromLeft (140));
        r.removeFromTop (6);
        gmMode.setBounds (r.removeFromTop (28));
        r.removeFromTop (16);
        learnTitle.setBounds (r.removeFromTop (26));
        learnInfo->setBounds (r.removeFromTop (44));
        auto buttons = r.removeFromTop (32);
        for (auto* b : actionButtons)
        {
            b->setBounds (buttons.removeFromLeft (230));
            buttons.removeFromLeft (8);
        }
    }

private:
    struct Row
    {
        juce::String identifier;
        int slot = -1;
        juce::ToggleButton toggle;
        ActivityLamp lamp;
    };

    void timerCallback() override
    {
        for (auto* row : rows)
            row->lamp.update (controller.getMidiRouter().getActivity (row->slot));

        auto& learn = controller.getMidiLearn();
        for (int i = 0; i < actionButtons.size(); ++i)
        {
            const auto& id = actionIds[i];
            const auto name = learn.getDisplayName (id);
            auto* b = actionButtons[i];
            if (learn.getArmedParameter() == id)  b->setButtonText (name + ": beweeg nu een knop...");
            else if (learn.hasMapping (id))       b->setButtonText (name + ": " + learn.describeMapping (id));
            else                                  b->setButtonText (name + ": leren");
        }
    }

    AppController& controller;
    juce::Label devicesTitle, controlLabel, learnTitle;
    juce::ComboBox controlChannel;
    juce::ToggleButton gmMode;
    juce::OwnedArray<Row> rows;
    juce::OwnedArray<juce::Label> labels;
    juce::OwnedArray<juce::TextButton> actionButtons;
    juce::StringArray actionIds;
    juce::Label* learnInfo = nullptr;
    juce::Label* noDevices = nullptr;
};

//==============================================================================
class SettingsComponent::SoundsPage final : public juce::Component
{
public:
    explicit SoundsPage (AppController& c) : controller (c)
    {
        addAndMakeVisible (openFolder);
        openFolder.onClick = [] { paths::soundsDir().startAsProcess(); };
        addAndMakeVisible (openPerformances);
        openPerformances.onClick = [] { paths::performancesDir().startAsProcess(); };

        addAndMakeVisible (preload);
        preload.setButtonText ("Klanken van alle opgeslagen performances vooraf laden (wisselen binnen milliseconden, kost geheugen)");
        preload.setToggleState (controller.isPreloadAll(), juce::dontSendNotification);
        preload.onClick = [this] { controller.setPreloadAll (preload.getToggleState()); };

        info = makeInfo (*this, labels,
            "Klankbibliotheken (SFZ) zet je in: " + paths::soundsDir().getFullPathName() + "\n"
            "Gratis bibliotheken downloaden: voer Scripts\\download_sounds.ps1 uit (zie README).\n"
            "Performances staan in: " + paths::performancesDir().getFullPathName() + "\n"
            "De nummering in de lijst (0, 1, 2, ...) is het Program Change-nummer om ze via MIDI te kiezen.");
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        auto buttons = r.removeFromTop (32);
        openFolder.setBounds (buttons.removeFromLeft (220));
        buttons.removeFromLeft (8);
        openPerformances.setBounds (buttons.removeFromLeft (220));
        r.removeFromTop (12);
        preload.setBounds (r.removeFromTop (28));
        r.removeFromTop (12);
        info->setBounds (r.removeFromTop (100));
    }

private:
    AppController& controller;
    juce::TextButton openFolder { "Map met klanken openen" }, openPerformances { "Map met performances openen" };
    juce::ToggleButton preload;
    juce::OwnedArray<juce::Label> labels;
    juce::Label* info = nullptr;
};

//==============================================================================
SettingsComponent::SettingsComponent (AppController& c) : controller (c)
{
    const auto bg = colours::panel;
    tabs.addTab ("Audio", bg, new AudioPage (controller), true);
    midiPage = new MidiPage (controller);
    tabs.addTab ("MIDI", bg, midiPage, true);
    tabs.addTab ("Plug-ins", bg,
                 new juce::PluginListComponent (controller.getFormatManager(), controller.getKnownPlugins(),
                                                paths::deadMansPedalFile(), &controller.getSettings(), true),
                 true);
    tabs.addTab ("Klanken", bg, new SoundsPage (controller), true);
    tabs.setTabBarDepth (34);
    addAndMakeVisible (tabs);
    setSize (760, 560);
}

SettingsComponent::~SettingsComponent()
{
    if (auto xml = controller.getKnownPlugins().createXml())
        xml->writeTo (paths::pluginListFile());
}

void SettingsComponent::refreshMidi()
{
    if (midiPage != nullptr)
        midiPage->refresh();
}

void SettingsComponent::resized()
{
    tabs.setBounds (getLocalBounds());
}

} // namespace km
