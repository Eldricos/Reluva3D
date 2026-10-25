// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace reluva::net {

// An IPv4 address and UDP port, as the games of the time used.
struct Address {
    uint32_t ip = 0; // a.b.c.d as (a << 24) | (b << 16) | (c << 8) | d
    uint16_t port = 0;

    bool operator==(const Address& o) const { return ip == o.ip && port == o.port; }
    bool operator!=(const Address& o) const { return !(*this == o); }
    bool operator<(const Address& o) const { return ip != o.ip ? ip < o.ip : port < o.port; }
    bool isLoopback() const { return (ip >> 24) == 127; }
    // Private networks (10/8, 172.16/12, 192.168/16) and the machine itself.
    bool isLan() const;
    // "a.b.c.d:port".
    std::string toString() const;
    static Address broadcast(uint16_t port) { return {0xffffffffu, port}; }
};

// "a.b.c.d" or "a.b.c.d:port" (the port `defaultPort` when not given);
// false if it isn't one. Host names are the system's to look up
// (System::resolveHost).
bool parseAddress(const std::string& text, uint16_t defaultPort, Address& out);
// Splits "host:port" into the host and port (`defaultPort` when not given).
std::pair<std::string, uint16_t> splitHostPort(const std::string& text, uint16_t defaultPort);

// A UDP socket: datagrams sent and received whole, without waiting.
class UdpSocket {
public:
    virtual ~UdpSocket() = default;
    // The port it's bound to.
    virtual uint16_t port() const = 0;
    // Sends `data` to `to` (Address::broadcast for the local network);
    // false if it couldn't.
    virtual bool send(const Address& to, const std::vector<uint8_t>& data) = 0;
    // The next datagram that has arrived, if any, and who sent it.
    virtual bool receive(Address& from, std::vector<uint8_t>& data) = 0;
};

// Sockets that only reach each other, in memory: for tests, and for a
// game's own player talking to its own server without the network.
// Each socket gets the address 127.0.0.1 and its port; a broadcast
// reaches every other socket on that port.
class LoopbackNetwork : public std::enable_shared_from_this<LoopbackNetwork> {
public:
    static std::shared_ptr<LoopbackNetwork> create() { return std::shared_ptr<LoopbackNetwork>(new LoopbackNetwork); }
    // A socket on `port` (0: the next one free from 27960); nullptr if
    // it's taken.
    std::unique_ptr<UdpSocket> open(uint16_t port);
    // Every datagram from now on that `drop` says yes to is lost, as on a
    // real network.
    void setDropFilter(bool (*drop)(const Address& from, const Address& to, size_t index));

private:
    LoopbackNetwork() = default;
    class Socket;
    friend class Socket;
    struct Queue {
        std::deque<std::pair<Address, std::vector<uint8_t>>> datagrams;
        bool open = false;
    };
    std::vector<std::pair<uint16_t, std::shared_ptr<Queue>>> queues_;
    bool (*drop_)(const Address&, const Address&, size_t) = nullptr;
    size_t sent_ = 0;
    bool deliver(const Address& from, const Address& to, const std::vector<uint8_t>& data);
};

} // namespace reluva::net
