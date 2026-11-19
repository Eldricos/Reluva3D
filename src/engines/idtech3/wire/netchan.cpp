// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/wire/netchan.h"

#include <algorithm>
#include <utility>

namespace reluva::idtech3::wire {

namespace {

constexpr size_t kScrambleStart = 6;
constexpr uint32_t kFragmentBit = 0x80000000u;

void putLong(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.push_back(uint8_t(v >> (8 * i)));
}
void putShort(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(uint8_t(v));
    out.push_back(uint8_t(v >> 8));
}
uint32_t getLong(const std::vector<uint8_t>& d, size_t at) {
    return uint32_t(d[at]) | uint32_t(d[at + 1]) << 8 | uint32_t(d[at + 2]) << 16 | uint32_t(d[at + 3]) << 24;
}
uint16_t getShort(const std::vector<uint8_t>& d, size_t at) { return uint16_t(d[at] | d[at + 1] << 8); }

// The sequence of numbers and the swap mask scrambling uses.
std::vector<uint32_t> scrambleSequence(const std::vector<uint8_t>& d, uint32_t& mask) {
    const auto n = uint32_t(d.size());
    uint32_t seed = (getLong(d, 0) * 3u) ^ (n * 123u) ^ 0x87243987u;
    std::vector<uint32_t> r(n);
    for (uint32_t i = 0; i < n; ++i) {
        seed = 69069u * seed + 1u;
        r[i] = seed;
    }
    // The largest 2^k - 1 below the length after the start (0 for 1).
    mask = 1;
    while (mask < n - kScrambleStart)
        mask = (mask << 1) + 1;
    mask >>= 1;
    return r;
}

} // namespace

void Netchan::scramble(std::vector<uint8_t>& d) {
    if (d.size() <= kScrambleStart)
        return;
    uint32_t mask = 0;
    const std::vector<uint32_t> r = scrambleSequence(d, mask);
    for (size_t i = kScrambleStart; i < d.size(); ++i)
        d[i] = uint8_t(d[i] ^ r[i]);
    for (size_t i = kScrambleStart; i < d.size(); ++i)
        std::swap(d[i], d[kScrambleStart + (r[i] & mask)]);
}

void Netchan::unscramble(std::vector<uint8_t>& d) {
    if (d.size() <= kScrambleStart)
        return;
    uint32_t mask = 0;
    const std::vector<uint32_t> r = scrambleSequence(d, mask);
    for (size_t i = d.size(); i-- > kScrambleStart;)
        std::swap(d[i], d[kScrambleStart + (r[i] & mask)]);
    for (size_t i = kScrambleStart; i < d.size(); ++i)
        d[i] = uint8_t(d[i] ^ r[i]);
}

std::vector<std::vector<uint8_t>> Netchan::transmit(const std::vector<uint8_t>& message) {
    std::vector<std::vector<uint8_t>> out;
    const auto sequence = uint32_t(outgoing_++);
    auto header = [&](uint32_t seq) {
        std::vector<uint8_t> d;
        putLong(d, seq);
        if (fromClient_)
            putShort(d, qport_);
        return d;
    };
    if (message.size() < kFragmentSize) {
        std::vector<uint8_t> d = header(sequence);
        d.insert(d.end(), message.begin(), message.end());
        scramble(d);
        out.push_back(std::move(d));
        return out;
    }
    // In pieces; one shorter than a whole piece is the last, so an exact
    // multiple ends with an empty one.
    size_t start = 0;
    while (true) {
        const size_t length = std::min(kFragmentSize, message.size() - start);
        std::vector<uint8_t> d = header(sequence | kFragmentBit);
        putShort(d, uint16_t(start));
        putShort(d, uint16_t(length));
        d.insert(d.end(), message.begin() + std::ptrdiff_t(start), message.begin() + std::ptrdiff_t(start + length));
        scramble(d);
        out.push_back(std::move(d));
        start += length;
        if (length < kFragmentSize)
            break;
    }
    return out;
}

bool Netchan::clientQport(const std::vector<uint8_t>& datagram, uint16_t& qport) {
    if (datagram.size() < 6)
        return false;
    qport = getShort(datagram, 4);
    return true;
}

bool Netchan::process(std::vector<uint8_t> d, std::vector<uint8_t>& message) {
    if (d.size() < 4)
        return false;
    unscramble(d);
    const uint32_t word = getLong(d, 0);
    const bool fragment = (word & kFragmentBit) != 0;
    const auto sequence = int32_t(word & ~kFragmentBit);
    size_t at = 4;
    if (!fromClient_)
        at += 2; // what a client sends has its qport; this end reads from one
    uint16_t fragmentStart = 0, fragmentLength = 0;
    if (fragment) {
        if (d.size() < at + 4)
            return false;
        fragmentStart = getShort(d, at);
        fragmentLength = getShort(d, at + 2);
        at += 4;
    }
    if (d.size() < at)
        return false;
    // Late or repeated: dropped.
    if (sequence <= incoming_)
        return false;
    dropped_ = sequence - (incoming_ + 1);
    if (fragment) {
        if (sequence != fragmentSequence_) {
            fragmentSequence_ = sequence;
            fragments_.clear();
        }
        // A piece out of order loses the message.
        if (fragmentStart != fragments_.size() || d.size() - at != fragmentLength ||
            fragments_.size() + fragmentLength > kMaxMessage)
            return false;
        fragments_.insert(fragments_.end(), d.begin() + std::ptrdiff_t(at), d.end());
        if (fragmentLength == kFragmentSize)
            return false; // more to come
        message = std::move(fragments_);
        fragments_.clear();
        incoming_ = sequence;
        return true;
    }
    incoming_ = sequence;
    message.assign(d.begin() + std::ptrdiff_t(at), d.end());
    return true;
}

} // namespace reluva::idtech3::wire
