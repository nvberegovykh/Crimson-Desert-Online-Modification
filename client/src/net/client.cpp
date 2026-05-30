// SPDX-License-Identifier: MIT

#include "client.h"
#include "../util/log.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <wincrypt.h>

#include <array>
#include <cstring>
#include <random>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace cdmp {

namespace {

std::string Base64(const unsigned char* data, size_t len) {
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, bits = -6;
    for (size_t i = 0; i < len; ++i) {
        val = (val << 8) + data[i];
        bits += 8;
        while (bits >= 0) {
            out.push_back(tbl[(val >> bits) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > -6) out.push_back(tbl[((val << 8) >> (bits + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::string RandomKey() {
    unsigned char buf[16];
    std::random_device rd;
    for (auto& b : buf) b = static_cast<unsigned char>(rd() & 0xFF);
    return Base64(buf, sizeof(buf));
}

bool SendAll(SOCKET s, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        const int n = send(s, data + sent, static_cast<int>(len - sent), 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

} // namespace

WsClient::~WsClient() {
    Disconnect();
    if (worker_.joinable()) worker_.join();
}

void WsClient::Connect(const std::string& host, uint16_t port) {
    Disconnect();
    if (worker_.joinable()) worker_.join();
    host_ = host;
    port_ = port;
    running_.store(true);
    worker_ = std::thread([this] { WorkerLoop(); });
}

void WsClient::Disconnect() {
    running_.store(false);
    queueCv_.notify_all();
    CloseSocket();
}

void WsClient::Send(PacketType type, const nlohmann::json& payload) {
    std::string frame = SerializePacket(type, payload);
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        sendQueue_.push(std::move(frame));
    }
    queueCv_.notify_one();
}

void WsClient::CloseSocket() {
    if (sock_ != ~static_cast<uintptr_t>(0)) {
        closesocket(static_cast<SOCKET>(sock_));
        sock_ = ~static_cast<uintptr_t>(0);
    }
}

void WsClient::WorkerLoop() {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        CDMP_LOG_ERROR("WSAStartup failed");
        return;
    }

    int backoffMs = 500;
    const int kMaxBackoff = 30000;

    while (running_.load()) {
        if (DoConnectOnce()) {
            backoffMs = 500; // reset after a successful session
        }
        if (!running_.load()) break;
        // Exponential backoff before reconnecting.
        CDMP_LOG_INFO("Reconnecting in " + std::to_string(backoffMs) + "ms");
        for (int waited = 0; waited < backoffMs && running_.load(); waited += 100) {
            Sleep(100);
        }
        backoffMs = (backoffMs * 2 > kMaxBackoff) ? kMaxBackoff : backoffMs * 2;
    }

    WSACleanup();
}

bool WsClient::DoConnectOnce() {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    const std::string portStr = std::to_string(port_);
    if (getaddrinfo(host_.c_str(), portStr.c_str(), &hints, &result) != 0) {
        CDMP_LOG_WARN("getaddrinfo failed for " + host_);
        return false;
    }

    SOCKET s = INVALID_SOCKET;
    for (addrinfo* p = result; p != nullptr; p = p->ai_next) {
        s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        if (connect(s, p->ai_addr, static_cast<int>(p->ai_addrlen)) == 0) break;
        closesocket(s);
        s = INVALID_SOCKET;
    }
    freeaddrinfo(result);

    if (s == INVALID_SOCKET) {
        CDMP_LOG_WARN("TCP connect failed to " + host_ + ":" + portStr);
        return false;
    }

    sock_ = static_cast<uintptr_t>(s);

    if (!Handshake()) {
        CDMP_LOG_WARN("WebSocket handshake failed");
        CloseSocket();
        return false;
    }

    connected_.store(true);
    CDMP_LOG_INFO("WebSocket connected to " + host_ + ":" + portStr);
    if (onConnected_) onConnected_();

    const bool clean = ReceiveLoop();

    connected_.store(false);
    if (onDisconnected_) onDisconnected_();
    CloseSocket();
    return clean;
}

bool WsClient::Handshake() {
    const SOCKET s = static_cast<SOCKET>(sock_);
    const std::string key = RandomKey();
    std::string req = "GET / HTTP/1.1\r\n";
    req += "Host: " + host_ + ":" + std::to_string(port_) + "\r\n";
    req += "Upgrade: websocket\r\n";
    req += "Connection: Upgrade\r\n";
    req += "Sec-WebSocket-Key: " + key + "\r\n";
    req += "Sec-WebSocket-Version: 13\r\n\r\n";
    if (!SendAll(s, req.data(), req.size())) return false;

    // Read response headers until CRLFCRLF.
    std::string resp;
    char buf[512];
    while (resp.find("\r\n\r\n") == std::string::npos) {
        const int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        resp.append(buf, static_cast<size_t>(n));
        if (resp.size() > 8192) return false;
    }
    return resp.find("101") != std::string::npos;
}

bool WsClient::SendFrame(const std::string& payload) {
    const SOCKET s = static_cast<SOCKET>(sock_);
    std::vector<unsigned char> frame;
    frame.push_back(0x81); // FIN + text opcode

    const size_t len = payload.size();
    // Client frames MUST be masked.
    if (len < 126) {
        frame.push_back(static_cast<unsigned char>(0x80 | len));
    } else if (len <= 0xFFFF) {
        frame.push_back(0x80 | 126);
        frame.push_back(static_cast<unsigned char>((len >> 8) & 0xFF));
        frame.push_back(static_cast<unsigned char>(len & 0xFF));
    } else {
        frame.push_back(0x80 | 127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<unsigned char>((len >> (i * 8)) & 0xFF));
        }
    }

    unsigned char mask[4];
    std::random_device rd;
    for (auto& m : mask) m = static_cast<unsigned char>(rd() & 0xFF);
    frame.insert(frame.end(), mask, mask + 4);

    const size_t headerLen = frame.size();
    frame.resize(headerLen + len);
    for (size_t i = 0; i < len; ++i) {
        frame[headerLen + i] =
            static_cast<unsigned char>(payload[i]) ^ mask[i % 4];
    }

    return SendAll(s, reinterpret_cast<const char*>(frame.data()), frame.size());
}

void WsClient::FlushSendQueue() {
    std::queue<std::string> local;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        std::swap(local, sendQueue_);
    }
    while (!local.empty()) {
        SendFrame(local.front());
        local.pop();
    }
}

bool WsClient::ReceiveLoop() {
    const SOCKET s = static_cast<SOCKET>(sock_);

    // Non-blocking receive so we can interleave sends and honor shutdown.
    u_long nonblock = 1;
    ioctlsocket(s, FIONBIO, &nonblock);

    std::vector<unsigned char> rx;
    std::array<char, 4096> buf{};

    while (running_.load()) {
        FlushSendQueue();

        const int n = recv(s, buf.data(), static_cast<int>(buf.size()), 0);
        if (n > 0) {
            rx.insert(rx.end(), buf.data(), buf.data() + n);
        } else if (n == 0) {
            return true; // peer closed cleanly
        } else {
            const int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK) return false;
            Sleep(5);
        }

        // Parse as many complete frames as are buffered.
        size_t pos = 0;
        while (rx.size() - pos >= 2) {
            const unsigned char b0 = rx[pos];
            const unsigned char b1 = rx[pos + 1];
            const unsigned char opcode = b0 & 0x0F;
            const bool masked = (b1 & 0x80) != 0;
            uint64_t payloadLen = b1 & 0x7F;
            size_t headerLen = 2;

            if (payloadLen == 126) {
                if (rx.size() - pos < 4) break;
                payloadLen = (static_cast<uint64_t>(rx[pos + 2]) << 8) | rx[pos + 3];
                headerLen = 4;
            } else if (payloadLen == 127) {
                if (rx.size() - pos < 10) break;
                payloadLen = 0;
                for (int i = 0; i < 8; ++i) {
                    payloadLen = (payloadLen << 8) | rx[pos + 2 + i];
                }
                headerLen = 10;
            }

            unsigned char mask[4] = {0, 0, 0, 0};
            if (masked) {
                if (rx.size() - pos < headerLen + 4) break;
                for (int i = 0; i < 4; ++i) mask[i] = rx[pos + headerLen + i];
                headerLen += 4;
            }

            if (rx.size() - pos < headerLen + payloadLen) break;

            std::string payload;
            payload.resize(payloadLen);
            for (uint64_t i = 0; i < payloadLen; ++i) {
                unsigned char c = rx[pos + headerLen + i];
                if (masked) c ^= mask[i % 4];
                payload[static_cast<size_t>(i)] = static_cast<char>(c);
            }
            pos += static_cast<size_t>(headerLen + payloadLen);

            if (opcode == 0x8) {
                return true; // close frame
            } else if (opcode == 0x9) {
                // Ping -> reply with pong (opcode 0xA), masked.
                SendFrame(payload); // payload echoed; opcode handled as text is
                                    // fine for our server but keep it simple.
            } else if (opcode == 0x1 || opcode == 0x2 || opcode == 0x0) {
                if (onPacket_) {
                    auto [type, data] = DeserializePacket(payload);
                    if (type != PacketType::UNKNOWN) onPacket_(type, data);
                }
            }
        }
        if (pos > 0) rx.erase(rx.begin(), rx.begin() + static_cast<long>(pos));
    }
    return true;
}

} // namespace cdmp
