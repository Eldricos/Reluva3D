// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstdint>
#include <vector>

namespace reluva::idtech3::wire {

// One end of an established connection (protocol 24, see
// docs/engines/idtech3/games/stvef/systems/network.md): each message goes out as
// a datagram with its sequence number (and, from the client, its qport)
// before it, in 1300-byte pieces if it's that long or longer, and
// scrambled after its first 6 bytes; ones that come in out of order or
// twice are dropped.
class Netchan {
public:
    static constexpr size_t kMaxPacket = 1400;
    static constexpr size_t kFragmentSize = 1300;
    static constexpr size_t kMaxMessage = 16384;

    // `fromClient`: this end is the client's, whose datagrams carry the qport.
    Netchan(bool fromClient, uint16_t qport) : _fromClient(fromClient), _qport(qport) {}

    // The datagrams that carry `message`.
    std::vector<std::vector<uint8_t>> transmit(const std::vector<uint8_t> &message);
    // A datagram that came in on this connection: true with the message
    // (after its header) once a whole new one has arrived.
    bool process(std::vector<uint8_t> datagram, std::vector<uint8_t> &message);

    // The sequence of the last message sent and the last one taken in.
    int32_t outgoingSequence() const { return _outgoing - 1; }
    int32_t incomingSequence() const { return _incoming; }
    int droppedLast() const { return _dropped; }

    // The qport a datagram from a client carries (0 if too short), read
    // before it's unscrambled: it's among the bytes left as they are.
    static bool clientQport(const std::vector<uint8_t> &datagram, uint16_t &qport);
    // Scrambling as the original: bytes 6 on XORed with a sequence made
    // from the first four bytes and the length, then shuffled.
    static void scramble(std::vector<uint8_t> &datagram);
    static void unscramble(std::vector<uint8_t> &datagram);

private:
    bool _fromClient;
    uint16_t _qport;
    int32_t _outgoing = 1, _incoming = 0;
    int _dropped = 0;
    // A message coming in pieces: its sequence and what's come so far.
    int32_t _fragmentSequence = 0;
    std::vector<uint8_t> _fragments;
};

} // namespace reluva::idtech3::wire
