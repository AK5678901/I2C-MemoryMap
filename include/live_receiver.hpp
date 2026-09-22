#pragma once

#include "i2c_device.hpp"
#include "log_data.hpp"

#include <cstdint>
#include <limits>
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
    [[nodiscard]] bool poll(I2CDeviceManager& devices, LogData& log, bool& reset);
    [[nodiscard]] std::uint64_t receivedFrames() const { return received_frames_; }
    [[nodiscard]] std::uint64_t missingFrames() const { return missing_frames_; }
    [[nodiscard]] bool hasSequence() const { return has_sequence_; }
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
    I2CDevice* current_device_{nullptr};
    Timestamp last_frame_timestamp_{std::numeric_limits<Timestamp>::min()};
    std::uint64_t last_sequence_{0};
    std::uint64_t received_frames_{0};
    std::uint64_t missing_frames_{0};
    bool has_sequence_{false};
    bool expect_first_sequence_{false};
};
