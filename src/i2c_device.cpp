#include "i2c_device.hpp"

#include <set>
#include <algorithm>
#include <string>
#include <format>

I2CDevice::I2CDevice(const Config& config)
    : config_(config) {
    // reg_addr_bytes から reg_map_size を安全に再計算して設定する場合の処理
    config_.reg_map_size = static_cast<uint32_t>(1) << (config.reg_addr_bytes * 8);
}

void I2CDevice::CallThisEachI2CAddrByteWrite(bool is_read){
    // バスコンディションの更新
    switch (state_.bus_condition) {
        case I2CBusCondition::STOP:
            state_.bus_condition = I2CBusCondition::START_Standard;
            break;
            
        case I2CBusCondition::START_Standard:
            state_.bus_condition = I2CBusCondition::START_Repeated;
            break;
            
        case I2CBusCondition::START_Repeated:
            // repeated STARTが再度来たら何もしない
            break;
    }

    state_.is_read = is_read;
    state_.write_data_count = 0;

    // Register Address毎の統計情報集計用。1回のI2Cデバイスアドレスのアクセスにつき1回だけカウントするため、フラグをクリア
    stats_.is_stat_counted = false;

    // レジスタ値をクリアする条件1
    // レジスタアドレスのライト無しでいきなりリードされたときに、特定のレジスタアドレスの値を返すデバイスの処理
    if (state_.is_read && config_.support_direct_read_from_default_reg && (state_.bus_condition == I2CBusCondition::START_Standard)){
        state_.reg_pointer = config_.default_reg_addr;
        auto& reg_info = registers_[state_.reg_pointer];
        reg_info.data_r.clear();
        reg_info.byte_has_read.clear();
    }

    // レジスタ値をクリアする条件2
    // アドレスバイトがない(レジスタレス)デバイスの場合、
    // 新しいトランザクション開始時にデータをクリアする
    if (config_.reg_addr_bytes == 0) {
        state_.reg_pointer = 0;
        auto& reg_info = registers_[0];
        if (is_read) {
            reg_info.data_r.clear();
            reg_info.byte_has_read.clear();
        } else {
            reg_info.data_w.clear();
            reg_info.byte_has_written.clear();
        }
    }
}

void I2CDevice::CallThisEachI2CStopCondition() {
    state_.bus_condition = I2CBusCondition::STOP;
}

// Write時の処理 アドレス構築と、Write用のレジスタマップを更新
void I2CDevice::DataByte(uint8_t data, double timestamp) {

    // アドレス幅が0（レジスタレス）の場合の処理。ライト、リード兼用
    if (config_.reg_addr_bytes == 0) {
        state_.reg_pointer = 0;
        auto& reg_info = registers_[state_.reg_pointer];
        
        if (state_.is_read) {
            reg_info.data_r.push_back(data);
            reg_info.byte_has_read.push_back(true);
        } else {
            reg_info.data_w.push_back(data);
            reg_info.byte_has_written.push_back(true);
        }
        history_.push_back({timestamp, registers_, state_.is_read, state_.reg_pointer, 0});
        return;
    }

    if (state_.is_read){
        // Register Address毎の統計情報集計用
        // 既にreg_pointer_は構築されているので、リードカウントを上げる
        if (!stats_.is_stat_counted){
            auto& stat = stats_.read_stats[state_.reg_pointer];
            stat.call_count++;
            if (stat.last_timestamp >= 0.0) {
                double interval = timestamp - stat.last_timestamp;
                stat.intervals.push_back(interval);
                stat.min_interval = std::min(stat.min_interval, interval);
                stat.max_interval = std::max(stat.max_interval, interval);
            }
            stat.last_timestamp = timestamp;
            stats_.is_stat_counted = true;
        }

        auto& reg_info = registers_[state_.reg_pointer];
        // レジスタ値をクリアする条件3
        // レジスタアドレス書き込み直後のデータなので、過去のデータをクリアする
        if (state_.is_new_register_addr_just_set){
            reg_info.data_r.clear();
            reg_info.byte_has_read.clear();
            state_.is_new_register_addr_just_set = false;
        }

        reg_info.data_r.push_back(data);
        reg_info.byte_has_read.push_back(true);
        history_.push_back({timestamp, registers_, false, state_.reg_pointer, 0});

        // オートインクリメントがONの時だけレジスタアドレスをインクリメントする
        if (config_.auto_addr_inc){
            state_.reg_pointer++;
            // レジスタアドレスが新しくなったので、フラグ立てて次の連続ライトの場合でも古いデータが消えるようにする
            state_.is_new_register_addr_just_set = true;
        }
    } else {
        // Write時の処理
        // 1個目のライトデータの場合はI2Cアドレスバイト確定なのでポインタをクリア
        if (state_.write_data_count == 0){
            state_.reg_pointer = 0;
        }

        // レジスタアドレス幅分のライトは、レジスタアドレスの書き込みとして扱う
        if (state_.write_data_count < config_.reg_addr_bytes){
            state_.reg_pointer = (state_.reg_pointer << 8) + data;
        } else {
            // それ以上のデータ書き込みは、本当のデータとして扱う

            // Register Address毎の統計情報集計用
            // 既にreg_pointer_は構築されているので、ライトカウントを上げる
            if (!stats_.is_stat_counted){
                auto& stat = stats_.write_stats[state_.reg_pointer];
                stat.call_count++;
                if (stat.last_timestamp >= 0.0) {
                    double interval = timestamp - stat.last_timestamp;
                    stat.intervals.push_back(interval);
                    stat.min_interval = std::min(stat.min_interval, interval);
                    stat.max_interval = std::max(stat.max_interval, interval);
                }
                stat.last_timestamp = timestamp;
                stats_.is_stat_counted = true;
            }

            auto& reg_info = registers_[state_.reg_pointer];

            // レジスタ値をクリアする条件4
            // レジスタアドレス書き込み直後のデータなので、過去のデータをクリアする
            if (state_.is_new_register_addr_just_set){
                reg_info.data_w.clear();
                reg_info.byte_has_written.clear();
                state_.is_new_register_addr_just_set = false;
            }
            reg_info.data_w.push_back(data);
            reg_info.byte_has_written.push_back(true);
            history_.push_back({timestamp, registers_, true, state_.reg_pointer, 0});

            // オートインクリメントがONの時だけレジスタアドレスをインクリメントする
            if (config_.auto_addr_inc){
                state_.reg_pointer++;
                // レジスタアドレスが新しくなったので、フラグ立てて次の連続ライトの場合でも古いデータが消えるようにする
                state_.is_new_register_addr_just_set = true;
            }
        }
        state_.write_data_count++;

        state_.is_new_register_addr_just_set = (state_.write_data_count == config_.reg_addr_bytes); // コマンドID方式の時(=1つのコマンドIDに複数バイト紐づくとき、コマンド指定後の最初のライトもしくはリードの前に、RegisterInfoのdata_w、data_rをクリアする必要があるため、コマンドID指定直後にフラグを立てる)
    }
}

