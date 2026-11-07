// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/oodle1.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace reluva::awe {

namespace {

// The range decoder. Its code value runs one bit behind the bytes: each
// byte's low bit is held back and goes in at the top of the next.
class RangeDecoder {
public:
    RangeDecoder(const uint8_t* data, size_t size) : p_(data), end_(data + size) {
        const uint8_t b = next();
        code_ = b >> 1;
        carry_ = b & 1;
    }

    // A value in [0, n), all equally likely.
    uint32_t uniform(uint32_t n) {
        normalize();
        return split(range_ / n, n);
    }
    // A value in [0, 2), equally likely.
    uint32_t bit() {
        normalize();
        return split(range_ >> 1, 2);
    }
    // Where the code falls in a total of 0x4000; consume() then takes the
    // symbol found there off.
    uint32_t peek14() {
        normalize();
        step_ = range_ >> 14;
        return std::min<uint32_t>(code_ / step_, 0x3fff);
    }
    void consume(uint32_t low, uint32_t freq) {
        code_ -= step_ * low;
        range_ = low + freq < 0x4000 ? step_ * freq : range_ - step_ * low;
    }

private:
    uint8_t next() { return p_ < end_ ? *p_++ : 0; }
    void normalize() {
        while (range_ <= 0x800000) {
            const uint8_t b = next();
            code_ = code_ << 8 | carry_ << 7 | b >> 1;
            carry_ = b & 1;
            range_ <<= 8;
        }
    }
    // The last value takes what the others leave over.
    uint32_t split(uint32_t step, uint32_t n) {
        const uint32_t q = std::min(code_ / step, n - 1);
        code_ -= q * step;
        range_ = q + 1 < n ? step : range_ - q * step;
        return q;
    }

    const uint8_t* p_;
    const uint8_t* end_;
    uint32_t code_ = 0, carry_ = 0, range_ = 0x80, step_ = 1;
};

// An adaptive model of up to `capacity` distinct symbols. Slot 0 is the
// escape; slots 1..seen hold the symbols met so far with their counts.
// The cumulative table, scaled to 0x4000, is rebuilt only now and then;
// symbols met since (slots built+1..seen) are coded after an escape.
// When the counts grow too large they are halved, and symbols whose count
// drops to 0 are forgotten.
class Model {
public:
    static constexpr int kNew = -1;

    void init(uint32_t hint, uint32_t capacity) {
        const size_t slots = (capacity + 5) & ~3u;
        counts_.assign(slots, 0);
        symbols_.assign(slots, 0);
        cum_.assign(slots + 2, 0xc000);
        cum_[0] = 0x8000;
        counts_[0] = 4;
        total_ = 4;
        threshold_ = 8;
        step_ = 4;
        seen_ = built_ = 0;
        capacity_ = uint16_t(capacity);
        maxTotal_ = uint16_t(std::clamp<uint32_t>(hint << 5, 0x100, 0x3b38));
        const uint32_t maxStep = hint * 2;
        maxStep_ = uint16_t(maxStep < 0x80 ? 0x80 : std::min<uint32_t>(maxStep, maxTotal_ / 2 - 0x20));
    }

