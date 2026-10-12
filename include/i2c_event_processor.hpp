#pragma once

#include "i2c_event.hpp"
#include "timeline_info.hpp"
#include <string>
#include <vector>

class I2CDevice;
class I2CDeviceManager;

class I2CEventProcessor
{
  public:
    struct Info
    {
        TimelineInfo timeline;
        std::vector<std::uint8_t> active_addresses;
    };

    const Info& GetInfo() const { return info_; }

    // Returns true only when a snapshot is added.
    bool process(const I2CEvent& event, I2CDeviceManager& devices);
    void reset(I2CDeviceManager& devices);

    // Source-specific display information supplied by the CSV parser.
    void SetCsvTimeFormat(bool absolute);
    void RecordCsvTimestamp(Timestamp timestamp, const std::string& text);

  private:
    Info info_;
    I2CDevice* current_device_{nullptr};
};
