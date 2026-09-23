#pragma once

#include "timestamp.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

struct TimelineSnapshotPosition
{
    std::uint8_t device_address;
    std::size_t snapshot_index;
};

struct TimelinePosition
{
    Timestamp time{0};
    std::optional<TimelineSnapshotPosition> snapshot;
};

class TimelineSyncState
{
  public:
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    [[nodiscard]] const TimelinePosition& position() const noexcept { return position_; }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    void setEnabled(const bool enabled)
    {
        if (enabled_ == enabled)
            return;
        enabled_ = enabled;
        if (enabled_)
            ++revision_;
    }

    void selectTime(const Timestamp time)
    {
        position_ = {time, std::nullopt};
        ++revision_;
    }

    void selectSnapshot(const Timestamp time, const std::uint8_t device_address,
                        const std::size_t snapshot_index)
    {
        position_ = {time, TimelineSnapshotPosition{device_address, snapshot_index}};
        ++revision_;
    }

  private:
    bool enabled_{true};
    TimelinePosition position_;
    std::uint64_t revision_{1};
};
