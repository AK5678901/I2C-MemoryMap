#pragma once

#include "i2c_device.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

namespace DeviceWindow
{
std::string name(const I2CDevice& device, std::uint8_t address);
void render(I2CDeviceManager& devicemanager, std::map<std::uint8_t, bool>& visibility,
            const TimeValue::DisplayView& display, Timestamp& target_time,
            bool sync_timeline_positions, bool& scroll_all_devices_timeline, bool& scroll_device_timeline,
            std::uint8_t& scroll_device_address, std::size_t& scroll_snapshot_index,
            bool scroll_timelines_to_target);
} // namespace DeviceWindow
