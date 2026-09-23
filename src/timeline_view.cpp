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
constexpr ViewHelpers::TimelineColumn kTimelineColumns[] = {{"I2C Address", 80.0F}, {"Device", 119.0F},
                                                             {"Register", 61.0F},    {"Name", 110.0F},
                                                             {"Write Data", 110.0F}, {"Read Data", 110.0F}};

void renderTransactions(const std::vector<ViewHelpers::TransactionRow>& rows, I2CDeviceManager& devicemanager,
                        const TimeValue::DisplayView& display, TimelineSyncState& sync,
                        const bool sync_pending, const bool jump_to_target,
                        ViewHelpers::TimelineFilters<7>& filters)
{
    const auto target_time = sync.position().time;
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
    if (sync_pending && sync.position().snapshot)
    {
        const auto target = *sync.position().snapshot;
        const auto found = std::ranges::find_if(visible_rows, [&](std::size_t index) {
            const auto& row = rows[index];
            return row.device_address == target.device_address &&
                   row.first_snapshot_index <= target.snapshot_index && target.snapshot_index <= row.last_snapshot_index;
        });
        if (found != visible_rows.end())
            scroll_index = static_cast<std::size_t>(std::distance(visible_rows.begin(), found));
    }
    if (!scroll_index && (sync_pending || jump_to_target || filter_changed) && selected_count > 0)
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
            scroll_index = key_index;
            sync.selectSnapshot(row.timestamp, row.device_address, row.last_snapshot_index);
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
                sync.selectSnapshot(row.timestamp, row.device_address, row.last_snapshot_index);
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
}
} // namespace

void TimelineView::rebuild(State& state, const I2CDeviceManager& devicemanager)
{
    state.access_timeline.clear();
    state.grouped_timeline.clear();
    for (const auto& [address, device] : devicemanager.GetAllDevices())
        for (std::size_t index = 0; index < device->GetSnapshotCount(); ++index)
            state.access_timeline.push_back({device->GetSnapshotByIndex(index).timestamp, address, index});
    std::stable_sort(state.access_timeline.begin(), state.access_timeline.end(),
                     [](const AccessRow& left, const AccessRow& right) { return left.timestamp < right.timestamp; });
    for (const auto& access : state.access_timeline)
    {
        const auto* device = devicemanager.GetAllDevices().at(access.device_address).get();
        ViewHelpers::appendTransactionByte(state.grouped_timeline, access.device_address, access.snapshot_index,
                                           device->GetSnapshotByIndex(access.snapshot_index));
    }
}

