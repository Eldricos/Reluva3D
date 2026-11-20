// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Asking the router to pass a UDP port on to this machine, so others can
// reach a game it hosts from the Internet: UPnP's Internet Gateway Device
// (the UPnP Device Architecture's discovery, SSDP, and its
// WANIPConnection service's AddPortMapping), else NAT-PMP (RFC 6886).
// Both are published standards; Reluva's own code. See
// docs/engines/age/games/mm2/systems/network-protocol.md.

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

// The pieces PortMapper is made of, public for tests.

/** An SSDP search (M-SEARCH) for devices or services of type `target`, a URN; they answer within 2 seconds. */
std::string ssdpSearch(const std::string &target);
/**
 * An SSDP reply's LOCATION header: where the device's description is.
 * @return False if the reply has none.
 */
bool ssdpLocation(const std::string &reply, std::string &url);
/** An http:// address as routers give them: an IPv4 address (as Address::ip), a port and a path. */
struct Url {
    uint32_t ip = 0;
    uint16_t port = 80;
    std::string path = "/";
};
/**
 * Reads "http://a.b.c.d[:port][/path]"; a host name isn't looked up, as routers give their addresses.
 * @return False for another scheme, a host that isn't a.b.c.d or a port out of 1-65535.
 */
bool parseUrl(const std::string &text, Url &out);
/**
 * An HTTP response's body, its chunks joined when it's sent chunked.
 * @param status Set to the response's status code; 0 if it isn't HTTP.
 * @return Empty if it isn't HTTP or has no body.
 */
std::string httpBody(const std::string &response, int &status);
/**
 * The text in the first <tag>...</tag> (or <prefix:tag>), trimmed; empty if none.
 *
 * Not an XML parser: the text runs to the next closing tag, so it's for elements holding only text.
 */
std::string xmlValue(const std::string &xml, const std::string &tag);
/**
 * Finds in a gateway's description its WANIPConnection (or WANPPPConnection)
 * service's type and where to send its actions.
 * @param location Where the description came from: a control path is taken
 *     on its address when the description gives no URLBase.
 * @return False if there's no such service.
 */
bool findWanService(const std::string &description, const Url &location, std::string &serviceType, Url &control);
/** An HTTP GET request for `url`, asking the server to close the connection after. */
std::string httpGet(const Url &url);
/**
 * An HTTP POST calling a UPnP service's SOAP action at its control address.
 * @param arguments The action's arguments in order, as name and value; they aren't escaped.
 */
std::string soapRequest(const Url &control, const std::string &serviceType, const std::string &action,
                        const std::vector<std::pair<std::string, std::string>> &arguments);
/** A NAT-PMP request for the gateway's external address (RFC 6886). */
std::vector<uint8_t> natPmpAddressRequest();
/**
 * Reads a NAT-PMP external address reply.
 * @param ip The address, as Address::ip.
 * @param result The reply's result code: 0 when it worked.
 * @return False if it isn't such a reply (too short, or another version or opcode).
 */
bool natPmpAddressReply(const std::vector<uint8_t> &reply, uint32_t &ip, int &result);
/**
 * A NAT-PMP request to map a UDP port.
 * @param externalPort The port asked for outside: the gateway may give another. 0 with a lifetime of 0 takes
 *     the mapping away.
 * @param lifetime Seconds to keep it; 0 takes it away.
 */
std::vector<uint8_t> natPmpMapRequest(uint16_t internalPort, uint16_t externalPort, uint32_t lifetime);
/**
 * Reads a NAT-PMP reply to a UDP mapping request.
 * @param externalPort The port the gateway gave.
 * @param lifetime Seconds the gateway keeps it.
 * @param result The reply's result code: 0 when it worked.
 * @return False if it isn't such a reply (too short, or another version or opcode).
 */
bool natPmpMapReply(const std::vector<uint8_t> &reply, uint16_t &externalPort, uint32_t &lifetime, int &result);

/** What came of asking the router: how the port was passed on and where others reach it. */
struct PortMapping {
    /** How the port was passed on; kNone when it wasn't (not yet, or neither way worked). */
    enum class Way { kNone, kUpnp, kNatPmp };
    Way way = Way::kNone;
    uint32_t externalIp = 0; // the address others reach (0: not known)
    uint16_t externalPort = 0;
    std::string why; // when it didn't work
};

/**
 * Asks the router to pass a UDP port on to this machine, by UPnP else NAT-PMP,
 * and takes the mapping away again.
 */
class PortMapper {
public:
    /** How a PortMapper reaches the network: tests give their own. */
    struct Transport {
        /** Sends a datagram and gathers the replies that come within the time. */
        std::function<std::vector<std::pair<Address, std::vector<uint8_t>>>(
            const Address &to, const std::vector<uint8_t> &datagram, int waitMs)>
            udp;
        // One HTTP exchange; the address this machine used.
        std::function<bool(const Address &to, const std::string &request, std::string &response, uint32_t &localIp)>
            http;
        std::vector<uint32_t> gateways; // where to ask NAT-PMP
        int waitMs = 2000;
    };
    /**
     * The system's network, the gateways it knows and, failing those, the
     * first address of each network the machine is on (192.168.1.1...).
     *
     * The transport keeps a reference to `sys`, which has to outlive it.
     */
    static Transport through(System &sys);

    /**
     * A mapper for UDP `port`, asked for as the same port outside.
     * @param threaded The work on a thread of its own, the mapping kept (NAT-PMP's
     *     renewed) and taken away when this goes; false: map() and unmap() are
     *     called by hand (tests), and nothing is taken away on destruction.
     */
    PortMapper(Transport transport, uint16_t port, bool threaded = true);
    ~PortMapper();
    PortMapper(const PortMapper &) = delete;
    PortMapper &operator=(const PortMapper &) = delete;

    /** Whether map() has finished at least once, whether or not it worked. */
    bool done() const { return _done; }
    /** What came of it, read safely from any thread: Way::kNone until done(), and again after unmap(). */
    PortMapping result() const;

    /**
     * Tries UPnP, then NAT-PMP, waiting for each to answer or time out.
     * @return The result, kept for result() too; its `why` says so when neither worked.
     */
    PortMapping map();
    /** Takes away the mapping map() made, if any, and forgets it. */
    void unmap();

private:
    bool mapUpnp(PortMapping &out);
    bool mapNatPmp(PortMapping &out);
    void work();

    Transport _transport;
    uint16_t _port;
    std::thread _thread;
    mutable std::mutex _mutex;
    std::condition_variable _wake;
    bool _stop = false;
    std::atomic<bool> _done{false};
    PortMapping _result;
    // What's needed to take it away again.
    Url _control;
    std::string _serviceType;
    uint32_t _gateway = 0;
    uint32_t _lifetime = 0; // NAT-PMP's seconds, renewed at half
};

} // namespace reluva::net
