#include "App/AppController.h"
#include "Core/AppPaths.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>

using namespace km;

namespace
{
    /** Points all app data at a fresh temp folder so tests never touch the user's real settings. */
    struct TempHome
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("km_home_" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        TempHome()
        {
            dir.createDirectory();
           #if JUCE_WINDOWS
            _putenv_s ("KLANKMODULE_HOME", dir.getFullPathName().toRawUTF8());
           #else
            setenv ("KLANKMODULE_HOME", dir.getFullPathName().toRawUTF8(), 1);
           #endif
        }
        ~TempHome()
        {
           #if JUCE_WINDOWS
            _putenv_s ("KLANKMODULE_HOME", "");
           #else
            unsetenv ("KLANKMODULE_HOME");
           #endif
            dir.deleteRecursively();
        }
    };
} // namespace

TEST_CASE ("App starts with a playable default, saves and restores the session", "[app]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TempHome home;
    REQUIRE (paths::overrideRoot() == home.dir);

    {
        AppController app;
        app.initialise();

        // Default: part 1 plays the built-in electric piano.
        REQUIRE (app.getPerformance().parts[0].enabled);
        REQUIRE (app.getAssignedInstrument (0) != nullptr);
        REQUIRE (app.getAssignedInstrument (0)->getName() == "Electric Piano");

        // Build a split: bass left, organ right on its own channel.
        app.setPartKeyRange (0, 60, 127);
        app.setPartEnabled (1, true);
        app.setPartInstrument (1, InstrumentKey::builtIn (3, "Synth Bass"));
        app.setPartKeyRange (1, 0, 59);
        app.setPartChannel (1, 2);
        app.partParams (1).volumeDb = -6.0f; // realtime value, captured on save
        REQUIRE (app.isDirty());

        const auto file = paths::performancesDir().getChildFile ("Split.kmperf");
        REQUIRE (app.savePerformanceAs (file, "Split"));
        REQUIRE_FALSE (app.isDirty());
        REQUIRE (app.getPerformanceFiles().size() == 1);

        // Unsaved change after saving: must survive a restart through the autosave.
        app.setPartTranspose (1, -12);
        app.shutdown();
    }

    {
        AppController app;
        app.initialise();
        const auto& p = app.getPerformance();
        REQUIRE (p.name == "Split");
        REQUIRE (p.parts[1].enabled);
        REQUIRE (p.parts[1].keyHigh == 59);
        REQUIRE (p.parts[1].channel == 2);
        REQUIRE (p.parts[1].transpose == -12);       // from the autosave
        REQUIRE (p.parts[1].volumeDb == -6.0f);
        REQUIRE (app.getAssignedInstrument (1) != nullptr);
        REQUIRE (app.getAssignedInstrument (1)->getName() == "Synth Bass");
        REQUIRE (app.getCurrentFile().getFileName() == "Split.kmperf");
        app.shutdown();
    }
}

TEST_CASE ("Switching between saved performances keeps instruments that stay the same", "[app]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TempHome home;

    AppController app;
    app.initialise();
    auto* piano = app.getAssignedInstrument (0);
    REQUIRE (app.savePerformanceAs (paths::performancesDir().getChildFile ("0 Piano.kmperf"), "Piano"));

    app.setPartEnabled (1, true);
    app.setPartInstrument (1, InstrumentKey::builtIn (1, "Warm Pad"));
    REQUIRE (app.savePerformanceAs (paths::performancesDir().getChildFile ("1 Piano+Pad.kmperf"), "Piano+Pad"));

    app.selectPerformance (0);
    REQUIRE (app.getPerformance().name == "Piano");
    REQUIRE (app.getAssignedInstrument (0) == piano); // same sound: not reloaded, held notes continue
    REQUIRE (app.getAssignedInstrument (1) == nullptr);

    app.nextPerformance();
    REQUIRE (app.getPerformance().name == "Piano+Pad");
    REQUIRE (app.getAssignedInstrument (0) == piano);
    REQUIRE (app.getAssignedInstrument (1) != nullptr);
    app.shutdown();
}
