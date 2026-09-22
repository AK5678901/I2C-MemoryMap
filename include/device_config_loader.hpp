#pragma once

#include "i2c_device.hpp"

#include <filesystem>

[[nodiscard]] bool loadDeviceConfigs(I2CDeviceManager& devicemanager,
                                     const std::filesystem::path& directory = "devices");
