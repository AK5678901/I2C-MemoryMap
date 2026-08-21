#include "i2c_device.hpp"

#include <set>
#include <algorithm>
#include <string>

I2CDevice::I2CDevice(std::string device_name, uint8_t i2c_dev_addr, int reg_addr_bytes) :
    device_name_(device_name),
    i2c_dev_addr_(i2c_dev_addr),
    reg_addr_bytes_(reg_addr_bytes),
    reg_map_size_(static_cast<uint32_t>(1) << (reg_addr_bytes * 8)),
    reg_pointer_(0),
    is_read_(false) {}

void I2CDevice::CallThisEachI2CAddrByteWrite(bool is_read){
    is_read_ = is_read;
    writeDataCount_ = 0;
}

// Write時の処理 アドレス構築と、Write用のレジスタマップを更新
void I2CDevice::DataByte(uint8_t data, double timestamp) {
    if (is_read_){
        // Read時の処理 Read用のレジスタマップを更新
        reg_map_r_[reg_pointer_] = data;
        byte_has_read_[reg_pointer_] = true;  // ★ Readフラグを立てる
        history_.push_back({timestamp, reg_map_w_, reg_map_r_, byte_has_written_, byte_has_read_, false, static_cast<int32_t>(reg_pointer_)});
        reg_pointer_++;
    }else{
        // Write時の処理
        // 1個目のライトデータの場合はI2Cアドレスバイト確定なのでポインタをクリア
        if (writeDataCount_ == 0){
            reg_pointer_ = 0;
        }

        // レジスタアドレス幅分のライトは、レジスタアドレスの書き込みとして扱う
        if (writeDataCount_ < reg_addr_bytes_){
            reg_pointer_ = (reg_pointer_ << 8) + data;
        }else{
            // それ以上のデータ書き込みは、本当のデータとして扱う
            reg_map_w_[reg_pointer_] = data;
            byte_has_written_[reg_pointer_] = true; // ★ Writeフラグを立てる
            history_.push_back({timestamp, reg_map_w_, reg_map_r_, byte_has_written_, byte_has_read_, true, static_cast<int32_t>(reg_pointer_)});
            reg_pointer_++;
        }
        writeDataCount_++;
    }
}

std::string I2CDevice::GetDeviceName() const{
    return device_name_;
}

// 指定時刻以下の最新のスナップショット（の memory_w）を取得
I2CDevice::SnapshotView I2CDevice::GetSnapshotViewAt(double timestamp) const {
    if (history_.empty()) return SnapshotView{reg_map_w_, reg_map_r_, byte_has_written_, byte_has_read_, false, -1}; // 履歴がない場合は現在のマップを返す

    // 指定時刻「以下」の要素のうち、最も右側（時刻が一番大きいもの＝直近のもの）を探す
    auto it = std::upper_bound(history_.begin(), history_.end(), timestamp, 
        [](double t, const Snapshot& s) { return t < s.timestamp; });
    
    // もし指定時刻が「最初の履歴の時刻」よりも前なら、初期状態（まっさらな状態）を返す
    if (it == history_.begin()) {
        static std::vector<uint8_t> empty_w(reg_map_size_, 0);
        static std::vector<uint8_t> empty_r(reg_map_size_, 0);
        static std::vector<bool> empty_written(reg_map_size_, false);
        static std::vector<bool> empty_read(reg_map_size_, false);
        return SnapshotView{empty_w, empty_r, empty_written, empty_read, false, -1};
    }
    
    // it が指すのは「timestamp を超える最初の要素」なので、
    // その 1つ手前 (`std::prev(it)`) が「timestamp 以下の最新の変更」になる
    const auto& view = std::prev(it);
    return SnapshotView{view->memory_w, view->memory_r, view->byte_has_written, view->byte_has_read, view->is_write, view->changed_index};
}

std::vector<double> I2CDevice::GetAllTimestamp() const {
    std::vector<double> ts;
    for (auto& ss : history_){
        ts.push_back(ss.timestamp);
    }
    return ts;
}
