#include "csv_processor.hpp"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <string>

namespace
{
I2CDevice* findOrRegisterDevice(I2CDeviceManager& devicemanager, const std::uint8_t address)
{
    if (auto* device = devicemanager.GetDevice(address))
    {
        return device;
    }

    I2CDevice::Config config{
        .device_name = std::format("0x{:02X}", address),
        .i2c_dev_addr = address,
        .reg_addr_bytes = 1,
        .auto_addr_inc = true,
        .support_direct_read_from_default_reg = false,
        .default_reg_addr = 0x00,
        .reg_map_size = 256,
        .is_little_endian = true,
    };
    auto* device = devicemanager.RegisterDevice(config);
    std::cout << "Unregistered I2C device detected. Registered automatically: " << config.device_name
              << ", address bytes=" << config.reg_addr_bytes << ", auto_addr_inc=" << config.auto_addr_inc << '\n';
    return device;
}

void addActiveAddress(std::vector<std::uint8_t>& addresses, const std::uint8_t address)
{
    if (std::ranges::find(addresses, address) == addresses.end())
    {
        addresses.push_back(address);
    }
}
} // namespace

LogData importCsvLog(I2CDeviceManager& devicemanager, const std::filesystem::path& filename)
{
    LogData log;
    if (!std::filesystem::exists(filename))
    {
        return log;
    }

    std::ifstream file(filename);
    if (!file)
    {
        return log;
    }

    std::string line;
    std::getline(file, line);
    log.has_csv_timestamps = true;
    I2CDevice* current_device = nullptr;

    while (std::getline(file, line))
    {
        std::stringstream row(line);
        std::string name;
        std::string type;
        std::string acknowledgement;
        std::string address_text;
        std::string read_text;
        std::string data_text;
        std::string timestamp_text;
        std::string duration_text;

        std::getline(row, name, ',');
        std::getline(row, type, ',');
        std::getline(row, timestamp_text, ',');
        std::getline(row, duration_text, ',');
        std::getline(row, acknowledgement, ',');
        std::getline(row, address_text, ',');
        std::getline(row, read_text, ',');
        std::getline(row, data_text, ',');

        if (timestamp_text.size() >= 2 && timestamp_text.front() == '"' && timestamp_text.back() == '"')
            timestamp_text = timestamp_text.substr(1, timestamp_text.size() - 2);
        auto parsed_timestamp = TimeValue::parse(timestamp_text);
        if (!parsed_timestamp)
        {
            // Legacy Saleae exports use capture-relative seconds, with no absolute origin.
            try
            {
                std::size_t consumed = 0;
                const auto seconds = std::stod(timestamp_text, &consumed);
                if (consumed == timestamp_text.size() && std::isfinite(seconds) &&
                    seconds >= static_cast<double>(std::numeric_limits<Timestamp>::min()) / 1000000000.0 &&
                    seconds <= static_cast<double>(std::numeric_limits<Timestamp>::max()) / 1000000000.0)
                {
                    parsed_timestamp = static_cast<Timestamp>(std::llround(seconds * 1000000000.0));
                    log.has_absolute_timestamps = false;
                }
            }
            catch (const std::exception&)
            {
            }
        }
        if (!parsed_timestamp)
            continue;
        const Timestamp timestamp = *parsed_timestamp;
        log.raw_csv_timestamps.try_emplace(timestamp, timestamp_text);

        if (type == "\"address\"")
        {
            const auto address = static_cast<std::uint8_t>(std::stoul(address_text, nullptr, 16));
            addActiveAddress(log.active_addresses, address);
            current_device = findOrRegisterDevice(devicemanager, address);
            current_device->CallThisEachI2CAddrByteWrite(read_text == "true");
        }
        else if (type == "\"data\"")
        {
            if (current_device != nullptr)
            {
                const auto data = static_cast<std::uint8_t>(std::stoul(data_text, nullptr, 16));
                current_device->DataByte(data, timestamp);
            }
        }
        else if (type == "\"stop\"")
        {
            for (const auto& [address, device] : devicemanager.GetAllDevices())
            {
                device->CallThisEachI2CStopCondition();
            }
        }
    }

    std::set<Timestamp> unique_timestamps;
    for (const auto& [address, device] : devicemanager.GetAllDevices())
    {
        for (std::size_t index = 0; index < device->GetSnapshotCount(); ++index)
        {
            const auto& timestamp = device->GetSnapshotByIndex(index).timestamp;
            unique_timestamps.insert(timestamp);
            log.max_timestamp = std::max(log.max_timestamp, timestamp);
            log.min_timestamp = std::min(log.min_timestamp, timestamp);
        }
    }
    log.timestamps.assign(unique_timestamps.begin(), unique_timestamps.end());
    return log;
}
