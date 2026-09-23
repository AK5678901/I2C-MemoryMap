#pragma once

#include "i2c_device.hpp"
#include "timeline_sync.hpp"
#include "view_helpers.hpp"

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

struct State
{
    AccessTimeline access_timeline;
    std::vector<ViewHelpers::TransactionRow> grouped_timeline;
    ViewHelpers::TimelineFilters<7> filters{};
    bool group_transactions{false};
    std::uint64_t applied_revision{0};
};

void rebuild(State& state, const I2CDeviceManager& devicemanager);
void render(State& state, I2CDeviceManager& devicemanager, const TimeValue::DisplayView& display,
            TimelineSyncState& sync, std::optional<Timestamp> jump_time);
} // namespace TimelineView
