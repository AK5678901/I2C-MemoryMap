#pragma once

#include "timestamp.hpp"
#include <cstdint>

// Input-independent bus event. Stop does not require a timestamp.
struct I2CEvent
{
    enum class Type { Address, Data, Stop };

    Type type;
    Timestamp timestamp{0};
    std::uint8_t value{0}; // Device address for Address, byte value for Data.
    bool is_read{false};  // Used only for Address.
};
