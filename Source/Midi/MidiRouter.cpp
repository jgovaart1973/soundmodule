#include "Midi/MidiRouter.h"

namespace km
{
MidiRouter::MidiRouter (MidiEventQueue& q, MidiLearn& l) : queue (q), learn (l) {}

MidiRouter::~MidiRouter()
{
    stop();
}

void MidiRouter::start()
{
    JUCE_ASSERT_MESSAGE_THREAD
    running = true;
    connection = juce::MidiDeviceListConnection::make ([this] { refresh(); });
    refresh();
}

void MidiRouter::stop()
{
    running = false;
    connection = {};
    for (auto& in : inputs)
        if (in->device != nullptr)
            in->device->stop();
    inputs.clear();
}

int MidiRouter::slotFor (const juce::String& identifier)
{
    if (auto it = slots.find (identifier); it != slots.end())
        return it->second;

    const int next = (int) slots.size();
    if (next >= kMaxMidiSources)
        return -2;
    slots[identifier] = next;
    return next;
}

int MidiRouter::getSlotFor (const juce::String& identifier) const
{
    auto it = slots.find (identifier);
    return it != slots.end() ? it->second : -2;
}

void MidiRouter::refresh()
{
    if (! running)
        return;

    available = juce::MidiInput::getAvailableDevices();
    bool removedUnplugged = false;

    // Close inputs that disappeared or were disabled.
    inputs.erase (std::remove_if (inputs.begin(), inputs.end(), [&] (const std::unique_ptr<Input>& in)
    {
        const auto id = in->device != nullptr ? in->device->getIdentifier() : juce::String();
        bool stillThere = false;
        for (auto& d : available)
            stillThere = stillThere || d.identifier == id;

        if (stillThere && ! disabledIds.contains (id))
            return false;

        removedUnplugged = removedUnplugged || ! stillThere;
        if (in->device != nullptr)
            in->device->stop();
        return true;
    }), inputs.end());

    // Open new ones.
    for (auto& d : available)
    {
        names[d.identifier] = d.name;
        if (disabledIds.contains (d.identifier))
            continue;

        bool alreadyOpen = false;
        for (auto& in : inputs)
            alreadyOpen = alreadyOpen || (in->device != nullptr && in->device->getIdentifier() == d.identifier);
        if (alreadyOpen)
            continue;

        const int slot = slotFor (d.identifier);
        if (slot < 0)
            continue;

        auto input = std::make_unique<Input> (*this, slot);
        input->device = juce::MidiInput::openDevice (d.identifier, input.get());
        if (input->device == nullptr)
        {
            DBG ("Could not open MIDI input " << d.name);
            continue;
        }
        input->device->start();
        inputs.push_back (std::move (input));
    }

    if (removedUnplugged && onDeviceRemoved)
        onDeviceRemoved();
    if (onDevicesChanged)
        onDevicesChanged();
}

juce::Array<MidiRouter::DeviceInfo> MidiRouter::getDevices() const
{
    juce::Array<DeviceInfo> result; // from the cached list: cheap enough to call per UI row

    for (auto& d : available)
    {
        DeviceInfo info;
        info.identifier = d.identifier;
        info.name = d.name;
        info.slot = getSlotFor (d.identifier);
        info.enabled = ! disabledIds.contains (d.identifier);
        info.connected = true;
        result.add (info);
    }
    return result;
}

void MidiRouter::setDeviceEnabled (const juce::String& identifier, bool enabled)
{
    if (enabled) disabledIds.removeString (identifier);
    else         disabledIds.addIfNotAlreadyThere (identifier);
    refresh();
}

void MidiRouter::setDisabledDevices (const juce::StringArray& ids)
{
    disabledIds = ids;
    refresh();
}

//==============================================================================
void MidiRouter::Input::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    const auto* raw = m.getRawData();
    const int size = m.getRawDataSize();

    // Only channel messages (notes, CC, program, pressure, bend). Clock/SysEx come in phase 3.
    if (size < 1 || size > 3 || raw[0] < 0x80 || raw[0] >= 0xf0)
        return;

    const auto e = MidiEvent::make (slot, raw[0], size > 1 ? raw[1] : 0, size > 2 ? raw[2] : 0, (uint8_t) size);
    router.activity[(size_t) slot].fetch_add (1, std::memory_order_relaxed);

    if (router.learn.handle (e))
        return;

    router.queue.push (e);
}

} // namespace km
