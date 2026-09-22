#pragma once
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

    struct CommandStat
    {
        std::string command_name;
        uint32_t call_count = 0;
        double min_interval = std::numeric_limits<double>::max();
        double max_interval = 0.0;
        double last_timestamp = -1.0;
        std::vector<double> intervals;
        std::vector<double> access_timestamps;
        double mean_interval = 0.0;
        double interval_m2 = 0.0;

        void RecordAccess(double timestamp);
        double GetIntervalStdDev() const;
        std::optional<double> GetIntervalAt(double timestamp) const;
    };

    // i2c_device.hpp の RegisterInfo 内に追加するイメージ
    struct BitFieldInfo
    {
        std::string name;
        uint8_t byte_offset;
        uint8_t bit_offset;
        uint8_t bit_width;
        bool is_little_endian;
    };

    // 1つのレジスタが持つ情報（名前とデータ）
    struct RegisterInfo
    {
        std::string name = "";
        std::vector<uint8_t> data_w;
        std::vector<uint8_t> data_r;
        std::vector<bool> byte_has_written;
        std::vector<bool> byte_has_read;
        std::vector<BitFieldInfo> bit_fields_write; // write用ビット情報
        std::vector<BitFieldInfo> bit_fields_read;  // read用ビット情報
    };

    struct SnapshotView
    {
        const std::map<uint32_t, RegisterInfo>& registers;
        bool is_write;
        uint32_t changed_reg_addr;
        int32_t changed_index;
    };

    struct HistoryEntryView
    {
        double timestamp;
        bool is_write;
        uint32_t register_address;
        const RegisterInfo& register_info;
        std::uint64_t segment_id;
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
        uint32_t default_reg_addr; // support_direct_read_from_default_reg_が有効の時のみ、STOP conditionでreg_pointer_
                                   // = default_reg_addr_ となる
        uint32_t reg_map_size;
        bool is_little_endian;
    };

  private:
    Config config_;

    // トランザクションごとに変化する値（ランタイムステート）
    struct State
    {
        I2CBusCondition bus_condition = I2CBusCondition::STOP;
        uint32_t reg_pointer = 0;
        bool is_read = false;
        int write_data_count = 0;
        std::uint64_t segment_id = 0;
        bool is_new_register_addr_just_set =
            false; // コマンドID方式の時(=1つのコマンドIDに複数バイト紐づくとき、コマンド指定後の最初のライトもしくはリードの前に、RegisterInfoのdata_w、data_rをクリアする必要があるため、コマンドID指定直後にフラグを立てる)
    } state_;

    // レジスタアドレス毎の統計情報・コマンド名管理用
    struct Statistics
    {
        bool is_stat_counted = false;
        std::map<uint32_t, std::string> command_names;
        std::map<uint32_t, CommandStat> write_stats;
        std::map<uint32_t, CommandStat> read_stats;
    } stats_;

    // ある時点のレジスタマップ。write用とread用を分けておく
    std::map<uint32_t, RegisterInfo> registers_;

    struct Snapshot
    {
        double timestamp;
        std::map<uint32_t, RegisterInfo> registers;
        bool is_write;
        uint32_t changed_reg_addr;
        int32_t changed_index;
        std::uint64_t segment_id;
    };
    std::vector<Snapshot> history_;

  public:
    // コンストラクタが Config を受け取るように変更
    explicit I2CDevice(const Config& config);

    void CallThisEachI2CAddrByteWrite(bool is_read);
    void CallThisEachI2CStopCondition();
    void DataByte(uint8_t data, double timestamp);

    const std::string& GetDeviceName() const noexcept;
    SnapshotView GetSnapshotViewAt(double timestamp) const;
    std::size_t GetHistorySize() const
    {
        return history_.size();
    }
    HistoryEntryView GetHistoryEntry(std::size_t index) const;

    void SetRegisterName(uint32_t reg_addr, const std::string& name);
    const std::string& GetRegisterName(uint32_t reg_addr) const;

    void SetWriteRegisterBitField(uint32_t reg_addr, const BitFieldInfo& bitfield);
    void SetReadRegisterBitField(uint32_t reg_addr, const BitFieldInfo& bitfield);

    const std::vector<BitFieldInfo>& GetWriteRegisterBitField(uint32_t reg_addr) const;
    const std::vector<BitFieldInfo>& GetReadRegisterBitField(uint32_t reg_addr) const;

    int GetRegisterAddressBytes() const
    {
        return config_.reg_addr_bytes;
    }

    const std::map<uint32_t, CommandStat>& GetWriteStats() const
    {
        return stats_.write_stats;
    }
    const std::map<uint32_t, CommandStat>& GetReadStats() const
    {
        return stats_.read_stats;
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
