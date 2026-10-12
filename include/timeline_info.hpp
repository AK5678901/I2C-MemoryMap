#pragma once

#include "timestamp.hpp"

#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

// Time information shared by an individual bus and the combined bus timeline.
struct TimelineInfo
{
    Timestamp max_timestamp{std::numeric_limits<Timestamp>::min()};
    Timestamp min_timestamp{std::numeric_limits<Timestamp>::max()};
    std::vector<Timestamp> timestamps;
    bool has_absolute_timestamps{true};
    bool has_csv_timestamps{false};
    std::unordered_map<Timestamp, std::string> raw_csv_timestamps;
};
