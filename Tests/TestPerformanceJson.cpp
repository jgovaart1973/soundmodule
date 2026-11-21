#include "Model/Performance.h"

#include <catch2/catch_test_macros.hpp>

using namespace km;

TEST_CASE ("Performance survives a JSON round trip", "[performance]")
{
    Performance p = Performance::makeDefault();
    p.name = "Avondset – nummer 3";
    p.parts[2].enabled = true;
    p.parts[2].channel = 10;
    p.parts[2].inputDeviceId = "\\\\?\\usb#vid_0582";
    p.parts[2].instrument = InstrumentKey::sfz (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Piano.sfz"));
    p.parts[2].keyLow = 36;
    p.parts[2].keyHigh = 59;
    p.parts[2].velocityCurve = filter::VelocityCurve::hard;
    p.parts[2].pan = -0.5f;
    p.master.delayMs = 500.0f;

    Performance q;
    juce::String error;
    REQUIRE (q.fromJson (p.toJson(), error));
    REQUIRE (q.name == p.name);
    REQUIRE (q.parts[2].channel == 10);
    REQUIRE (q.parts[2].inputDeviceId == p.parts[2].inputDeviceId);
    REQUIRE (q.parts[2].instrument == p.parts[2].instrument);
    REQUIRE (q.parts[2].keyLow == 36);
    REQUIRE (q.parts[2].velocityCurve == filter::VelocityCurve::hard);
    REQUIRE (q.parts[2].pan == -0.5f);
    REQUIRE (q.parts[0].instrument.type == InstrumentKey::Type::builtIn);
    REQUIRE (q.master.delayMs == 500.0f);
}

TEST_CASE ("Invalid or foreign JSON is rejected without changing the performance", "[performance]")
{
    Performance p = Performance::makeDefault();
    juce::String error;

    REQUIRE_FALSE (p.fromJson ("{ not json", error));
    REQUIRE (error.isNotEmpty());
    REQUIRE_FALSE (p.fromJson (R"({"format":"SomethingElse"})", error));
    REQUIRE (p.name == "Start");
}

TEST_CASE ("Out-of-range values are clamped and missing fields get defaults", "[performance]")
{
    const auto json = R"({"format":"KlankModule.Performance","version":1,"name":"x",
                         "parts":[{"enabled":true,"channel":99,"keyLow":-5,"volumeDb":40,"pan":7}]})";
    Performance p;
    juce::String error;
    REQUIRE (p.fromJson (json, error));
    REQUIRE (p.parts[0].channel == 16);
    REQUIRE (p.parts[0].keyLow == 0);
    REQUIRE (p.parts[0].keyHigh == 127);
    REQUIRE (p.parts[0].volumeDb == 12.0f);
    REQUIRE (p.parts[0].pan == 1.0f);
    REQUIRE_FALSE (p.parts[1].enabled);
}

TEST_CASE ("Files written by a newer version still load", "[performance]")
{
    const auto json = R"({"format":"KlankModule.Performance","version":99,"name":"toekomst","futureField":1,"parts":[]})";
    Performance p;
    juce::String error;
    REQUIRE (p.fromJson (json, error));
    REQUIRE (p.name == "toekomst");
}

TEST_CASE ("Saving is atomic and loads back", "[performance]")
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("km_test.kmperf");
    Performance p = Performance::makeDefault();
    p.name = "Disk";
    REQUIRE (p.saveTo (file));

    Performance q;
    juce::String error;
    REQUIRE (q.loadFrom (file, error));
    REQUIRE (q.name == "Disk");
    file.deleteFile();
}

TEST_CASE ("Plug-in state is part of the pool key", "[performance]")
{
    InstrumentKey a { InstrumentKey::Type::plugin, "VST3-Synth-123", "Synth", "AAAA" };
    InstrumentKey b = a;
    b.pluginState = "BBBB";
    REQUIRE (a.poolKey() != b.poolKey());
    b.pluginState = "AAAA";
    REQUIRE (a == b);
}
