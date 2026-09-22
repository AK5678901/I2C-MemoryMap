#pragma once

#include "control_panel.hpp"
#include "i2c_device.hpp"
#include "log_data.hpp"
#include "timeline_view.hpp"

#include <cstddef>
#include <cstdint>
#include <map>

class LiveReceiver;

class MainWindow final
{
  public:
    void reset(const LogData& log);
    void refreshLive(I2CDeviceManager& devicemanager, const LogData& log);
    void render(I2CDeviceManager& devicemanager, const LogData& log, const LiveReceiver* live_receiver = nullptr);

  private:
    void handleArrowKeys(const LogData& log);
    void renderDeviceList(const I2CDeviceManager& devicemanager, const LogData& log);
    std::map<uint8_t, bool> device_visibility_;
    TimelineView::AccessTimeline access_timeline_;
    TimeValue::DisplayFormat time_format_{TimeValue::DisplayFormat::ShortLocal};
    ControlPanel::State control_panel_;

    Timestamp target_time_{0};
    bool first_layout_{true};
    bool arrange_devices_{false};
    bool sync_all_devices_timeline_{true};
    bool scroll_all_devices_timeline_{false};
    bool scroll_device_timeline_{false};
    bool scroll_timelines_to_target_{false};
    uint8_t scroll_device_address_{0};
    std::size_t scroll_history_index_{0};
};
