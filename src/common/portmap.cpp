// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/portmap.h"

#include "common/system.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>

namespace reluva::net {

namespace {

const Address kSsdp{0xEFFFFFFAu, 1900}; // 239.255.255.250, where SSDP searches go
constexpr uint16_t kNatPmpPort = 5351;
constexpr uint32_t kNatPmpLifetime = 7200; // seconds asked for (RFC 6886 recommends two hours)
constexpr int kHttpWaitMs = 3000;

std::string lower(std::string s) {
    for (char& c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string ipText(uint32_t ip) {
    return std::to_string(ip >> 24) + "." + std::to_string(ip >> 16 & 255) + "." + std::to_string(ip >> 8 & 255) + "." +
           std::to_string(ip & 255);
}

uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

// A header's value in an HTTP-like message (SSDP's replies, HTTP's).
std::string header(const std::string& message, const std::string& name) {
    const std::string want = lower(name) + ":";
    size_t at = 0;
    while (at < message.size()) {
        size_t end = message.find('\n', at);
        if (end == std::string::npos)
            end = message.size();
        std::string line = message.substr(at, end - at);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            break; // the body's next
        if (lower(line.substr(0, want.size())) == want) {
            std::string v = line.substr(want.size());
            while (!v.empty() && v.front() == ' ')
                v.erase(v.begin());
            while (!v.empty() && v.back() == ' ')
                v.pop_back();
            return v;
        }
        at = end + 1;
    }
    return {};
}

std::string hostOf(const Url& u) { return ipText(u.ip) + ":" + std::to_string(u.port); }

} // namespace

std::string ssdpSearch(const std::string& target) {
    return "M-SEARCH * HTTP/1.1\r\n"
           "HOST: 239.255.255.250:1900\r\n"
           "MAN: \"ssdp:discover\"\r\n"
           "MX: 2\r\n"
           "ST: " +
           target + "\r\n\r\n";
}

bool ssdpLocation(const std::string& reply, std::string& url) {
    url = header(reply, "LOCATION");
    return !url.empty();
}

bool parseUrl(const std::string& text, Url& out) {
    const std::string scheme = "http://";
    if (lower(text.substr(0, scheme.size())) != scheme)
        return false;
    std::string rest = text.substr(scheme.size());
    const size_t slash = rest.find('/');
    out.path = slash == std::string::npos ? "/" : rest.substr(slash);
    rest = rest.substr(0, slash);
    out.port = 80;
    if (const size_t colon = rest.find(':'); colon != std::string::npos) {
        const int port = std::atoi(rest.c_str() + colon + 1);
        if (port <= 0 || port > 65535)
            return false;
        out.port = uint16_t(port);
        rest = rest.substr(0, colon);
    }
    Address a;
    if (!parseAddress(rest, 0, a))
        return false;
    out.ip = a.ip;
    return true;
}

std::string httpBody(const std::string& response, int& status) {
    status = 0;
    const size_t space = response.find(' ');
    if (response.compare(0, 5, "HTTP/") != 0 || space == std::string::npos)
        return {};
    status = std::atoi(response.c_str() + space + 1);
    const size_t start = response.find("\r\n\r\n");
    if (start == std::string::npos)
        return {};
    std::string body = response.substr(start + 4);
    if (lower(header(response, "Transfer-Encoding")).find("chunked") == std::string::npos)
        return body;
    // Each chunk: its size in hex, a line, the bytes, a line; 0 ends.
    std::string out;
    size_t at = 0;
    while (at < body.size()) {
        const size_t line = body.find("\r\n", at);
        if (line == std::string::npos)
            break;
        const size_t n = std::strtoul(body.substr(at, line - at).c_str(), nullptr, 16);
        if (n == 0 || line + 2 + n > body.size())
            break;
        out += body.substr(line + 2, n);
        at = line + 2 + n + 2;
    }
    return out;
}

std::string xmlValue(const std::string& xml, const std::string& tag) {
    for (size_t at = 0; (at = xml.find(tag + ">", at)) != std::string::npos; at += tag.size()) {
        // An opening tag: "<tag>" or "<prefix:tag>".
        size_t open = at;
        if (open > 0 && xml[open - 1] == ':') {
            open = xml.rfind('<', open);
            if (open == std::string::npos || xml.find('>', open) < at)
                continue;
        } else if (open == 0 || xml[open - 1] != '<') {
            continue;
        } else {
            --open;
        }
        if (xml[open + 1] == '/')
            continue;
        const size_t from = at + tag.size() + 1;
        const size_t to = xml.find("</", from);
        if (to == std::string::npos)
            return {};
        std::string v = xml.substr(from, to - from);
        while (!v.empty() && std::isspace(static_cast<unsigned char>(v.front())))
            v.erase(v.begin());
        while (!v.empty() && std::isspace(static_cast<unsigned char>(v.back())))
            v.pop_back();
        return v;
    }
    return {};
}

bool findWanService(const std::string& description, const Url& location, std::string& serviceType, Url& control) {
    // The service blocks, the first that's a WAN connection's.
    for (size_t at = 0; (at = description.find("<service>", at)) != std::string::npos;) {
        const size_t end = description.find("</service>", at);
        if (end == std::string::npos)
            return false;
        const std::string block = description.substr(at, end - at);
        at = end;
        const std::string type = xmlValue(block, "serviceType");
        if (type.find(":WANIPConnection:") == std::string::npos && type.find(":WANPPPConnection:") == std::string::npos)
            continue;
        const std::string url = xmlValue(block, "controlURL");
        if (url.empty())
            continue;
        serviceType = type;
        if (parseUrl(url, control))
            return true;
        // A path: on the base the description gives, else where it came from.
        control = location;
        Url base;
        if (parseUrl(xmlValue(description, "URLBase"), base))
            control = base;
        control.path = url.front() == '/' ? url : "/" + url;
        return true;
    }
    return false;
}

std::string httpGet(const Url& url) {
    return "GET " + url.path + " HTTP/1.1\r\nHost: " + hostOf(url) + "\r\nConnection: close\r\n\r\n";
}

std::string soapRequest(const Url& control, const std::string& serviceType, const std::string& action,
                        const std::vector<std::pair<std::string, std::string>>& arguments) {
    std::string body = "<?xml version=\"1.0\"?>"
                       "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                       "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:" +
                       action + " xmlns:u=\"" + serviceType + "\">";
    for (const auto& [name, value] : arguments)
        body += "<" + name + ">" + value + "</" + name + ">";
    body += "</u:" + action + "></s:Body></s:Envelope>";
    return "POST " + control.path + " HTTP/1.1\r\nHost: " + hostOf(control) +
           "\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"" + serviceType + "#" + action +
           "\"\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
}

std::vector<uint8_t> natPmpAddressRequest() { return {0, 0}; }

bool natPmpAddressReply(const std::vector<uint8_t>& reply, uint32_t& ip, int& result) {
    if (reply.size() < 12 || reply[0] != 0 || reply[1] != 128)
        return false;
    result = be16(&reply[2]);
    ip = be32(&reply[8]);
    return true;
}

std::vector<uint8_t> natPmpMapRequest(uint16_t internalPort, uint16_t externalPort, uint32_t lifetime) {
    return {0,
            1, // UDP
            0,
            0,
            uint8_t(internalPort >> 8),
            uint8_t(internalPort),
            uint8_t(externalPort >> 8),
            uint8_t(externalPort),
            uint8_t(lifetime >> 24),
            uint8_t(lifetime >> 16),
            uint8_t(lifetime >> 8),
            uint8_t(lifetime)};
}

bool natPmpMapReply(const std::vector<uint8_t>& reply, uint16_t& externalPort, uint32_t& lifetime, int& result) {
    if (reply.size() < 16 || reply[0] != 0 || reply[1] != 129)
        return false;
    result = be16(&reply[2]);
    externalPort = be16(&reply[10]);
    lifetime = be32(&reply[12]);
    return true;
}

PortMapper::Transport PortMapper::through(System& sys) {
    Transport t;
    t.udp = [&sys](const Address& to, const std::vector<uint8_t>& datagram, int waitMs) {
        std::vector<std::pair<Address, std::vector<uint8_t>>> replies;
        std::unique_ptr<UdpSocket> socket;
        if (sys.openUdp(0, socket) || !socket || !socket->send(to, datagram))
            return replies;
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(waitMs);
        while (std::chrono::steady_clock::now() < until) {
            Address from;
            std::vector<uint8_t> data;
            while (socket->receive(from, data))
                replies.push_back({from, data});
            // A unicast question is answered once: no more to wait for.
            if (!replies.empty() && to.ip != kSsdp.ip)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return replies;
    };
    t.http = [&sys](const Address& to, const std::string& request, std::string& response, uint32_t& localIp) {
        return sys.httpExchange(to, request, response, localIp, kHttpWaitMs);
    };
    t.gateways = sys.gateways();
    for (uint32_t b : sys.broadcastAddresses()) { // a guess: the network's first address
        const uint32_t first = (b & 0xFFFFFF00u) | 1;
        if (std::find(t.gateways.begin(), t.gateways.end(), first) == t.gateways.end())
            t.gateways.push_back(first);
    }
    return t;
}

PortMapper::PortMapper(Transport transport, uint16_t port, bool threaded)
    : transport_(std::move(transport)), port_(port) {
    if (threaded)
        thread_ = std::thread([this] { work(); });
}

PortMapper::~PortMapper() {
    if (thread_.joinable()) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
        thread_.join();
    }
}

PortMapping PortMapper::result() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return result_;
}

void PortMapper::work() {
    map();
    // NAT-PMP's mapping lasts its lifetime: asked again at half of it.
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stop_) {
        const auto every = std::chrono::seconds(std::max<uint32_t>(60, lifetime_ / 2));
        if (wake_.wait_for(lock, lifetime_ ? every : std::chrono::hours(24), [this] { return stop_; }))
            break;
        if (result_.way == PortMapping::Way::NatPmp) {
            lock.unlock();
            PortMapping again;
            mapNatPmp(again);
            lock.lock();
        }
    }
    lock.unlock();
    unmap();
}

PortMapping PortMapper::map() {
    PortMapping out;
    if (!mapUpnp(out) && !mapNatPmp(out)) {
        out = PortMapping{};
        out.why = "The router didn't answer UPnP or NAT-PMP";
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        result_ = out;
    }
    done_ = true;
    return out;
}

bool PortMapper::mapUpnp(PortMapping& out) {
    if (!transport_.udp || !transport_.http)
        return false;
    // Gateways found by an SSDP search, each asked for its description.
    std::vector<std::string> locations;
    for (const char* target :
         {"urn:schemas-upnp-org:device:InternetGatewayDevice:1", "urn:schemas-upnp-org:service:WANIPConnection:1"}) {
        const std::string search = ssdpSearch(target);
        for (const auto& [from, data] :
             transport_.udp(kSsdp, std::vector<uint8_t>(search.begin(), search.end()), transport_.waitMs)) {
            std::string url;
            if (ssdpLocation(std::string(data.begin(), data.end()), url) &&
                std::find(locations.begin(), locations.end(), url) == locations.end())
                locations.push_back(url);
        }
        if (!locations.empty())
            break;
    }
    for (const std::string& location : locations) {
        Url where;
        std::string response;
        uint32_t localIp = 0;
        int status = 0;
        if (!parseUrl(location, where) || !transport_.http({where.ip, where.port}, httpGet(where), response, localIp))
            continue;
        const std::string description = httpBody(response, status);
        std::string type;
        Url control;
        if (status != 200 || !findWanService(description, where, type, control))
            continue;
        // A mapping for good (lease 0), else for a day if the router wants
        // one with an end (some won't have permanent ones).
        bool mapped = false;
        for (const char* lease : {"0", "86400"}) {
            const std::string request = soapRequest(control, type, "AddPortMapping",
                                                    {{"NewRemoteHost", ""},
                                                     {"NewExternalPort", std::to_string(port_)},
                                                     {"NewProtocol", "UDP"},
                                                     {"NewInternalPort", std::to_string(port_)},
                                                     {"NewInternalClient", ipText(localIp)},
                                                     {"NewEnabled", "1"},
                                                     {"NewPortMappingDescription", "Reluva"},
                                                     {"NewLeaseDuration", lease}});
            uint32_t ignored = 0;
            if (transport_.http({control.ip, control.port}, request, response, ignored)) {
                httpBody(response, status);
                if (status == 200) {
                    mapped = true;
                    break;
                }
            }
        }
        if (!mapped)
            continue;
        out.way = PortMapping::Way::Upnp;
        out.externalPort = port_;
        uint32_t ignored = 0;
        if (transport_.http({control.ip, control.port}, soapRequest(control, type, "GetExternalIPAddress", {}),
                            response, ignored)) {
            Address a;
            if (parseAddress(xmlValue(httpBody(response, status), "NewExternalIPAddress"), 0, a))
                out.externalIp = a.ip;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        control_ = control;
        serviceType_ = type;
        return true;
    }
    return false;
}

bool PortMapper::mapNatPmp(PortMapping& out) {
    if (!transport_.udp)
        return false;
    for (uint32_t gateway : transport_.gateways) {
        const auto map =
            transport_.udp({gateway, kNatPmpPort}, natPmpMapRequest(port_, port_, kNatPmpLifetime), transport_.waitMs);
        uint16_t external = 0;
        uint32_t lifetime = 0;
        int result = -1;
        bool ok = false;
        for (const auto& [from, data] : map)
            ok = ok || (natPmpMapReply(data, external, lifetime, result) && result == 0);
        if (!ok)
            continue;
        out.way = PortMapping::Way::NatPmp;
        out.externalPort = external;
        for (const auto& [from, data] :
             transport_.udp({gateway, kNatPmpPort}, natPmpAddressRequest(), transport_.waitMs)) {
            uint32_t ip = 0;
            if (natPmpAddressReply(data, ip, result) && result == 0)
                out.externalIp = ip;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        gateway_ = gateway;
        lifetime_ = lifetime;
        return true;
    }
    return false;
}

void PortMapper::unmap() {
    PortMapping was;
    Url control;
    std::string type;
    uint32_t gateway = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        was = result_;
        control = control_;
        type = serviceType_;
        gateway = gateway_;
        result_ = PortMapping{};
    }
    if (was.way == PortMapping::Way::Upnp && transport_.http) {
        std::string response;
        uint32_t ignored = 0;
        transport_.http(
            {control.ip, control.port},
            soapRequest(
                control, type, "DeletePortMapping",
                {{"NewRemoteHost", ""}, {"NewExternalPort", std::to_string(was.externalPort)}, {"NewProtocol", "UDP"}}),
            response, ignored);
    } else if (was.way == PortMapping::Way::NatPmp && transport_.udp) {
        // A lifetime of 0 takes it away (RFC 6886 3.4).
        transport_.udp({gateway, kNatPmpPort}, natPmpMapRequest(port_, 0, 0), transport_.waitMs / 4);
    }
}

} // namespace reluva::net
