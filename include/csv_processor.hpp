#pragma once

#include "i2c_device.hpp"
#include "i2c_event_processor.hpp"

#include <filesystem>

void importCsvLog(I2CDeviceManager& devicemanager, I2CEventProcessor& processor, const std::filesystem::path& filename);
