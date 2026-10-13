#pragma once

#include "live_protocol.hpp"

class I2CBusManager;

#include <cstdint>
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#define NOMINMAX
#include <winsock2.h>

class LiveReceiver final
{
  public:
    LiveReceiver();
    ~LiveReceiver();
    LiveReceiver(const LiveReceiver&) = delete;
    LiveReceiver& operator=(const LiveReceiver&) = delete;
    [[nodiscard]] bool ready() const { return socket_ != INVALID_SOCKET; }
    [[nodiscard]] bool poll(I2CBusManager& bus_manager, bool& reset);
    [[nodiscard]] std::uint64_t receivedFrames() const { return protocol_.receivedFrames(); }
    [[nodiscard]] std::uint64_t missingFrames() const { return protocol_.missingFrames(); }
    [[nodiscard]] bool hasSequence() const { return protocol_.hasSequence(); }
    [[nodiscard]] std::size_t queuedFrames() const;
    [[nodiscard]] std::uint64_t droppedQueuedFrames() const { return dropped_queued_frames_.load(); }
    [[nodiscard]] int socketBufferBytes() const { return socket_buffer_bytes_; }
    static constexpr std::size_t max_queued_frames = 32768;

  private:
    void receiveLoop();
    SOCKET socket_{INVALID_SOCKET};
    std::atomic<bool> running_{false};
    std::thread receive_thread_;
    mutable std::mutex queue_mutex_;
    std::deque<std::string> queue_;
    std::atomic<std::uint64_t> dropped_queued_frames_{0};
    int socket_buffer_bytes_{0};
    LiveProtocol protocol_;
};
