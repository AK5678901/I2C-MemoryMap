#pragma once

#include <compare>
#include <cstdint>
#include <format>
#include <string>

struct DeviceKey
{
    std::string bus;
    std::uint8_t address{};
    auto operator<=>(const DeviceKey&) const = default;

    std::string label() const { return std::format("{} / 0x{:02X}", bus, address); }
};
