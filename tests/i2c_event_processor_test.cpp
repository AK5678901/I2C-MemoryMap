#include "i2c_event_processor.hpp"
#include "i2c_device.hpp"

#include <iostream>
#include <stdexcept>
#include <type_traits>

static_assert(std::is_same_v<decltype(std::declval<I2CEventProcessor&>().GetInfo()),
                             const I2CEventProcessor::Info&>);

void check(bool condition)
{
    if (!condition)
        throw std::runtime_error("I2C event processing check failed");
}

int main()
{
    using Type = I2CEvent::Type;
    I2CDeviceManager devices;
    I2CEventProcessor processor;
    const auto& log = processor.GetInfo();
    check(!processor.process({Type::Data, 1, 0xFF}, devices));
    check(!processor.process({Type::Address, 2, 0x50}, devices));
    check(!processor.process({Type::Data, 3, 0x10}, devices));
    check(log.timestamps.empty());
    check(processor.process({Type::Data, 4, 0xAB}, devices));
    check(processor.process({Type::Data, 4, 0xCD}, devices));
    check(log.timestamps == std::vector<Timestamp>{4});
    const auto* device = devices.GetDevice(0x50);
    check(device->GetSnapshotCount() == 2);
    check(device->GetSnapshotByIndex(0).updated_register_address == 0x10);
    check(device->GetSnapshotByIndex(1).updated_register_address == 0x11);
    processor.process({Type::Address, 5, 0x50, true}, devices); // Repeated START.
    check(processor.process({Type::Data, 6, 0xEF}, devices));
    check(!device->GetSnapshotByIndex(2).is_write);
    check(device->GetWriteStats().at(0x10).call_count == 1);
    check(device->GetReadStats().at(0x12).call_count == 1);
    processor.process({Type::Stop}, devices);
    check(!processor.process({Type::Data, 7, 0xFF}, devices));
    check(log.timestamps == (std::vector<Timestamp>{4, 6}));
    check(log.min_timestamp == 4 && log.max_timestamp == 6);
    check(log.active_addresses == std::vector<std::uint8_t>{0x50});

    devices.GetDevice(0x50)->SetRegisterName(0x10, "Control");
    processor.SetCsvTimeFormat(false);
    processor.RecordCsvTimestamp(4, "0.000000004");
    check(log.has_csv_timestamps && !log.has_absolute_timestamps);
    check(log.raw_csv_timestamps.at(4) == "0.000000004");
    processor.reset(devices);
    check(log.timestamps.empty() && log.active_addresses.empty());
    check(log.raw_csv_timestamps.empty() && !log.has_csv_timestamps && log.has_absolute_timestamps);
    check(device->GetSnapshotCount() == 0 && device->GetRegisterName(0x10) == "Control");
    check(!processor.process({Type::Data, 8, 0xFF}, devices));

    I2CDevice::Config config{};
    config.i2c_dev_addr = 0x60;
    config.reg_addr_bytes = 0;
    devices.RegisterDevice(config);
    processor.process({Type::Address, 10, 0x60}, devices);
    check(processor.process({Type::Data, 11, 0x12}, devices));
    processor.process({Type::Address, 12, 0x60, true}, devices);
    check(processor.process({Type::Data, 13, 0x34}, devices));
    check(log.timestamps == (std::vector<Timestamp>{11, 13}));
    check(devices.GetDevice(0x60)->GetSnapshotByIndex(1).GetUpdatedRegister().value.read_data ==
          std::vector<std::uint8_t>{0x34});
    std::cout << "I2C event processing checks passed.\n";
}
