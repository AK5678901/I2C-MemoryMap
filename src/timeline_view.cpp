#include "timeline_view.hpp"

#include "view_helpers.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <numeric>
#include <optional>
#include <vector>

namespace
{
std::vector<ViewHelpers::TransactionRow> grouped_timeline;

constexpr ViewHelpers::TimelineColumn kTimelineColumns[] = {{"I2C Address", 80.0F}, {"Device", 119.0F},
                                                             {"Register", 61.0F},    {"Name", 110.0F},
                                                             {"Write Data", 110.0F}, {"Read Data", 110.0F}};

void renderTransactions(const std::vector<ViewHelpers::TransactionRow>& rows, I2CDeviceManager& devicemanager,
                        const TimeValue::DisplayView& display, Timestamp& target_time,
                        const bool sync_timeline_positions, bool& scroll_all_devices_timeline,
                        bool& scroll_device_timeline, std::uint8_t& scroll_device_address,
                        std::size_t& scroll_history_index, const bool scroll_timelines_to_target,
                        ViewHelpers::TimelineFilters<7>& filters)
{
    ImGui::Text("Transactions: %zu", rows.size());
    if (!ImGui::BeginTable("AllTransactions", 7, ViewHelpers::table_flags))
        return;
    const auto sample_time = rows.empty() ? target_time : rows.front().timestamp;
    ViewHelpers::setupTimelineColumns(display, sample_time, kTimelineColumns);
    ImGui::TableSetupScrollFreeze(0, 2);
    ImGui::TableHeadersRow();
    const bool filter_changed = ViewHelpers::renderTimelineFilterRow(filters);

    std::vector<std::size_t> visible_rows;
    visible_rows.reserve(rows.size());
    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        const auto& row = rows[index];
        const auto* device = devicemanager.GetDevice(row.device_address);
        const auto register_label = ViewHelpers::formatTransactionRegister(row, device->GetRegisterAddressBytes() == 0);
        const auto name = row.first_register_address == row.last_register_address
                              ? device->GetRegisterName(row.first_register_address) : std::string("(multiple)");
        const auto matches = [&](std::size_t column, const std::string& value) {
            return ViewHelpers::matchesTimelineFilter(filters[column].data(), value);
        };
        if ((!filters[0][0] || matches(0, display.formatTimestamp(row.timestamp))) &&
            (!filters[1][0] || matches(1, std::format("0x{:02X}", row.device_address))) &&
            (!filters[2][0] || matches(2, device->GetDeviceName())) &&
            (!filters[3][0] || matches(3, register_label)) &&
            (!filters[4][0] || matches(4, name)) &&
            (!filters[5][0] || matches(5, row.is_write ? ViewHelpers::formatTimelineBytes(row.bytes) : "")) &&
            (!filters[6][0] || matches(6, row.is_write ? "" : ViewHelpers::formatTimelineBytes(row.bytes))))
            visible_rows.push_back(index);
    }
    const auto position = std::upper_bound(visible_rows.begin(), visible_rows.end(), target_time,
                                           [&](Timestamp time, std::size_t index) {
                                               return time < rows[index].first_timestamp;
                                           });
    const auto selected_count = static_cast<std::size_t>(std::distance(visible_rows.begin(), position));
    std::optional<std::size_t> scroll_index;
    if (scroll_all_devices_timeline)
    {
        const auto found = std::ranges::find_if(visible_rows, [&](std::size_t index) {
            const auto& row = rows[index];
            return row.device_address == scroll_device_address &&
                   row.first_history_index <= scroll_history_index && scroll_history_index <= row.last_history_index;
        });
        if (found != visible_rows.end())
            scroll_index = static_cast<std::size_t>(std::distance(visible_rows.begin(), found));
    }
    if (!scroll_index && (scroll_timelines_to_target || filter_changed) && selected_count > 0)
        scroll_index = selected_count - 1;
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
            scroll_index = key_index;
            if (sync_timeline_positions)
            {
                scroll_device_timeline = true;
                scroll_device_address = row.device_address;
                scroll_history_index = row.last_history_index;
            }
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
            const auto* device = devicemanager.GetDevice(row.device_address);
            const bool selected = selected_count > 0 && static_cast<std::size_t>(index) == selected_count - 1;
            ImGui::PushID(index);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const auto label = display.formatTimestamp(row.timestamp);
            if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
            {
                target_time = row.timestamp;
                if (sync_timeline_positions)
                {
                    scroll_device_timeline = true;
                    scroll_device_address = row.device_address;
                    scroll_history_index = row.last_history_index;
                }
            }
            if (scroll_index == static_cast<std::size_t>(index))
                ImGui::SetScrollHereY(0.5F);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("0x%02X", row.device_address);
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(device->GetDeviceName().c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(ViewHelpers::formatTransactionRegister(row, device->GetRegisterAddressBytes() == 0).c_str());
            ImGui::TableSetColumnIndex(4);
            const auto name = row.first_register_address == row.last_register_address
                                  ? device->GetRegisterName(row.first_register_address) : std::string("(multiple)");
            ImGui::TextUnformatted(name.c_str());
            ImGui::TableSetColumnIndex(row.is_write ? 5 : 6);
            ViewHelpers::renderTransactionBytesWithTooltips(row, *device);
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
    scroll_all_devices_timeline = false;
}
} // namespace

