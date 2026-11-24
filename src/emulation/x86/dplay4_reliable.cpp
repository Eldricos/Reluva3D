// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "emulation/x86/dplay4_reliable.h"

#include <algorithm>
#include <cmath>

namespace reluva::x86::dp4 {

namespace {

constexpr uint8_t kExtension = 0x80;
constexpr uint8_t kCommand = 0x20;
constexpr uint8_t kStart = 0x10;
constexpr uint8_t kEnd = 0x08;
constexpr uint8_t kSendAck = 0x04;
constexpr uint8_t kAck = 0x02;
constexpr uint8_t kReliable = 0x01;

constexpr uint64_t kFirstRetryMs = 1000; // before any round trip is measured ([MC-DPL4R] 3.1.2)
constexpr uint64_t kLeastRetryMs = 50;
constexpr uint64_t kGapMs = 3000;     // frames missing this long were unreliable ones, lost for good
constexpr uint64_t kUnknownMs = 5000; // a message never seen, holding up a reliable one, taken as lost
constexpr size_t kIndexRoom = 6;      // two indexes of up to 3 bytes
constexpr size_t kDataHeader = 4;     // flags, messageid, sequence, serial

// How far `b` is after `a`, counting round past 255, as the protocol's
// 8-bit numbers are compared.
uint8_t after(uint8_t a, uint8_t b) {
    return uint8_t(b - a);
}

void put32(std::vector<uint8_t> &v, uint32_t x) {
    for (int i = 0; i < 4; ++i)
        v.push_back(uint8_t(x >> (8 * i)));
}

} // namespace

void putIndex(std::vector<uint8_t> &v, uint16_t index) {
    if (index < 0x80) {
        v.push_back(uint8_t(index));
    } else if (index < 0x4000) {
        v.push_back(uint8_t(0x80 | (index & 0x7f)));
        v.push_back(uint8_t(index >> 7));
    } else {
        v.push_back(uint8_t(0x80 | (index & 0x7f)));
        v.push_back(uint8_t(0x80 | ((index >> 7) & 0x7f)));
        v.push_back(uint8_t(index >> 14));
    }
}

bool getIndex(const std::vector<uint8_t> &v, size_t &at, uint16_t &index) {
    uint32_t value = 0;
    for (int i = 0; i < 3; ++i) {
        if (at >= v.size())
            return false;
        const uint8_t b = v[at++];
        value |= uint32_t(b & (i < 2 ? 0x7f : 0xff)) << (7 * i);
        if (i == 2 && (b & 0x80))
            return false;
        if (!(b & 0x80) || i == 2)
            break;
    }
    if (value > 0xffff)
        return false;
    index = uint16_t(value);
    return true;
}

ReliableLink::ReliableLink(std::function<uint64_t()> clock, size_t mtu)
    : _clock(std::move(clock)), _mtu(std::max<size_t>(mtu, kIndexRoom + kDataHeader + 1)) {
}

uint64_t ReliableLink::retryMs() const {
    if (!_measured)
        return kFirstRetryMs;
    return std::max<uint64_t>(kLeastRetryMs, uint64_t(_averageMs + 3 * _deviationMs));
}

void ReliableLink::roundTrip(uint64_t ms) {
    const double sample = double(ms);
    if (!_measured) {
        _averageMs = sample;
        _deviationMs = sample / 2;
        _measured = true;
        return;
    }
    _deviationMs += (std::fabs(sample - _averageMs) - _deviationMs) / 4;
    _averageMs += (sample - _averageMs) / 8;
}

size_t ReliableLink::outstanding() const {
    size_t n = 0;
    for (const Outgoing &m : _sent)
        n += m.reliable ? 1 : 0;
    for (const Queued &q : _queue)
        n += q.reliable ? 1 : 0;
    return n;
}

void ReliableLink::send(const std::vector<uint8_t> &message, bool reliable, uint16_t from, uint16_t to) {
    _queue.push_back({message, reliable, from, to});
    startMessages();
}

// A message is started only while the ids from the oldest not yet
// acknowledged stay inside the window the receiver keeps.
void ReliableLink::startMessages() {
    const size_t room = _mtu - kIndexRoom - kDataHeader;
    while (!_queue.empty()) {
        if (!_sent.empty() && after(_sent.front().id, _nextId) >= kWindow)
            return;
        Queued q = std::move(_queue.front());
        _queue.pop_front();
        Outgoing m;
        m.id = _nextId++;
        m.reliable = q.reliable;
        m.from = q.from;
        m.to = q.to;
        size_t at = 0;
        do {
            const size_t n = std::min(room, q.data.size() - at);
            Frame f;
            f.sequence = _nextSequence++;
            f.start = at == 0;
            f.end = at + n == q.data.size();
            f.data.assign(q.data.begin() + long(at), q.data.begin() + long(at + n));
            m.frames.push_back(std::move(f));
            at += n;
        } while (at < q.data.size());
        for (Frame &f : m.frames)
            transmit(m, f);
        // An unreliable message isn't kept: nothing is sent again for it.
        if (m.reliable)
            _sent.push_back(std::move(m));
    }
}

void ReliableLink::transmit(Outgoing &m, Frame &f) {
    std::vector<uint8_t> v;
    putIndex(v, m.from);
    putIndex(v, m.to);
    uint8_t flags = kCommand;
    if (f.start)
        flags |= kStart;
    if (f.end)
        flags |= kEnd;
    if (m.reliable)
        flags |= kReliable;
    // The end of a reliable message, and any frame sent again, ask to be
    // acknowledged at once, rather than when the receiver gets round to it.
    if (m.reliable && (f.end || f.sends > 0))
        flags |= kSendAck;
    // One frame alone counts its sends; a longer message counts every
    // frame it has sent, retries too ([MC-DPL4R] 2.2.2).
    const bool alone = f.start && f.end;
    const uint8_t serial = alone ? uint8_t(f.sends) : m.serial++;
    v.insert(v.end(), {flags, m.id, f.sequence, serial});
    v.insert(v.end(), f.data.begin(), f.data.end());
    _out.push_back(std::move(v));
    f.sentMs = _clock();
    ++f.sends;
}

// Every frame up to `upTo` in sequence has come.
void ReliableLink::acknowledged(uint8_t upTo) {
    if (_sent.empty())
        return;
    const uint8_t oldest = _sent.front().frames.front().sequence;
    const uint8_t span = after(oldest, upTo);
    if (span >= 128)
        return; // older than anything outstanding
    const uint64_t t = _clock();
    for (Outgoing &m : _sent)
        for (Frame &f : m.frames)
            if (!f.acked && after(oldest, f.sequence) <= span) {
                f.acked = true;
                if (f.sends == 1) // only a frame sent once times the trip truly
                    roundTrip(t - f.sentMs);
            }
}

// Messages every frame of which has come are done with, making room in
// the window for more.
void ReliableLink::dropAcked() {
    _sent.erase(std::remove_if(_sent.begin(), _sent.end(),
                               [](const Outgoing &m) {
                                   return std::all_of(m.frames.begin(), m.frames.end(),
                                                      [](const Frame &f) { return f.acked; });
                               }),
                _sent.end());
    startMessages();
}

void ReliableLink::retransmit(uint8_t sequence) {
    for (Outgoing &m : _sent)
        for (Frame &f : m.frames)
            if (f.sequence == sequence && !f.acked)
                transmit(m, f);
}

void ReliableLink::receive(const std::vector<uint8_t> &frame) {
    size_t at = 0;
    uint16_t from = 0;
    uint16_t to = 0;
    if (!getIndex(frame, at, from) || !getIndex(frame, at, to) || at >= frame.size())
        return;
    _ourIndex = to;
    _theirIndex = from;
    const uint8_t flags = frame[at];
    if (flags & kAck) {
        if (flags & kExtension) {
            // NACK: flags, extended flags, messageid, sequence, bytes
            // received, tick count, then the mask of the others missing.
            if (frame.size() < at + 12)
                return;
            const int masks = (frame[at + 1] >> 5) & 3;
            const uint8_t missing = frame[at + 3];
            if (frame.size() < at + 12 + size_t(masks))
                return;
            acknowledged(uint8_t(missing - 1));
            retransmit(missing);
            // Within the mask, a clear bit is a frame that has come.
            for (int b = 0; b < masks * 8; ++b) {
                const uint8_t sequence = uint8_t(missing + 1 + b);
                if (frame[at + 12 + size_t(b / 8)] & (1 << (b % 8)))
                    retransmit(sequence);
                else
                    for (Outgoing &m : _sent)
                        for (Frame &f : m.frames)
                            if (f.sequence == sequence)
                                f.acked = true;
            }
        } else {
            if (frame.size() < at + 4)
                return;
            acknowledged(frame[at + 2]);
        }
        dropAcked();
        return;
    }
    if (!(flags & kCommand) || (flags & kExtension))
        return; // [MC-DPL4R] 2.2.2: a data frame with EXT is let go
    dataFrame(frame, at, from, to);
}

void ReliableLink::dataFrame(const std::vector<uint8_t> &frame, size_t at, uint16_t from, uint16_t to) {
    if (frame.size() < at + kDataHeader)
        return;
    const uint8_t flags = frame[at];
    const uint8_t id = frame[at + 1];
    const uint8_t sequence = frame[at + 2];
    _bytesReceived += uint32_t(frame.size() - at);
    const uint8_t ahead = after(_expected, sequence);
    const bool behind = ahead >= 128;
    auto found = _incoming.find(id);
    const bool known = found != _incoming.end() && (found->second.complete || found->second.frames.count(sequence));
    // A reliable message already handed on, its record since let go.
    const bool handedOn = (flags & kReliable) && after(id, _lastReliable) < 128 && found == _incoming.end();
    if (known || (behind && handedOn) || (!behind && _ahead.count(ahead))) {
        // Again: its acknowledgement may have been lost, so it's sent again.
        sendAck();
        return;
    }
    Incoming &in = _incoming[id];
    if (!behind) {
        _ahead[ahead] = true;
        if (ahead > 0 && !_gapSinceMs)
            _gapSinceMs = _clock();
        // Contiguous from the next wanted: those are taken in.
        _lastSerial = frame[at + 3];
        while (_ahead.count(0)) {
            std::map<uint8_t, bool> moved;
            for (const auto &[d, c] : _ahead)
                if (d > 0)
                    moved[uint8_t(d - 1)] = c;
            _ahead = std::move(moved);
            ++_expected;
        }
        if (_ahead.empty())
            _gapSinceMs = 0;
        else
            sendNack();
    }
    if (flags & kReliable)
        in.reliable = true;
    in.from = from;
    in.to = to;
    in.frames[sequence].assign(frame.begin() + long(at + kDataHeader), frame.end());
    if (flags & kStart)
        in.start = sequence;
    if (flags & kEnd)
        in.end = sequence;
    if (in.start && in.end) {
        const uint8_t span = after(*in.start, *in.end);
        bool whole = span < 128;
        for (uint8_t d = 0; whole && d <= span; ++d)
            whole = in.frames.count(uint8_t(*in.start + d)) > 0;
        if (whole) {
            for (uint8_t d = 0; d <= span; ++d) {
                const std::vector<uint8_t> &part = in.frames[uint8_t(*in.start + d)];
                in.data.insert(in.data.end(), part.begin(), part.end());
            }
            in.complete = true;
            if (!in.reliable)
                _delivered.push_back({in.from, in.to, false, in.data});
        }
    }
    if ((flags & (kSendAck | kEnd)) || in.complete)
        _ackDue = true;
    deliverReady();
    if (_ackDue)
        sendAck();
    // An unreliable message still in pieces 24 ids behind will never be
    // finished ([MC-DPL4R] 3.1.5.2), and finished ones far behind are let go.
    for (auto it = _incoming.begin(); it != _incoming.end();) {
        const uint8_t age = after(it->first, id);
        const bool stale = age < 128 && age >= kWindow && !it->second.reliable;
        const bool old = age < 128 && age >= 2 * kWindow && it->second.complete;
        it = (stale || old) ? _incoming.erase(it) : std::next(it);
    }
}

// Reliable messages go on in the order sent. One whose turn it isn't
// waits for those before it: an unreliable one doesn't hold it up, nor,
// after a while, one never seen at all, which was an unreliable one lost.
void ReliableLink::deliverReady() {
    const uint64_t t = _clock();
    while (true) {
        bool laterWaiting = false;
        for (const auto &[id, in] : _incoming)
            if (in.reliable && in.complete && after(_lastReliable, id) > 0 && after(_lastReliable, id) < 128)
                laterWaiting = true;
        if (!laterWaiting) {
            _blockedSinceMs = 0;
            return;
        }
        const uint8_t next = uint8_t(_lastReliable + 1);
        auto it = _incoming.find(next);
        if (it != _incoming.end() && it->second.reliable) {
            if (!it->second.complete)
                return;
            _delivered.push_back({it->second.from, it->second.to, true, it->second.data});
            it->second.data.clear();
            _lastReliable = next;
            _blockedSinceMs = 0;
            continue;
        }
        if (it != _incoming.end()) { // unreliable: no turn to wait for
            _lastReliable = next;
            continue;
        }
        if (!_blockedSinceMs)
            _blockedSinceMs = t;
        if (t - _blockedSinceMs < kUnknownMs)
            return;
        _incoming[next].complete = true;
        _lastReliable = next;
        _blockedSinceMs = 0;
    }
}

void ReliableLink::sendAck() {
    _ackDue = false;
    std::vector<uint8_t> v;
    putIndex(v, _ourIndex);
    putIndex(v, _theirIndex);
    const uint8_t last = uint8_t(_expected - 1);
    uint8_t id = 0;
    for (const auto &[mid, in] : _incoming)
        if (in.frames.count(last))
            id = mid;
    v.insert(v.end(), {kAck, id, last, _lastSerial});
    put32(v, _bytesReceived);
    put32(v, uint32_t(_clock()));
    _out.push_back(std::move(v));
}

void ReliableLink::sendNack() {
    const uint64_t t = _clock();
    if (_nackedMs && t - _nackedMs < retryMs() / 2)
        return;
    _nackedMs = t;
    uint32_t mask = 0;
    uint8_t highest = 0;
    for (const auto &[d, c] : _ahead)
        highest = std::max(highest, d);
    for (uint8_t d = 1; d < highest && d <= 24; ++d)
        if (!_ahead.count(d))
            mask |= 1u << (d - 1);
    const int bytes = mask ? (mask >> 16 ? 3 : mask >> 8 ? 2 : 1) : 0;
    std::vector<uint8_t> v;
    putIndex(v, _ourIndex);
    putIndex(v, _theirIndex);
    v.insert(v.end(), {uint8_t(kExtension | kAck), uint8_t(bytes << 5), 0, _expected});
    put32(v, _bytesReceived);
    put32(v, uint32_t(t));
    for (int i = 0; i < bytes; ++i)
        v.push_back(uint8_t(mask >> (8 * i)));
    _out.push_back(std::move(v));
}

void ReliableLink::update() {
    const uint64_t t = _clock();
    const uint64_t wait = retryMs();
    for (Outgoing &m : _sent)
        for (Frame &f : m.frames)
            // Each try waits longer, up to eight times, for a link gone slow.
            if (!f.acked && t - f.sentMs >= wait * uint64_t(1u << std::min(f.sends - 1, 3)))
                transmit(m, f);
    if (!_ahead.empty()) {
        if (_gapSinceMs && t - _gapSinceMs >= kGapMs) {
            // What's missing was unreliable and won't come again: the
            // wait moves on to the first frame that has come.
            uint8_t first = 0;
            for (const auto &[d, c] : _ahead)
                if (d > 0 && (!first || d < first))
                    first = d;
            _expected = uint8_t(_expected + first);
            std::map<uint8_t, bool> moved;
            for (const auto &[d, c] : _ahead)
                if (d >= first)
                    moved[uint8_t(d - first)] = c;
            _ahead = std::move(moved);
            while (_ahead.count(0)) {
                std::map<uint8_t, bool> next;
                for (const auto &[d, c] : _ahead)
                    if (d > 0)
                        next[uint8_t(d - 1)] = c;
                _ahead = std::move(next);
                ++_expected;
            }
            _gapSinceMs = _ahead.empty() ? 0 : t;
            sendAck();
        } else {
            sendNack();
        }
    }
    deliverReady();
    if (_ackDue)
        sendAck();
}

std::vector<std::vector<uint8_t>> ReliableLink::takeFrames() {
    std::vector<std::vector<uint8_t>> out = std::move(_out);
    _out.clear();
    return out;
}

std::optional<ReliableLink::Delivered> ReliableLink::take() {
    if (_delivered.empty())
        return std::nullopt;
    Delivered d = std::move(_delivered.front());
    _delivered.pop_front();
    return d;
}

} // namespace reluva::x86::dp4
