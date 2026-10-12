#include "csv_processor.hpp"
#include "device_config_loader.hpp"
#include "timeline_sync.hpp"

#include <chrono>
#include <fstream>
#include <stdexcept>

namespace
{
void check(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}

struct Fixture
{
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("i2c_multibus_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture() { std::filesystem::create_directories(root); }
    ~Fixture() { std::filesystem::remove_all(root); }
};
}

int main()
{
    Fixture fixture;
    const auto configs = fixture.root / "devices";
    for (const auto* bus : {"I2C", "I2C [1]"})
    {
        std::filesystem::create_directories(configs / bus);
        std::ofstream(configs / bus / "device.json") <<
            R"({"devices":[{"name":"Same name","dev_addr_7bit":"0x48","registers":[{"address":"0x10","name":"Configured"}]}]})";
    }
    std::ofstream(configs / "I2C" / "another.json") <<
        R"({"devices":[{"name":"Other","dev_addr_7bit":"0x49"}]})";
    I2CBusManager bus_manager;
    check(loadDeviceConfigs(bus_manager, configs), "Bus folders must load");
    check(bus_manager.GetAllDevices().size() == 3, "All JSON files belong to their folder's bus");
    const DeviceKey main{"I2C", 0x48}, sub{"I2C [1]", 0x48};
    check(bus_manager.GetDevice(main) != bus_manager.GetDevice(sub), "Same address must have separate instances");

    const auto csv = fixture.root / "capture.csv";
    std::ofstream(csv) <<
        "name,type,start_time,duration,ack,address,read,data\n"
        "\"I2C\",\"address\",1,0,true,0x48,false,,\n"
        "\"I2C\",\"data\",2,0,true,,,0x10,\n"
        "\"I2C [1]\",\"address\",1,0,true,0x48,false,,\n"
        "\"I2C [1]\",\"data\",2,0,true,,,0x10,\n"
        "\"I2C\",\"data\",3,0,true,,,0xAA,\n"
        "\"I2C [1]\",\"data\",3,0,true,,,0xBB,\n"
        "\"I2C [1]\",\"stop\",4,0,,,,,\n"
        "\"I2C\",\"data\",5,0,true,,,0xCC,\n"
        "\"I2C [1]\",\"data\",6,0,true,,,0xFF,\n"
        "\"I2C\",\"address\",7,0,true,0x48,true,,\n"
        "\"I2C\",\"data\",8,0,true,,,0xDD,\n";
    importCsvLog(bus_manager, csv);
    const auto* a = bus_manager.GetDevice(main);
    const auto* b = bus_manager.GetDevice(sub);
    check(a->GetRegisterName(0x10) == "Configured", "Import must retain definitions");
    check(a->GetSnapshotCount() == 3 && b->GetSnapshotCount() == 1, "STOP must only affect its bus");
    check(a->GetSnapshotByIndex(0).GetUpdatedRegister().value.write_data.front() == 0xAA,
          "Main data must reach main device");
    check(b->GetSnapshotByIndex(0).GetUpdatedRegister().value.write_data.front() == 0xBB,
          "Sub data must reach sub device");
    check(a->GetSnapshotByIndex(1).updated_register_address == 0x11, "Sub STOP must preserve main register pointer");
    check(!a->GetSnapshotByIndex(2).is_write, "Repeated address read must be preserved");
    check(bus_manager.GetInfo().timeline.timestamps == std::vector<Timestamp>{3000000000LL, 5000000000LL, 8000000000LL},
          "Shared timeline must be sorted and deduplicate equal times");
    check(a->GetSnapshotAt(3000000000LL).registers.at(0x10).value.write_data.front() == 0xAA &&
          b->GetSnapshotAt(5000000000LL).registers.at(0x10).value.write_data.front() == 0xBB,
          "Common time must retain each bus's latest state");
    TimelineSyncState sync;
    sync.selectSnapshot(3000000000LL, sub, 0);
    check(sync.position().snapshot->device_address == sub && sync.position().snapshot->device_address != main,
          "Selection must distinguish identical addresses");

    // Reload resets all decoder states and histories, without removing definitions.
    importCsvLog(bus_manager, csv);
    check(a->GetSnapshotCount() == 3 && b->GetSnapshotCount() == 1, "Reload must not accumulate history");
    std::ofstream(csv) << "name,type,time,duration,ack,address,read,data\n"
                         "\"Unconfigured, bus\",address,1,0,true,0x50,false,\n";
    importCsvLog(bus_manager, csv);
    check(bus_manager.GetDevice({"Unconfigured, bus", 0x50}) != nullptr, "Quoted unknown bus must auto-register");
    check(a->GetSnapshotCount() == 0 && b->GetSnapshotCount() == 0, "Reload must clear absent buses too");

    std::ofstream(configs / "I2C" / "duplicate.json") <<
        R"({"devices":[{"name":"Duplicate","dev_addr_7bit":"0x48"}]})";
    I2CBusManager duplicates;
    std::string error;
    check(!loadDeviceConfigs(duplicates, configs, &error), "Duplicate address within a bus must fail");
    check(error.find(".json") != std::string::npos && error.find("Duplicate device address") != std::string::npos,
          "Duplicate error must include the file and reason");
    const auto invalid = fixture.root / "invalid";
    std::filesystem::create_directories(invalid);
    std::ofstream(invalid / "broken.json") << R"({"devices":[{"name":"bad",}]})";
    I2CDeviceManager invalid_devices;
    check(!loadDeviceConfigs(invalid_devices, invalid, &error) && error.find("broken.json") != std::string::npos &&
          error.find("parse_error") != std::string::npos, "Syntax error must identify its file");
    std::ofstream(invalid / "broken.json") << R"({"name":"unwrapped device"})";
    check(!loadDeviceConfigs(invalid_devices, invalid, &error) && error.find("devices") != std::string::npos,
          "Unsupported root must not silently discard a device");
}
