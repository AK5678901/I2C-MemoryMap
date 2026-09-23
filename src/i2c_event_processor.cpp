#include "i2c_event_processor.hpp"
#include "i2c_device.hpp"

#include <algorithm>
#include <format>

namespace
{
I2CDevice* findOrRegisterDevice(I2CDeviceManager& devices, std::uint8_t address)
{
    if (auto* device = devices.GetDevice(address))
        return device;
    I2CDevice::Config config{
        .device_name = std::format("0x{:02X}", address), .i2c_dev_addr = address,
        .reg_addr_bytes = 1, .auto_addr_inc = true, .support_direct_read_from_default_reg = false,
        .default_reg_addr = 0, .reg_map_size = 256, .is_little_endian = true,
    };
    return devices.RegisterDevice(config);
}
}

bool I2CEventProcessor::process(const I2CEvent& event, I2CDeviceManager& devices)
{
    switch (event.type)
    {
    case I2CEvent::Type::Address:
        current_device_ = findOrRegisterDevice(devices, event.value);
        current_device_->CallThisEachI2CAddrByteWrite(event.is_read);
        if (std::ranges::find(info_.active_addresses, event.value) == info_.active_addresses.end())
            info_.active_addresses.push_back(event.value);
        return false;
    case I2CEvent::Type::Data:
    {
        if (!current_device_)
            return false;
        const auto count = current_device_->GetSnapshotCount();
        current_device_->DataByte(event.value, event.timestamp);
        if (current_device_->GetSnapshotCount() == count)
            return false; // Register address bytes do not produce snapshots.

        // Fast append for ordered input; keep navigation times sorted and unique.
        if (info_.timestamps.empty() || event.timestamp > info_.timestamps.back())
            info_.timestamps.push_back(event.timestamp);
        else if (event.timestamp != info_.timestamps.back())
        {
            const auto position = std::ranges::lower_bound(info_.timestamps, event.timestamp);
            if (position == info_.timestamps.end() || *position != event.timestamp)
                info_.timestamps.insert(position, event.timestamp);
        }
        info_.min_timestamp = info_.timestamps.front();
        info_.max_timestamp = info_.timestamps.back();
        return true;
    }
    case I2CEvent::Type::Stop:
        for (const auto& [address, device] : devices.GetAllDevices())
            device->CallThisEachI2CStopCondition();
        current_device_ = nullptr;
        return false;
    }
    return false;
}

void I2CEventProcessor::reset(I2CDeviceManager& devices)
{
    current_device_ = nullptr;
    for (const auto& [address, device] : devices.GetAllDevices())
        device->ResetRuntime();
    info_ = Info{};
}

void I2CEventProcessor::SetCsvTimeFormat(bool absolute)
{
    info_.has_csv_timestamps = true;
    info_.has_absolute_timestamps = absolute;
}

void I2CEventProcessor::RecordCsvTimestamp(Timestamp timestamp, const std::string& text)
{
    info_.raw_csv_timestamps.try_emplace(timestamp, text);
}
