#include "live_protocol.hpp"
#include "i2c_bus_manager.hpp"

#include <stdexcept>

namespace
{
void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
std::string envelope(const std::string& bus, const std::string& frame)
{
    return "B," + std::to_string(bus.size()) + "," + bus + "," + frame;
}
}

int main()
{
    LiveProtocol protocol;
    I2CBusManager buses;
    bool reset = false;
    const std::string a = "I2C", b = "I2C [1],日本語";
    const auto send = [&](const std::string& bus, const std::string& frame) {
        return protocol.process(envelope(bus, frame), buses, reset);
    };
    const auto address = [](int second) {
        return "A,2026-09-22T08:20:" + std::to_string(second) + ".000000000+00:00,80,0";
    };
    const auto data = [](int second, int value) {
        return "D,2026-09-22T08:20:" + std::to_string(second) + ".000000000+00:00," + std::to_string(value);
    };
    send(a, "R");
    send(b, "R");
    send(a, "Q,0," + address(30));
    send(b, "Q,0," + address(10)); // Earlier global time must not reset bus A.
    check(!reset, "Bus timestamps must be independent");
    send(a, "Q,1," + data(31, 0));
    send(b, "Q,1," + data(11, 0));
    send(a, "Q,2," + data(32, 66));
    send(b, "Q,2," + data(12, 99));
    const auto* da = buses.GetDevice({a, 80});
    const auto* db = buses.GetDevice({b, 80});
    check(da && db && da != db, "Same address must produce independent devices");
    check(da->GetSnapshotCount() == 1 && db->GetSnapshotCount() == 1, "Interleaved snapshots");
    check(da->GetSnapshotByIndex(0).GetUpdatedRegister().value.write_data == std::vector<std::uint8_t>{66} &&
          db->GetSnapshotByIndex(0).GetUpdatedRegister().value.write_data == std::vector<std::uint8_t>{99}, "Independent register values");
    check(buses.GetInfo().timeline.timestamps.size() == 2 && protocol.receivedFrames() == 6 &&
          protocol.missingFrames() == 0, "Merged timeline and independent sequences");
    send(a, "Q,3,S");
    send(b, "Q,4," + data(13, 100)); // One missing packet only on B.
    check(db->GetSnapshotCount() == 2 && protocol.missingFrames() == 1, "STOP must affect only its bus");
    send(b, "Q,4," + data(13, 100));
    send(b, "Q,2," + data(12, 99));
    check(!reset && db->GetSnapshotCount() == 2 && protocol.receivedFrames() == 8,
          "Duplicate and late frames must not reset or modify history");
    check(send(a, "R") && reset, "Reset must request UI refresh");
    check(da->GetSnapshotCount() == 0 && db->GetSnapshotCount() == 2 &&
          protocol.receivedFrames() == 4 && protocol.missingFrames() == 1, "Reset must preserve other bus state");
    send(a, "Q,2," + address(40));
    check(protocol.missingFrames() == 3, "Initial missing sequence is bus-local");
    send(a, "Q,3," + data(41, 0));
    send(a, "Q,4," + data(42, 77));
    send(a, "Q,0," + address(50)); // Recover if the restart R packet was lost.
    check(reset && da->GetSnapshotCount() == 0 && db->GetSnapshotCount() == 2,
          "Sequence restart must reset only one bus");
    for (const auto* invalid : {"B,0,,R", "B,2049,I2C,R", "B,no,I2C,R", "B,3,I2C", "B,3,I2CxR"})
        check(!protocol.process(invalid, buses, reset) && !reset, "Malformed envelope must be ignored");
    protocol.process("R", buses, reset);
    protocol.process(address(20), buses, reset);
    protocol.process(data(21, 0), buses, reset);
    protocol.process(data(22, 55), buses, reset);
    check(da->GetSnapshotCount() == 1 && db->GetSnapshotCount() == 2, "Legacy frames use I2C only");
}
