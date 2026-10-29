// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Asking the router to pass a UDP port on to this machine, so others can
// reach a game it hosts from the Internet: UPnP's Internet Gateway Device
// (the UPnP Device Architecture's discovery, SSDP, and its
// WANIPConnection service's AddPortMapping), else NAT-PMP (RFC 6886).
// Both are published standards; Reluva's own code. See
// docs/engines/age-multiplayer.md.

#include "common/net.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace reluva {
class System;
}

namespace reluva::net {

// The pieces, for tests.
std::string ssdpSearch(const std::string& target);             // an M-SEARCH for `target`
bool ssdpLocation(const std::string& reply, std::string& url); // its LOCATION header
struct Url {
    uint32_t ip = 0;
    uint16_t port = 80;
    std::string path = "/";
};
// "http://a.b.c.d[:port][/path]" (routers give their addresses).
bool parseUrl(const std::string& text, Url& out);
// An HTTP response's status and body (chunked or not).
std::string httpBody(const std::string& response, int& status);
// The text in <tag>...</tag> (or <prefix:tag>), the first; empty if none.
std::string xmlValue(const std::string& xml, const std::string& tag);
// A gateway's description: its WANIPConnection (or WANPPPConnection)
// service's type and where to send its actions.
bool findWanService(const std::string& description, const Url& location, std::string& serviceType, Url& control);
std::string httpGet(const Url& url);
std::string soapRequest(const Url& control, const std::string& serviceType, const std::string& action,
                        const std::vector<std::pair<std::string, std::string>>& arguments);
std::vector<uint8_t> natPmpAddressRequest();
bool natPmpAddressReply(const std::vector<uint8_t>& reply, uint32_t& ip, int& result);
std::vector<uint8_t> natPmpMapRequest(uint16_t internalPort, uint16_t externalPort, uint32_t lifetime);
bool natPmpMapReply(const std::vector<uint8_t>& reply, uint16_t& externalPort, uint32_t& lifetime, int& result);

struct PortMapping {
    enum class Way { None, Upnp, NatPmp };
    Way way = Way::None;
    uint32_t externalIp = 0; // the address others reach (0: not known)
    uint16_t externalPort = 0;
    std::string why; // when it didn't work
};

class PortMapper {
public:
    // How it reaches the network.
    struct Transport {
        // Sends a datagram and gathers the replies that come within the time.
        std::function<std::vector<std::pair<Address, std::vector<uint8_t>>>(
            const Address& to, const std::vector<uint8_t>& datagram, int waitMs)>
            udp;
        // One HTTP exchange; the address this machine used.
        std::function<bool(const Address& to, const std::string& request, std::string& response, uint32_t& localIp)>
            http;
        std::vector<uint32_t> gateways; // where to ask NAT-PMP
        int waitMs = 2000;
    };
    // The system's network, the gateways it knows and, failing those, the
    // first address of each network the machine is on (192.168.1.1...).
    static Transport through(System& sys);

    // `threaded`: the work on a thread of its own, the mapping kept (NAT-PMP's
    // renewed) and taken away when this goes; else map() and unmap() are
    // called by hand (tests).
    PortMapper(Transport transport, uint16_t port, bool threaded = true);
    ~PortMapper();
    PortMapper(const PortMapper&) = delete;
    PortMapper& operator=(const PortMapper&) = delete;

    bool done() const { return done_; }
    PortMapping result() const;

    PortMapping map();
    void unmap();

private:
    bool mapUpnp(PortMapping& out);
    bool mapNatPmp(PortMapping& out);
    void work();

    Transport transport_;
    uint16_t port_;
    std::thread thread_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_ = false;
    std::atomic<bool> done_{false};
    PortMapping result_;
    // What's needed to take it away again.
    Url control_;
    std::string serviceType_;
    uint32_t gateway_ = 0;
    uint32_t lifetime_ = 0; // NAT-PMP's seconds, renewed at half
};

} // namespace reluva::net
