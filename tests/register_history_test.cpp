#include "i2c_device.hpp"

#include <iostream>
#include <stdexcept>

void check(bool condition)
{
    if (!condition)
        throw std::runtime_error("Register history check failed");
}

constexpr Timestamp seconds(int value) { return static_cast<Timestamp>(value) * 1000000000; }

int main()
{
    for (int address_bytes : {0, 1})
    {
        for (bool auto_increment : {false, true})
        {
            I2CDevice::Config config{};
            config.reg_addr_bytes = address_bytes;
            config.auto_addr_inc = auto_increment;
            config.support_direct_read_from_default_reg = true;
            I2CDevice device(config);
            check(device.GetHistorySize() == 0);
            device.CallThisEachI2CAddrByteWrite(false);
            if (address_bytes)
                device.DataByte(0, seconds(0));
            check(device.GetHistorySize() == 0); // Address bytes are not register updates.
            device.DataByte(0x12, seconds(1));
            device.DataByte(0x34, seconds(2));
            device.CallThisEachI2CStopCondition();
            device.CallThisEachI2CAddrByteWrite(true);
            device.DataByte(0x56, seconds(3));
            device.DataByte(0x78, seconds(3)); // Equal timestamps must keep capture order.
            device.CallThisEachI2CStopCondition();
            check(device.GetHistorySize() == 4);
            const auto first = device.GetHistoryEntry(0);
            const auto second = device.GetHistoryEntry(1);
            const auto third = device.GetHistoryEntry(2);
            const auto fourth = device.GetHistoryEntry(3);
            check(first.timestamp == seconds(1) && second.timestamp == seconds(2) &&
                  third.timestamp == seconds(3) && fourth.timestamp == seconds(3));
            check(first.is_write && second.is_write && !third.is_write && !fourth.is_write);
            check(first.segment_id == second.segment_id && third.segment_id == fourth.segment_id);
            check(first.segment_id != third.segment_id);
            check(first.register_info.data_w.back() == 0x12 && second.register_info.data_w.back() == 0x34 &&
                  third.register_info.data_r.back() == 0x56 && fourth.register_info.data_r.back() == 0x78);
            check(first.register_address == 0 && third.register_address == 0);
            const bool incremented = address_bytes && auto_increment;
            check(second.register_address == (incremented ? 1U : 0U));
            check(first.register_info.data_w == std::vector<uint8_t>{0x12});
            check(second.register_info.data_w ==
                  (incremented ? std::vector<uint8_t>{0x34} : std::vector<uint8_t>{0x12, 0x34}));
            check(third.register_info.data_r == std::vector<uint8_t>{0x56});
            check(fourth.register_info.data_r ==
                  (incremented ? std::vector<uint8_t>{0x78} : std::vector<uint8_t>{0x56, 0x78}));
            check(device.GetSnapshotViewAt(seconds(1) / 2).registers.empty());
            check(device.GetSnapshotViewAt(seconds(1)).registers.at(0).data_w == first.register_info.data_w);
            check(!device.GetSnapshotViewAt(seconds(3)).is_write);
        }
    }
    I2CDevice::Config repeated_start_config{};
    repeated_start_config.reg_addr_bytes = 0;
    I2CDevice repeated_start_device(repeated_start_config);
    repeated_start_device.CallThisEachI2CAddrByteWrite(false);
    repeated_start_device.DataByte(0xAA, seconds(1));
    repeated_start_device.CallThisEachI2CAddrByteWrite(true);
    repeated_start_device.DataByte(0xBB, seconds(2));
    check(repeated_start_device.GetHistoryEntry(0).segment_id !=
          repeated_start_device.GetHistoryEntry(1).segment_id);
    repeated_start_device.SetRegisterName(0, "Test register");
    repeated_start_device.ResetRuntime();
    check(repeated_start_device.GetHistorySize() == 0);
    check(repeated_start_device.GetSnapshotViewAt(seconds(10)).registers.at(0).data_w.empty());
    check(repeated_start_device.GetSnapshotViewAt(seconds(10)).registers.at(0).data_r.empty());
    check(repeated_start_device.GetRegisterName(0) == "Test register");
    repeated_start_device.CallThisEachI2CAddrByteWrite(false);
    repeated_start_device.DataByte(0xCC, seconds(4));
    check(repeated_start_device.GetHistorySize() == 1);
    check(repeated_start_device.GetHistoryEntry(0).register_info.data_w == std::vector<uint8_t>{0xCC});
    std::cout << "Register history checks passed.\n";
}
