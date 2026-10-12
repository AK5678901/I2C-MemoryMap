#include "i2c_bus_manager.hpp"

#include <stdexcept>
#include <type_traits>

static_assert(std::is_same_v<decltype(std::declval<const I2CBusManager&>().GetDevice(DeviceKey{})),
                             const I2CDevice*>);

namespace
{
void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
}

int main()
{
    I2CBusManager manager;
    const auto& view = manager;
    using Type = I2CEvent::Type;
    check(view.GetInfo().active_devices.empty() && view.GetAllDevices().empty(), "Initial view must be empty");

    // Address creates a device without a snapshot: its visibility must still update.
    check(!manager.process("main", {Type::Address, 1, 0x50}), "Address must not create a snapshot");
    check(view.GetDevice({"main", 0x50}) != nullptr, "GetDevice must refresh after an address event");
    check(view.GetInfo().active_devices == std::vector<DeviceKey>{{"main", 0x50}}, "Active devices must be current");

    manager.process("main", {Type::Data, 2, 0x10});
    manager.process("main", {Type::Data, 4, 0xAA});
    manager.process("sub", {Type::Address, 1, 0x50});
    manager.process("sub", {Type::Data, 2, 0x20});
    manager.process("sub", {Type::Data, 3, 0xBB});
    manager.process("sub", {Type::Data, 4, 0xCC});
    check(view.GetAllDevices().size() == 2, "GetAllDevices must refresh after a batch");
    check(view.GetInfo().timeline.timestamps == std::vector<Timestamp>{3, 4}, "Batch times must be merged and unique");
    const auto* times = view.GetInfo().timeline.timestamps.data();
    (void)view.GetDevice({"sub", 0x50});
    (void)view.GetAllDevices();
    check(view.GetInfo().timeline.timestamps.data() == times, "Reads without updates must retain the cached timeline");

    // CSV metadata can change even when no new snapshot is generated.
    manager.processCsvEvent("main", {Type::Stop, 5}, "0.000000005", false);
    check(view.GetInfo().timeline.has_csv_timestamps && !view.GetInfo().timeline.has_absolute_timestamps &&
          view.GetInfo().timeline.raw_csv_timestamps.at(5) == "0.000000005", "CSV metadata must invalidate the aggregate");

    manager.resetBus("sub");
    check(view.GetInfo().timeline.timestamps == std::vector<Timestamp>{4}, "Bus reset must remove only that bus's times");
    check(view.GetDevice({"sub", 0x50})->GetSnapshotCount() == 0 &&
          view.GetDevice({"main", 0x50})->GetSnapshotCount() == 1, "Bus reset must preserve other bus histories");

    I2CDeviceManager configured;
    I2CDevice::Config config{};
    config.device_name = "Configured";
    config.i2c_dev_addr = 0x60;
    configured.RegisterDevice(config)->SetRegisterName(0x10, "Control");
    manager.registerDevices("main", std::move(configured));
    check(view.GetAllDevices().size() == 3 && view.GetDevice({"main", 0x60})->GetRegisterName(0x10) == "Control",
          "Registration must invalidate the cached device index");

    // A failed registration must not replace a device referenced by the decoder.
    I2CDeviceManager duplicate;
    config.i2c_dev_addr = 0x50;
    duplicate.RegisterDevice(config);
    bool rejected = false;
    try { manager.registerDevices("main", std::move(duplicate)); }
    catch (const std::runtime_error&) { rejected = true; }
    check(rejected && view.GetDevice({"main", 0x50})->GetSnapshotCount() == 1,
          "Failed registration must preserve existing devices and readable aggregate");

    manager.reset();
    check(view.GetInfo().active_devices.empty() && view.GetInfo().timeline.timestamps.empty() &&
          view.GetInfo().timeline.raw_csv_timestamps.empty(), "Reset must clear all aggregate history");
    check(view.GetAllDevices().size() == 3, "Reset must keep registered devices");
}
