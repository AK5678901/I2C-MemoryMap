#include "control_panel.hpp"

#include "view_helpers.hpp"

#include "imgui.h"

#include <algorithm>

namespace
{
void moveToPreviousTimestamp(const LogData& log, double& target_time)
{
    if (log.timestamps.empty())
        return;
    const auto position = std::ranges::lower_bound(log.timestamps, target_time);
    const auto index = static_cast<std::size_t>(std::distance(log.timestamps.begin(), position));
    target_time = index > 0 ? log.timestamps[index - 1] : log.timestamps.front();
}

void moveToNextTimestamp(const LogData& log, double& target_time)
{
    if (log.timestamps.empty())
        return;
    const auto position = std::ranges::upper_bound(log.timestamps, target_time);
    target_time = position != log.timestamps.end() ? *position : log.timestamps.back();
}
} // namespace

bool ControlPanel::render(const I2CDeviceManager& devicemanager, const LogData& log, double& target_time,
                          bool& sync_timeline_positions)
{
    if (!ViewHelpers::beginFixedLeftWindow("Control Panel"))
    {
        ImGui::End();
        return false;
    }
    ImGui::Text("Active Devices: %zu", devicemanager.GetAllDevices().size());
    ImGui::Separator();
    if (ImGui::Button("<< Prev"))
        moveToPreviousTimestamp(log, target_time);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0F);
    const bool slider_changed =
        ImGui::SliderScalar("##TimeSlider", ImGuiDataType_Double, &target_time, &log.min_timestamp,
                            &log.max_timestamp, "Time: %.6f s");
    ImGui::SameLine();
    if (ImGui::Button("Next >>"))
        moveToNextTimestamp(log, target_time);
    ImGui::Checkbox("Sync timeline view positions across windows", &sync_timeline_positions);
    ImGui::End();
    return slider_changed;
}
