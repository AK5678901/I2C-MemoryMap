#pragma once

#include "control_panel.hpp"
#include "device_window.hpp"
#include "i2c_device.hpp"
#include "i2c_event_processor.hpp"
#include "timeline_view.hpp"
#include "timeline_sync.hpp"

#include <cstddef>
#include <cstdint>
#include <map>

class LiveReceiver;

class MainWindow final
{
  public:
    void reset(const I2CEventProcessor::Info& info);
    void refreshLive(I2CDeviceManager& devicemanager, const I2CEventProcessor::Info& info);
    void render(I2CDeviceManager& devicemanager, const I2CEventProcessor::Info& info, const LiveReceiver* live_receiver = nullptr);

  private:
    void handleArrowKeys(const I2CEventProcessor::Info& info);
    void renderDeviceList(const I2CDeviceManager& devicemanager, const I2CEventProcessor::Info& info);
    std::map<std::uint8_t, DeviceWindow::State> device_window_states_;
    TimelineView::State timeline_view_;
    TimeValue::DisplayFormat time_format_{TimeValue::DisplayFormat::ShortLocal};
    ControlPanel::State control_panel_;

    TimelineSyncState timeline_sync_;
    bool first_layout_{true};
    bool arrange_devices_{false};
};
