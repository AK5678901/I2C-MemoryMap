#include "i2c_device.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <set>
#include <string>
#include <utility>

void I2CDevice::RegisterAccessStats::RecordAccess(Timestamp timestamp)
{
    if (call_count > 0)
    {
        const double interval = TimeValue::durationSeconds(timestamp - last_timestamp);
        intervals.push_back(interval);
        min_interval = std::min(min_interval, interval);
        max_interval = std::max(max_interval, interval);
        // Welford's update avoids rescanning the log on every GUI frame.
        const double delta = interval - mean_interval;
        mean_interval += delta / static_cast<double>(intervals.size());
        interval_m2 += delta * (interval - mean_interval);
    }
    ++call_count;
    last_timestamp = timestamp;
    access_timestamps.push_back(timestamp);
}

std::optional<double> I2CDevice::RegisterAccessStats::GetIntervalAt(Timestamp timestamp) const
{
    const auto position = std::upper_bound(access_timestamps.begin(), access_timestamps.end(), timestamp);
    const auto count = static_cast<std::size_t>(std::distance(access_timestamps.begin(), position));
    if (count < 2)
        return std::nullopt;
    return intervals[count - 2];
}

double I2CDevice::RegisterAccessStats::GetIntervalStdDev() const
{
    // Population standard deviation over all observed intervals.
    return intervals.empty() ? 0.0 : std::sqrt(std::max(0.0, interval_m2 / static_cast<double>(intervals.size())));
}

I2CDevice::I2CDevice(const Config& config) : config_(config)
{
    // reg_addr_bytes から reg_map_size を安全に再計算して設定する場合の処理
    config_.reg_map_size = static_cast<uint32_t>(1) << (config.reg_addr_bytes * 8);
}

