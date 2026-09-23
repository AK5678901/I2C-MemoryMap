#pragma once

#include "i2c_device.hpp"
#include "timeline_sync.hpp"
#include "view_helpers.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

namespace DeviceWindow
{
struct State
{
    bool visible{true};
    std::uint64_t applied_revision{0};
    ViewHelpers::TimelineFilters<5> timeline_filters{};
    bool group_transactions{false};
    float statistics_height{200.0F};
};

std::string name(const I2CDevice& device, std::uint8_t address);
void render(I2CDeviceManager& devicemanager, std::map<std::uint8_t, State>& states,
            const TimeValue::DisplayView& display, TimelineSyncState& sync);
} // namespace DeviceWindow
