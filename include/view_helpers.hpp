#pragma once

#include "i2c_device.hpp"

#include "imgui.h"

#include <array>
#include <cfloat>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ViewHelpers
{
inline constexpr auto table_flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                    ImGuiTableFlags_ScrollX | ImGuiTableFlags_Resizable;

template <std::size_t N>
using TimelineFilters = std::array<std::array<char, 64>, N>;

template <std::size_t N>
bool renderTimelineFilterRow(TimelineFilters<N>& filters)
{
    bool changed = false;
    ImGui::TableNextRow();
    for (std::size_t index = 0; index < N; ++index)
    {
        ImGui::TableSetColumnIndex(static_cast<int>(index));
        ImGui::PushID(static_cast<int>(index));
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputTextWithHint("##Filter", "Filter", filters[index].data(), filters[index].size()))
            changed = true;
        ImGui::PopID();
    }
    return changed;
}

bool matchesTimelineFilter(const char* filter, std::string_view value);
std::string formatTimelineBytes(const std::vector<std::uint8_t>& data);

struct TransactionRow
{
    Timestamp first_timestamp;
    Timestamp timestamp;
    std::uint8_t device_address;
    std::size_t first_snapshot_index;
    std::size_t last_snapshot_index;
    std::uint32_t first_register_address;
    std::uint32_t last_register_address;
    std::uint64_t segment_id;
    bool is_write;
    std::vector<std::uint8_t> bytes;
};

void appendTransactionByte(std::vector<TransactionRow>& rows, std::uint8_t device_address,
                           std::size_t snapshot_index, const I2CDevice::Snapshot& entry);
std::string formatTransactionRegister(const TransactionRow& row, bool registerless);
void renderTransactionBytesWithTooltips(const TransactionRow& row, const I2CDevice& device);

bool beginFixedLeftWindow(const char* name);
struct TimelineColumn
{
    const char* label;
    float width;
};

void setupTimelineColumns(const TimeValue::DisplayView& display, Timestamp sample_time,
                          std::span<const TimelineColumn> other_columns);
void renderBytes(const std::vector<std::uint8_t>& data);
void renderBytesWithTooltips(const std::vector<std::uint8_t>& data,
                             const std::vector<I2CDevice::BitFieldDefinition>& fields,
                             std::uint32_t register_address, const char* direction);
} // namespace ViewHelpers
