#include "main_window.hpp"

#include "control_panel.hpp"
#include "device_window.hpp"
#include "view_helpers.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <vector>

namespace
{
constexpr float kControlPanelHeight = 360.0F;

void setupDockLayout(I2CDeviceManager& devicemanager, const LogData& log, const ImGuiID dockspace_id,
                     const float left_column_width = 0.0F)
{
    std::vector<std::uint8_t> valid_addresses;
    for (const auto address : log.active_addresses)
        if (devicemanager.GetDevice(address) != nullptr)
            valid_addresses.push_back(address);

    const auto* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);
    ImGuiID remaining_id = dockspace_id;
    ImGuiID timeline_id = dockspace_id;
    float left_ratio = 0.4332F; // About 830 px at 1920 px, including room for the Read Data column.
    if (left_column_width > 0.0F)
    {
        const float available_width = viewport->WorkSize.x - ImGui::GetStyle().DockingSeparatorSize;
        left_ratio = std::clamp(left_column_width / std::max(available_width, 1.0F), 0.05F, 0.95F);
    }
    ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, left_ratio, &timeline_id, &remaining_id);
    ImGuiID controls_id = 0;
    const float control_ratio = std::clamp(kControlPanelHeight / std::max(viewport->WorkSize.y, 1.0F), 0.1F, 0.5F);
    ImGui::DockBuilderSplitNode(timeline_id, ImGuiDir_Down, control_ratio, &controls_id, &timeline_id);
    ImGui::DockBuilderDockWindow("Control Panel", controls_id);
    ImGuiID device_list_id = 0;
    ImGui::DockBuilderSplitNode(timeline_id, ImGuiDir_Down, 0.2F, &device_list_id, &timeline_id);
    ImGui::DockBuilderDockWindow("Devices", device_list_id);
    ImGui::DockBuilderDockWindow("All Devices - Timeline", timeline_id);
    for (std::size_t index = 0; index < valid_addresses.size(); ++index)
    {
        const auto address = valid_addresses[index];
        ImGuiID device_id = remaining_id;
        if (index + 1 < valid_addresses.size())
        {
            const float ratio = 1.0F / static_cast<float>(valid_addresses.size() - index);
            ImGui::DockBuilderSplitNode(remaining_id, ImGuiDir_Left, ratio, &device_id, &remaining_id);
        }
        const auto window_name = DeviceWindow::name(*devicemanager.GetDevice(address), address);
        ImGui::DockBuilderDockWindow(window_name.c_str(), device_id);
    }
    ImGui::DockBuilderFinish(dockspace_id);
}
} // namespace

void MainWindow::reset(const LogData& log)
{
    control_panel_.resetJumpInput();
    time_format_ = log.has_absolute_timestamps ? TimeValue::DisplayFormat::ShortLocal
                                               : TimeValue::DisplayFormat::RawCsv;
    access_timeline_.clear();
    device_visibility_.clear();
    for (const auto address : log.active_addresses)
        device_visibility_[address] = true;
    target_time_ = log.timestamps.empty() ? 0 : log.min_timestamp;
    first_layout_ = true;
    arrange_devices_ = false;
    scroll_all_devices_timeline_ = false;
    scroll_device_timeline_ = false;
    scroll_timelines_to_target_ = false;
}

void MainWindow::refreshLive(I2CDeviceManager& devicemanager, const LogData& log)
{
    for (const auto address : log.active_addresses)
        if (!device_visibility_.contains(address))
        {
            device_visibility_[address] = true;
            first_layout_ = true;
        }
    target_time_ = log.max_timestamp;
    TimelineView::rebuild(access_timeline_, devicemanager);
    scroll_timelines_to_target_ = true;
}

void MainWindow::handleArrowKeys(const LogData& log)
{
    if (log.timestamps.empty() || ImGui::GetIO().WantTextInput)
        return;
    const auto position = std::ranges::lower_bound(log.timestamps, target_time_);
    auto index = static_cast<std::size_t>(std::distance(log.timestamps.begin(), position));
    index = std::min(index, log.timestamps.size() - 1);
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && index > 0)
        target_time_ = log.timestamps[index - 1];
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && index + 1 < log.timestamps.size())
        target_time_ = log.timestamps[index + 1];
}

