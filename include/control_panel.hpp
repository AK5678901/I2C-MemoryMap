#pragma once

#include "i2c_device.hpp"
#include "log_data.hpp"
#include <array>
#include <optional>

class LiveReceiver;

namespace ControlPanel
{
struct Result { bool slider_changed{false}; std::optional<Timestamp> jump_time; };
class State
{
  public:
    void resetJumpInput();
    Result render(const I2CDeviceManager& devicemanager, const LogData& log,
                  TimeValue::DisplayFormat& format, Timestamp& target_time,
                  bool& sync_timeline_positions, const LiveReceiver* live_receiver);

  private:
    std::array<char, 96> jump_text_{};
    bool jump_initialized_{false};
    std::optional<TimeValue::DisplayFormat> previous_format_;
};
}
