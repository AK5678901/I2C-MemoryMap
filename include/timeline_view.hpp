#pragma once

#include "i2c_device.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>
#include <optional>

namespace TimelineView
{
struct AccessRow
{
    Timestamp timestamp;
    std::uint8_t device_address;
    std::size_t snapshot_index;
};

using AccessTimeline = std::vector<AccessRow>;

void rebuild(AccessTimeline& timeline, const I2CDeviceManager& devicemanager);
void render(AccessTimeline& timeline, I2CDeviceManager& devicemanager,
            const TimeValue::DisplayView& display, Timestamp& target_time,
            bool sync_timeline_positions, bool& scroll_all_devices_timeline, bool& scroll_device_timeline,
            std::uint8_t& scroll_device_address, std::size_t& scroll_snapshot_index,
            bool scroll_timelines_to_target, std::optional<Timestamp> jump_time);
} // namespace TimelineView
