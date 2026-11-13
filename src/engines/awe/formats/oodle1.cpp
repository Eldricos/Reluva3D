// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/formats/oodle1.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace reluva::awe {

namespace {

// The range decoder. Its code value runs one bit behind the bytes: each
// byte's low bit is held back and goes in at the top of the next.
class RangeDecoder {
public:
    RangeDecoder(const uint8_t *data, size_t size) : _p(data), _end(data + size) {
        const uint8_t b = next();
        _code = b >> 1;
        _carry = b & 1;
    }

    // A value in [0, n), all equally likely.
    uint32_t uniform(uint32_t n) {
        normalize();
        return split(_range / n, n);
    }
    // A value in [0, 2), equally likely.
    uint32_t bit() {
        normalize();
        return split(_range >> 1, 2);
    }
    // Where the code falls in a total of 0x4000; consume() then takes the
    // symbol found there off.
    uint32_t peek14() {
        normalize();
        _step = _range >> 14;
        return std::min<uint32_t>(_code / _step, 0x3fff);
    }
    void consume(uint32_t low, uint32_t freq) {
        _code -= _step * low;
        _range = low + freq < 0x4000 ? _step * freq : _range - _step * low;
    }

private:
    uint8_t next() { return _p < _end ? *_p++ : 0; }
    void normalize() {
        while (_range <= 0x800000) {
            const uint8_t b = next();
            _code = _code << 8 | _carry << 7 | b >> 1;
            _carry = b & 1;
            _range <<= 8;
        }
    }
    // The last value takes what the others leave over.
    uint32_t split(uint32_t step, uint32_t n) {
        const uint32_t q = std::min(_code / step, n - 1);
        _code -= q * step;
        _range = q + 1 < n ? step : _range - q * step;
        return q;
    }

    const uint8_t *_p;
    const uint8_t *_end;
    uint32_t _code = 0, _carry = 0, _range = 0x80, _step = 1;
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
        _counts.assign(slots, 0);
        _symbols.assign(slots, 0);
        _cum.assign(slots + 2, 0xc000);
        _cum[0] = 0x8000;
        _counts[0] = 4;
        _total = 4;
        _threshold = 8;
        _step = 4;
        _seen = _built = 0;
        _capacity = uint16_t(capacity);
        _maxTotal = uint16_t(std::clamp<uint32_t>(hint << 5, 0x100, 0x3b38));
        const uint32_t maxStep = hint * 2;
        _maxStep = uint16_t(maxStep < 0x80 ? 0x80 : std::min<uint32_t>(maxStep, _maxTotal / 2 - 0x20));
    }

    // The next symbol, or kNew for one not met yet, whose value the caller
    // reads and gives to setNew(). False on data that can't be decoded.
    bool decode(RangeDecoder &rd, int &symbol) {
        if (_total >= _threshold) {
            if (_threshold >= _maxTotal)
                rescale();
            rebuild();
        }
        const uint32_t at = rd.peek14() + 0x8000;
        const size_t i = size_t(std::upper_bound(_cum.begin(), _cum.begin() + _built + 2, at) - _cum.begin()) - 1;
        rd.consume(_cum[i] - 0x8000u, _cum[i + 1] - _cum[i]);
        _counts[i]++;
        _total++;
        if (i > 0) {
            symbol = _symbols[i];
            return true;
        }
        if (_seen != _built && rd.bit()) {
            // One met since the last rebuild.
            const size_t k = rd.uniform(uint32_t(_seen - _built)) + _built + 1u;
            _counts[k] += 2;
            _total += 2;
            symbol = _symbols[k];
            return true;
        }
        if (size_t(_seen) + 1 >= _counts.size())
            return false;
        _seen++;
        _counts[_seen] += 2;
        _total += 2;
        if (_seen == _capacity) {
            _total -= _counts[0];
            _counts[0] = 0;
        }
        symbol = kNew;
        return true;
    }
    void setNew(uint32_t value) { _symbols[_seen] = uint16_t(value); }

private:
    void rebuild() {
        const uint32_t scale = 0x20000u / _total;
        uint32_t c = 0x8000 + (_counts[0] * scale >> 3);
        for (size_t i = 1;; i++) {
            _cum[i] = uint16_t(c);
            if (i > _seen)
                break;
            c += _counts[i] * scale >> 3;
        }
        _cum[_seen + 1u] = 0xc000;
        // The next rebuild comes after a step that doubles each time, up to
        // maxStep_.
        if (_step * 2u > _maxStep) {
            _threshold = uint16_t(_total + _maxStep);
        } else {
            _step = uint16_t(_step * 2);
            _threshold = uint16_t(_total + _step);
        }
        _built = _seen;
    }