void I2CDevice::CallThisEachI2CAddrByteWrite(bool is_read)
{
    ++state_.segment_id;
    // バスコンディションの更新
    switch (state_.bus_condition)
    {
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
    state_.write_byte_count = 0;

    // Register
    // Address毎の統計情報集計用。1回のI2Cデバイスアドレスのアクセスにつき1回だけカウントするため、フラグをクリア
    stats_.access_recorded = false;

    // レジスタ値をクリアする条件1
    // レジスタアドレスのライト無しでいきなりリードされたときに、特定のレジスタアドレスの値を返すデバイスの処理
    if (state_.is_read && config_.support_direct_read_from_default_reg &&
        (state_.bus_condition == I2CBusCondition::START_Standard))
    {
        state_.current_register_address = config_.default_reg_addr;
        auto& value = GetOrCreateRegister(state_.current_register_address).value;
        value.read_data.clear();
        value.byte_has_read.clear();
    }

    // レジスタ値をクリアする条件2
    // アドレスバイトがない(レジスタレス)デバイスの場合、
    // 新しいトランザクション開始時にデータをクリアする
    if (config_.reg_addr_bytes == 0)
    {
        state_.current_register_address = 0;
        auto& value = GetOrCreateRegister(0).value;
        if (is_read)
        {
            value.read_data.clear();
            value.byte_has_read.clear();
        }
        else
        {
            value.write_data.clear();
            value.byte_has_written.clear();
        }
    }
}

void I2CDevice::CallThisEachI2CStopCondition()
{
    state_.bus_condition = I2CBusCondition::STOP;
}

void I2CDevice::ResetRuntime()
{
    state_ = State{};
    snapshots_.clear();
    snapshots_.shrink_to_fit();
    stats_ = Statistics{};
    for (auto& [address, reg] : registers_)
    {
        reg.value = RegisterValue{};
        stats_.write_access_stats.try_emplace(address);
        stats_.read_access_stats.try_emplace(address);
    }
}

// Write時の処理 アドレス構築と、Write用のレジスタマップを更新
void I2CDevice::DataByte(uint8_t data, Timestamp timestamp)
{

    // アドレス幅が0（レジスタレス）の場合の処理。ライト、リード兼用
    if (config_.reg_addr_bytes == 0)
    {
        state_.current_register_address = 0;
        auto& value = GetOrCreateRegister(state_.current_register_address).value;

        if (!stats_.access_recorded)
        {
            auto& stat = state_.is_read ? stats_.read_access_stats[0] : stats_.write_access_stats[0];
            stat.RecordAccess(timestamp);
            stats_.access_recorded = true;
        }

        if (state_.is_read)
        {
            value.read_data.push_back(data);
            value.byte_has_read.push_back(true);
        }
        else
        {
            value.write_data.push_back(data);
            value.byte_has_written.push_back(true);
        }
        RecordSnapshot(timestamp);
        return;
    }

    if (state_.is_read)
    {
        // Register Address毎の統計情報集計用
        // レジスタアドレスは確定済みなので、リードカウントを上げる
        if (!stats_.access_recorded)
        {
            auto& stat = stats_.read_access_stats[state_.current_register_address];
            stat.RecordAccess(timestamp);
            stats_.access_recorded = true;
        }

        auto& value = GetOrCreateRegister(state_.current_register_address).value;
        // レジスタ値をクリアする条件3
        // レジスタアドレス書き込み直後のデータなので、過去のデータをクリアする
        if (state_.clear_buffer_on_next_data_byte)
        {
            value.read_data.clear();
            value.byte_has_read.clear();
            state_.clear_buffer_on_next_data_byte = false;
        }

        value.read_data.push_back(data);
        value.byte_has_read.push_back(true);
        RecordSnapshot(timestamp);

        // オートインクリメントがONの時だけレジスタアドレスをインクリメントする
        if (config_.auto_addr_inc)
        {
            state_.current_register_address++;
            // レジスタアドレスが新しくなったので、フラグ立てて次の連続ライトの場合でも古いデータが消えるようにする
            state_.clear_buffer_on_next_data_byte = true;
        }
    }
    else
    {
        // Write時の処理
        // 1個目のライトデータの場合はI2Cアドレスバイト確定なのでポインタをクリア
        if (state_.write_byte_count == 0)
        {
            state_.current_register_address = 0;
        }

        // レジスタアドレス幅分のライトは、レジスタアドレスの書き込みとして扱う
        if (state_.write_byte_count < config_.reg_addr_bytes)
        {
            state_.current_register_address = (state_.current_register_address << 8) + data;
        }
        else
        {
            // それ以上のデータ書き込みは、本当のデータとして扱う

            // Register Address毎の統計情報集計用
            // レジスタアドレスは確定済みなので、ライトカウントを上げる
            if (!stats_.access_recorded)
            {
                auto& stat = stats_.write_access_stats[state_.current_register_address];
                stat.RecordAccess(timestamp);
                stats_.access_recorded = true;
            }

            auto& value = GetOrCreateRegister(state_.current_register_address).value;

            // レジスタ値をクリアする条件4
            // レジスタアドレス書き込み直後のデータなので、過去のデータをクリアする
            if (state_.clear_buffer_on_next_data_byte)
            {
                value.write_data.clear();
                value.byte_has_written.clear();
                state_.clear_buffer_on_next_data_byte = false;
            }
            value.write_data.push_back(data);
            value.byte_has_written.push_back(true);
            RecordSnapshot(timestamp);

            // オートインクリメントがONの時だけレジスタアドレスをインクリメントする
            if (config_.auto_addr_inc)
            {
                state_.current_register_address++;
                // レジスタアドレスが新しくなったので、フラグ立てて次の連続ライトの場合でも古いデータが消えるようにする
                state_.clear_buffer_on_next_data_byte = true;
            }
        }
        state_.write_byte_count++;

        // アドレス確定後の最初のデータで、以前のWriteまたはReadバッファをクリアする。
        state_.clear_buffer_on_next_data_byte =
            (state_.write_byte_count == config_.reg_addr_bytes);
    }
}

const std::string& I2CDevice::GetDeviceName() const noexcept
{
    return config_.device_name;
}

I2CDevice::Register& I2CDevice::GetOrCreateRegister(uint32_t address)
{
    auto [position, inserted] = registers_.try_emplace(address);
    auto& reg = position->second;
    if (inserted)
        initial_snapshot_.registers.emplace(address, RegisterSnapshot{reg.definition, reg.value});
    return reg;
}

void I2CDevice::RecordSnapshot(Timestamp timestamp)
{
    Snapshot snapshot;
    snapshot.timestamp = timestamp;
    snapshot.is_write = !state_.is_read;
    snapshot.updated_register_address = state_.current_register_address;
    snapshot.segment_id = state_.segment_id;
    for (const auto& [address, reg] : registers_)
        snapshot.registers.emplace(address, RegisterSnapshot{reg.definition, reg.value});
    const auto& value = snapshot.GetUpdatedRegister().value;
    const auto& data = snapshot.is_write ? value.write_data : value.read_data;
    snapshot.updated_byte_index = static_cast<int32_t>(data.size() - 1);
    snapshots_.push_back(std::move(snapshot));
}

const I2CDevice::Snapshot& I2CDevice::GetSnapshotAt(Timestamp timestamp) const
{
    static const Snapshot empty_snapshot;
    if (snapshots_.empty())
        return initial_snapshot_;
    const auto position = std::upper_bound(snapshots_.begin(), snapshots_.end(), timestamp,
        [](Timestamp time, const Snapshot& snapshot) { return time < snapshot.timestamp; });
    return position == snapshots_.begin() ? empty_snapshot : *std::prev(position);
}

const I2CDevice::Snapshot& I2CDevice::GetSnapshotByIndex(std::size_t index) const
{
    return snapshots_.at(index);
}

void I2CDevice::SetRegisterName(uint32_t address, const std::string& name)
{
    GetOrCreateRegister(address).definition.name = name;
    stats_.write_access_stats.try_emplace(address);
    stats_.read_access_stats.try_emplace(address);
}

const std::string& I2CDevice::GetRegisterName(uint32_t address) const
{
    static const std::string unknown_name = "-";
    const auto found = registers_.find(address);
    return found != registers_.end() ? found->second.definition.name : unknown_name;
}

void I2CDevice::SetWriteRegisterBitField(uint32_t address, const BitFieldDefinition& bitfield)
{
    GetOrCreateRegister(address).definition.write_bit_fields.push_back(bitfield);
}

void I2CDevice::SetReadRegisterBitField(uint32_t address, const BitFieldDefinition& bitfield)
{
    GetOrCreateRegister(address).definition.read_bit_fields.push_back(bitfield);
}

const std::vector<I2CDevice::BitFieldDefinition>& I2CDevice::GetWriteRegisterBitField(uint32_t address) const
{
    static const std::vector<BitFieldDefinition> empty_fields;
    const auto found = registers_.find(address);
    return found != registers_.end() ? found->second.definition.write_bit_fields : empty_fields;
}

const std::vector<I2CDevice::BitFieldDefinition>& I2CDevice::GetReadRegisterBitField(uint32_t address) const
{
    static const std::vector<BitFieldDefinition> empty_fields;
    const auto found = registers_.find(address);
    return found != registers_.end() ? found->second.definition.read_bit_fields : empty_fields;
}
