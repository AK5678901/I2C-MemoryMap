#include "live_receiver.hpp"
#include "time_value.hpp"

#include <algorithm>
#include <charconv>
#include <optional>
#include <string_view>
#include <vector>

LiveReceiver::LiveReceiver()
{
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        return;
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == INVALID_SOCKET)
        return;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(48152);
    const int requested_buffer = 1024 * 1024;
    setsockopt(socket_, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&requested_buffer),
               sizeof(requested_buffer));
    int option_length = sizeof(socket_buffer_bytes_);
    getsockopt(socket_, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&socket_buffer_bytes_), &option_length);
    const DWORD timeout_ms = 100;
    setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
    if (bind(socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
    {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        return;
    }
    running_ = true;
    receive_thread_ = std::thread(&LiveReceiver::receiveLoop, this);
}

LiveReceiver::~LiveReceiver()
{
    running_ = false;
    if (receive_thread_.joinable())
        receive_thread_.join();
    if (socket_ != INVALID_SOCKET)
        closesocket(socket_);
    WSACleanup();
}

std::size_t LiveReceiver::queuedFrames() const
{
    std::lock_guard lock(queue_mutex_);
    return queue_.size();
}

void LiveReceiver::receiveLoop()
{
    char buffer[128];
    while (running_)
    {
        const int length = recv(socket_, buffer, sizeof(buffer), 0);
        if (length == SOCKET_ERROR)
        {
            const int error = WSAGetLastError();
            if (error == WSAETIMEDOUT || error == WSAEWOULDBLOCK || error == WSAEMSGSIZE)
                continue;
            break;
        }
        std::lock_guard lock(queue_mutex_);
        if (queue_.size() == max_queued_frames)
            ++dropped_queued_frames_;
        else
            queue_.emplace_back(buffer, static_cast<std::size_t>(length));
    }
}

bool LiveReceiver::poll(I2CDeviceManager& devices, I2CEventProcessor& processor, bool& reset)
{
    reset = false;
    if (!ready())
        return false;
    bool changed = false;
    const auto reset_capture = [&]() {
        processor.reset(devices);
        last_frame_timestamp_ = std::numeric_limits<Timestamp>::min();
        last_sequence_ = 0;
        received_frames_ = 0;
        missing_frames_ = 0;
        has_sequence_ = false;
        expect_first_sequence_ = true;
        changed = false;
        reset = true;
    };
    const auto record_sequence = [&](std::optional<std::uint64_t> sequence) {
        if (!sequence)
            return true; // Older bridge versions have no sequence numbers.
        if (has_sequence_ && *sequence <= last_sequence_)
            return false; // Duplicate or late datagram.
        if (!has_sequence_ && expect_first_sequence_)
            missing_frames_ += *sequence;
        if (has_sequence_ && *sequence - last_sequence_ > 1)
            missing_frames_ += *sequence - last_sequence_ - 1;
        last_sequence_ = *sequence;
        has_sequence_ = true;
        expect_first_sequence_ = false;
        ++received_frames_;
        return true;
    };
    std::vector<std::string> batch;
    {
        std::lock_guard lock(queue_mutex_);
        const auto count = std::min<std::size_t>(queue_.size(), 1024);
        batch.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            batch.push_back(std::move(queue_.front()));
            queue_.pop_front();
        }
    }
    for (const auto& packet : batch)
    {
        // Q,<sequence>,<frame> wraps A, D and S; R starts a new capture.
        std::string_view frame(packet);
        if (frame == "R")
        {
            reset_capture();
            continue;
        }
        std::optional<std::uint64_t> sequence;
        if (frame.starts_with("Q,"))
        {
            const auto end = frame.find(',', 2);
            if (end == std::string_view::npos)
                continue;
            std::uint64_t value = 0;
            const auto parsed = std::from_chars(frame.data() + 2, frame.data() + end, value);
            if (parsed.ec != std::errc{} || parsed.ptr != frame.data() + end)
                continue;
            sequence = value;
            frame.remove_prefix(end + 1);
        }
        if (sequence && *sequence == 0 && has_sequence_ && last_sequence_ > 0)
            reset_capture();
        if (frame == "S")
        {
            if (!record_sequence(sequence))
                continue;
            processor.process({I2CEvent::Type::Stop}, devices);
            continue;
        }
        if (frame.size() < 3 || frame[1] != ',')
            continue;
        const auto comma = frame.find(',', 2);
        if (comma == std::string_view::npos)
            continue;
        const auto parsed_time = TimeValue::parse(frame.substr(2, comma - 2));
        if (!parsed_time)
            continue;
        const Timestamp timestamp = *parsed_time;
        if (timestamp < last_frame_timestamp_)
            reset_capture();
        if (!record_sequence(sequence))
            continue;
        if (frame[0] == 'A')
        {
            const auto next = frame.find(',', comma + 1);
            if (next == std::string_view::npos || next + 2 != frame.size())
                continue;
            unsigned int address = 0;
            const auto parsed = std::from_chars(frame.data() + comma + 1, frame.data() + next, address);
            if (parsed.ec != std::errc{} || parsed.ptr != frame.data() + next || address > 127 || (frame[next + 1] != '0' && frame[next + 1] != '1'))
                continue;
            changed |= processor.process({I2CEvent::Type::Address, timestamp,
                                           static_cast<std::uint8_t>(address), frame[next + 1] == '1'}, devices);
            last_frame_timestamp_ = timestamp;
        }
        else if (frame[0] == 'D')
        {
            unsigned int value = 0;
            const auto parsed = std::from_chars(frame.data() + comma + 1, frame.data() + frame.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != frame.data() + frame.size() || value > 255)
                continue;
            changed |= processor.process({I2CEvent::Type::Data, timestamp,
                                           static_cast<std::uint8_t>(value)}, devices);
            last_frame_timestamp_ = timestamp;
        }
    }
    return changed;
}
