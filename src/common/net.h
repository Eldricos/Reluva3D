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

// Sockets as BSD sockets give them (TCP streams and UDP datagrams, IPv4),
// for standing in for an old program's own socket calls (Winsock 1.1).
// Each is a number; none of the calls waits: they say WouldBlock, and
// ready() says when to try again.
class Sockets {
public:
    virtual ~Sockets() = default;
    enum Result {
        Ok,
        WouldBlock,  // nothing yet (or, for connect, under way)
        InProgress,  // connect: started, ready() says when it's done
        Refused,     // connect: nothing listening
        Reset,       // the other end went
        NotConnected,
        AddressInUse,
        AddressNotAvailable,
        Unreachable,
        Invalid,     // not a socket, or not that kind of call for it
        Failed,      // anything else
    };
    enum Type { Stream, Datagram };
    enum Option { Broadcast, NoDelay, ReuseAddress, KeepAlive, ReceiveBuffer, SendBuffer, Linger };
    // A new socket, -1 if there can't be one.
    virtual int open(Type type) = 0;
    virtual void close(int s) = 0;
    // `at`'s ip 0 is every interface, port 0 any port.
    virtual Result bind(int s, const Address& at) = 0;
    virtual Result listen(int s, int backlog) = 0;
    virtual Result accept(int s, int& accepted, Address& from) = 0;
    virtual Result connect(int s, const Address& to) = 0;
    // `done` bytes sent or received: received 0 with Ok, the other end has
    // finished. `peek` leaves them to be read again.
    virtual Result send(int s, const uint8_t* data, size_t size, size_t& done) = 0;
    virtual Result receive(int s, uint8_t* data, size_t size, size_t& done, bool peek) = 0;
    virtual Result sendTo(int s, const Address& to, const uint8_t* data, size_t size) = 0;
    // A datagram larger than `size` is cut short (`done` is `size`).
    virtual Result receiveFrom(int s, uint8_t* data, size_t size, size_t& done, Address& from, bool peek) = 0;
    virtual Result setOption(int s, Option option, int value) = 0;
    // 0 shut for receiving, 1 for sending, 2 both.
    virtual Result shutdown(int s, int how) = 0;
    virtual Result localAddress(int s, Address& out) = 0;
    virtual Result peerAddress(int s, Address& out) = 0;
    // How many bytes could be received now (a datagram's size).
    virtual Result pending(int s, uint32_t& bytes) = 0;
    // Whether it could be read from (or accepted on) and written to without
    // waiting, and whether it has failed (a connect refused); waits up to
    // `timeoutMs` for any of those (0: not at all).
    struct Ready {
        bool read = false, write = false, failed = false;
    };
    virtual Ready ready(int s, int timeoutMs = 0) = 0;
    // The machine's name, and a name's address (false if not found).
    virtual std::string hostName() = 0;
    virtual bool resolve(const std::string& name, uint32_t& ip) = 0;
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