std::string I2CDevice::GetDeviceName() const{
    return config_.device_name;
}

// 指定時刻以下の最新のスナップショットを取得
I2CDevice::SnapshotView I2CDevice::GetSnapshotViewAt(double timestamp) const {
    // 履歴がない場合は現在のマップを返す
    if (history_.empty()) return SnapshotView{registers_, false, 0, -1};

    // 指定時刻「以下」の要素のうち、最も右側（時刻が一番大きいもの＝直近のもの）を探す
    auto it = std::upper_bound(history_.begin(), history_.end(), timestamp, 
        [](double t, const Snapshot& s) { return t < s.timestamp; });
    
    // もし指定時刻が「最初の履歴の時刻」よりも前なら、初期状態（まっさらな状態）を返す
    if (it == history_.begin()) {
        return SnapshotView{{}, false, 0, -1};
    }
    
    // it が指すのは「timestamp を超える最初の要素」なので、
    // その 1つ手前 (`std::prev(it)`) が「timestamp 以下の最新の変更」になる
    const auto& view = std::prev(it);
    return SnapshotView{view->registers, view->is_write, view->changed_reg_addr, view->changed_index};
}

std::vector<double> I2CDevice::GetAllTimestamp() const {
    std::vector<double> ts;
    for (const auto& ss : history_){
        ts.push_back(ss.timestamp);
    }
    return ts;
}

void I2CDevice::SetRegisterName(uint32_t reg_addr, const std::string& name) {
    stats_.command_names[reg_addr] = name;
    stats_.write_stats[reg_addr].command_name = name;
    stats_.read_stats[reg_addr].command_name = name;
    registers_[reg_addr].name = name;
}

std::string I2CDevice::GetRegisterName(uint32_t reg_addr) const {
    if (stats_.command_names.contains(reg_addr)) {
        return stats_.command_names.at(reg_addr);
    }
    return "-";
}

void I2CDevice::SetWriteRegisterBitField(uint32_t reg_addr, const BitFieldInfo& bitfield) {
    registers_[reg_addr].bit_fields_write.push_back(bitfield);
}

void I2CDevice::SetReadRegisterBitField(uint32_t reg_addr, const BitFieldInfo& bitfield) {
    registers_[reg_addr].bit_fields_read.push_back(bitfield);
}

std::vector<I2CDevice::BitFieldInfo> I2CDevice::GetWriteRegisterBitField(uint32_t reg_addr) const {
    auto it = registers_.find(reg_addr);
    if (it != registers_.end()) {
        return it->second.bit_fields_write;
    }
    return {}; // 存在しない場合は空を返す
}

std::vector<I2CDevice::BitFieldInfo> I2CDevice::GetReadRegisterBitField(uint32_t reg_addr) const {
    auto it = registers_.find(reg_addr);
    if (it != registers_.end()) {
        return it->second.bit_fields_read;
    }
    return {};
}