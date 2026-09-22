#pragma once

#include "i2c_device.hpp"
#include "log_data.hpp"

namespace ControlPanel
{
bool render(const I2CDeviceManager& devicemanager, const LogData& log, double& target_time,
            bool& sync_timeline_positions);
}
