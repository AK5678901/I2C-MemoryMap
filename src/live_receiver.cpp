#include "live_receiver.hpp"
#include "i2c_bus_manager.hpp"

#include <algorithm>
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
    address.sin_port = htons(48151);
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
    char buffer[2048];
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

bool LiveReceiver::poll(I2CBusManager& bus_manager, bool& reset)
{
    reset = false;
    if (!ready())
        return false;
    bool changed = false;
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
        bool packet_reset = false;
        changed |= protocol_.process(packet, bus_manager, packet_reset);
        reset |= packet_reset;
    }
    return changed;
}
