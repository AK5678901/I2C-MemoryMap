#include "csv_processor.hpp"
#include "time_value.hpp"
#include "i2c_event_processor.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>

int main()
{
    const auto path = std::filesystem::temp_directory_path() / "i2c_memorymap_iso_test.csv";
    {
        std::ofstream file(path);
        file << "name,type,start_time,duration,ack,address,read,data\n"
                "\"I2C\",\"address\",2026-09-22T08:20:18.204048396+00:00,0,true,0x50,false,\n"
                "\"I2C\",\"data\",2026-09-22T08:20:18.204048500+00:00,0,true,,,0x00\n"
                "\"I2C\",\"data\",2026-09-22T08:20:18.204048600+00:00,0,true,,,0x42\n";
    }
    I2CDeviceManager devices;
    I2CEventProcessor csv_processor;
    importCsvLog(devices, csv_processor, path);
    const auto& log = csv_processor.GetInfo();
    std::filesystem::remove(path);
    const auto expected = TimeValue::parse("2026-09-22T08:20:18.204048600+00:00");
    const auto* device = devices.GetDevice(0x50);
    if (!expected || !device || log.timestamps.size() != 1 || log.timestamps.front() != *expected ||
        device->GetSnapshotCount() != 1 || device->GetSnapshotByIndex(0).timestamp != *expected ||
        !log.has_csv_timestamps ||
        log.raw_csv_timestamps.at(*expected) != "2026-09-22T08:20:18.204048600+00:00")
        throw std::runtime_error("ISO CSV import failed");

    // The same input as typed events must produce the same navigation times and values.
    I2CDeviceManager event_devices;
    I2CEventProcessor processor;
    const auto& event_log = processor.GetInfo();
    using Type = I2CEvent::Type;
    processor.process({Type::Address, *expected - 204, 0x50}, event_devices);
    processor.process({Type::Data, *expected - 100, 0x00}, event_devices);
    processor.process({Type::Data, *expected, 0x42}, event_devices);
    if (event_log.timestamps != log.timestamps || event_log.min_timestamp != log.min_timestamp ||
        event_log.max_timestamp != log.max_timestamp || event_log.active_addresses != log.active_addresses ||
        event_devices.GetDevice(0x50)->GetSnapshotByIndex(0).GetUpdatedRegister().value.write_data !=
            device->GetSnapshotByIndex(0).GetUpdatedRegister().value.write_data)
        throw std::runtime_error("CSV and event processing differ");
}
