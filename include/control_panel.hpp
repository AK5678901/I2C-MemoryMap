#pragma once

#include "i2c_bus_manager.hpp"
#include "timeline_info.hpp"
#include "timeline_sync.hpp"
#include <array>
#include <optional>

class LiveReceiver;

namespace ControlPanel
{
struct Result { std::optional<Timestamp> jump_time; };
class State
{
  public:
    void resetJumpInput();
    Result render(const I2CBusManager& bus_manager, const TimelineInfo& info,
                  TimeValue::DisplayFormat& format, Timestamp& target_time,
                  TimelineSyncState& timeline_sync, const LiveReceiver* live_receiver);

  private:
    std::array<char, 96> jump_text_{};
    bool jump_initialized_{false};
    bool jump_invalid_{false};
    std::optional<TimeValue::DisplayFormat> previous_format_;
};
}
