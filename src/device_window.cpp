#include "device_window.hpp"

#include "view_helpers.hpp"

#include "imgui.h"

#include <algorithm>
#include <format>
#include <iterator>
#include <map>
#include <numeric>
#include <optional>
#include <vector>

namespace
{
constexpr float kRegisterColumnWidth = 61.0F;
constexpr float kDataColumnWidth = 110.0F;
constexpr float kNameColumnWidth = 110.0F;
constexpr ViewHelpers::TimelineColumn kTimelineColumns[] = {{"Register", kRegisterColumnWidth},
                                                             {"Name", kNameColumnWidth},
                                                             {"Write Data", kDataColumnWidth},
                                                             {"Read Data", kDataColumnWidth}};

void setupDataColumns(const bool registerless)
{
    if (registerless)
    {
        ImGui::TableSetupColumn("Write Data", ImGuiTableColumnFlags_WidthFixed, kDataColumnWidth);
        ImGui::TableSetupColumn("Read Data", ImGuiTableColumnFlags_WidthFixed, kDataColumnWidth);
    }
    else
    {
        ImGui::TableSetupColumn("Register", ImGuiTableColumnFlags_WidthFixed, kRegisterColumnWidth);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, kNameColumnWidth);
        ImGui::TableSetupColumn("Write Data", ImGuiTableColumnFlags_WidthFixed, kDataColumnWidth);
        ImGui::TableSetupColumn("Read Data", ImGuiTableColumnFlags_WidthFixed, kDataColumnWidth);
    }
}

void setupRegisterTable(const bool registerless)
{
    setupDataColumns(registerless);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
}