    // The next symbol, or kNew for one not met yet, whose value the caller
    // reads and gives to setNew(). False on data that can't be decoded.
    bool decode(RangeDecoder& rd, int& symbol) {
        if (total_ >= threshold_) {
            if (threshold_ >= maxTotal_)
                rescale();
            rebuild();
        }
        const uint32_t at = rd.peek14() + 0x8000;
        const size_t i = size_t(std::upper_bound(cum_.begin(), cum_.begin() + built_ + 2, at) - cum_.begin()) - 1;
        rd.consume(cum_[i] - 0x8000u, cum_[i + 1] - cum_[i]);
        counts_[i]++;
        total_++;
        if (i > 0) {
            symbol = symbols_[i];
            return true;
        }
        if (seen_ != built_ && rd.bit()) {
            // One met since the last rebuild.
            const size_t k = rd.uniform(uint32_t(seen_ - built_)) + built_ + 1u;
            counts_[k] += 2;
            total_ += 2;
            symbol = symbols_[k];
            return true;
        }
        if (size_t(seen_) + 1 >= counts_.size())
            return false;
        seen_++;
        counts_[seen_] += 2;
        total_ += 2;
        if (seen_ == capacity_) {
            total_ -= counts_[0];
            counts_[0] = 0;
        }
        symbol = kNew;
        return true;
    }
    void setNew(uint32_t value) { symbols_[seen_] = uint16_t(value); }

private:
    void rebuild() {
        const uint32_t scale = 0x20000u / total_;
        uint32_t c = 0x8000 + (counts_[0] * scale >> 3);
        for (size_t i = 1;; i++) {
            cum_[i] = uint16_t(c);
            if (i > seen_)
                break;
            c += counts_[i] * scale >> 3;
        }
        cum_[seen_ + 1u] = 0xc000;
        // The next rebuild comes after a step that doubles each time, up to
        // maxStep_.
        if (step_ * 2u > maxStep_) {
            threshold_ = uint16_t(total_ + maxStep_);
        } else {
            step_ = uint16_t(step_ * 2);
            threshold_ = uint16_t(total_ + step_);
        }
        built_ = seen_;
    }

    void rescale() {
        counts_[0] >>= 1;
        total_ = counts_[0];
        uint16_t most = 0;
        size_t mostAt = 0;
        bool removedLast = false;
        for (size_t i = 1; i <= seen_ && !removedLast; i++) {
            // A symbol whose count would drop to 0 is replaced by the last.
            while (counts_[i] <= 1) {
                if (i >= seen_) {
                    counts_[i] = 0;
                    seen_--;
                    removedLast = true;
                    break;
                }
                counts_[i] = counts_[seen_];
                counts_[seen_] = 0;
                symbols_[i] = symbols_[seen_];
                seen_--;
            }
            if (removedLast)
                break;
            counts_[i] >>= 1;
            total_ = uint16_t(total_ + counts_[i]);
            if (counts_[i] > most) {
                most = counts_[i];
                mostAt = i;
            }
        }
        // The most frequent symbol goes last.
        if (most && seen_ && mostAt != seen_) {
            std::swap(counts_[seen_], counts_[mostAt]);
            std::swap(symbols_[seen_], symbols_[mostAt]);
        }
        if (seen_ != capacity_ && counts_[0] == 0) {
            counts_[0] = 1;
            total_++;
        }
    }

    std::vector<uint16_t> counts_, symbols_, cum_;
    uint16_t total_ = 0, threshold_ = 0, step_ = 0, maxStep_ = 0, maxTotal_ = 0;
    uint16_t seen_ = 0, built_ = 0, capacity_ = 0;
};

// The LZ77 coder of one part, with its models set up from the part's
// three header words.
class Part {
public:
    void init(const uint8_t* header) {
        const uint32_t h0 = rd32(header), h1 = rd32(header + 4), h2 = rd32(header + 8);
        literalValues_ = h0 & 0x1ff;
        window_ = h0 >> 9;
        lowValues_ = std::min<uint32_t>(window_ + 1, 4);
        highValues_ = std::min<uint32_t>((window_ >> 2) + 1, 0x100);
        const uint32_t midValues = (window_ >> 10) + 1;
        for (Model& m : literals_)
            m.init(literalValues_ - 1, h1 & 0x1ff);
        // Lengths are modelled after the previous one; each quarter of the
        // models takes its capacity from a byte of h2, the top one first.
        for (uint32_t i = 0; i < 65; i++)
            lengths_[i].init(0x40, h2 >> (24 - 8 * std::min<uint32_t>(i / 16, 3)) & 0xff);
        low_.init(lowValues_ - 1, lowValues_);
        mid_.init(midValues - 1, (h1 >> 19) + 1);
        high_.resize(midValues);
        for (Model& m : high_)
            m.init(highValues_ - 1, highValues_);
        lastLength_ = 0;
        done_ = 0;
    }

