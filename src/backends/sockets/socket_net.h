// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/net.h"

#include <memory>
#include <string>
#include <vector>

namespace reluva::net {

// UDP through the operating system's sockets (BSD sockets, or Winsock on
// Windows), for backends and tools to hand out.

// A socket bound to `port` on every interface (0: any port), able to
// broadcast; nullptr with the reason in `error` if it can't be opened.
std::unique_ptr<UdpSocket> openSystemUdp(uint16_t port, std::string& error);
// `host` (a name or a.b.c.d) as an IPv4 address; false if it can't be found.
bool resolveSystemHost(const std::string& host, uint32_t& ip);
// The broadcast address of each network the machine is on (192.168.1.255
// on 192.168.1.x/24...): 255.255.255.255 leaves by one of them only, so a
// machine on several (a virtual machine's network as well) asks each.
std::vector<uint32_t> systemBroadcastAddresses();
// One HTTP exchange over TCP (a router's UPnP): connects to `to`, sends
// `request`, and reads until the other end closes, at most `timeoutMs` in
// all; `localIp` is the address this machine used. False if it couldn't.
bool systemHttpExchange(const Address& to, const std::string& request, std::string& response, uint32_t& localIp,
                        int timeoutMs);
// The default gateways (the routers), where the system says: Linux's
// /proc/net/route, Windows' adapters; elsewhere none.
std::vector<uint32_t> systemGateways();

} // namespace reluva::net
