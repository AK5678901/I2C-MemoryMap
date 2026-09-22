#pragma once

#include "i2c_device.hpp"
#include "log_data.hpp"

#include <filesystem>

[[nodiscard]] LogData importCsvLog(I2CDeviceManager& devicemanager, const std::filesystem::path& filename);
