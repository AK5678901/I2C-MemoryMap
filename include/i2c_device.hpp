#pragma once
#include "time_value.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

class I2CDevice
{
  public:
    enum class I2CBusCondition
    {
        START_Standard,
        START_Repeated,
        STOP
    };

    struct RegisterAccessStats
    {
        uint32_t call_count = 0;
        double min_interval = std::numeric_limits<double>::max();
        double max_interval = 0.0;
        Timestamp last_timestamp = 0;
        std::vector<double> intervals;
        std::vector<Timestamp> access_timestamps;
        double mean_interval = 0.0;
        double interval_m2 = 0.0;

        void RecordAccess(Timestamp timestamp);
        double GetIntervalStdDev() const;
        std::optional<double> GetIntervalAt(Timestamp timestamp) const;
    };

    struct BitFieldDefinition
    {
        std::string name;
        uint8_t byte_offset;
        uint8_t bit_offset;
        uint8_t bit_width;
        bool is_little_endian;
    };

    struct RegisterDefinition
    {
        std::string name = "-";
        std::vector<BitFieldDefinition> write_bit_fields;
        std::vector<BitFieldDefinition> read_bit_fields;
    };

    struct RegisterValue
    {
        std::vector<uint8_t> write_data;
        std::vector<uint8_t> read_data;
        std::vector<bool> byte_has_written;
        std::vector<bool> byte_has_read;
    };

    struct Register
    {
        RegisterDefinition definition;
        RegisterValue value;
    };

    // Own the historical value, but share the definition owned by the device's Register.
    struct RegisterSnapshot
    {
        const RegisterDefinition& definition;
        RegisterValue value;
    };

    struct Snapshot
    {
        Timestamp timestamp{0};
        std::map<uint32_t, RegisterSnapshot> registers;
        bool is_write{false};
        uint32_t updated_register_address{0};
        int32_t updated_byte_index{-1};
        std::uint64_t segment_id{0};

        const RegisterSnapshot& GetUpdatedRegister() const
        {
            return registers.at(updated_register_address);
        }
    };

    // 一度設定されたら変わらない値（コンフィグ）
    // 外部から設定しやすくするために public に移動
    struct Config
    {
        std::string device_name;
        uint8_t i2c_dev_addr;
        int reg_addr_bytes;
        bool auto_addr_inc;
        bool support_direct_read_from_default_reg;
        uint32_t default_reg_addr; // STOP後の直接Readで使用するレジスタアドレス
        uint32_t reg_map_size;
        bool is_little_endian;
    };

  private:
    Config config_;

    // トランザクションごとに変化する値（ランタイムステート）
    struct State
    {
        I2CBusCondition bus_condition = I2CBusCondition::STOP;
        uint32_t current_register_address = 0;
        bool is_read = false;
        int write_byte_count = 0; // レジスタアドレスのバイトも含む
        std::uint64_t segment_id = 0;
        bool clear_buffer_on_next_data_byte = false;
    } state_;

    // Register access statistics and bookkeeping for collecting them.
    struct Statistics
    {
        bool access_recorded = false;
        std::map<uint32_t, RegisterAccessStats> write_access_stats;
        std::map<uint32_t, RegisterAccessStats> read_access_stats;
    } stats_;

    // Registers outlive the snapshots that refer to their definitions.
    std::map<uint32_t, Register> registers_;
    Snapshot initial_snapshot_; // 履歴がないときに返す、定義と空の値を持つ初期状態
    std::vector<Snapshot> snapshots_;

    Register& GetOrCreateRegister(uint32_t address);
    void RecordSnapshot(Timestamp timestamp);

  public:
    // コンストラクタが Config を受け取るように変更
    explicit I2CDevice(const Config& config);
    I2CDevice(const I2CDevice&) = delete;
    I2CDevice& operator=(const I2CDevice&) = delete;
    I2CDevice(I2CDevice&&) = delete;
    I2CDevice& operator=(I2CDevice&&) = delete;

    void CallThisEachI2CAddrByteWrite(bool is_read);
    void CallThisEachI2CStopCondition();
    void ResetRuntime();
    void DataByte(uint8_t data, Timestamp timestamp);

    const std::string& GetDeviceName() const noexcept;
    // Snapshot references must be retrieved again after data is appended or reset.
    const Snapshot& GetSnapshotAt(Timestamp timestamp) const;
    std::size_t GetSnapshotCount() const
    {
        return snapshots_.size();
    }
    const Snapshot& GetSnapshotByIndex(std::size_t index) const;

    void SetRegisterName(uint32_t reg_addr, const std::string& name);
    const std::string& GetRegisterName(uint32_t reg_addr) const;

    void SetWriteRegisterBitField(uint32_t reg_addr, const BitFieldDefinition& bitfield);
    void SetReadRegisterBitField(uint32_t reg_addr, const BitFieldDefinition& bitfield);

    const std::vector<BitFieldDefinition>& GetWriteRegisterBitField(uint32_t reg_addr) const;
    const std::vector<BitFieldDefinition>& GetReadRegisterBitField(uint32_t reg_addr) const;

    int GetRegisterAddressBytes() const
    {
        return config_.reg_addr_bytes;
    }

    const std::map<uint32_t, RegisterAccessStats>& GetWriteStats() const
    {
        return stats_.write_access_stats;
    }
    const std::map<uint32_t, RegisterAccessStats>& GetReadStats() const
    {
        return stats_.read_access_stats;
    }
};

class I2CDeviceManager
{
  private:
    // アドレスをキーにしてデバイスを保持
    std::map<uint8_t, std::unique_ptr<I2CDevice>> devices_;

  public:
    // デバイスの登録
    I2CDevice* RegisterDevice(const I2CDevice::Config& config)
    {
        uint8_t addr = config.i2c_dev_addr;
        devices_[addr] = std::make_unique<I2CDevice>(config);
        return devices_[addr].get();
    }

    // 特定のアドレスのデバイスを取得
    I2CDevice* GetDevice(uint8_t i2c_dev_addr)
    {
        auto it = devices_.find(i2c_dev_addr);
        if (it != devices_.end())
        {
            return it->second.get();
        }
        return nullptr;
    }

    const std::map<uint8_t, std::unique_ptr<I2CDevice>>& GetAllDevices() const
    {
        return devices_;
    }
};
