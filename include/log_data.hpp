#pragma once

#include <cstdint>
#include <limits>
#include <vector>

struct LogData
{
    double max_timestamp{0.0};
    double min_timestamp{std::numeric_limits<double>::max()};
    std::vector<double> timestamps;
    std::vector<std::uint8_t> active_addresses;
};
