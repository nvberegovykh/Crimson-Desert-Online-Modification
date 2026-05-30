// SPDX-License-Identifier: MIT
//
// Minimal RFC 6455 WebSocket client built on Winsock. Runs a single worker
// thread that owns the socket, performs the HTTP upgrade handshake, frames
// outgoing messages from a thread-safe queue, and parses incoming frames.
// Reconnects with exponential backoff.

#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include "protocol.h"

namespace cdmp {

class WsClient {
public:
    using PacketCallback =
        std::function<void(PacketType, const nlohmann::json&)>;
    using ConnCallback = std::function<void()>;

    WsClient() = default;
    ~WsClient();

    void SetOnConnected(ConnCallback cb) { onConnected_ = std::move(cb); }
    void SetOnDisconnected(ConnCallback cb) { onDisconnected_ = std::move(cb); }
    void SetOnPacket(PacketCallback cb) { onPacket_ = std::move(cb); }

    // Begin connecting to host:port. Spawns the worker thread if needed.
    void Connect(const std::string& host, uint16_t port);

    // Stop reconnecting and close the socket.
    void Disconnect();

    // Queue a packet for sending. Thread-safe; returns immediately.
    void Send(PacketType type, const nlohmann::json& payload);

    bool IsConnected() const { return connected_.load(); }

private:
    void WorkerLoop();
    bool DoConnectOnce();
    bool Handshake();
    bool SendFrame(const std::string& payload);
    bool ReceiveLoop();
    void FlushSendQueue();
    void CloseSocket();

    std::string host_;
    uint16_t port_ = 0;

    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};

    uintptr_t sock_ = ~static_cast<uintptr_t>(0); // INVALID_SOCKET

    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::queue<std::string> sendQueue_;

    ConnCallback onConnected_;
    ConnCallback onDisconnected_;
    PacketCallback onPacket_;
};

} // namespace cdmp
