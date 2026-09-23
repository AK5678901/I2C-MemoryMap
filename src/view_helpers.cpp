#include "view_helpers.hpp"

#include "imgui_internal.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <stdexcept>
#include <unordered_map>

namespace
{
float timeColumnWidth(const TimeValue::DisplayView& display, Timestamp sample_time)
{
    const char* example = nullptr;
    switch (display.format())
    {
    case TimeValue::DisplayFormat::IsoUtc: example = "2026-09-22T08:20:18.123456789Z"; break;
    case TimeValue::DisplayFormat::IsoLocal: example = "2026-09-22T08:20:18.123456789"; break;
    case TimeValue::DisplayFormat::ShortUtc:
    case TimeValue::DisplayFormat::ShortLocal: example = "08:20:18.123456789"; break;
    case TimeValue::DisplayFormat::SinceFirstEvent: example = "6789.123456789"; break;
    case TimeValue::DisplayFormat::RawCsv: example = "6789.123456789";break;
    }
    const auto label = example != nullptr ? std::string(example) : display.formatTimestamp(sample_time);
    return std::max(ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().CellPadding.x * 2.0F + 12.0F, 65.0F);
}

void resizeTimeColumnOnFormatChange(const TimeValue::DisplayView& display, Timestamp sample_time)
{
    struct PreviousState
    {
        TimeValue::DisplayFormat format;
        int frame;
    };
    static std::unordered_map<ImGuiID, PreviousState> previous_states;
    const ImGuiTable* table = GImGui->CurrentTable;
    const auto format = display.format();
    const int frame = ImGui::GetFrameCount();
    const auto found = previous_states.find(table->ID);
    const bool continuously_visible = found != previous_states.end() && found->second.frame == frame - 1;
    const bool format_unchanged = found != previous_states.end() && found->second.format == format;
    if (continuously_visible && format_unchanged)
    {
        found->second.frame = frame;
        return;
    }
    // A new table has no width bounds until its first layout pass. Retry on the next frame.
    if (table->Columns[0].WidthMax <= 0.0F)
        return;
    ImGui::TableSetColumnWidth(0, timeColumnWidth(display, sample_time));
    previous_states[table->ID] = {format, frame};
}
} // namespace

void ViewHelpers::setupTimelineColumns(const TimeValue::DisplayView& display, Timestamp sample_time,
                                       std::span<const TimelineColumn> other_columns)
{
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, timeColumnWidth(display, sample_time));
    for (const auto& column : other_columns)
        ImGui::TableSetupColumn(column.label, ImGuiTableColumnFlags_WidthFixed, column.width);
    resizeTimeColumnOnFormatChange(display, sample_time);
}

namespace
{
bool containsByte(const std::size_t byte_index, const I2CDevice::BitFieldDefinition& field)
{
    const std::uint32_t first_byte = field.byte_offset;
    const std::uint32_t last_byte = field.byte_offset + (field.bit_offset + field.bit_width - 1U) / 8U;
    return byte_index >= first_byte && byte_index <= last_byte;
}

std::uint64_t extractBitFieldValue(const std::vector<std::uint8_t>& data, const I2CDevice::BitFieldDefinition& field)
{
    if (!field.is_little_endian)
        throw std::runtime_error("Big endian bitfield parsing is not supported.");

    std::uint64_t value = 0;
    const auto width = std::min<std::uint32_t>(field.bit_width, 64U);
    for (std::uint32_t bit = 0; bit < width; ++bit)
    {
        const std::uint32_t global_bit = field.bit_offset + bit;
        const std::uint32_t physical_byte = field.byte_offset + global_bit / 8U;
        if (physical_byte < data.size())
            value |= static_cast<std::uint64_t>((data[physical_byte] >> (global_bit % 8U)) & 1U) << bit;
    }
    return value;
}

void showByteTooltip(const std::vector<std::uint8_t>& data,
                     const std::vector<I2CDevice::BitFieldDefinition>& fields, const std::size_t byte_index,
                     const std::uint32_t register_address, const char* direction)
{
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem) ||
        !std::ranges::any_of(fields, [byte_index](const auto& field) { return containsByte(byte_index, field); }))
        return;

    ImGui::BeginTooltip();
    ImGui::Text("%s Byte %zu Breakdown (0x%04X):", direction, byte_index, register_address);
    ImGui::Separator();
    for (const auto& field : fields)
    {
        if (!containsByte(byte_index, field))
            continue;
        const int high_bit = field.bit_offset + field.bit_width - 1;
        if (field.bit_width <= 64)
        {
            const auto value = extractBitFieldValue(data, field);
            ImGui::Text("  [%d:%d] %s: 0x%llX (%llu)", high_bit, field.bit_offset, field.name.c_str(),
                        static_cast<unsigned long long>(value), static_cast<unsigned long long>(value));
        }
        else
        {
            ImGui::Text("  [%d:%d] %s: (Width > 64 bits)", high_bit, field.bit_offset, field.name.c_str());
        }
    }
    ImGui::EndTooltip();
}
} // namespace

