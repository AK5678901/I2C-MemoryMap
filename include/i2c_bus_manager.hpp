#pragma once

#include "device_key.hpp"
#include "i2c_device.hpp"
#include "i2c_event_processor.hpp"

struct I2CBus
{
    I2CDeviceManager devices;
    I2CEventProcessor processor;

    // Returns true when this bus adds a device snapshot.
    bool process(const I2CEvent& event)
    {
        return processor.process(event, devices);
    }
};

// Owns independent bus decoders and provides a non-owning device index for the UI.
class I2CBusManager
{
  public:
    struct Info
    {
        TimelineInfo timeline;
        std::vector<DeviceKey> active_devices;
    };

    // Mutations go through this manager. Returned views remain valid until the
    // next mutation and aggregate read; callers must not retain them across updates.
    bool process(const std::string& name, const I2CEvent& event)
    {
        dirty_ = true;
        return getOrCreateBus(name).process(event);
    }

    bool processCsvEvent(const std::string& name, const I2CEvent& event,
                         const std::string& raw_timestamp, bool absolute)
    {
        dirty_ = true;
        auto& bus = getOrCreateBus(name);
        bus.processor.SetCsvTimeFormat(absolute);
        bus.processor.RecordCsvTimestamp(event.timestamp, raw_timestamp);
        return bus.process(event);
    }

    // Transfer fully configured devices; never expose mutable owned devices.
    void registerDevices(const std::string& name, I2CDeviceManager&& devices)
    {
        dirty_ = true;
        getOrCreateBus(name).devices.mergeFrom(std::move(devices));
    }

    const Info& GetInfo() const
    {
        ensureAggregated();
        return info_;
    }

    const auto& GetAllDevices() const
    {
        ensureAggregated();
        return devices_;
    }

    const I2CDevice* GetDevice(const DeviceKey& key) const
    {
        ensureAggregated();
        const auto found = devices_.find(key);
        return found == devices_.end() ? nullptr : found->second;
    }

    void reset()
    {
        dirty_ = true;
        for (auto& [name, bus] : buses_)
            bus.processor.reset(bus.devices);
    }

    void resetBus(const std::string& name)
    {
        if (const auto found = buses_.find(name); found != buses_.end())
        {
            dirty_ = true;
            found->second.processor.reset(found->second.devices);
        }
    }

  private:
    I2CBus& getOrCreateBus(const std::string& name) { return buses_.try_emplace(name).first->second; }

    // Called on the UI thread, after the receiver's queued events are processed.
    void ensureAggregated() const
    {
        if (!dirty_)
            return;
        Info next_info;
        std::map<DeviceKey, const I2CDevice*> next_devices;
        for (const auto& [name, bus] : buses_)
        {
            for (const auto& [address, device] : bus.devices.GetAllDevices())
                next_devices.emplace(DeviceKey{name, address}, device.get());
            const auto& source = bus.processor.GetInfo();
            for (const auto address : source.active_addresses)
                next_info.active_devices.push_back({name, address});
            next_info.timeline.timestamps.insert(next_info.timeline.timestamps.end(), source.timeline.timestamps.begin(), source.timeline.timestamps.end());
            next_info.timeline.has_absolute_timestamps &= source.timeline.has_absolute_timestamps;
            next_info.timeline.has_csv_timestamps |= source.timeline.has_csv_timestamps;
            next_info.timeline.raw_csv_timestamps.insert(source.timeline.raw_csv_timestamps.begin(), source.timeline.raw_csv_timestamps.end());
        }
        std::ranges::sort(next_info.timeline.timestamps);
        next_info.timeline.timestamps.erase(std::unique(next_info.timeline.timestamps.begin(), next_info.timeline.timestamps.end()), next_info.timeline.timestamps.end());
        if (!next_info.timeline.timestamps.empty())
        {
            next_info.timeline.min_timestamp = next_info.timeline.timestamps.front();
            next_info.timeline.max_timestamp = next_info.timeline.timestamps.back();
        }
        info_ = std::move(next_info);
        devices_ = std::move(next_devices);
        dirty_ = false;
    }

    std::map<std::string, I2CBus> buses_;
    mutable std::map<DeviceKey, const I2CDevice*> devices_;
    mutable Info info_;
    mutable bool dirty_{true};
};