    void rescale() {
        _counts[0] >>= 1;
        _total = _counts[0];
        uint16_t most = 0;
        size_t mostAt = 0;
        bool removedLast = false;
        for (size_t i = 1; i <= _seen && !removedLast; i++) {
            // A symbol whose count would drop to 0 is replaced by the last.
            while (_counts[i] <= 1) {
                if (i >= _seen) {
                    _counts[i] = 0;
                    _seen--;
                    removedLast = true;
                    break;
                }
                _counts[i] = _counts[_seen];
                _counts[_seen] = 0;
                _symbols[i] = _symbols[_seen];
                _seen--;
            }
            if (removedLast)
                break;
            _counts[i] >>= 1;
            _total = uint16_t(_total + _counts[i]);
            if (_counts[i] > most) {
                most = _counts[i];
                mostAt = i;
            }
        }
        // The most frequent symbol goes last.
        if (most && _seen && mostAt != _seen) {
            std::swap(_counts[_seen], _counts[mostAt]);
            std::swap(_symbols[_seen], _symbols[mostAt]);
        }
        if (_seen != _capacity && _counts[0] == 0) {
            _counts[0] = 1;
            _total++;
        }
    }

    std::vector<uint16_t> _counts, _symbols, _cum;
    uint16_t _total = 0, _threshold = 0, _step = 0, _maxStep = 0, _maxTotal = 0;
    uint16_t _seen = 0, _built = 0, _capacity = 0;
};

// The LZ77 coder of one part, with its models set up from the part's
// three header words.
class Part {
public:
    void init(const uint8_t *header) {
        const uint32_t h0 = rd32(header);
        const uint32_t h1 = rd32(header + 4);
        const uint32_t h2 = rd32(header + 8);
        _literalValues = h0 & 0x1ff;
        _window = h0 >> 9;
        _lowValues = std::min<uint32_t>(_window + 1, 4);
        _highValues = std::min<uint32_t>((_window >> 2) + 1, 0x100);
        const uint32_t midValues = (_window >> 10) + 1;
        for (Model &m : _literals)
            m.init(_literalValues - 1, h1 & 0x1ff);
        // Lengths are modelled after the previous one; each quarter of the
        // models takes its capacity from a byte of h2, the top one first.
        for (uint32_t i = 0; i < 65; i++)
            _lengths[i].init(0x40, h2 >> (24 - 8 * std::min<uint32_t>(i / 16, 3)) & 0xff);
        _low.init(_lowValues - 1, _lowValues);
        _mid.init(midValues - 1, (h1 >> 19) + 1);
        _high.resize(midValues);
        for (Model &m : _high)
            m.init(_highValues - 1, _highValues);
        _lastLength = 0;
        _done = 0;
    }

    // Decodes a literal or a match at out[pos], and moves pos past it.
    bool step(RangeDecoder &rd, std::vector<uint8_t> &out, size_t &pos) {
        uint32_t length = 0;
        if (!decode(_lengths[_lastLength], rd, 65, length))
            return false;
        _lastLength = length;
        if (length == 0) {
            // A literal, modelled by where it falls in a 4-byte word.
            uint32_t value = 0;
            if (pos >= out.size() || !decode(_literals[pos & 3], rd, _literalValues, value))
                return false;
            out[pos++] = uint8_t(value);
            _done++;
            return true;
        }
        // A match: its offset back (minus 1) is high * 4 + low, with the
        // high part's top bits (mid) choosing the model for the rest. The
        // window is the part's size limit, or what the part has made so far.
        static constexpr uint32_t kLongLengths[4] = {128, 192, 256, 512};
        const uint32_t count = length < 61 ? length + 1 : kLongLengths[length - 61];
        const uint32_t window = std::min(_window, _done);
        uint32_t low = 0;
        uint32_t mid = 0;
        uint32_t high = 0;
        if (!decode(_low, rd, _lowValues, low) || !decode(_mid, rd, (window >> 10) + 1, mid) || mid >= _high.size() ||
            !decode(_high[mid], rd, window < 0x400 ? (window >> 2) + 1 : 0x100, high))
            return false;
        _done += count;
        const size_t back = size_t(mid << 8 | high) * 4 + low + 1;
        if (back > pos || count > out.size() - pos)
            return false;
        // Byte by byte: the source may overlap what is being written.
        for (uint32_t i = 0; i < count; i++, pos++)
            out[pos] = out[pos - back];
        return true;
    }

private:
    static uint32_t rd32(const uint8_t *p) { return uint32_t(p[0]) | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }

    // A symbol from a model; a new one is read as a value in [0, values).
    static bool decode(Model &m, RangeDecoder &rd, uint32_t values, uint32_t &out) {
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

    Model _literals[4], _lengths[65], _low, _mid;
    std::vector<Model> _high;
    uint32_t _literalValues = 0, _window = 0, _lowValues = 0, _highValues = 0;
    uint32_t _lastLength = 0, _done = 0;
};

// Matches can run a little past the end of a part.
constexpr size_t kSlack = 512;

} // namespace

Error oodle1Decompress(const std::vector<uint8_t> &in, uint32_t stop0, uint32_t stop1, std::vector<uint8_t> &out) {
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

Error gr2Decompress(uint32_t format, const std::vector<uint8_t> &in, uint32_t stop0, uint32_t stop1,
                    std::vector<uint8_t> &out) {
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
