// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/net.h"

#include <memory>
#include <string>

namespace reluva::net {

// UDP through the operating system's sockets (BSD sockets, or Winsock on
// Windows), for backends and tools to hand out.

// A socket bound to `port` on every interface (0: any port), able to
// broadcast; nullptr with the reason in `error` if it can't be opened.
std::unique_ptr<UdpSocket> openSystemUdp(uint16_t port, std::string& error);
// `host` (a name or a.b.c.d) as an IPv4 address; false if it can't be found.
bool resolveSystemHost(const std::string& host, uint32_t& ip);

} // namespace reluva::net