    // Decodes a literal or a match at out[pos], and moves pos past it.
    bool step(RangeDecoder& rd, std::vector<uint8_t>& out, size_t& pos) {
        uint32_t length = 0;
        if (!decode(lengths_[lastLength_], rd, 65, length))
            return false;
        lastLength_ = length;
        if (length == 0) {
            // A literal, modelled by where it falls in a 4-byte word.
            uint32_t value = 0;
            if (pos >= out.size() || !decode(literals_[pos & 3], rd, literalValues_, value))
                return false;
            out[pos++] = uint8_t(value);
            done_++;
            return true;
        }
        // A match: its offset back (minus 1) is high * 4 + low, with the
        // high part's top bits (mid) choosing the model for the rest. The
        // window is the part's size limit, or what the part has made so far.
        static constexpr uint32_t kLongLengths[4] = {128, 192, 256, 512};
        const uint32_t count = length < 61 ? length + 1 : kLongLengths[length - 61];
        const uint32_t window = std::min(window_, done_);
        uint32_t low = 0, mid = 0, high = 0;
        if (!decode(low_, rd, lowValues_, low) || !decode(mid_, rd, (window >> 10) + 1, mid) || mid >= high_.size() ||
            !decode(high_[mid], rd, window < 0x400 ? (window >> 2) + 1 : 0x100, high))
            return false;
        done_ += count;
        const size_t back = size_t(mid << 8 | high) * 4 + low + 1;
        if (back > pos || count > out.size() - pos)
            return false;
        // Byte by byte: the source may overlap what is being written.
        for (uint32_t i = 0; i < count; i++, pos++)
            out[pos] = out[pos - back];
        return true;
    }

private:
    static uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }

    // A symbol from a model; a new one is read as a value in [0, values).
    static bool decode(Model& m, RangeDecoder& rd, uint32_t values, uint32_t& out) {
        int symbol = 0;
        if (!m.decode(rd, symbol))
            return false;
        if (symbol != Model::kNew) {
            out = uint32_t(symbol);
            return true;
        }
        if (values == 0)
            return false;
        out = rd.uniform(values);
        m.setNew(out);
        return true;
    }

    Model literals_[4], lengths_[65], low_, mid_;
    std::vector<Model> high_;
    uint32_t literalValues_ = 0, window_ = 0, lowValues_ = 0, highValues_ = 0;
    uint32_t lastLength_ = 0, done_ = 0;
};

// Matches can run a little past the end of a part.
constexpr size_t kSlack = 512;

} // namespace

Error oodle1Decompress(const std::vector<uint8_t>& in, uint32_t stop0, uint32_t stop1, std::vector<uint8_t>& out) {
    // Three part headers of three words each, then one range-coded stream
    // for all three parts.
    constexpr size_t kHeader = 36;
    if (in.size() < kHeader)
        return Error::fail("Oodle1 data is too short");
    RangeDecoder rd(in.data() + kHeader, in.size() - kHeader);
    std::vector<uint8_t> buf(out.size() + kSlack);
    const uint32_t stops[3] = {stop0, stop1, uint32_t(out.size())};
    size_t pos = 0;
    Part part;
    for (int i = 0; i < 3; i++) {
        part.init(in.data() + 12 * i);
        while (pos < stops[i])
            if (!part.step(rd, buf, pos))
                return Error::fail("Oodle1 data is corrupt (at byte " + std::to_string(pos) + ")");
    }
    std::memcpy(out.data(), buf.data(), out.size());
    return Error::success();
}

Error gr2Decompress(uint32_t format, const std::vector<uint8_t>& in, uint32_t stop0, uint32_t stop1,
                    std::vector<uint8_t>& out) {
    if (format == 2)
        return oodle1Decompress(in, stop0, stop1, out);
    if (format == 0) {
        if (in.size() < out.size())
            return Error::fail("Uncompressed section is short");
        std::memcpy(out.data(), in.data(), out.size());
        return Error::success();
    }
    return Error::fail("Compression format " + std::to_string(format) + " isn't supported");
}

} // namespace reluva::awe