void TimelineView::rebuild(AccessTimeline& timeline, const I2CDeviceManager& devicemanager)
{
    timeline.clear();
    grouped_timeline.clear();
    for (const auto& [address, device] : devicemanager.GetAllDevices())
        for (std::size_t index = 0; index < device->GetHistorySize(); ++index)
            timeline.push_back({device->GetHistoryEntry(index).timestamp, address, index});
    std::stable_sort(timeline.begin(), timeline.end(),
                     [](const AccessRow& left, const AccessRow& right) { return left.timestamp < right.timestamp; });
    for (const auto& access : timeline)
    {
        const auto* device = devicemanager.GetAllDevices().at(access.device_address).get();
        ViewHelpers::appendTransactionByte(grouped_timeline, access.device_address, access.history_index,
                                           device->GetHistoryEntry(access.history_index));
    }
}

void TimelineView::render(AccessTimeline& timeline, I2CDeviceManager& devicemanager,
                          const TimeValue::DisplayView& display, Timestamp& target_time,
                          const bool sync_timeline_positions, bool& scroll_all_devices_timeline,
                          bool& scroll_device_timeline, std::uint8_t& scroll_device_address,
                          std::size_t& scroll_history_index, const bool scroll_timelines_to_target,
                          std::optional<Timestamp> jump_time)
{
    static ViewHelpers::TimelineFilters<7> filters{};
    static bool group_transactions = false;
    if (!ViewHelpers::beginFixedLeftWindow("All Devices - Timeline"))
    {
        ImGui::End();
        return;
    }
    bool jump_to_target = false;
    if (jump_time && !grouped_timeline.empty())
    {
        const auto next = std::lower_bound(grouped_timeline.begin(), grouped_timeline.end(), *jump_time,
                                           [](const auto& row, Timestamp time) { return row.timestamp < time; });
        auto nearest = next;
        if (next == grouped_timeline.end() ||
            (next != grouped_timeline.begin() &&
            *jump_time - std::prev(next)->timestamp <= next->timestamp - *jump_time))
            nearest = std::prev(next);
        target_time = nearest->timestamp;
        scroll_all_devices_timeline = true;
        scroll_device_address = nearest->device_address;
        scroll_history_index = nearest->last_history_index;
        if (sync_timeline_positions)
            scroll_device_timeline = true;
        for (auto& filter : filters)
            filter[0] = '\0';
        jump_to_target = true;
    }
    ImGui::Checkbox("Group by transaction", &group_transactions);
    ImGui::TextWrapped(group_transactions
                           ? "All devices, oldest first. One row per transaction segment.\nClick a row or use Up/Down to select its time."
                           : "All devices, oldest first. One row per data-byte update.\nClick a row or use Up/Down to select its time.");
    if (group_transactions)
    {
        renderTransactions(grouped_timeline, devicemanager, display, target_time, sync_timeline_positions,
                           scroll_all_devices_timeline, scroll_device_timeline, scroll_device_address,
                           scroll_history_index, scroll_timelines_to_target || jump_to_target, filters);
        ImGui::End();
        return;
    }
    ImGui::Text("Databytes: %zu", timeline.size());
    if (ImGui::BeginTable("AllAccesses", 7, ViewHelpers::table_flags))
    {
        const auto sample_time = timeline.empty() ? target_time : timeline.front().timestamp;
        ViewHelpers::setupTimelineColumns(display, sample_time, kTimelineColumns);
        ImGui::TableSetupScrollFreeze(0, 2);
        ImGui::TableHeadersRow();
        const bool filter_changed = ViewHelpers::renderTimelineFilterRow(filters);
        std::vector<std::size_t> visible_rows;
        visible_rows.reserve(timeline.size());
        const bool has_filter = std::ranges::any_of(filters, [](const auto& filter) { return filter[0] != '\0'; });
        if (!has_filter)
        {
            visible_rows.resize(timeline.size());
            std::iota(visible_rows.begin(), visible_rows.end(), 0);
        }
        else
        {
            for (std::size_t index = 0; index < timeline.size(); ++index)
            {
                const auto& row = timeline[index];
                const auto* device = devicemanager.GetDevice(row.device_address);
                const auto entry = device->GetHistoryEntry(row.history_index);
                const auto matches = [&](std::size_t column, const std::string& value) {
                    return ViewHelpers::matchesTimelineFilter(filters[column].data(), value);
                };
                if ((!filters[0][0] || matches(0, display.formatTimestamp(row.timestamp))) &&
                    (!filters[1][0] || matches(1, std::format("0x{:02X}", row.device_address))) &&
                    (!filters[2][0] || matches(2, device->GetDeviceName())) &&
                    (!filters[3][0] || matches(3, device->GetRegisterAddressBytes() == 0
                                                   ? "-" : std::format("0x{:04X}", entry.register_address))) &&
                    (!filters[4][0] || matches(4, device->GetRegisterName(entry.register_address))) &&
                    (!filters[5][0] || matches(5, entry.is_write
                                                   ? ViewHelpers::formatTimelineBytes(entry.register_info.data_w) : "")) &&
                    (!filters[6][0] || matches(6, entry.is_write
                                                   ? "" : ViewHelpers::formatTimelineBytes(entry.register_info.data_r))))
                    visible_rows.push_back(index);
            }
        }
        const auto position = std::upper_bound(visible_rows.begin(), visible_rows.end(), target_time,
                                               [&](Timestamp time, std::size_t index) {
                                                   return time < timeline[index].timestamp;
                                               });
        const auto selected_count = static_cast<std::size_t>(std::distance(visible_rows.begin(), position));
        std::optional<std::size_t> scroll_index;
        if (scroll_all_devices_timeline)
        {
            const auto scroll_row = std::ranges::find_if(visible_rows, [&](std::size_t index) {
                const auto& row = timeline[index];
                return row.device_address == scroll_device_address && row.history_index == scroll_history_index;
            });
            if (scroll_row != visible_rows.end())
                scroll_index = static_cast<std::size_t>(std::distance(visible_rows.begin(), scroll_row));
        }
        if (!scroll_index.has_value() && (scroll_timelines_to_target || jump_to_target || filter_changed) && selected_count > 0)
            scroll_index = selected_count - 1;
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput)
        {
            std::optional<std::size_t> key_index;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && selected_count > 1)
                key_index = selected_count - 2;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && selected_count < visible_rows.size())
                key_index = selected_count;
            if (key_index)
            {
                const auto& row = timeline[visible_rows[*key_index]];
                target_time = row.timestamp;
                scroll_index = key_index;
                if (sync_timeline_positions)
                {
                    scroll_device_timeline = true;
                    scroll_device_address = row.device_address;
                    scroll_history_index = row.history_index;
                }
            }
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visible_rows.size()));
        if (scroll_index.has_value())
            clipper.IncludeItemByIndex(static_cast<int>(*scroll_index));
        while (clipper.Step())
        {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
            {
                const auto& row = timeline[visible_rows[index]];
                const auto* device = devicemanager.GetDevice(row.device_address);
                const auto entry = device->GetHistoryEntry(row.history_index);
                const bool selected = selected_count > 0 && static_cast<std::size_t>(index) == selected_count - 1;
                ImGui::PushID(index);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const auto label = display.formatTimestamp(row.timestamp);
                if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
                {
                    target_time = row.timestamp;
                    if (sync_timeline_positions)
                    {
                        scroll_device_timeline = true;
                        scroll_device_address = row.device_address;
                        scroll_history_index = row.history_index;
                    }
                }
                if (scroll_index == static_cast<std::size_t>(index))
                    ImGui::SetScrollHereY(0.5F);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("0x%02X", row.device_address);
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(device->GetDeviceName().c_str());
                ImGui::TableSetColumnIndex(3);
                if (device->GetRegisterAddressBytes() == 0)
                    ImGui::TextUnformatted("-");
                else
                    ImGui::Text("0x%04X", entry.register_address);
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(device->GetRegisterName(entry.register_address).c_str());
                ImGui::TableSetColumnIndex(entry.is_write ? 5 : 6);
                const auto& info = entry.register_info;
                ViewHelpers::renderBytesWithTooltips(entry.is_write ? info.data_w : info.data_r,
                                                     entry.is_write ? info.bit_fields_write : info.bit_fields_read,
                                                     entry.register_address, entry.is_write ? "Write" : "Read");
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
        scroll_all_devices_timeline = false;
    }
    ImGui::End();
}
