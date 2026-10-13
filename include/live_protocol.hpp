#pragma once
#include "timestamp.hpp"

#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <string_view>

class I2CBusManager;

// Decoder state is independent for each bus; UI counters total the current bus sessions.
class LiveProtocol
{
  public:
    bool process(std::string_view packet, I2CBusManager& bus_manager, bool& reset);
    std::uint64_t receivedFrames() const
    {
        std::uint64_t total = 0;
        for (const auto& [name, state] : buses_) total += state.received_frames_;
        return total;
    }
    std::uint64_t missingFrames() const
    {
        std::uint64_t total = 0;
        for (const auto& [name, state] : buses_) total += state.missing_frames_;
        return total;
    }
    bool hasSequence() const
    {
        for (const auto& [name, state] : buses_) if (state.has_sequence_) return true;
        return false;
    }

  private:
    struct BusState
    {
        Timestamp last_frame_timestamp_{std::numeric_limits<Timestamp>::min()};
        std::uint64_t last_sequence_{0};
        std::uint64_t received_frames_{0};
        std::uint64_t missing_frames_{0};
        bool has_sequence_{false};
        bool expect_first_sequence_{false};
    };
    std::map<std::string, BusState> buses_;
};
