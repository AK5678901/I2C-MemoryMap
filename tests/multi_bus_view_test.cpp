#include "device_window.hpp"
#include "timeline_view.hpp"

#include <stdexcept>

int main()
{
    I2CBusManager bus_manager;
    const DeviceKey main{"main", 0x48}, sub{"sub", 0x48};
    using Type = I2CEvent::Type;
    const auto send = [&](const DeviceKey& key, const I2CEvent& event) {
        bus_manager.process(key.bus, event);
    };
    for (const auto& key : {main, sub})
    {
        send(key, {Type::Address, 1, key.address});
        send(key, {Type::Data, 2, 0x10});
    }
    send(main, {Type::Data, 3, 0xAA});
    send(sub, {Type::Data, 3, 0xBB});
    send(main, {Type::Data, 4, 0xCC});
    TimelineView::State state;
    TimelineView::rebuild(state, bus_manager);
    if (state.access_timeline.size() != 3 || state.grouped_timeline.size() != 2)
        throw std::runtime_error("Interleaved buses must not split transactions");
    const auto& first = state.grouped_timeline.front();
    if (first.device_address != main || first.bytes != std::vector<std::uint8_t>{0xAA, 0xCC} ||
        state.grouped_timeline.back().device_address != sub)
        throw std::runtime_error("Transaction bytes must stay with their bus");
    if (DeviceWindow::name(*bus_manager.GetDevice(main), main) == DeviceWindow::name(*bus_manager.GetDevice(sub), sub))
        throw std::runtime_error("Window identity must include the bus");
}
