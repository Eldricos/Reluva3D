// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/net.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace reluva::net {

bool Address::isLan() const {
    const uint32_t a = ip >> 24, b = (ip >> 16) & 0xff;
    return isLoopback() || a == 10 || (a == 172 && b >= 16 && b < 32) || (a == 192 && b == 168);
}

std::string Address::toString() const {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%u.%u.%u.%u:%u", ip >> 24, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff,
                  unsigned(port));
    return buf;
}

std::pair<std::string, uint16_t> splitHostPort(const std::string& text, uint16_t defaultPort) {
    const size_t colon = text.rfind(':');
    if (colon == std::string::npos)
        return {text, defaultPort};
    const long port = std::strtol(text.c_str() + colon + 1, nullptr, 10);
    return {text.substr(0, colon), port > 0 && port < 65536 ? uint16_t(port) : defaultPort};
}

bool parseAddress(const std::string& text, uint16_t defaultPort, Address& out) {
    const auto [host, port] = splitHostPort(text, defaultPort);
    uint32_t ip = 0;
    int parts = 0;
    size_t i = 0;
    while (parts < 4) {
        if (i >= host.size() || host[i] < '0' || host[i] > '9')
            return false;
        uint32_t part = 0;
        size_t digits = 0;
        while (i < host.size() && host[i] >= '0' && host[i] <= '9' && digits < 4) {
            part = part * 10 + uint32_t(host[i++] - '0');
            ++digits;
        }
        if (part > 255)
            return false;
        ip = (ip << 8) | part;
        ++parts;
        if (parts < 4) {
            if (i >= host.size() || host[i] != '.')
                return false;
            ++i;
        }
    }
    if (i != host.size())
        return false;
    out = {ip, port};
    return true;
}

class LoopbackNetwork::Socket : public UdpSocket {
public:
    Socket(std::shared_ptr<LoopbackNetwork> network, uint16_t port, std::shared_ptr<Queue> queue)
        : network_(std::move(network)), port_(port), queue_(std::move(queue)) {}
    ~Socket() override {
        queue_->open = false;
        queue_->datagrams.clear();
    }
    uint16_t port() const override { return port_; }
    bool send(const Address& to, const std::vector<uint8_t>& data) override {
        return network_->deliver({0x7f000001u, port_}, to, data);
    }
    bool receive(Address& from, std::vector<uint8_t>& data) override {
        if (queue_->datagrams.empty())
            return false;
        from = queue_->datagrams.front().first;
        data = std::move(queue_->datagrams.front().second);
        queue_->datagrams.pop_front();
        return true;
    }

private:
    std::shared_ptr<LoopbackNetwork> network_;
    uint16_t port_;
    std::shared_ptr<Queue> queue_;
};

std::unique_ptr<UdpSocket> LoopbackNetwork::open(uint16_t port) {
    auto taken = [&](uint16_t p) {
        return std::any_of(queues_.begin(), queues_.end(),
                           [&](const auto& q) { return q.first == p && q.second->open; });
    };
    if (port == 0) {
        port = 27960;
        while (taken(port))
            ++port;
    } else if (taken(port)) {
        return nullptr;
    }
    std::shared_ptr<Queue> queue;
    for (auto& [p, q] : queues_)
        if (p == port)
            queue = q;
    if (!queue) {
        queue = std::make_shared<Queue>();
        queues_.emplace_back(port, queue);
    }
    queue->open = true;
    return std::make_unique<Socket>(shared_from_this(), port, queue);
}

void LoopbackNetwork::setDropFilter(bool (*drop)(const Address&, const Address&, size_t)) { drop_ = drop; }

bool LoopbackNetwork::deliver(const Address& from, const Address& to, const std::vector<uint8_t>& data) {
    const size_t index = sent_++;
    if (to.ip != 0xffffffffu && !to.isLoopback())
        return true; // nowhere to go, as a datagram to nobody
    if (drop_ && drop_(from, to, index))
        return true;
    for (auto& [port, queue] : queues_)
        if (port == to.port && queue->open && !(to.ip == 0xffffffffu && port == from.port))
            queue->datagrams.emplace_back(from, data);
    return true;
}

} // namespace reluva::net
