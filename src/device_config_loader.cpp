#include "device_config_loader.hpp"

#include "json.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
using Json = nlohmann::json;
using RegisterBitFieldSetter = void (I2CDevice::*)(std::uint32_t, const I2CDevice::BitFieldInfo&);

void registerBitFields(I2CDevice& device, const Json& register_json, const std::string& key,
                       const std::uint32_t register_address, const bool is_little_endian,
                       const RegisterBitFieldSetter setter)
{
    if (!register_json.contains(key) || !register_json[key].is_array())
    {
        return;
    }

    for (const auto& bit : register_json[key])
    {
        I2CDevice::BitFieldInfo bitfield{
            .name = bit.value("name", "UNKNOWN"),
            .byte_offset = static_cast<std::uint8_t>(bit.value("byte_offset", 0)),
            .bit_offset = static_cast<std::uint8_t>(bit.value("bit_offset", 0)),
            .bit_width = static_cast<std::uint8_t>(bit.value("width", 1)),
            .is_little_endian = is_little_endian,
        };
        (device.*setter)(register_address, bitfield);
    }
}

void registerDefinitions(I2CDevice& device, const Json& device_json, const bool is_little_endian)
{
    if (!device_json.contains("registers") || !device_json["registers"].is_array())
    {
        return;
    }

    for (const auto& register_json : device_json["registers"])
    {
        const auto address_text = register_json.value("address", "0x00");
        const auto address = static_cast<std::uint32_t>(std::stoul(address_text, nullptr, 16));
        device.SetRegisterName(address, register_json.value("name", "UNKNOWN"));

        if (register_json.contains("bits") && register_json["bits"].is_array())
        {
            registerBitFields(device, register_json, "bits", address, is_little_endian,
                              &I2CDevice::SetReadRegisterBitField);
            registerBitFields(device, register_json, "bits", address, is_little_endian,
                              &I2CDevice::SetWriteRegisterBitField);
        }
        registerBitFields(device, register_json, "bits_read", address, is_little_endian,
                          &I2CDevice::SetReadRegisterBitField);
        registerBitFields(device, register_json, "bits_write", address, is_little_endian,
                          &I2CDevice::SetWriteRegisterBitField);
    }
}

void registerDevices(I2CDeviceManager& devicemanager, const Json& root)
{
    if (!root.contains("devices") || !root["devices"].is_array())
    {
        return;
    }

    for (const auto& device_json : root["devices"])
    {
        const auto address_text = device_json.value("dev_addr_7bit", "0x00");
        const auto default_register_text = device_json.value("default_reg_addr", "0x00");
        const auto endian = device_json.value("endian", "little");

        I2CDevice::Config config{
            .device_name = device_json.value("name", "UNKNOWN"),
            .i2c_dev_addr = static_cast<std::uint8_t>(std::stoul(address_text, nullptr, 16)),
            .reg_addr_bytes = device_json.value("reg_addr_bytes", 1),
            .auto_addr_inc = device_json.value("auto_addr_inc", true),
            .support_direct_read_from_default_reg = device_json.value("support_direct_read_from_default_reg", false),
            .default_reg_addr = static_cast<std::uint32_t>(std::stoul(default_register_text, nullptr, 16)),
            .reg_map_size = static_cast<std::uint32_t>(device_json.value("reg_map_size", 256)),
            .is_little_endian = endian == "little",
        };

        auto* device = devicemanager.RegisterDevice(config);
        registerDefinitions(*device, device_json, config.is_little_endian);
    }
}
} // namespace

bool loadDeviceConfigs(I2CDeviceManager& devicemanager, const std::filesystem::path& directory)
{
    if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory))
    {
        std::cerr << "Config directory not found: " << directory.string() << '\n';
        return false;
    }

    bool loaded_any = false;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
        {
            continue;
        }

        std::ifstream file(entry.path());
        if (!file)
        {
            std::cerr << "Failed to open config file: " << entry.path().string() << '\n';
            continue;
        }

        try
        {
            Json root;
            file >> root;
            registerDevices(devicemanager, root);
            // Preserve the original definition of success: any parseable JSON file.
            loaded_any = true;
        }
        catch (const std::exception& error)
        {
            std::cerr << "JSON Parse Error in " << entry.path().string() << ": " << error.what() << '\n';
        }
    }

    if (!loaded_any)
    {
        std::cerr << "No valid JSON config files found in 'devices' directory.\n";
    }
    return loaded_any;
}
