#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>
#include "time_value.hpp"

struct LogData
{
    Timestamp max_timestamp{std::numeric_limits<Timestamp>::min()};
    Timestamp min_timestamp{std::numeric_limits<Timestamp>::max()};
    std::vector<Timestamp> timestamps;
    bool has_absolute_timestamps{true};
    bool has_csv_timestamps{false};
    std::unordered_map<Timestamp, std::string> raw_csv_timestamps;
    std::vector<std::uint8_t> active_addresses;
};
