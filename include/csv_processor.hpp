#pragma once

#include "i2c_bus_manager.hpp"

#include <filesystem>

void importCsvLog(I2CBusManager& bus_manager, const std::filesystem::path& filename);
