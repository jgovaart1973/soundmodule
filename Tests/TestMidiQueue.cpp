#include "Midi/MidiEvent.h"

#include <catch2/catch_test_macros.hpp>
#include <thread>

using namespace km;

TEST_CASE ("SPSC queue keeps order and reports full", "[queue]")
{
    SpscQueue<int, 8> q;
    for (int i = 0; i < 8; ++i)
        REQUIRE (q.push (i));
    REQUIRE_FALSE (q.push (99)); // full: dropped, never blocks

    for (int i = 0; i < 8; ++i)
    {
        auto v = q.pop();
        REQUIRE (v.has_value());
        REQUIRE (*v == i);
    }
    REQUIRE_FALSE (q.pop().has_value());
}

TEST_CASE ("MIDI event helpers decode status and channel", "[queue]")
{
    auto on = MidiEvent::make (2, 0x93, 60, 100);
    REQUIRE (on.isNoteOn());
    REQUIRE (on.channel() == 4);
    REQUIRE (on.source == 2);

    auto zeroVelOn = MidiEvent::make (0, 0x90, 60, 0);
    REQUIRE (zeroVelOn.isNoteOff());
    REQUIRE_FALSE (zeroVelOn.isNoteOn());

    REQUIRE (MidiEvent::make (0, 0xc5, 10, 0, 2).isProgramChange());
}

TEST_CASE ("MIDI queue accepts events from several producer threads", "[queue]")
{
    MidiEventQueue q;
    constexpr int perThread = 500;

    std::thread a ([&] { for (int i = 0; i < perThread; ++i) q.push (MidiEvent::make (0, 0x90, 60, 100)); });
    std::thread b ([&] { for (int i = 0; i < perThread; ++i) q.push (MidiEvent::make (1, 0x80, 60, 0)); });
    a.join();
    b.join();

    int fromA = 0, fromB = 0;
    while (auto e = q.pop())
        (e->source == 0 ? fromA : fromB)++;

    REQUIRE (fromA == perThread);
    REQUIRE (fromB == perThread);
}
