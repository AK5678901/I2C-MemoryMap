#pragma once

#include "i2c_bus_manager.hpp"

#include <filesystem>

[[nodiscard]] bool loadDeviceConfigs(I2CDeviceManager& devicemanager,
                                     const std::filesystem::path& directory = "devices",
                                     std::string* error = nullptr);

[[nodiscard]] bool loadDeviceConfigs(I2CBusManager& bus_manager, const std::filesystem::path& directory = "devices",
                                     std::string* error = nullptr);
