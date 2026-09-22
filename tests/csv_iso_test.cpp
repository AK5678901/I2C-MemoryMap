#include "csv_processor.hpp"
#include "time_value.hpp"

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
    const auto log = importCsvLog(devices, path);
    std::filesystem::remove(path);
    const auto expected = TimeValue::parse("2026-09-22T08:20:18.204048600+00:00");
    const auto* device = devices.GetDevice(0x50);
    if (!expected || !device || log.timestamps.size() != 1 || log.timestamps.front() != *expected ||
        device->GetHistorySize() != 1 || device->GetHistoryEntry(0).timestamp != *expected ||
        !log.has_csv_timestamps ||
        log.raw_csv_timestamps.at(*expected) != "2026-09-22T08:20:18.204048600+00:00")
        throw std::runtime_error("ISO CSV import failed");
}
