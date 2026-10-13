#include "csv_processor.hpp"
#include "time_value.hpp"
#include "i2c_event_processor.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <iostream>

int main(int argc, char** argv)
{
    const auto path = std::filesystem::temp_directory_path() / "i2c_memorymap_iso_test.csv";
    {
        std::ofstream file(path);
        file << "name,type,start_time,duration,ack,address,read,data\n"
                "\"I2C\",\"address\",2026-09-22T08:20:18.204048396+00:00,0,true,0x50,false,\n"
                "\"I2C\",\"data\",2026-09-22T08:20:18.204048500+00:00,0,true,,,0x00\n"
                "\"I2C\",\"data\",2026-09-22T08:20:18.204048600+00:00,0,true,,,0x42\n";
    }
    I2CBusManager bus_manager;
    importCsvLog(bus_manager, path);
    const auto& log = bus_manager.GetInfo();
    std::filesystem::remove(path);
    const auto expected = TimeValue::parse("2026-09-22T08:20:18.204048600+00:00");
    const auto* device = bus_manager.GetDevice({"I2C", 0x50});
    if (!expected || !device || log.timeline.timestamps.size() != 1 || log.timeline.timestamps.front() != *expected ||
        device->GetSnapshotCount() != 1 || device->GetSnapshotByIndex(0).timestamp != *expected ||
        !log.timeline.has_csv_timestamps ||
        log.timeline.raw_csv_timestamps.at(*expected) != "2026-09-22T08:20:18.204048600+00:00")
        throw std::runtime_error("ISO CSV import failed");

    // The same input as typed events must produce the same navigation times and values.
    I2CDeviceManager event_devices;
    I2CEventProcessor processor;
    const auto& event_log = processor.GetInfo();
    using Type = I2CEvent::Type;
    processor.process({Type::Address, *expected - 204, 0x50}, event_devices);
    processor.process({Type::Data, *expected - 100, 0x00}, event_devices);
    processor.process({Type::Data, *expected, 0x42}, event_devices);
    if (event_log.timeline.timestamps != log.timeline.timestamps || event_log.timeline.min_timestamp != log.timeline.min_timestamp ||
        event_log.timeline.max_timestamp != log.timeline.max_timestamp || log.active_devices != std::vector<DeviceKey>{{"I2C", 0x50}} ||
        event_devices.GetDevice(0x50)->GetSnapshotByIndex(0).GetUpdatedRegister().value.write_data !=
            device->GetSnapshotByIndex(0).GetUpdatedRegister().value.write_data)
        throw std::runtime_error("CSV and event processing differ");

    // Saleae may export protocol columns in a different order, as in MET.csv.
    {
        std::ofstream file(path);
        file << "\xEF\xBB\xBF" "name,type,start_time,duration,\"data\",\"ack\",\"address\",\"read\"\n"
                "MET,address,5.093490576,0,,true,0x22,false\n"
                "MET,data,5.093514976,0,0x02,true,,\n"
                "MET,data,5.093539872,0,0x2F,true,,\n"
                "MET,stop,5.093566312,0,,,,\n"
                "MET,address,6,0,,true,0x22,true\n"
                "MET,data,7,0,0x30,true,,\n";
    }
    I2CBusManager reordered;
    importCsvLog(reordered, path);
    const auto* met = reordered.GetDevice({"MET", 0x22});
    if (!met || met->GetSnapshotCount() != 2 ||
        met->GetSnapshotByIndex(0).updated_register_address != 2 ||
        met->GetSnapshotByIndex(0).GetUpdatedRegister().value.write_data != std::vector<std::uint8_t>{0x2F} ||
        met->GetSnapshotByIndex(0).timestamp != 5093539872LL ||
        met->GetSnapshotByIndex(1).is_write ||
        met->GetSnapshotByIndex(1).GetUpdatedRegister().value.read_data != std::vector<std::uint8_t>{0x30})
        throw std::runtime_error("Reordered CSV columns must retain address, data, read flag and time");

    const auto expect_error = [&](const std::string& csv, const std::string& detail) {
        { std::ofstream file(path); file << csv; }
        I2CBusManager invalid;
        try { importCsvLog(invalid, path); }
        catch (const std::runtime_error& error)
        {
            if (std::string(error.what()).find(detail) != std::string::npos)
                return;
            throw;
        }
        throw std::runtime_error("Invalid CSV must report an actionable error");
    };
    expect_error("name,type,start_time,address,read\n", "missing column: data");
    expect_error("name,type,start_time,address,read,data,data\n", "duplicate column: data");
    expect_error("name,type,start_time,address,read,data\nMET,address,1\n", "CSV line 2");
    for (const auto* bad : {"", "true", "0x100", "0x22junk", "-1"})
        expect_error("name,type,start_time,address,read,data\nMET,address,1," + std::string(bad) + ",false,\n",
                     "CSV line 2 (bus MET): invalid address value");
    expect_error("name,type,start_time,address,read,data\nMET,data,1,,,0x100\n", "invalid data value");
    std::filesystem::remove(path);

    // Optional local input validates a user's capture without storing it in the repository.
    if (argc > 1)
    {
        I2CBusManager capture;
        importCsvLog(capture, std::filesystem::path(argv[1]));
        std::size_t snapshots = 0;
        for (const auto& [key, captured_device] : capture.GetAllDevices())
            snapshots += captured_device->GetSnapshotCount();
        if (snapshots == 0)
            throw std::runtime_error("Capture produced no snapshots");
        std::cout << "Imported " << capture.GetAllDevices().size() << " devices, " << snapshots << " snapshots\n";
    }
}
