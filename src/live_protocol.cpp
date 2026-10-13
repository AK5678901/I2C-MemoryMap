#include "live_protocol.hpp"
#include "i2c_bus_manager.hpp"
#include "time_value.hpp"

#include <charconv>
#include <optional>

bool LiveProtocol::process(std::string_view packet, I2CBusManager& bus_manager, bool& reset)
{
    reset = false;
    bool changed = false;
    // B,<UTF-8 byte count>,<bus name>,<legacy frame>. Length permits commas in names.
    std::string_view frame(packet);
    std::string name = "I2C";
    if (frame.starts_with("B,"))
    {
        const auto end = frame.find(',', 2);
        if (end == std::string_view::npos)
            return false;
        std::size_t length = 0;
        const auto parsed = std::from_chars(frame.data() + 2, frame.data() + end, length);
        if (parsed.ec != std::errc{} || parsed.ptr != frame.data() + end || length == 0 || length > 1024)
            return false;
        frame.remove_prefix(end + 1);
        if (frame.size() <= length + 1 || frame[length] != ',')
            return false;
        name = std::string(frame.substr(0, length));
        frame.remove_prefix(length + 1);
    }
    auto& state = buses_[name];
    const auto reset_capture = [&]() {
        bus_manager.resetBus(name);
        state.last_frame_timestamp_ = std::numeric_limits<Timestamp>::min();
        state.last_sequence_ = 0;
        state.received_frames_ = 0;
        state.missing_frames_ = 0;
        state.has_sequence_ = false;
        state.expect_first_sequence_ = true;
        changed = true;
        reset = true;
    };
    const auto record_sequence = [&](std::optional<std::uint64_t> sequence) {
        if (!sequence)
            return true; // Older bridge versions have no sequence numbers.
        if (state.has_sequence_ && *sequence <= state.last_sequence_)
            return false; // Duplicate or late datagram.
        if (!state.has_sequence_ && state.expect_first_sequence_)
            state.missing_frames_ += *sequence;
        if (state.has_sequence_ && *sequence - state.last_sequence_ > 1)
            state.missing_frames_ += *sequence - state.last_sequence_ - 1;
        state.last_sequence_ = *sequence;
        state.has_sequence_ = true;
        state.expect_first_sequence_ = false;
        ++state.received_frames_;
        return true;
    };
    // Q,<sequence>,<frame> wraps A, D and S; R starts a new capture.
    if (frame == "R")
    {
        reset_capture();
        return changed;
    }
    std::optional<std::uint64_t> sequence;
    if (frame.starts_with("Q,"))
    {
        const auto end = frame.find(',', 2);
        if (end == std::string_view::npos)
            return changed;
        std::uint64_t value = 0;
        const auto parsed = std::from_chars(frame.data() + 2, frame.data() + end, value);
        if (parsed.ec != std::errc{} || parsed.ptr != frame.data() + end)
            return changed;
        sequence = value;
        frame.remove_prefix(end + 1);
    }
    if (sequence && *sequence == 0 && state.has_sequence_ && state.last_sequence_ > 0)
        reset_capture();
    if (frame == "S")
    {
        if (!record_sequence(sequence))
            return changed;
        bus_manager.process(name, {I2CEvent::Type::Stop});
        return changed;
    }
    if (frame.size() < 3 || frame[1] != ',')
        return changed;
    const auto comma = frame.find(',', 2);
    if (comma == std::string_view::npos)
        return changed;
    const auto parsed_time = TimeValue::parse(frame.substr(2, comma - 2));
    if (!parsed_time)
        return changed;
    const Timestamp timestamp = *parsed_time;
    if (timestamp < state.last_frame_timestamp_)
    {
        if (sequence)
            return changed;
        reset_capture();
    }
    if (!record_sequence(sequence))
        return changed;
    if (frame[0] == 'A')
    {
        const auto next = frame.find(',', comma + 1);
        if (next == std::string_view::npos || next + 2 != frame.size())
            return changed;
        unsigned int address = 0;
        const auto parsed = std::from_chars(frame.data() + comma + 1, frame.data() + next, address);
        if (parsed.ec != std::errc{} || parsed.ptr != frame.data() + next || address > 127 || (frame[next + 1] != '0' && frame[next + 1] != '1'))
            return changed;
        changed |= bus_manager.process(name, {I2CEvent::Type::Address, timestamp,
                                       static_cast<std::uint8_t>(address), frame[next + 1] == '1'});
        state.last_frame_timestamp_ = timestamp;
    }
    else if (frame[0] == 'D')
    {
        unsigned int value = 0;
        const auto parsed = std::from_chars(frame.data() + comma + 1, frame.data() + frame.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != frame.data() + frame.size() || value > 255)
            return changed;
        changed |= bus_manager.process(name, {I2CEvent::Type::Data, timestamp,
                                       static_cast<std::uint8_t>(value)});
        state.last_frame_timestamp_ = timestamp;
    }
    return changed;
}
