// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Tests for the system's sockets as net::Sockets (socket_net.cpp), on the
// machine's loopback: a TCP connection made, used and ended, a connection
// refused, and UDP datagrams.

#include "backends/sockets/socket_net.h"

#include <cstdio>
#include <cstring>

using namespace reluva;
using Result = net::Sockets::Result;

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, \
                         __LINE__, #cond);                              \
            ++failures;                                                 \
        }                                                               \
    } while (0)

namespace {

constexpr uint32_t kLoopback = 0x7f000001;

void testStream(net::Sockets& s) {
    const int listener = s.open(net::Sockets::Stream);
    CHECK(listener >= 0);
    CHECK(s.bind(listener, {kLoopback, 0}) == Result::Ok);
    CHECK(s.listen(listener, 5) == Result::Ok);
    net::Address at;
    CHECK(s.localAddress(listener, at) == Result::Ok && at.ip == kLoopback && at.port != 0);
    int accepted = -1;
    net::Address from;
    CHECK(s.accept(listener, accepted, from) == Result::WouldBlock); // nobody yet

    // A connection, under way until the socket's writable.
    const int client = s.open(net::Sockets::Stream);
    const Result r = s.connect(client, at);
    CHECK(r == Result::Ok || r == Result::InProgress);
    CHECK(s.ready(client, 2000).write);
    CHECK(s.ready(listener, 2000).read);
    CHECK(s.accept(listener, accepted, from) == Result::Ok && accepted >= 0 && from.ip == kLoopback);
    net::Address peer;
    CHECK(s.peerAddress(client, peer) == Result::Ok && peer == at);

    // Bytes one way: peeked, then read; nothing more until more come.
    const uint8_t hello[5] = {'h', 'e', 'l', 'l', 'o'};
    size_t done = 0;
    CHECK(s.send(client, hello, 5, done) == Result::Ok && done == 5);
    CHECK(s.ready(accepted, 2000).read);
    uint32_t pending = 0;
    CHECK(s.pending(accepted, pending) == Result::Ok && pending == 5);
    uint8_t got[16] = {};
    CHECK(s.receive(accepted, got, 2, done, true) == Result::Ok && done == 2 && got[0] == 'h');
    CHECK(s.receive(accepted, got, sizeof got, done, false) == Result::Ok && done == 5 &&
          std::memcmp(got, hello, 5) == 0);
    CHECK(s.receive(accepted, got, sizeof got, done, false) == Result::WouldBlock);

    // The client goes: the other end reads 0 bytes, the end.
    s.close(client);
    CHECK(s.ready(accepted, 2000).read);
    CHECK(s.receive(accepted, got, sizeof got, done, false) == Result::Ok && done == 0);
    s.close(accepted);
    s.close(listener);
    CHECK(s.send(listener, hello, 5, done) == Result::Invalid); // closed: no longer a socket

    // Nothing listening: refused, as ready() shows.
    const int lonely = s.open(net::Sockets::Stream);
    const Result c = s.connect(lonely, at);
    if (c == Result::InProgress)
        CHECK(s.ready(lonely, 2000).failed);
    else
        CHECK(c == Result::Refused);
    s.close(lonely);
}

void testDatagrams(net::Sockets& s) {
    const int a = s.open(net::Sockets::Datagram), b = s.open(net::Sockets::Datagram);
    CHECK(s.bind(a, {kLoopback, 0}) == Result::Ok && s.bind(b, {kLoopback, 0}) == Result::Ok);
    CHECK(s.setOption(a, net::Sockets::Broadcast, 1) == Result::Ok);
    net::Address atB;
    s.localAddress(b, atB);
    const uint8_t data[3] = {1, 2, 3};
    CHECK(s.sendTo(a, atB, data, 3) == Result::Ok);
    CHECK(s.ready(b, 2000).read);
    uint8_t got[8] = {};
    size_t done = 0;
    net::Address from, atA;
    s.localAddress(a, atA);
    CHECK(s.receiveFrom(b, got, sizeof got, done, from, false) == Result::Ok && done == 3 && got[2] == 3 &&
          from == atA);
    CHECK(s.receiveFrom(b, got, sizeof got, done, from, false) == Result::WouldBlock);
    s.close(a);
    s.close(b);
}

} // namespace

int main() {
    auto sockets = net::systemSockets();
    testStream(*sockets);
    testDatagrams(*sockets);
    uint32_t ip = 0;
    CHECK(sockets->resolve("127.0.0.1", ip) && ip == kLoopback);
    CHECK(!sockets->hostName().empty());
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("All tests passed\n");
    return failures ? 1 : 0;
}
