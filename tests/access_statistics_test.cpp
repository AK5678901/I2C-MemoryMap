#include "i2c_device.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

void check(bool condition)
{
    if (!condition)
        throw std::runtime_error("Access statistics check failed");
}

void checkNear(double actual, double expected)
{
    check(std::abs(actual - expected) < 1e-10);
}

int main()
{
    I2CDevice::CommandStat stat;
    check(!stat.GetIntervalAt(100));
    stat.RecordAccess(1);
    check(stat.intervals.empty());
    check(!stat.GetIntervalAt(1));
    stat.RecordAccess(3);
    checkNear(stat.GetIntervalStdDev(), 0);
    stat.RecordAccess(7);
    checkNear(stat.mean_interval, 3);
    checkNear(stat.min_interval, 2);
    checkNear(stat.max_interval, 4);
    checkNear(stat.GetIntervalStdDev(), 1);
    check(!stat.GetIntervalAt(0));
    check(!stat.GetIntervalAt(2.99));
    checkNear(stat.GetIntervalAt(3).value(), 2);
    checkNear(stat.GetIntervalAt(6).value(), 2);
    checkNear(stat.GetIntervalAt(7).value(), 4);
    checkNear(stat.GetIntervalAt(100).value(), 4);
    checkNear(stat.GetIntervalAt(3).value(), 2); // Scrubbing backwards must not retain a future value.

    I2CDevice::CommandStat equal_times;
    equal_times.RecordAccess(0);
    equal_times.RecordAccess(0);
    checkNear(equal_times.GetIntervalAt(0).value(), 0);
    checkNear(equal_times.GetIntervalStdDev(), 0);

    for (int address_bytes : {0, 1})
    {
        I2CDevice::Config config{};
        config.reg_addr_bytes = address_bytes;
        config.auto_addr_inc = true;
        config.support_direct_read_from_default_reg = true;
        I2CDevice device(config);
        device.SetRegisterName(0, "Test");
        check(device.GetWriteStats().at(0).call_count == 0);
        for (double time : {1.0, 3.0, 7.0})
        {
            device.CallThisEachI2CAddrByteWrite(false);
            if (address_bytes > 0)
                device.DataByte(0, time - 0.1);
            device.DataByte(0x12, time);
            device.DataByte(0x34, time + 0.01);
            device.CallThisEachI2CStopCondition();
        }
        for (double time : {10.0, 15.0})
        {
            device.CallThisEachI2CAddrByteWrite(true);
            device.DataByte(0x56, time);
            device.DataByte(0x78, time + 0.01);
            device.CallThisEachI2CStopCondition();
        }
        const auto& writes = device.GetWriteStats().at(0);
        const auto& reads = device.GetReadStats().at(0);
        check(writes.call_count == 3 && reads.call_count == 2);
        check(writes.intervals.size() == 2 && reads.intervals.size() == 1);
        checkNear(writes.mean_interval, 3);
        checkNear(writes.GetIntervalStdDev(), 1);
        checkNear(reads.mean_interval, 5);
        checkNear(reads.GetIntervalAt(15).value(), 5);
        check(!reads.GetIntervalAt(14));
    }
    std::cout << "Access statistics checks passed.\n";
}
