#pragma once

#include "Midi/MidiEvent.h"
#include "Midi/MidiLearn.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <map>

namespace km
{
/**
    Opens every enabled MIDI input, follows hot-plugging and feeds the engine's queue.

    Each device gets a stable "slot" number for the session, so parts can listen to one
    specific keyboard. On Windows 11 the inputs go through Windows MIDI Services
    automatically, which allows other apps to use the same keyboard at the same time.
*/
class MidiRouter
{
public:
    struct DeviceInfo
    {
        juce::String identifier, name;
        int slot = -1;
        bool enabled = true;
        bool connected = false;
    };

    MidiRouter (MidiEventQueue& queue, MidiLearn& learn);
    ~MidiRouter();

    void start();
    void stop();

    juce::Array<DeviceInfo> getDevices() const;

    /** Slot for a device identifier: >= 0 when the device has been seen this session, else -2. */
    int getSlotFor (const juce::String& identifier) const;

    void setDeviceEnabled (const juce::String& identifier, bool enabled);
    juce::StringArray getDisabledDevices() const { return disabledIds; }
    void setDisabledDevices (const juce::StringArray& ids);

    uint32_t getActivity (int slot) const noexcept
    {
        return slot >= 0 && slot < kMaxMidiSources ? activity[(size_t) slot].load (std::memory_order_relaxed) : 0;
    }

    /** Message thread: devices were plugged in / removed / enabled. */
    std::function<void()> onDevicesChanged;

    /** Message thread: an open input disappeared (unplugged). Notes from it can never be released. */
    std::function<void()> onDeviceRemoved;

private:
    struct Input final : public juce::MidiInputCallback
    {
        Input (MidiRouter& r, int s) : router (r), slot (s) {}
        void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

        MidiRouter& router;
        const int slot;
        std::unique_ptr<juce::MidiInput> device;
    };

    void refresh();
    int slotFor (const juce::String& identifier);

    MidiEventQueue& queue;
    MidiLearn& learn;

    std::vector<std::unique_ptr<Input>> inputs;
    std::map<juce::String, int> slots;
    std::map<juce::String, juce::String> names;
    juce::StringArray disabledIds;
    juce::Array<juce::MidiDeviceInfo> available;
    std::array<std::atomic<uint32_t>, kMaxMidiSources> activity {};
    juce::MidiDeviceListConnection connection;
    bool running = false;

    JUCE_DECLARE_NON_COPYABLE (MidiRouter)
};

} // namespace km