void renderRegisterRows(const I2CDevice& device, const I2CDevice::SnapshotView& snapshot, const bool registerless)
{
    for (const auto& [address, info] : snapshot.registers)
    {
        if (info.data_w.empty() && info.data_r.empty())
            continue;
        ImGui::TableNextRow();
        const bool selected = snapshot.changed_reg_addr == address;
        if (registerless)
        {
            ImGui::TableSetColumnIndex(0);
            const auto cell_position = ImGui::GetCursorScreenPos();
            ImGui::Selectable("##SnapshotRow", selected, ImGuiSelectableFlags_SpanAllColumns);
            ImGui::SetCursorScreenPos(cell_position);
            ViewHelpers::renderBytes(info.data_w);
            ImGui::TableSetColumnIndex(1);
            ViewHelpers::renderBytes(info.data_r);
            continue;
        }
        ImGui::TableSetColumnIndex(0);
        const auto address_label = std::format("0x{:04X}##SnapshotRow", address);
        ImGui::Selectable(address_label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(device.GetRegisterName(address).c_str());
        ImGui::TableSetColumnIndex(2);
        ViewHelpers::renderBytesWithTooltips(info.data_w, info.bit_fields_write, address, "Write");
        ImGui::TableSetColumnIndex(3);
        ViewHelpers::renderBytesWithTooltips(info.data_r, info.bit_fields_read, address, "Read");
    }
}

std::optional<std::size_t> renderTransactionTimeline(const I2CDevice& device,
                                                     const TimeValue::DisplayView& display, Timestamp& target_time,
                                                     std::optional<std::size_t> requested_index,
                                                     const bool scroll_to_selected,
                                                     ViewHelpers::TimelineFilters<5>& filters)
{
    std::vector<ViewHelpers::TransactionRow> rows;
    for (std::size_t index = 0; index < device.GetHistorySize(); ++index)
        ViewHelpers::appendTransactionByte(rows, 0, index, device.GetHistoryEntry(index));
    ImGui::Text("Transactions: %zu", rows.size());
    if (!ImGui::BeginTable("TransactionTimeline", 5, ViewHelpers::table_flags))
        return std::nullopt;
    ViewHelpers::setupTimelineColumns(display, rows.empty() ? target_time : rows.front().timestamp, kTimelineColumns);
    ImGui::TableSetupScrollFreeze(0, 2);
    ImGui::TableHeadersRow();
    const bool filter_changed = ViewHelpers::renderTimelineFilterRow(filters);

    std::vector<std::size_t> visible_rows;
    visible_rows.reserve(rows.size());
    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        const auto& row = rows[index];
        const auto register_label = ViewHelpers::formatTransactionRegister(row, device.GetRegisterAddressBytes() == 0);
        const auto name = row.first_register_address == row.last_register_address
                              ? device.GetRegisterName(row.first_register_address) : std::string("(multiple)");
        const auto matches = [&](std::size_t column, const std::string& value) {
            return ViewHelpers::matchesTimelineFilter(filters[column].data(), value);
        };
        if ((!filters[0][0] || matches(0, display.formatTimestamp(row.timestamp))) &&
            (!filters[1][0] || matches(1, register_label)) &&
            (!filters[2][0] || matches(2, name)) &&
            (!filters[3][0] || matches(3, row.is_write ? ViewHelpers::formatTimelineBytes(row.bytes) : "")) &&
            (!filters[4][0] || matches(4, row.is_write ? "" : ViewHelpers::formatTimelineBytes(row.bytes))))
            visible_rows.push_back(index);
    }
    const auto position = std::upper_bound(visible_rows.begin(), visible_rows.end(), target_time,
                                           [&](Timestamp time, std::size_t index) {
                                               return time < rows[index].first_timestamp;
                                           });
    const auto selected_count = static_cast<std::size_t>(std::distance(visible_rows.begin(), position));
    std::optional<std::size_t> scroll_index;
    if (requested_index)
    {
        const auto found = std::ranges::find_if(visible_rows, [&](std::size_t index) {
            const auto& row = rows[index];
            return row.first_history_index <= *requested_index && *requested_index <= row.last_history_index;
        });
        if (found != visible_rows.end())
            scroll_index = static_cast<std::size_t>(std::distance(visible_rows.begin(), found));
    }
    if (!scroll_index && (scroll_to_selected || filter_changed) && selected_count > 0)
        scroll_index = selected_count - 1;
    std::optional<std::size_t> selected_index;
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput)
    {
        std::optional<std::size_t> key_index;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && selected_count > 1)
            key_index = selected_count - 2;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && selected_count < visible_rows.size())
            key_index = selected_count;
        if (key_index)
        {
            const auto& row = rows[visible_rows[*key_index]];
            target_time = row.timestamp;
            selected_index = row.last_history_index;
            scroll_index = key_index;
        }
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visible_rows.size()));
    if (scroll_index)
        clipper.IncludeItemByIndex(static_cast<int>(*scroll_index));
    while (clipper.Step())
    {
        for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
        {
            const auto& row = rows[visible_rows[index]];
            const bool selected = selected_count > 0 && static_cast<std::size_t>(index) == selected_count - 1;
            ImGui::PushID(index);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const auto label = display.formatTimestamp(row.timestamp);
            if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
            {
                target_time = row.timestamp;
                selected_index = row.last_history_index;
            }
            if (scroll_index == static_cast<std::size_t>(index))
                ImGui::SetScrollHereY(0.5F);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(ViewHelpers::formatTransactionRegister(row, device.GetRegisterAddressBytes() == 0).c_str());
            ImGui::TableSetColumnIndex(2);
            const auto name = row.first_register_address == row.last_register_address
                                  ? device.GetRegisterName(row.first_register_address) : std::string("(multiple)");
            ImGui::TextUnformatted(name.c_str());
            ImGui::TableSetColumnIndex(row.is_write ? 3 : 4);
            ViewHelpers::renderTransactionBytesWithTooltips(row, device);
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
    return selected_index;
}

std::optional<std::size_t> renderRegisterTimeline(const I2CDevice& device,
                                                  const TimeValue::DisplayView& display, Timestamp& target_time,
                                                  std::optional<std::size_t> scroll_index,
                                                  const bool scroll_to_selected,
                                                  ViewHelpers::TimelineFilters<5>& filters)
{
    ImGui::Text("Databytes: %zu", device.GetHistorySize());
    if (!ImGui::BeginTable("RegisterTimeline", 5, ViewHelpers::table_flags))
        return std::nullopt;
    ViewHelpers::setupTimelineColumns(display,
                                      device.GetHistorySize() == 0 ? target_time : device.GetHistoryEntry(0).timestamp,
                                      kTimelineColumns);
    ImGui::TableSetupScrollFreeze(0, 2);
    ImGui::TableHeadersRow();
    const bool filter_changed = ViewHelpers::renderTimelineFilterRow(filters);

    std::vector<std::size_t> visible_rows;
    visible_rows.reserve(device.GetHistorySize());
    const bool has_filter = std::ranges::any_of(filters, [](const auto& filter) { return filter[0] != '\0'; });
    if (!has_filter)
    {
        visible_rows.resize(device.GetHistorySize());
        std::iota(visible_rows.begin(), visible_rows.end(), 0);
    }
    else
    {
        for (std::size_t index = 0; index < device.GetHistorySize(); ++index)
        {
            const auto entry = device.GetHistoryEntry(index);
            const auto matches = [&](std::size_t column, const std::string& value) {
                return ViewHelpers::matchesTimelineFilter(filters[column].data(), value);
            };
            if ((!filters[0][0] || matches(0, display.formatTimestamp(entry.timestamp))) &&
                (!filters[1][0] || matches(1, device.GetRegisterAddressBytes() == 0
                                               ? "-" : std::format("0x{:04X}", entry.register_address))) &&
                (!filters[2][0] || matches(2, device.GetRegisterName(entry.register_address))) &&
                (!filters[3][0] || matches(3, entry.is_write
                                               ? ViewHelpers::formatTimelineBytes(entry.register_info.data_w) : "")) &&
                (!filters[4][0] || matches(4, entry.is_write
                                               ? "" : ViewHelpers::formatTimelineBytes(entry.register_info.data_r))))
                visible_rows.push_back(index);
        }
    }
    const auto position = std::upper_bound(visible_rows.begin(), visible_rows.end(), target_time,
                                           [&](Timestamp time, std::size_t index) {
                                               return time < device.GetHistoryEntry(index).timestamp;
                                           });
    const auto selected_count = static_cast<std::size_t>(std::distance(visible_rows.begin(), position));
    if (scroll_index)
    {
        const auto found = std::ranges::find(visible_rows, *scroll_index);
        scroll_index = found == visible_rows.end() ? std::nullopt
                      : std::optional<std::size_t>(std::distance(visible_rows.begin(), found));
    }
    if (!scroll_index && (scroll_to_selected || filter_changed) && selected_count > 0)
        scroll_index = selected_count - 1;
    std::optional<std::size_t> selected_index;
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && selected_count > 1)
            selected_index = visible_rows[selected_count - 2];
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && selected_count < visible_rows.size())
            selected_index = visible_rows[selected_count];
        if (selected_index)
        {
            target_time = device.GetHistoryEntry(*selected_index).timestamp;
            scroll_index = static_cast<std::size_t>(std::distance(
                visible_rows.begin(), std::ranges::find(visible_rows, *selected_index)));
        }
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visible_rows.size()));
    if (scroll_index.has_value() && *scroll_index < visible_rows.size())
        clipper.IncludeItemByIndex(static_cast<int>(*scroll_index));
    while (clipper.Step())
    {
        for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
        {
            const auto entry = device.GetHistoryEntry(visible_rows[index]);
            const bool selected = selected_count > 0 && static_cast<std::size_t>(index) == selected_count - 1;
            ImGui::PushID(index);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const auto time_label = display.formatTimestamp(entry.timestamp);
            if (ImGui::Selectable(time_label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
            {
                target_time = entry.timestamp;
                selected_index = visible_rows[index];
            }
            if (scroll_index == static_cast<std::size_t>(index))
                ImGui::SetScrollHereY(0.5F);
            ImGui::TableSetColumnIndex(1);
            if (device.GetRegisterAddressBytes() == 0)
                ImGui::TextUnformatted("-");
            else
                ImGui::Text("0x%04X", entry.register_address);
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(device.GetRegisterName(entry.register_address).c_str());
            ImGui::TableSetColumnIndex(entry.is_write ? 3 : 4);
            const auto& info = entry.register_info;
            ViewHelpers::renderBytesWithTooltips(entry.is_write ? info.data_w : info.data_r,
                                                 entry.is_write ? info.bit_fields_write : info.bit_fields_read,
                                                 entry.register_address, entry.is_write ? "Write" : "Read");
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
    return selected_index;
}

void renderAccessStatRows(const std::map<std::uint32_t, I2CDevice::CommandStat>& stats, const char* direction,
                          const bool registerless, const Timestamp target_time)
{
    for (const auto& [address, stat] : stats)
    {
        if (stat.call_count == 0)
            continue;
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        registerless ? ImGui::TextUnformatted("-") : ImGui::Text("0x%04X", address);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(stat.command_name.empty() ? "-" : stat.command_name.c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(direction);
        const auto current = stat.GetIntervalAt(target_time);
        const std::optional<double> values[] = {
            stat.intervals.empty() ? std::nullopt : std::optional<double>(stat.min_interval),
            stat.intervals.empty() ? std::nullopt : std::optional<double>(stat.mean_interval),
            stat.intervals.empty() ? std::nullopt : std::optional<double>(stat.max_interval),
            stat.intervals.empty() ? std::nullopt : std::optional<double>(stat.GetIntervalStdDev())};
        ImGui::TableSetColumnIndex(3);
        current ? ImGui::Text("%.6f", *current) : ImGui::TextUnformatted("-");
        ImGui::TableSetColumnIndex(4);
        ImGui::Text("%u", stat.call_count);
        for (int index = 0; index < 4; ++index)
        {
            ImGui::TableSetColumnIndex(5 + index);
            values[index] ? ImGui::Text("%.6f", *values[index]) : ImGui::TextUnformatted("-");
        }
    }
}

void renderAccessStatistics(const I2CDevice& device, const Timestamp target_time)
{
    if (!ImGui::CollapsingHeader("Access intervals", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    if (!ImGui::BeginTable("AccessStatistics", 9, ViewHelpers::table_flags, ImGui::GetContentRegionAvail()))
        return;
    struct ColumnHeader
    {
        const char* label;
        const char* tooltip;
    };
    constexpr ColumnHeader headers[] = {
        {"Register", nullptr},
        {"Name", nullptr},
        {"R/W", nullptr},
        {"Current (s)", "Latest completed interval at the selected time."},
        {"Count", "Calculated from the entire log."},
        {"Min (s)", "Calculated from the entire log."},
        {"Mean (s)", "Calculated from the entire log."},
        {"Max (s)", "Calculated from the entire log."},
        {"Std dev (s)", "Calculated from the entire log."}};
    for (const auto& header : headers)
        ImGui::TableSetupColumn(header.label, ImGuiTableColumnFlags_WidthFixed, 100.0F);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    for (int index = 0; index < static_cast<int>(std::size(headers)); ++index)
    {
        ImGui::TableSetColumnIndex(index);
        ImGui::TableHeader(headers[index].label);
        if (headers[index].tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem))
            ImGui::SetTooltip("%s", headers[index].tooltip);
    }
    const bool registerless = device.GetRegisterAddressBytes() == 0;
    renderAccessStatRows(device.GetWriteStats(), "Write", registerless, target_time);
    renderAccessStatRows(device.GetReadStats(), "Read", registerless, target_time);
    ImGui::EndTable();
}
} // namespace

std::string DeviceWindow::name(const I2CDevice& device, const std::uint8_t address)
{
    return std::format("{} [0x{:02X}]", device.GetDeviceName(), address);
}

void DeviceWindow::render(I2CDeviceManager& devicemanager, std::map<std::uint8_t, bool>& visibility,
                          const TimeValue::DisplayView& display, Timestamp& target_time,
                          const bool sync_timeline_positions,
                          bool& scroll_all_devices_timeline, bool& scroll_device_timeline,
                          std::uint8_t& scroll_device_address, std::size_t& scroll_history_index,
                          const bool scroll_timelines_to_target)
{
    static std::map<std::uint8_t, ViewHelpers::TimelineFilters<5>> timeline_filters;
    static std::map<std::uint8_t, bool> group_transactions;
    static std::map<std::uint8_t, float> statistics_heights;
    for (const auto& [address, device] : devicemanager.GetAllDevices())
    {
        auto& visible = visibility[address];
        if (!visible)
            continue;
        const auto window_name = name(*device, address);
        ImGui::SetNextWindowSize(ImVec2(500.0F, 600.0F), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(window_name.c_str(), &visible))
        {
            ImGui::End();
            continue;
        }
        constexpr float splitter_height = 6.0F;
        constexpr float minimum_pane_height = 100.0F;
        const float available_height = ImGui::GetContentRegionAvail().y;
        const float maximum_statistics_height = std::max(minimum_pane_height,
                                                         available_height - splitter_height - minimum_pane_height);
        auto& statistics_height = statistics_heights.try_emplace(address, 200.0F).first->second;
        statistics_height = std::clamp(statistics_height, minimum_pane_height, maximum_statistics_height);
        if (ImGui::BeginChild("AccessStatisticsPane", ImVec2(0.0F, statistics_height)))
            renderAccessStatistics(*device, target_time);
        ImGui::EndChild();

        ImGui::InvisibleButton("AccessStatisticsSplitter", ImVec2(-1.0F, splitter_height));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (ImGui::IsItemActive())
            statistics_height = std::clamp(statistics_height + ImGui::GetIO().MouseDelta.y,
                                           minimum_pane_height, maximum_statistics_height);
        const auto splitter_min = ImGui::GetItemRectMin();
        const auto splitter_max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(splitter_min.x, (splitter_min.y + splitter_max.y) * 0.5F),
                                            ImVec2(splitter_max.x, (splitter_min.y + splitter_max.y) * 0.5F),
                                            ImGui::GetColorU32(ImGuiCol_Separator));

        ImGui::BeginChild("RegisterViewPane", ImVec2(0.0F, 0.0F));
        if (ImGui::BeginTabBar("RegisterView"))
        {
            if (ImGui::BeginTabItem("Snapshot"))
            {
                const auto snapshot = device->GetSnapshotViewAt(target_time);
                const bool registerless = device->GetRegisterAddressBytes() == 0;
                if (ImGui::BeginTable("RegisterSnapshot", registerless ? 2 : 4, ViewHelpers::table_flags))
                {
                    setupRegisterTable(registerless);
                    renderRegisterRows(*device, snapshot, registerless);
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Timeline"))
            {
                auto& grouped = group_transactions.try_emplace(address, false).first->second;
                ImGui::Checkbox("Group by transaction", &grouped);
                ImGui::TextWrapped(grouped
                                       ? "Entire log, oldest first. One row per transaction segment.\nClick a row or use Up/Down to select its time."
                                       : "Entire log, oldest first. One row per data-byte update.\nClick a row or use Up/Down to select its time.");
                const auto requested_index = scroll_device_timeline && scroll_device_address == address
                                                 ? std::optional<std::size_t>(scroll_history_index)
                                                 : std::nullopt;
                if (const auto clicked_index =
                        grouped
                            ? renderTransactionTimeline(*device, display, target_time, requested_index,
                                                        scroll_timelines_to_target, timeline_filters[address])
                            : renderRegisterTimeline(*device, display, target_time, requested_index,
                                                     scroll_timelines_to_target, timeline_filters[address]);
                    sync_timeline_positions && clicked_index.has_value())
                {
                    scroll_all_devices_timeline = true;
                    scroll_device_address = address;
                    scroll_history_index = *clicked_index;
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
        ImGui::End();
    }
}