void MainWindow::renderDeviceList(const I2CDeviceManager& devicemanager, const LogData& log)
{
    if (ViewHelpers::beginFixedLeftWindow("Devices"))
    {
        ImGui::Text("Devices loaded from JSON: %zu", devicemanager.GetAllDevices().size());
        const bool any_visible = std::ranges::any_of(log.active_addresses, [&](std::uint8_t address) {
            return device_visibility_[address] && devicemanager.GetAllDevices().contains(address);
        });
        ImGui::BeginDisabled(!any_visible);
        if (ImGui::Button("Arrange evenly"))
            arrange_devices_ = true;
        ImGui::EndDisabled();
        bool has_devices = false;
        for (const auto& [address, device] : devicemanager.GetAllDevices())
        {
            if (std::ranges::find(log.active_addresses, address) == log.active_addresses.end())
                continue;
            has_devices = true;
            ImGui::PushID(static_cast<int>(address));
            auto& visible = device_visibility_[address];
            ImGui::Checkbox("##Visible", &visible);
            ImGui::SameLine();
            ImGui::Text("0x%02X  %s", address, device->GetDeviceName().c_str());
            if (device->GetHistorySize() == 0)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(no data)");
            }
            ImGui::PopID();
        }
        if (!has_devices)
            ImGui::TextDisabled("No accessed devices");
    }
    ImGui::End();
}

void MainWindow::render(I2CDeviceManager& devicemanager, const LogData& log, const LiveReceiver* live_receiver)
{
    const auto dockspace_id = ImGui::GetID("DeviceDockSpace");
    if (first_layout_)
    {
        TimelineView::rebuild(access_timeline_, devicemanager);
        setupDockLayout(devicemanager, log, dockspace_id);
    }
    if (arrange_devices_)
    {
        float left_column_width = 0.0F;
        if (const auto* dockspace_node = ImGui::DockBuilderGetNode(dockspace_id);
            dockspace_node != nullptr && dockspace_node->ChildNodes[0] != nullptr)
            left_column_width = dockspace_node->ChildNodes[0]->Size.x;
        LogData visible_log;
        for (const auto address : log.active_addresses)
            if (device_visibility_[address] && devicemanager.GetDevice(address) != nullptr)
                visible_log.active_addresses.push_back(address);
        if (!visible_log.active_addresses.empty())
            setupDockLayout(devicemanager, visible_log, dockspace_id, left_column_width);
        arrange_devices_ = false;
    }

    ImGui::DockSpaceOverViewport(dockspace_id, ImGui::GetMainViewport());
    handleArrowKeys(log);
    const auto control_result = control_panel_.render(devicemanager, log, time_format_, target_time_,
                                                      sync_all_devices_timeline_, live_receiver);
    const TimeValue::DisplayView time_display(log, time_format_);
    if (control_result.slider_changed &&
        sync_all_devices_timeline_)
        scroll_timelines_to_target_ = true;
    if (!sync_all_devices_timeline_)
    {
        scroll_all_devices_timeline_ = false;
        scroll_device_timeline_ = false;
        scroll_timelines_to_target_ = false;
    }
    renderDeviceList(devicemanager, log);
    TimelineView::render(access_timeline_, devicemanager, time_display, target_time_, sync_all_devices_timeline_,
                         scroll_all_devices_timeline_, scroll_device_timeline_, scroll_device_address_,
                         scroll_history_index_, scroll_timelines_to_target_, control_result.jump_time);
    DeviceWindow::render(devicemanager, device_visibility_, time_display, target_time_, sync_all_devices_timeline_,
                         scroll_all_devices_timeline_, scroll_device_timeline_, scroll_device_address_,
                         scroll_history_index_, scroll_timelines_to_target_);
    scroll_device_timeline_ = false;
    scroll_timelines_to_target_ = false;
    first_layout_ = false;
}
