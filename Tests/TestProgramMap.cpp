#include "Model/ProgramMap.h"

#include <catch2/catch_test_macros.hpp>

using namespace km;

TEST_CASE ("General MIDI families map to sensible built-in presets", "[programmap]")
{
    REQUIRE (ProgramMap::gmFamilyToBuiltInPreset (0) == 0);   // Acoustic Grand -> E-Piano
    REQUIRE (ProgramMap::gmFamilyToBuiltInPreset (16) == 2);  // Drawbar Organ
    REQUIRE (ProgramMap::gmFamilyToBuiltInPreset (33) == 3);  // Fingered Bass
    REQUIRE (ProgramMap::gmFamilyToBuiltInPreset (48) == 4);  // String Ensemble
    REQUIRE (ProgramMap::gmFamilyToBuiltInPreset (61) == 5);  // Brass Section
    REQUIRE (ProgramMap::gmFamilyToBuiltInPreset (89) == 1);  // Warm Pad
    REQUIRE (ProgramMap::gmFamilyToBuiltInPreset (127) == 1);
}

TEST_CASE ("Program map entries round-trip through JSON", "[programmap]")
{
    ProgramMap map;
    map.set (ProgramMap::bankNumber (1, 0), 5, InstrumentKey::builtIn (2, "Drawbar Organ"));
    map.set (0, 0, InstrumentKey { InstrumentKey::Type::sfz, "C:/Sounds/Piano.sfz", "Piano", {} });

    ProgramMap loaded;
    REQUIRE (loaded.fromJson (map.toJson()));
    REQUIRE (loaded.size() == 2);

    auto* organ = loaded.find (ProgramMap::bankNumber (1, 0), 5);
    REQUIRE (organ != nullptr);
    REQUIRE (organ->ref == "2");
    REQUIRE (loaded.find (0, 0)->type == InstrumentKey::Type::sfz);
    REQUIRE (loaded.find (0, 1) == nullptr);
}

TEST_CASE ("Bank select MSB/LSB combine into one bank number", "[programmap]")
{
    REQUIRE (ProgramMap::bankNumber (0, 0) == 0);
    REQUIRE (ProgramMap::bankNumber (1, 0) == 128);
    REQUIRE (ProgramMap::bankNumber (1, 3) == 131);
}
