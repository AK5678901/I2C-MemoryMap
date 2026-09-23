#include "control_panel.hpp"

#include "live_receiver.hpp"

#include "view_helpers.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstring>

namespace
{
void moveToPreviousTimestamp(const I2CEventProcessor::Info& info, Timestamp& target_time)
{
    if (info.timestamps.empty())
        return;
    const auto position = std::ranges::lower_bound(info.timestamps, target_time);
    const auto index = static_cast<std::size_t>(std::distance(info.timestamps.begin(), position));
    target_time = index > 0 ? info.timestamps[index - 1] : info.timestamps.front();
}

void moveToNextTimestamp(const I2CEventProcessor::Info& info, Timestamp& target_time)
{
    if (info.timestamps.empty())
        return;
    const auto position = std::ranges::upper_bound(info.timestamps, target_time);
    target_time = position != info.timestamps.end() ? *position : info.timestamps.back();
}
} // namespace

void ControlPanel::State::resetJumpInput()
{
    jump_text_.fill('\0');
    jump_initialized_ = false;
    jump_invalid_ = false;
    previous_format_.reset();
}

ControlPanel::Result ControlPanel::State::render(const I2CDeviceManager&, const I2CEventProcessor::Info& info,
                                                 TimeValue::DisplayFormat& format, Timestamp& target_time,
                                                 bool& sync_timeline_positions,
                                                 const LiveReceiver* live_receiver)
{
    Result result;
    if (!ViewHelpers::beginFixedLeftWindow("Control Panel"))
    {
        ImGui::End();
        return result;
    }
    constexpr ImGuiChildFlags section_flags = ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY;
    if (ImGui::BeginChild("DisplaySettings", ImVec2(0, 0), section_flags))
    {
        ImGui::TextUnformatted("Display settings");
        ImGui::Spacing();
        ImGui::Indent();
        int selected_format = static_cast<int>(format);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Time format");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(190.0F);
        const char* formats = info.has_csv_timestamps
                                  ? "UTC ISO 8601\0Local ISO 8601\0UTC short\0Local short\0Since first event\0CSV original\0"
                                  : "UTC ISO 8601\0Local ISO 8601\0UTC short\0Local short\0Since first event\0";
        if (ImGui::Combo("##Time format", &selected_format, formats))
            format = static_cast<TimeValue::DisplayFormat>(selected_format);
        ImGui::Unindent();
    }
    ImGui::EndChild();
    const TimeValue::DisplayView display(info, format);
    const bool format_changed = previous_format_ && *previous_format_ != format;
    if (!info.timestamps.empty() && (!jump_initialized_ || format_changed))
    {
        const auto value = display.formatTimestamp(info.timestamps.front());
        jump_text_.fill('\0');
        std::memcpy(jump_text_.data(), value.data(), std::min(value.size(), jump_text_.size() - 1));
        jump_initialized_ = true;
        jump_invalid_ = false;
    }
    previous_format_ = format;

    if (ImGui::BeginChild("TimeNavigation", ImVec2(0, 0), section_flags))
    {
        ImGui::TextUnformatted("Time navigation");
        ImGui::Spacing();
        ImGui::Indent();
        ImGui::Checkbox("Sync timeline view positions across windows", &sync_timeline_positions);
        if (ImGui::Button("<< Prev"))
            moveToPreviousTimestamp(info, target_time);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0F);
        const Timestamp minimum = info.timestamps.empty() ? 0 : info.min_timestamp;
        const Timestamp maximum = info.timestamps.empty() ? 0 : info.max_timestamp;
        result.slider_changed = ImGui::SliderScalar("##TimeSlider", ImGuiDataType_S64, &target_time,
                                                    &minimum, &maximum, "");
        ImGui::SameLine();
        if (ImGui::Button("Next >>"))
            moveToNextTimestamp(info, target_time);
        ImGui::TextWrapped("Selected: %s", display.formatTimestamp(target_time).c_str());
        ImGui::SetNextItemWidth(265.0F);
        const bool submitted = ImGui::InputText("Time##JumpTime", jump_text_.data(), jump_text_.size(),
                                                ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::IsItemEdited())
            jump_invalid_ = false;
        ImGui::SameLine();
        const bool clicked = ImGui::Button("Go to nearest transaction");
        if (submitted || clicked)
        {
            result.jump_time = display.parseDisplayed(jump_text_.data(),
                                                        info.timestamps.empty() ? target_time : info.timestamps.front());
            jump_invalid_ = !result.jump_time.has_value();
        }
        if (jump_invalid_)
            ImGui::TextColored(ImVec4(1.0F, 0.25F, 0.25F, 1.0F), "Invalid time for selected format");
        ImGui::Unindent();
    }
    ImGui::EndChild();

    if (ImGui::BeginChild("CaptureStatus", ImVec2(0, 0), section_flags))
    {
        ImGui::TextUnformatted("Capture status");
        ImGui::Spacing();
        ImGui::Indent();
        if (live_receiver != nullptr)
        {
            if (live_receiver->hasSequence())
                ImGui::Text("Live UDP frames: %llu received, %llu missing",
                            static_cast<unsigned long long>(live_receiver->receivedFrames()),
                            static_cast<unsigned long long>(live_receiver->missingFrames()));
            else
                ImGui::TextDisabled("Live UDP: waiting for sequenced frames");
            ImGui::Text("Receive queue: %zu / %zu frames", live_receiver->queuedFrames(),
                        LiveReceiver::max_queued_frames);
            const auto dropped = live_receiver->droppedQueuedFrames();
            if (dropped > 0)
                ImGui::TextColored(ImVec4(1.0F, 0.25F, 0.25F, 1.0F),
                                   "WARNING: RECEIVE QUEUE OVERFLOW - %llu FRAMES LOST",
                                   static_cast<unsigned long long>(dropped));
            else
                ImGui::TextDisabled("Receive queue drops: 0");
            ImGui::TextDisabled("OS receive buffer capacity: %d bytes", live_receiver->socketBufferBytes());
        }
        else
            ImGui::TextDisabled("Live reception inactive");
        ImGui::Unindent();
    }
    ImGui::EndChild();
    ImGui::End();
    return result;
}