bool ViewHelpers::matchesTimelineFilter(const char* filter, const std::string_view value)
{
    if (filter[0] == '\0')
        return true;
    const std::string_view needle(filter);
    return std::search(value.begin(), value.end(), needle.begin(), needle.end(), [](char left, char right) {
               return std::tolower(static_cast<unsigned char>(left)) ==
                      std::tolower(static_cast<unsigned char>(right));
           }) != value.end();
}

std::string ViewHelpers::formatTimelineBytes(const std::vector<std::uint8_t>& data)
{
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(data.size() * 3);
    for (const auto byte : data)
    {
        if (!result.empty())
            result += ' ';
        result += digits[byte >> 4];
        result += digits[byte & 0x0F];
    }
    return result;
}

void ViewHelpers::appendTransactionByte(std::vector<TransactionRow>& rows, const std::uint8_t device_address,
                                        const std::size_t snapshot_index, const I2CDevice::Snapshot& entry)
{
    const auto& data = entry.is_write ? entry.GetUpdatedRegister().value.write_data : entry.GetUpdatedRegister().value.read_data;
    const auto byte = data.back();
    if (!rows.empty() && rows.back().device_address == device_address && rows.back().segment_id == entry.segment_id &&
        rows.back().last_snapshot_index + 1 == snapshot_index)
    {
        auto& row = rows.back();
        row.timestamp = entry.timestamp;
        row.last_snapshot_index = snapshot_index;
        row.last_register_address = entry.updated_register_address;
        row.bytes.push_back(byte);
        return;
    }
    rows.push_back({entry.timestamp, entry.timestamp, device_address, snapshot_index, snapshot_index,
                    entry.updated_register_address, entry.updated_register_address, entry.segment_id, entry.is_write,
                    {byte}});
}

std::string ViewHelpers::formatTransactionRegister(const TransactionRow& row, const bool registerless)
{
    if (registerless)
        return "-";
    if (row.first_register_address == row.last_register_address)
        return std::format("0x{:04X}", row.first_register_address);
    return std::format("0x{:04X}-0x{:04X}", row.first_register_address, row.last_register_address);
}

bool ViewHelpers::beginFixedLeftWindow(const char* name)
{
    ImGuiWindowClass window_class;
    window_class.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoUndocking | ImGuiDockNodeFlags_NoDockingOverMe |
                                            ImGuiDockNodeFlags_NoDockingSplit | ImGuiDockNodeFlags_NoWindowMenuButton;
    ImGui::SetNextWindowClass(&window_class);
    return ImGui::Begin(name, nullptr, ImGuiWindowFlags_NoMove);
}

void ViewHelpers::renderBytes(const std::vector<std::uint8_t>& data)
{
    for (std::size_t index = 0; index < data.size(); ++index)
    {
        if (index > 0)
            ImGui::SameLine(0.0F, 4.0F);
        ImGui::Text("%02X", data[index]);
    }
}

void ViewHelpers::renderBytesWithTooltips(const std::vector<std::uint8_t>& data,
                                          const std::vector<I2CDevice::BitFieldDefinition>& fields,
                                          const std::uint32_t register_address, const char* direction)
{
    for (std::size_t index = 0; index < data.size(); ++index)
    {
        if (index > 0)
            ImGui::SameLine(0.0F, 4.0F);
        ImGui::Text("%02X", data[index]);
        showByteTooltip(data, fields, index, register_address, direction);
    }
}

void ViewHelpers::renderTransactionBytesWithTooltips(const TransactionRow& row, const I2CDevice& device)
{
    for (std::size_t index = 0; index < row.bytes.size(); ++index)
    {
        if (index > 0)
            ImGui::SameLine(0.0F, 4.0F);
        ImGui::Text("%02X", row.bytes[index]);
        const auto& entry = device.GetSnapshotByIndex(row.first_snapshot_index + index);
        const auto& info = entry.GetUpdatedRegister();
        const auto& data = entry.is_write ? info.value.write_data : info.value.read_data;
        const auto& fields = entry.is_write ? info.definition.write_bit_fields : info.definition.read_bit_fields;
        showByteTooltip(data, fields, data.size() - 1, entry.updated_register_address,
                        entry.is_write ? "Write" : "Read");
    }
}
