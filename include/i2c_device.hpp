#pragma once
#include <cstdint>
#include <vector>
#include <map>
#include <memory>
#include <set>
#include <string>

class I2CDevice {
protected:
    std::string device_name_;
    uint8_t i2c_dev_addr_;
    int reg_addr_bytes_;
    uint32_t reg_map_size_;
    bool is_read_;

public:
    // 上位側が安全に受け取るためのデータ構造
    struct SnapshotView {
        std::vector<uint8_t> memory_w;
        std::vector<uint8_t> memory_r;
        std::vector<bool> byte_has_written; // Writeアクセス履歴
        std::vector<bool> byte_has_read;    // Readアクセス履歴
        bool is_write;         // 最後に更新されたのがWriteかどうか
        int32_t changed_index;  // 最後に更新されたインデックス
    };

private:
    //アドレスポインタ。write時に更新される
    int writeDataCount_ = 0;
    uint32_t reg_pointer_; //レジスタアドレス

    //ある時点のレジスタマップ。write用とread用を分けておく
    std::vector<uint8_t> reg_map_w_ = std::vector<uint8_t>(65536, 0x00);
    std::vector<uint8_t> reg_map_r_ = std::vector<uint8_t>(65536, 0x00);

    //バイトごとのアクセス履歴の現在状態
    std::vector<bool> byte_has_written_ = std::vector<bool>(65536, false);
    std::vector<bool> byte_has_read_ = std::vector<bool>(65536, false);

    //レジスタマップが更新されたとき、reg_map_w_とreg_map_r_に時刻を付与して保存する。上位からの検索時に使用する
    struct Snapshot {
        double timestamp;
        std::vector<uint8_t> memory_w;
        std::vector<uint8_t> memory_r;
        std::vector<bool> byte_has_written; //履歴時点のWriteアクセス状態
        std::vector<bool> byte_has_read;    //履歴時点のReadアクセス状態
        bool is_write; // true: writeによる更新, false: readによる更新
        int32_t changed_index; // 変更された単一のインデックス
    };
    std::vector<Snapshot> history_; // 時刻とメモリ状態の履歴

public:
    I2CDevice(std::string device_name, uint8_t i2c_dev_addr, int reg_addr_bytes);
    
    void CallThisEachI2CAddrByteWrite(bool is_read);
    void DataByte(uint8_t data, double timestamp);

    std::string GetDeviceName() const;
    SnapshotView GetSnapshotViewAt(double timestamp) const;
    std::vector<double> GetAllTimestamp() const;

};

class I2CDeviceManager {
private:
    // アドレスをキーにしてデバイスを保持
    std::map<uint8_t, std::unique_ptr<I2CDevice>> devices;

public:
    // デバイスの登録
    void RegisterDevice(std::string device_name, uint8_t i2c_dev_addr, int reg_addr_bytes) {
        devices[i2c_dev_addr] = std::make_unique<I2CDevice>(device_name, i2c_dev_addr, reg_addr_bytes);
    }

    // 特定のアドレスのデバイスを取得
    I2CDevice* GetDevice(uint8_t i2c_dev_addr) {
        if (devices.find(i2c_dev_addr) != devices.end()) {
            return devices[i2c_dev_addr].get();
        }
        return nullptr;
    }
    
    auto& GetAllDevices() { return devices; }
};