void TimelineView::render(State& state, I2CDeviceManager& devicemanager,
                          const TimeValue::DisplayView& display, TimelineSyncState& sync,
                          std::optional<Timestamp> jump_time)
{
    auto& timeline = state.access_timeline;
    if (!ViewHelpers::beginFixedLeftWindow("All Devices - Timeline"))
    {
        ImGui::End();
        return;
    }
    bool jump_to_target = false;
    if (jump_time && !timeline.empty())
    {
        // Search individual byte updates, including those inside a transaction.
        const auto next = std::lower_bound(timeline.begin(), timeline.end(), *jump_time,
                                           [](const auto& row, Timestamp time) { return row.timestamp < time; });
        auto nearest = next;
        if (next == timeline.end() ||
            (next != timeline.begin() &&
            *jump_time - std::prev(next)->timestamp <= next->timestamp - *jump_time))
            nearest = std::prev(next);
        sync.selectSnapshot(nearest->timestamp, nearest->device_address, nearest->snapshot_index);
        for (auto& filter : state.filters)
            filter[0] = '\0';
        jump_to_target = true;
    }
    ImGui::Checkbox("Group by transaction", &state.group_transactions);
    ImGui::TextWrapped(state.group_transactions
                           ? "All devices, oldest first. One row per transaction segment.\nClick a row or use Up/Down to select its time."
                           : "All devices, oldest first. One row per data-byte update.\nClick a row or use Up/Down to select its time.");
    const bool sync_pending = sync.enabled() && state.applied_revision != sync.revision();
    if (state.group_transactions)
    {
        renderTransactions(state.grouped_timeline, devicemanager, display, sync, sync_pending,
                           jump_to_target, state.filters);
        state.applied_revision = sync.revision();
        ImGui::End();
        return;
    }
    const auto target_time = sync.position().time;
    ImGui::Text("Databytes: %zu", timeline.size());
    if (ImGui::BeginTable("AllAccesses", 7, ViewHelpers::table_flags))
    {
        const auto sample_time = timeline.empty() ? target_time : timeline.front().timestamp;
        ViewHelpers::setupTimelineColumns(display, sample_time, kTimelineColumns);
        ImGui::TableSetupScrollFreeze(0, 2);
        ImGui::TableHeadersRow();
        const bool filter_changed = ViewHelpers::renderTimelineFilterRow(state.filters);
        std::vector<std::size_t> visible_rows;
        visible_rows.reserve(timeline.size());
        const bool has_filter = std::ranges::any_of(state.filters, [](const auto& filter) { return filter[0] != '\0'; });
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
                const auto& entry = device->GetSnapshotByIndex(row.snapshot_index);
                const auto matches = [&](std::size_t column, const std::string& value) {
                    return ViewHelpers::matchesTimelineFilter(state.filters[column].data(), value);
                };
                if ((!state.filters[0][0] || matches(0, display.formatTimestamp(row.timestamp))) &&
                    (!state.filters[1][0] || matches(1, std::format("0x{:02X}", row.device_address))) &&
                    (!state.filters[2][0] || matches(2, device->GetDeviceName())) &&
                    (!state.filters[3][0] || matches(3, device->GetRegisterAddressBytes() == 0
                                                   ? "-" : std::format("0x{:04X}", entry.updated_register_address))) &&
                    (!state.filters[4][0] || matches(4, device->GetRegisterName(entry.updated_register_address))) &&
                    (!state.filters[5][0] || matches(5, entry.is_write
                                                   ? ViewHelpers::formatTimelineBytes(entry.GetUpdatedRegister().value.write_data) : "")) &&
                    (!state.filters[6][0] || matches(6, entry.is_write
                                                   ? "" : ViewHelpers::formatTimelineBytes(entry.GetUpdatedRegister().value.read_data))))
                    visible_rows.push_back(index);
            }
        }
        const auto position = std::upper_bound(visible_rows.begin(), visible_rows.end(), target_time,
                                               [&](Timestamp time, std::size_t index) {
                                                   return time < timeline[index].timestamp;
                                               });
        const auto selected_count = static_cast<std::size_t>(std::distance(visible_rows.begin(), position));
        std::optional<std::size_t> scroll_index;
        if (sync_pending && sync.position().snapshot)
        {
            const auto target = *sync.position().snapshot;
            const auto scroll_row = std::ranges::find_if(visible_rows, [&](std::size_t index) {
                const auto& row = timeline[index];
                return row.device_address == target.device_address && row.snapshot_index == target.snapshot_index;
            });
            if (scroll_row != visible_rows.end())
                scroll_index = static_cast<std::size_t>(std::distance(visible_rows.begin(), scroll_row));
        }
        if (!scroll_index.has_value() && (sync_pending || jump_to_target || filter_changed) && selected_count > 0)
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
                scroll_index = key_index;
                sync.selectSnapshot(row.timestamp, row.device_address, row.snapshot_index);
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
                const auto& entry = device->GetSnapshotByIndex(row.snapshot_index);
                const bool selected = selected_count > 0 && static_cast<std::size_t>(index) == selected_count - 1;
                ImGui::PushID(index);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const auto label = display.formatTimestamp(row.timestamp);
                if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
                    sync.selectSnapshot(row.timestamp, row.device_address, row.snapshot_index);
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
                    ImGui::Text("0x%04X", entry.updated_register_address);
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(device->GetRegisterName(entry.updated_register_address).c_str());
                ImGui::TableSetColumnIndex(entry.is_write ? 5 : 6);
                const auto& info = entry.GetUpdatedRegister();
                ViewHelpers::renderBytesWithTooltips(entry.is_write ? info.value.write_data : info.value.read_data,
                                                     entry.is_write ? info.definition.write_bit_fields : info.definition.read_bit_fields,
                                                     entry.updated_register_address, entry.is_write ? "Write" : "Read");
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
        state.applied_revision = sync.revision();
    }
    ImGui::End();
}
