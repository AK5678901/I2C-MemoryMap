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
            check(device.GetSnapshotCount() == 0);
            device.CallThisEachI2CAddrByteWrite(false);
            if (address_bytes)
                device.DataByte(0, seconds(0));
            check(device.GetSnapshotCount() == 0); // Address bytes are not register updates.
            device.DataByte(0x12, seconds(1));
            device.DataByte(0x34, seconds(2));
            device.CallThisEachI2CStopCondition();
            device.CallThisEachI2CAddrByteWrite(true);
            device.DataByte(0x56, seconds(3));
            device.DataByte(0x78, seconds(3)); // Equal timestamps must keep capture order.
            device.CallThisEachI2CStopCondition();
            check(device.GetSnapshotCount() == 4);
            const auto& first = device.GetSnapshotByIndex(0);
            const auto& second = device.GetSnapshotByIndex(1);
            const auto& third = device.GetSnapshotByIndex(2);
            const auto& fourth = device.GetSnapshotByIndex(3);
            check(first.timestamp == seconds(1) && second.timestamp == seconds(2) &&
                  third.timestamp == seconds(3) && fourth.timestamp == seconds(3));
            check(first.is_write && second.is_write && !third.is_write && !fourth.is_write);
            check(first.segment_id == second.segment_id && third.segment_id == fourth.segment_id);
            check(first.segment_id != third.segment_id);
            check(first.GetUpdatedRegister().value.write_data.back() == 0x12 && second.GetUpdatedRegister().value.write_data.back() == 0x34 &&
                  third.GetUpdatedRegister().value.read_data.back() == 0x56 && fourth.GetUpdatedRegister().value.read_data.back() == 0x78);
            check(first.updated_register_address == 0 && third.updated_register_address == 0);
            const bool incremented = address_bytes && auto_increment;
            check(second.updated_register_address == (incremented ? 1U : 0U));
            check(first.updated_byte_index == 0 && third.updated_byte_index == 0);
            check(second.updated_byte_index == (incremented ? 0 : 1));
            check(fourth.updated_byte_index == (incremented ? 0 : 1));
            check(first.GetUpdatedRegister().value.write_data == std::vector<uint8_t>{0x12});
            check(second.GetUpdatedRegister().value.write_data ==
                  (incremented ? std::vector<uint8_t>{0x34} : std::vector<uint8_t>{0x12, 0x34}));
            check(third.GetUpdatedRegister().value.read_data == std::vector<uint8_t>{0x56});
            check(fourth.GetUpdatedRegister().value.read_data ==
                  (incremented ? std::vector<uint8_t>{0x78} : std::vector<uint8_t>{0x56, 0x78}));
            check(device.GetSnapshotAt(seconds(1) / 2).registers.empty());
            check(device.GetSnapshotAt(seconds(1)).registers.at(0).value.write_data == first.GetUpdatedRegister().value.write_data);
            check(!device.GetSnapshotAt(seconds(3)).is_write);
            check(device.GetSnapshotAt(seconds(3)).updated_byte_index == fourth.updated_byte_index);
            check(device.GetSnapshotAt(seconds(1)).timestamp == first.timestamp);
            check(first.GetUpdatedRegister().definition.name == "-");
        }
    }
    I2CDevice::Config repeated_start_config{};
    repeated_start_config.reg_addr_bytes = 0;
    I2CDevice repeated_start_device(repeated_start_config);
    repeated_start_device.CallThisEachI2CAddrByteWrite(false);
    repeated_start_device.DataByte(0xAA, seconds(1));
    repeated_start_device.CallThisEachI2CAddrByteWrite(true);
    repeated_start_device.DataByte(0xBB, seconds(2));
    check(repeated_start_device.GetSnapshotByIndex(0).segment_id !=
          repeated_start_device.GetSnapshotByIndex(1).segment_id);
    repeated_start_device.SetRegisterName(0, "Test register");
    repeated_start_device.ResetRuntime();
    check(repeated_start_device.GetSnapshotCount() == 0);
    check(repeated_start_device.GetSnapshotAt(seconds(10)).registers.at(0).value.write_data.empty());
    check(repeated_start_device.GetSnapshotAt(seconds(10)).registers.at(0).value.read_data.empty());
    check(repeated_start_device.GetRegisterName(0) == "Test register");
    repeated_start_device.CallThisEachI2CAddrByteWrite(false);
    repeated_start_device.DataByte(0xCC, seconds(4));
    check(repeated_start_device.GetSnapshotCount() == 1);
    check(repeated_start_device.GetSnapshotByIndex(0).GetUpdatedRegister().value.write_data == std::vector<uint8_t>{0xCC});

    I2CDevice defined_device(repeated_start_config);
    defined_device.SetRegisterName(0, "Control");
    defined_device.SetWriteRegisterBitField(0, {"Enable", 0, 0, 1, true});
    defined_device.SetReadRegisterBitField(0, {"Ready", 0, 1, 1, true});
    const auto* definition = &defined_device.GetSnapshotAt(0).registers.at(0).definition;
    defined_device.CallThisEachI2CAddrByteWrite(false);
    defined_device.DataByte(0x01, seconds(1));
    defined_device.DataByte(0x02, seconds(2));
    // Grow both containers: historical values stay independent and definition references stay valid.
    defined_device.SetRegisterName(1, "Status");
    for (int i = 3; i <= 32; ++i)
        defined_device.DataByte(static_cast<uint8_t>(i), seconds(i));
    const auto& first_register = defined_device.GetSnapshotByIndex(0).registers.at(0);
    const auto& last_register = defined_device.GetSnapshotByIndex(31).registers.at(0);
    check(&first_register.definition == definition && &last_register.definition == definition);
    check(&first_register.definition.write_bit_fields == &defined_device.GetWriteRegisterBitField(0));
    check(&first_register.definition.read_bit_fields == &defined_device.GetReadRegisterBitField(0));
    check(first_register.value.write_data == std::vector<uint8_t>{0x01});
    check(last_register.value.write_data.size() == 32);
    check(!defined_device.GetSnapshotAt(seconds(1)).registers.contains(1));
    check(defined_device.GetSnapshotAt(seconds(32)).registers.contains(1));
    defined_device.ResetRuntime();
    const auto& initial_register = defined_device.GetSnapshotAt(0).registers.at(0);
    check(&initial_register.definition == definition);
    check(initial_register.definition.name == "Control");
    check(initial_register.definition.write_bit_fields.at(0).name == "Enable");
    check(initial_register.definition.read_bit_fields.at(0).name == "Ready");
    check(initial_register.value.write_data.empty() && initial_register.value.read_data.empty());
    check(defined_device.GetWriteStats().at(0).call_count == 0);

    // Large maps must not be copied into every byte's timeline entry.
    I2CDevice::Config large_config{};
    large_config.reg_addr_bytes = 2;
    large_config.auto_addr_inc = true;
    large_config.support_direct_read_from_default_reg = true;
    I2CDevice large_device(large_config);
    for (uint32_t address = 0; address < 1024; ++address)
        large_device.SetRegisterName(address, "Register");
    for (int i = 0; i < 10000; ++i)
    {
        large_device.CallThisEachI2CAddrByteWrite(false);
        large_device.DataByte(0, 0);
        large_device.DataByte(static_cast<uint8_t>(i % 2), 0);
        large_device.DataByte(static_cast<uint8_t>(i), seconds(i + 1));
        large_device.CallThisEachI2CStopCondition();
        check(large_device.GetSnapshotByIndex(i).registers.size() == 1);
    }
    const auto& past = large_device.GetSnapshotAt(seconds(5000));
    check(past.registers.size() == 1024);
    check(past.registers.at(0).value.write_data == std::vector<uint8_t>{static_cast<uint8_t>(4998)});
    check(past.registers.at(1).value.write_data == std::vector<uint8_t>{static_cast<uint8_t>(4999)});
    check(large_device.GetSnapshotAt(seconds(2)).registers.at(0).value.write_data == std::vector<uint8_t>{0});
    // A direct-read clear can affect a different register from the next data byte.
    large_device.CallThisEachI2CAddrByteWrite(true);
    large_device.DataByte(0xAA, seconds(10001));
    large_device.CallThisEachI2CStopCondition();
    large_device.CallThisEachI2CAddrByteWrite(true); // Clear register 0 without recording a byte.
    large_device.CallThisEachI2CAddrByteWrite(false);
    large_device.DataByte(0, 0);
    large_device.DataByte(1, 0);
    large_device.DataByte(0xBB, seconds(10002));
    check(large_device.GetSnapshotAt(seconds(10001)).registers.at(0).value.read_data == std::vector<uint8_t>{0xAA});
    check(large_device.GetSnapshotAt(seconds(10002)).registers.at(0).value.read_data.empty());
    large_device.ResetRuntime();
    check(large_device.GetSnapshotAt(seconds(10002)).registers.at(1).value.write_data.empty());
    std::cout << "Register history checks passed.\n";
}
