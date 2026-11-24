// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// DirectPlay 4's reliable protocol ([MC-DPL4R]), as Microsoft publishes
// it: the envelope a session made with DPSESSION_DIRECTPLAYPROTOCOL puts
// round every message between joined machines, over UDP. One link to one
// peer, with no socket in it: what it sends comes out of takeFrames(),
// what arrives goes into receive() (docs/common/x86/dplay.md).

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace reluva::x86::dp4 {

/** A player's index, its id less the session's mask, as 1 to 3 bytes; false past the end or above 0xffff. */
///@{
void putIndex(std::vector<uint8_t> &v, uint16_t index);
bool getIndex(const std::vector<uint8_t> &v, size_t &at, uint16_t &index);
///@}

/**
 * One machine's end of the reliable protocol's link to another. Messages
 * are cut into frames under the MTU, numbered, and the reliable ones sent
 * again until acknowledged; frames that arrive are put back together,
 * reliable messages handed on in the order sent and others as they're
 * whole.
 */
class ReliableLink {
public:
    /** A message that has arrived whole. */
    struct Delivered {
        uint16_t from = 0; ///< the players' indexes, from the frames' header
        uint16_t to = 0;
        bool reliable = false;
        std::vector<uint8_t> data;
    };

    /**
     * @param clock milliseconds from any fixed time
     * @param mtu the largest frame, header included
     */
    explicit ReliableLink(std::function<uint64_t()> clock, size_t mtu = 1200);

    /** Queues a message from player index `from` to `to`; sent now if the window allows. */
    void send(const std::vector<uint8_t> &message, bool reliable, uint16_t from, uint16_t to);
    /** Takes in one frame that has come from the peer. */
    void receive(const std::vector<uint8_t> &frame);
    /** Sends again what's due, and acknowledgements held back. */
    void update();
    /** The frames to send to the peer now, in order. */
    std::vector<std::vector<uint8_t>> takeFrames();
    /** The next message that has arrived whole. */
    std::optional<Delivered> take();

    /** Reliable messages sent and not yet acknowledged, and those waiting for the window. */
    size_t outstanding() const;
    /** How long it waits for an acknowledgement before sending a frame again, in milliseconds. */
    uint64_t retryMs() const;

    /** At most this many message ids out at once ([MC-DPL4R] 2.2.2). */
    static constexpr int kWindow = 24;

private:
    struct Frame {
        uint8_t sequence = 0;
        bool start = false;
        bool end = false;
        std::vector<uint8_t> data;
        bool acked = false;
        uint64_t sentMs = 0;
        int sends = 0;
    };
    struct Outgoing {
        uint8_t id = 0;
        bool reliable = false;
        uint16_t from = 0;
        uint16_t to = 0;
        uint8_t serial = 0;
        std::vector<Frame> frames;
    };
    struct Queued {
        std::vector<uint8_t> data;
        bool reliable = false;
        uint16_t from = 0;
        uint16_t to = 0;
    };
    struct Incoming {
        bool reliable = false;
        bool complete = false;
        uint16_t from = 0;
        uint16_t to = 0;
        std::map<uint8_t, std::vector<uint8_t>> frames; // by sequence
        std::optional<uint8_t> start;
        std::optional<uint8_t> end;
        std::vector<uint8_t> data;
    };

    void startMessages();
    void transmit(Outgoing &message, Frame &frame);
    void acknowledged(uint8_t upTo);
    void dropAcked();
    void retransmit(uint8_t sequence);
    void dataFrame(const std::vector<uint8_t> &frame, size_t at, uint16_t from, uint16_t to);
    void sendAck();
    void sendNack();
    void deliverReady();
    void roundTrip(uint64_t ms);

    std::function<uint64_t()> _clock;
    size_t _mtu;
    std::deque<Queued> _queue;
    std::deque<Outgoing> _sent;
    uint8_t _nextId = 1;
    uint8_t _nextSequence = 1;
    double _averageMs = 0;
    double _deviationMs = 0;
    bool _measured = false;
    std::vector<std::vector<uint8_t>> _out;
    uint16_t _ourIndex = 0;
    uint16_t _theirIndex = 0;

    // Receiving: the next sequence wanted, those that have come ahead of
    // it, the messages being put together, and the reliable ones' order.
    uint8_t _expected = 1;
    std::map<uint8_t, bool> _ahead; // by distance from _expected: whether it's come
    std::map<uint8_t, Incoming> _incoming;
    uint8_t _lastReliable = 0;    // the last reliable message handed on
    uint64_t _blockedSinceMs = 0; // since when an unknown message has held up a reliable one
    uint64_t _gapSinceMs = 0;     // since when frames have been missing
    uint64_t _nackedMs = 0;
    bool _ackDue = false;
    uint8_t _lastSerial = 0;
    uint32_t _bytesReceived = 0;
    std::deque<Delivered> _delivered;
};

} // namespace reluva::x86::dp4
