// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The TrueType instructions, as the specification describes them: the
// graphics state, the zones, the stack machine and its instructions.
// Distances are 26.6 fixed point and vectors unit vectors in 2.14, as the
// specification has them, and all the arithmetic is in whole numbers: the
// same points on every platform.

#include "graphics/truetype_hinting.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>

namespace reluva::ttf {

namespace {

constexpr int kMaxInstructions = 1000000; // a program running longer is stuck
constexpr int kMaxCallDepth = 64;
constexpr size_t kMaxStack = 4096;

uint16_t u16(const uint8_t *p) {
    return uint16_t(p[0] << 8 | p[1]);
}
int16_t s16(const uint8_t *p) {
    return int16_t(u16(p));
}
uint32_t u32(const uint8_t *p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

constexpr int32_t kOne14 = 0x4000; // 1 in 2.14

// a / b, rounded half away from zero.
int64_t divRound(int64_t a, int64_t b) {
    if (b < 0)
        a = -a, b = -b;
    return a >= 0 ? (a + b / 2) / b : -((-a + b / 2) / b);
}

// v * f, f in 2.14, rounded half away from zero.
int32_t mul14(int64_t v, int32_t f) {
    return int32_t(divRound(v * f, kOne14));
}

// The whole part of the square root.
uint64_t isqrt(uint64_t n) {
    uint64_t r = 0;
    for (uint64_t bit = uint64_t(1) << 62; bit; bit >>= 2) {
        if (n >= r + bit) {
            n -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
    }
    return r;
}

// A unit vector, x and y in 2.14.
struct Vec {
    int32_t x = kOne14;
    int32_t y = 0;
};

Vec unit(int64_t x, int64_t y) {
    if (x == 0 && y == 0)
        return {};
    // Scaled up until the larger is at least 2^29, for the length's
    // precision; the squares' sum stays under 2^63.
    while (std::llabs(x) < (int64_t(1) << 29) && std::llabs(y) < (int64_t(1) << 29))
        x *= 2, y *= 2;
    while (std::llabs(x) >= (int64_t(1) << 30) || std::llabs(y) >= (int64_t(1) << 30))
        x /= 2, y /= 2;
    const int64_t l = int64_t(isqrt(uint64_t(x * x + y * y)));
    return {int32_t(divRound(x * kOne14, l)), int32_t(divRound(y * kOne14, l))};
}

// A zone's points: where each is now and where it was before hinting.
struct Zone {
    std::vector<int32_t> x;
    std::vector<int32_t> y;
    std::vector<int32_t> ox; // original (scaled) positions
    std::vector<int32_t> oy;
    std::vector<bool> touchX;
    std::vector<bool> touchY;
    std::vector<bool> onCurve;
    std::vector<int> contourEnds;
    size_t size() const { return x.size(); }
    void resize(size_t n) {
        x.assign(n, 0);
        y.assign(n, 0);
        ox.assign(n, 0);
        oy.assign(n, 0);
        touchX.assign(n, false);
        touchY.assign(n, false);
        onCurve.assign(n, true);
    }
};

enum class Round { kGrid, kHalfGrid, kDoubleGrid, kDownToGrid, kUpToGrid, kOff, kSuper, kSuper45 };

struct GraphicsState {
    Vec pv;
    Vec fv;
    Vec dv; // the dual projection vector, for original positions
    int rp0 = 0;
    int rp1 = 0;
    int rp2 = 0;
    int zp0 = 1;
    int zp1 = 1;
    int zp2 = 1;
    int loop = 1;
    int32_t minDistance = 64;
    int32_t cvtCutIn = 68; // 17/16 pixel
    int32_t singleWidthCutIn = 0;
    int32_t singleWidth = 0;
    int deltaBase = 9;
    int deltaShift = 3;
    bool autoFlip = true;
    Round round = Round::kGrid;
    int32_t period = 64; // SROUND's
    int32_t phase = 0;
    int32_t threshold = 32;
    int instructControl = 0;
};

struct Function {
    const std::vector<uint8_t> *code = nullptr;
    size_t start = 0;
    size_t end = 0; // the ENDF's position
};

} // namespace

struct Hinter::Tables {
    const uint8_t *data = nullptr;
    size_t size = 0;
    const uint8_t *glyf = nullptr;
    size_t glyfSize = 0;
    const uint8_t *loca = nullptr;
    size_t locaSize = 0;
    bool longLoca = false;
    const uint8_t *hmtx = nullptr;
    size_t hmtxSize = 0;
    int numberOfHMetrics = 0;
    int numGlyphs = 0;
    int maxStorage = 0;
    int maxFunctionDefs = 0;
    int maxTwilightPoints = 0;
    std::vector<int16_t> cvt; // FUnits
    std::vector<uint8_t> fpgm;
    std::vector<uint8_t> prep;
};

struct Hinter::Size {
    int ppem = 0;
    int unitsPerEm = 0;
    /** Font units in 26.6 at this size. */
    int32_t scaled(int64_t v) const { return int32_t(divRound(v * ppem * 64, unitsPerEm)); }
    std::vector<int32_t> cvt;
    std::vector<int32_t> storage;
    GraphicsState gs; // the glyphs' default, as prep left it
    std::vector<Function> functions;
    std::map<int, Function> idefs;
    bool ok = true; // fpgm and prep ran
};

namespace {

// One run of the interpreter over a program, with its zones.
class Exec {
public:
    Exec(Hinter::Size &size, Zone &twilight, Zone &glyph, std::vector<int32_t> &cvt, std::vector<int32_t> &storage,
         bool definingAllowed)
        : _size(size), _cvt(cvt), _storage(storage), _definitions(definingAllowed) {
        _zones[0] = &twilight;
        _zones[1] = &glyph;
    }
    GraphicsState gs;

    bool run(const std::vector<uint8_t> &code) {
        _count = 0;
        return execute(&code, 0, code.size(), 0);
    }

private:
    Hinter::Size &_size;
    Zone *_zones[2] = {nullptr, nullptr};
    std::vector<int32_t> &_cvt;
    std::vector<int32_t> &_storage;
    bool _definitions;
    std::vector<int32_t> _stack;
    int _count = 0;

    bool pop(int32_t &v) {
        if (_stack.empty())
            return false;
        v = _stack.back();
        _stack.pop_back();
        return true;
    }
    bool push(int32_t v) {
        if (_stack.size() >= kMaxStack)
            return false;
        _stack.push_back(v);
        return true;
    }
    Zone *zone(int z) const { return z == 0 || z == 1 ? _zones[z] : nullptr; }
    static bool valid(const Zone *z, int32_t p) { return z && p >= 0 && size_t(p) < z->size(); }

    int32_t project(int64_t dx, int64_t dy) const { return int32_t(divRound(dx * gs.pv.x + dy * gs.pv.y, kOne14)); }
    int32_t dualProject(int64_t dx, int64_t dy) const { return int32_t(divRound(dx * gs.dv.x + dy * gs.dv.y, kOne14)); }
    int32_t projectCur(const Zone &z, int p) const { return project(z.x[size_t(p)], z.y[size_t(p)]); }
    int32_t projectOrg(const Zone &z, int p) const { return dualProject(z.ox[size_t(p)], z.oy[size_t(p)]); }

    // Moves the point by `d` measured along the projection vector, the
    // move made along the freedom vector; marks it touched there.
    void move(Zone &z, int p, int32_t d, bool touch = true) const {
        // In 2.28. Near 0 (the vectors at right angles) it's taken as 1,
        // not divided by.
        int64_t dot = int64_t(gs.fv.x) * gs.pv.x + int64_t(gs.fv.y) * gs.pv.y;
        if (std::llabs(dot) < kOne14)
            dot = int64_t(kOne14) * kOne14;
        const size_t i = size_t(p);
        if (gs.fv.x != 0) {
            z.x[i] += int32_t(divRound(int64_t(d) * gs.fv.x * kOne14, dot));
            if (touch)
                z.touchX[i] = true;
        }
        if (gs.fv.y != 0) {
            z.y[i] += int32_t(divRound(int64_t(d) * gs.fv.y * kOne14, dot));
            if (touch)
                z.touchY[i] = true;
        }
    }

    int32_t round(int32_t v) const {
        const bool neg = v < 0;
        const int32_t a = neg ? -v : v;
        int32_t r = a;
        switch (gs.round) {
        case Round::kGrid:
            r = (a + 32) & ~63;
            break;
        case Round::kHalfGrid:
            r = (a & ~63) + 32;
            break;
        case Round::kDoubleGrid:
            r = (a + 16) & ~31;
            break;
        case Round::kDownToGrid:
            r = a & ~63;
            break;
        case Round::kUpToGrid:
            r = (a + 63) & ~63;
            break;
        case Round::kOff:
            r = a;
            break;
        case Round::kSuper:
        case Round::kSuper45: {
            const int32_t period = std::max(1, gs.period);
            r = (a - gs.phase + gs.threshold);
            r = r >= 0 ? r - r % period : -((-r + period - 1) / period * period);
            r += gs.phase;
            if (r < 0)
                r = gs.phase;
            break;
        }
        }
        return neg ? -r : r;
    }

    void setSuperRound(int32_t n, bool diagonal) {
        // Half a pixel, one, two (3 is reserved: one); S45ROUND's are
        // those times sqrt(2)/2, rounded to 64ths.
        static constexpr int32_t kPeriods[2][4] = {{32, 64, 128, 64}, {23, 45, 91, 45}};
        gs.period = kPeriods[diagonal ? 1 : 0][(n >> 6) & 3];
        switch ((n >> 4) & 3) {
        case 0:
            gs.phase = 0;
            break;
        case 1:
            gs.phase = gs.period / 4;
            break;
        case 2:
            gs.phase = gs.period / 2;
            break;
        default:
            gs.phase = gs.period * 3 / 4;
            break;
        }
        const int t = n & 15;
        gs.threshold = t == 0 ? gs.period - 1 : (t - 4) * gs.period / 8;
        gs.round = diagonal ? Round::kSuper45 : Round::kSuper;
    }

    // An instruction's length: its opcode and what's pushed with it.
    static size_t length(const std::vector<uint8_t> &code, size_t pc) {
        const uint8_t op = code[pc];
        if (op == 0x40)
            return pc + 1 < code.size() ? 2 + code[pc + 1] : 1;
        if (op == 0x41)
            return pc + 1 < code.size() ? 2 + 2 * size_t(code[pc + 1]) : 1;
        if (op >= 0xb0 && op <= 0xb7)
            return 1 + (op - 0xb0 + 1);
        if (op >= 0xb8 && op <= 0xbf)
            return 1 + 2 * (op - 0xb8 + 1);
        return 1;
    }

    // From an IF or ELSE, past what it skips: to the ELSE (when `toElse`)
    // or the EIF closing it.
    static bool skip(const std::vector<uint8_t> &code, size_t &pc, size_t end, bool toElse) {
        int depth = 0;
        pc += 1;
        while (pc < end) {
            const uint8_t op = code[pc];
            if (op == 0x58) {
                depth++;
            } else if (op == 0x59) {
                if (depth == 0) {
                    pc += 1;
                    return true;
                }
                depth--;
            } else if (op == 0x1b && depth == 0 && toElse) {
                pc += 1;
                return true;
            }
            pc += length(code, pc);
        }
        return false;
    }

    static bool skipToEndf(const std::vector<uint8_t> &code, size_t &pc, size_t end) {
        while (pc < end) {
            if (code[pc] == 0x2d)
                return true;
            pc += length(code, pc);
        }
        return false;
    }

    // The vector along the line from p2 to p1 (popped p1 first: p1 in
    // zp2, p2 in zp1), or at right angles to it, turned anticlockwise.
    bool lineVector(int32_t p1, int32_t p2, bool perpendicular, bool original, Vec &out) const {
        const Zone *a = zone(gs.zp2);
        const Zone *b = zone(gs.zp1);
        if (!valid(a, p1) || !valid(b, p2))
            return false;
        int64_t dx = 0;
        int64_t dy = 0;
        if (original) {
            dx = int64_t(a->ox[size_t(p1)]) - b->ox[size_t(p2)];
            dy = int64_t(a->oy[size_t(p1)]) - b->oy[size_t(p2)];
        } else {
            dx = int64_t(a->x[size_t(p1)]) - b->x[size_t(p2)];
            dy = int64_t(a->y[size_t(p1)]) - b->y[size_t(p2)];
        }
        if (perpendicular) {
            const int64_t t = dx;
            dx = -dy;
            dy = t;
        }
        out = unit(dx, dy);
        return true;
    }
    bool setVectorToLine(Vec &out, bool perpendicular) {
        int32_t p1 = 0;
        int32_t p2 = 0;
        return pop(p1) && pop(p2) && lineVector(p1, p2, perpendicular, false, out);
    }

    // IUP: the untouched points of each contour moved as the touched
    // around them were, in x or in y.
    void interpolateUntouched(bool xAxis) {
        Zone &z = *_zones[1];
        size_t start = 0;
        for (const int endPoint : z.contourEnds) {
            const size_t end = size_t(endPoint);
            if (end >= z.size() || end < start)
                break;
            std::vector<int32_t> &cur = xAxis ? z.x : z.y;
            const std::vector<int32_t> &org = xAxis ? z.ox : z.oy;
            const std::vector<bool> &touched = xAxis ? z.touchX : z.touchY;
            size_t first = end + 1;
            for (size_t i = start; i <= end; i++)
                if (touched[i]) {
                    first = i;
                    break;
                }
            if (first <= end) {
                size_t prev = first;
                const size_t n = end - start + 1;
                for (size_t step = 1; step <= n; step++) {
                    const size_t i = start + (first - start + step) % n;
                    if (!touched[i])
                        continue;
                    // The untouched between prev and i.
                    for (size_t j = start + (prev - start + 1) % n; j != i; j = start + (j - start + 1) % n)
                        interpolatePoint(cur, org, j, prev, i);
                    prev = i;
                }
            }
            start = end + 1;
        }
    }

    static void interpolatePoint(std::vector<int32_t> &cur, const std::vector<int32_t> &org, size_t p, size_t a,
                                 size_t b) {
        int32_t o1 = org[a];
        int32_t o2 = org[b];
        int32_t c1 = cur[a];
        int32_t c2 = cur[b];
        if (o1 > o2)
            std::swap(o1, o2), std::swap(c1, c2);
        const int32_t o = org[p];
        if (o <= o1)
            cur[p] = o + (c1 - o1);
        else if (o >= o2)
            cur[p] = o + (c2 - o2);
        else if (o1 == o2)
            cur[p] = c1;
        else
            cur[p] = c1 + int32_t(divRound(int64_t(o - o1) * (c2 - c1), o2 - o1));
    }

    bool delta(int which) {
        int32_t n = 0;
        if (!pop(n))
            return false;
        Zone *z = zone(gs.zp0);
        for (int32_t i = 0; i < n; i++) {
            int32_t p = 0;
            int32_t arg = 0;
            if (!pop(p) || !pop(arg))
                return false;
            if (deltaStep(which, arg) == 0 || !valid(z, p))
                continue;
            move(*z, p, deltaStep(which, arg));
        }
        return true;
    }

    bool deltaCvt(int which) {
        int32_t n = 0;
        if (!pop(n))
            return false;
        for (int32_t i = 0; i < n; i++) {
            int32_t c = 0;
            int32_t arg = 0;
            if (!pop(c) || !pop(arg))
                return false;
            if (c >= 0 && size_t(c) < _cvt.size())
                _cvt[size_t(c)] += deltaStep(which, arg);
        }
        return true;
    }

    // A delta's move at this size, 0 if it's for another (which: 0..2).
    int32_t deltaStep(int which, int32_t arg) const {
        const int ppem = gs.deltaBase + which * 16 + ((arg >> 4) & 15);
        if (ppem != _size.ppem)
            return 0;
        int s = arg & 15;
        s = s >= 8 ? s - 7 : s - 8;
        return int32_t(s * 64 / (1 << gs.deltaShift));
    }

    bool execute(const std::vector<uint8_t> *codePtr, size_t pc, size_t end, int depth) {
        if (depth > kMaxCallDepth)
            return false;
        const std::vector<uint8_t> &code = *codePtr;
        while (pc < end) {
            if (++_count > kMaxInstructions)
                return false;
            const uint8_t op = code[pc];
            const size_t at = pc;
            pc += 1;
            int32_t a = 0;
            int32_t b = 0;
            int32_t c = 0;
            switch (op) {
            case 0x00: // SVTCA[a]
            case 0x01:
                gs.pv = gs.fv = gs.dv = op == 1 ? Vec{kOne14, 0} : Vec{0, kOne14};
                break;
            case 0x02: // SPVTCA
            case 0x03:
                gs.pv = gs.dv = op == 3 ? Vec{kOne14, 0} : Vec{0, kOne14};
                break;
            case 0x04: // SFVTCA
            case 0x05:
                gs.fv = op == 5 ? Vec{kOne14, 0} : Vec{0, kOne14};
                break;
            case 0x06: // SPVTL
            case 0x07:
                if (!setVectorToLine(gs.pv, op == 7))
                    return false;
                gs.dv = gs.pv;
                break;
            case 0x08: // SFVTL
            case 0x09:
                if (!setVectorToLine(gs.fv, op == 9))
                    return false;
                break;
            case 0x0a: // SPVFS
            case 0x0b: // SFVFS
                if (!pop(b) || !pop(a))
                    return false;
                if (op == 0x0a)
                    gs.pv = gs.dv = unit(a, b);
                else
                    gs.fv = unit(a, b);
                break;
            case 0x0c:   // GPV
            case 0x0d: { // GFV
                const Vec &v = op == 0x0c ? gs.pv : gs.fv;
                if (!push(v.x) || !push(v.y))
                    return false;
                break;
            }
            case 0x0e: // SFVTPV
                gs.fv = gs.pv;
                break;
            case 0x0f: { // ISECT
                int32_t b1 = 0;
                int32_t b0 = 0;
                int32_t a1 = 0;
                int32_t a0 = 0;
                int32_t p = 0;
                if (!pop(b1) || !pop(b0) || !pop(a1) || !pop(a0) || !pop(p))
                    return false;
                Zone *zp = zone(gs.zp2);
                const Zone *za = zone(gs.zp1);
                const Zone *zb = zone(gs.zp0);
                if (!valid(zp, p) || !valid(za, a0) || !valid(za, a1) || !valid(zb, b0) || !valid(zb, b1))
                    return false;
                const int64_t ax0 = za->x[size_t(a0)];
                const int64_t ay0 = za->y[size_t(a0)];
                const int64_t ax1 = za->x[size_t(a1)];
                const int64_t ay1 = za->y[size_t(a1)];
                const int64_t bx0 = zb->x[size_t(b0)];
                const int64_t by0 = zb->y[size_t(b0)];
                const int64_t bx1 = zb->x[size_t(b1)];
                const int64_t by1 = zb->y[size_t(b1)];
                const int64_t dax = ax1 - ax0;
                const int64_t day = ay1 - ay0;
                const int64_t dbx = bx1 - bx0;
                const int64_t dby = by1 - by0;
                const int64_t den = dax * dby - day * dbx;
                int64_t x = 0;
                int64_t y = 0;
                if (den == 0) {
                    // Parallel: the middle of the four.
                    x = divRound(ax0 + ax1 + bx0 + bx1, 4);
                    y = divRound(ay0 + ay1 + by0 + by1, 4);
                } else {
                    // A's start plus t of A, t = num / den; 26.6 points
                    // keep the products well inside 64 bits.
                    const int64_t num = (bx0 - ax0) * dby - (by0 - ay0) * dbx;
                    x = ax0 + divRound(num * dax, den);
                    y = ay0 + divRound(num * day, den);
                }
                zp->x[size_t(p)] = int32_t(x);
                zp->y[size_t(p)] = int32_t(y);
                zp->touchX[size_t(p)] = zp->touchY[size_t(p)] = true;
                break;
            }
            case 0x10: // SRP0..2
            case 0x11:
            case 0x12:
                if (!pop(a))
                    return false;
                (op == 0x10 ? gs.rp0 : op == 0x11 ? gs.rp1 : gs.rp2) = a;
                break;
            case 0x13: // SZP0..2, SZPS
            case 0x14:
            case 0x15:
            case 0x16:
                if (!pop(a) || (a != 0 && a != 1))
                    return false;
                if (op == 0x13 || op == 0x16)
                    gs.zp0 = a;
                if (op == 0x14 || op == 0x16)
                    gs.zp1 = a;
                if (op == 0x15 || op == 0x16)
                    gs.zp2 = a;
                break;
            case 0x17: // SLOOP
                if (!pop(a) || a < 0)
                    return false;
                gs.loop = a;
                break;
            case 0x18: // RTG
                gs.round = Round::kGrid;
                break;
            case 0x19: // RTHG
                gs.round = Round::kHalfGrid;
                break;
            case 0x1a: // SMD
                if (!pop(gs.minDistance))
                    return false;
                break;
            case 0x1b: // ELSE, met after its IF's true part
                if (!skip(code, pc = at, end, false))
                    return false;
                break;
            case 0x1c: // JMPR
                if (!pop(a))
                    return false;
                pc = size_t(int64_t(at) + a);
                if (a == 0 || pc > end)
                    return false;
                break;
            case 0x1d: // SCVTCI
                if (!pop(gs.cvtCutIn))
                    return false;
                break;
            case 0x1e: // SSWCI
                if (!pop(gs.singleWidthCutIn))
                    return false;
                break;
            case 0x1f: // SSW, in font units
                if (!pop(a))
                    return false;
                gs.singleWidth = _size.scaled(a);
                break;
            case 0x20: // DUP
                if (!pop(a) || !push(a) || !push(a))
                    return false;
                break;
            case 0x21: // POP
                if (!pop(a))
                    return false;
                break;
            case 0x22: // CLEAR
                _stack.clear();
                break;
            case 0x23: // SWAP
                if (!pop(b) || !pop(a) || !push(b) || !push(a))
                    return false;
                break;
            case 0x24: // DEPTH
                if (!push(int32_t(_stack.size())))
                    return false;
                break;
            case 0x25:   // CINDEX
            case 0x26: { // MINDEX
                if (!pop(a) || a <= 0 || size_t(a) > _stack.size())
                    return false;
                const size_t i = _stack.size() - size_t(a);
                const int32_t v = _stack[i];
                if (op == 0x26)
                    _stack.erase(_stack.begin() + long(i));
                if (!push(v))
                    return false;
                break;
            }
            case 0x27: { // ALIGNPTS
                if (!pop(b) || !pop(a))
                    return false;
                Zone *z1 = zone(gs.zp1);
                Zone *z0 = zone(gs.zp0);
                if (!valid(z1, a) || !valid(z0, b))
                    return false;
                const int32_t d = (projectCur(*z0, b) - projectCur(*z1, a)) / 2;
                move(*z1, a, d);
                move(*z0, b, -d);
                break;
            }
            case 0x29: { // UTP
                if (!pop(a))
                    return false;
                Zone *z = zone(gs.zp0);
                if (!valid(z, a))
                    return false;
                if (gs.fv.x != 0)
                    z->touchX[size_t(a)] = false;
                if (gs.fv.y != 0)
                    z->touchY[size_t(a)] = false;
                break;
            }
            case 0x2a:   // LOOPCALL
            case 0x2b: { // CALL
                if (!pop(a))
                    return false;
                b = 1;
                if (op == 0x2a && !pop(b))
                    return false;
                if (a < 0 || size_t(a) >= _size.functions.size() || !_size.functions[size_t(a)].code)
                    return false;
                const Function f = _size.functions[size_t(a)];
                for (int32_t i = 0; i < b; i++)
                    if (!execute(f.code, f.start, f.end, depth + 1))
                        return false;
                break;
            }
            case 0x2c: { // FDEF
                if (!_definitions || !pop(a) || a < 0 || a >= 65536)
                    return false;
                const size_t start = pc;
                if (!skipToEndf(code, pc, end))
                    return false;
                if (size_t(a) >= _size.functions.size())
                    _size.functions.resize(size_t(a) + 1);
                _size.functions[size_t(a)] = {codePtr, start, pc};
                pc += 1;
                break;
            }
            case 0x2d: // ENDF
                return depth > 0;
            case 0x2e: // MDAP[r]
            case 0x2f: {
                if (!pop(a))
                    return false;
                Zone *z = zone(gs.zp0);
                if (!valid(z, a))
                    return false;
                const int32_t d = projectCur(*z, a);
                move(*z, a, op == 0x2f ? round(d) - d : 0);
                gs.rp0 = gs.rp1 = a;
                break;
            }
            case 0x30: // IUP[y]
            case 0x31: // IUP[x]
                if (gs.zp2 != 1)
                    return false;
                interpolateUntouched(op == 0x31);
                break;
            case 0x32: // SHP[a]
            case 0x33:
            case 0x34: // SHC[a]
            case 0x35:
            case 0x36: // SHZ[a]
            case 0x37: {
                const bool rp1 = (op & 1) != 0;
                Zone *rz = zone(rp1 ? gs.zp0 : gs.zp1);
                const int32_t rp = rp1 ? gs.rp1 : gs.rp2;
                if (!valid(rz, rp))
                    return false;
                const int32_t d = project(int64_t(rz->x[size_t(rp)]) - rz->ox[size_t(rp)],
                                          int64_t(rz->y[size_t(rp)]) - rz->oy[size_t(rp)]);
                if (op <= 0x33) {
                    Zone *z = zone(gs.zp2);
                    for (; gs.loop > 0; gs.loop--) {
                        if (!pop(a) || !valid(z, a))
                            return false;
                        move(*z, a, d);
                    }
                    gs.loop = 1;
                } else if (op <= 0x35) {
                    if (!pop(a))
                        return false;
                    Zone *z = zone(gs.zp2);
                    if (!z || a < 0 || size_t(a) >= z->contourEnds.size())
                        return false;
                    const int first = a == 0 ? 0 : z->contourEnds[size_t(a - 1)] + 1;
                    for (int p = first; p <= z->contourEnds[size_t(a)]; p++)
                        if (!(z == rz && p == rp))
                            move(*z, p, d);
                } else {
                    if (!pop(a))
                        return false;
                    Zone *z = zone(a);
                    if (!z)
                        return false;
                    const size_t n = a == 1 ? z->size() - std::min<size_t>(4, z->size()) : z->size();
                    for (size_t p = 0; p < n; p++)
                        if (!(z == rz && int32_t(p) == rp))
                            move(*z, int(p), d, false);
                }
                break;
            }
            case 0x38: { // SHPIX
                if (!pop(c))
                    return false;
                Zone *z = zone(gs.zp2);
                for (; gs.loop > 0; gs.loop--) {
                    if (!pop(a) || !valid(z, a))
                        return false;
                    const size_t i = size_t(a);
                    if (gs.fv.x != 0)
                        z->x[i] += mul14(c, gs.fv.x), z->touchX[i] = true;
                    if (gs.fv.y != 0)
                        z->y[i] += mul14(c, gs.fv.y), z->touchY[i] = true;
                }
                gs.loop = 1;
                break;
            }
            case 0x39: { // IP
                Zone *z1 = zone(gs.zp0);
                Zone *z2 = zone(gs.zp1);
                Zone *z = zone(gs.zp2);
                if (!valid(z1, gs.rp1) || !valid(z2, gs.rp2))
                    return false;
                const size_t r1 = size_t(gs.rp1);
                const size_t r2 = size_t(gs.rp2);
                const int32_t orgRange =
                    dualProject(int64_t(z2->ox[r2]) - z1->ox[r1], int64_t(z2->oy[r2]) - z1->oy[r1]);
                const int32_t curRange = project(int64_t(z2->x[r2]) - z1->x[r1], int64_t(z2->y[r2]) - z1->y[r1]);
                for (; gs.loop > 0; gs.loop--) {
                    if (!pop(a) || !valid(z, a))
                        return false;
                    const size_t i = size_t(a);
                    const int32_t orgDist = dualProject(int64_t(z->ox[i]) - z1->ox[r1], int64_t(z->oy[i]) - z1->oy[r1]);
                    const int32_t curDist = project(int64_t(z->x[i]) - z1->x[r1], int64_t(z->y[i]) - z1->y[r1]);
                    int32_t want = orgDist;
                    if (orgRange != 0)
                        want = int32_t(divRound(int64_t(orgDist) * curRange, orgRange));
                    move(*z, a, want - curDist);
                }
                gs.loop = 1;
                break;
            }
            case 0x3a: // MSIRP[a]
            case 0x3b: {
                if (!pop(c) || !pop(a))
                    return false;
                Zone *z = zone(gs.zp1);
                Zone *r = zone(gs.zp0);
                if (!valid(z, a) || !valid(r, gs.rp0))
                    return false;
                const size_t i = size_t(a);
                const size_t r0 = size_t(gs.rp0);
                if (gs.zp1 == 0) {
                    z->ox[i] = r->ox[r0];
                    z->oy[i] = r->oy[r0];
                    z->x[i] = z->ox[i];
                    z->y[i] = z->oy[i];
                }
                const int32_t now = project(int64_t(z->x[i]) - r->x[r0], int64_t(z->y[i]) - r->y[r0]);
                move(*z, a, c - now);
                gs.rp1 = gs.rp0;
                gs.rp2 = a;
                if (op == 0x3b)
                    gs.rp0 = a;
                break;
            }
            case 0x3c: { // ALIGNRP
                Zone *z = zone(gs.zp1);
                Zone *r = zone(gs.zp0);
                if (!valid(r, gs.rp0))
                    return false;
                const size_t r0 = size_t(gs.rp0);
                for (; gs.loop > 0; gs.loop--) {
                    if (!pop(a) || !valid(z, a))
                        return false;
                    const size_t i = size_t(a);
                    move(*z, a, -project(int64_t(z->x[i]) - r->x[r0], int64_t(z->y[i]) - r->y[r0]));
                }
                gs.loop = 1;
                break;
            }
            case 0x3d: // RTDG
                gs.round = Round::kDoubleGrid;
                break;
            case 0x3e: // MIAP[r]
            case 0x3f: {
                if (!pop(b) || !pop(a))
                    return false;
                Zone *z = zone(gs.zp0);
                if (!valid(z, a) || b < 0 || size_t(b) >= _cvt.size())
                    return false;
                const size_t i = size_t(a);
                int32_t d = _cvt[size_t(b)];
                if (gs.zp0 == 0) {
                    z->ox[i] = z->x[i] = mul14(d, gs.pv.x);
                    z->oy[i] = z->y[i] = mul14(d, gs.pv.y);
                }
                const int32_t now = projectCur(*z, a);
                if (op == 0x3f) {
                    if (std::abs(d - now) > gs.cvtCutIn)
                        d = now;
                    d = round(d);
                }
                move(*z, a, d - now);
                gs.rp0 = gs.rp1 = a;
                break;
            }
            case 0x40:   // NPUSHB
            case 0x41: { // NPUSHW
                if (pc >= end)
                    return false;
                const size_t n = code[pc++];
                const size_t bytes = op == 0x40 ? n : 2 * n;
                if (pc + bytes > end)
                    return false;
                for (size_t k = 0; k < n; k++)
                    if (!push(op == 0x40 ? int32_t(code[pc + k]) : int32_t(s16(&code[pc + 2 * k]))))
                        return false;
                pc += bytes;
                break;
            }
            case 0x42: // WS
                if (!pop(b) || !pop(a))
                    return false;
                if (a < 0 || size_t(a) >= _storage.size())
                    return false;
                _storage[size_t(a)] = b;
                break;
            case 0x43: // RS
                if (!pop(a) || a < 0 || size_t(a) >= _storage.size() || !push(_storage[size_t(a)]))
                    return false;
                break;
            case 0x44: // WCVTP
            case 0x70: // WCVTF, in font units
                if (!pop(b) || !pop(a) || a < 0 || size_t(a) >= _cvt.size())
                    return false;
                _cvt[size_t(a)] = op == 0x44 ? b : _size.scaled(b);
                break;
            case 0x45: // RCVT
                if (!pop(a) || a < 0 || size_t(a) >= _cvt.size() || !push(_cvt[size_t(a)]))
                    return false;
                break;
            case 0x46: // GC[a]
            case 0x47: {
                if (!pop(a))
                    return false;
                Zone *z = zone(gs.zp2);
                if (!valid(z, a) || !push(op == 0x46 ? projectCur(*z, a) : projectOrg(*z, a)))
                    return false;
                break;
            }
            case 0x48: { // SCFS
                if (!pop(b) || !pop(a))
                    return false;
                Zone *z = zone(gs.zp2);
                if (!valid(z, a))
                    return false;
                move(*z, a, b - projectCur(*z, a));
                if (gs.zp2 == 0) {
                    z->ox[size_t(a)] = z->x[size_t(a)];
                    z->oy[size_t(a)] = z->y[size_t(a)];
                }
                break;
            }
            case 0x49: // MD[a]
            case 0x4a: {
                if (!pop(b) || !pop(a))
                    return false;
                Zone *za = zone(gs.zp0);
                Zone *zb = zone(gs.zp1);
                if (!valid(za, a) || !valid(zb, b))
                    return false;
                const size_t i = size_t(a);
                const size_t j = size_t(b);
                const int32_t d = op == 0x49
                                      ? project(int64_t(za->x[i]) - zb->x[j], int64_t(za->y[i]) - zb->y[j])
                                      : dualProject(int64_t(za->ox[i]) - zb->ox[j], int64_t(za->oy[i]) - zb->oy[j]);
                if (!push(d))
                    return false;
                break;
            }
            case 0x4b: // MPPEM
            case 0x4c: // MPS
                if (!push(_size.ppem))
                    return false;
                break;
            case 0x4d: // FLIPON
            case 0x4e: // FLIPOFF
                gs.autoFlip = op == 0x4d;
                break;
            case 0x4f: // DEBUG
                if (!pop(a))
                    return false;
                break;
            case 0x50: // LT, LTEQ, GT, GTEQ, EQ, NEQ
            case 0x51:
            case 0x52:
            case 0x53:
            case 0x54:
            case 0x55: {
                if (!pop(b) || !pop(a))
                    return false;
                const bool r = op == 0x50   ? a < b
                               : op == 0x51 ? a <= b
                               : op == 0x52 ? a > b
                               : op == 0x53 ? a >= b
                               : op == 0x54 ? a == b
                                            : a != b;
                if (!push(r ? 1 : 0))
                    return false;
                break;
            }
            case 0x56:   // ODD
            case 0x57: { // EVEN
                if (!pop(a))
                    return false;
                const bool odd = (round(a) & 127) == 64;
                if (!push((op == 0x56) == odd ? 1 : 0))
                    return false;
                break;
            }
            case 0x58: // IF
                if (!pop(a))
                    return false;
                if (a == 0 && !skip(code, pc = at, end, true))
                    return false;
                break;
            case 0x59: // EIF
                break;
            case 0x5a: // AND, OR
            case 0x5b:
                if (!pop(b) || !pop(a) || !push(op == 0x5a ? (a && b) : (a || b)))
                    return false;
                break;
            case 0x5c: // NOT
                if (!pop(a) || !push(a ? 0 : 1))
                    return false;
                break;
            case 0x5d: // DELTAP1
                if (!delta(0))
                    return false;
                break;
            case 0x71: // DELTAP2, DELTAP3
            case 0x72:
                if (!delta(op - 0x70))
                    return false;
                break;
            case 0x73: // DELTAC1..3
            case 0x74:
            case 0x75:
                if (!deltaCvt(op - 0x73))
                    return false;
                break;
            case 0x5e: // SDB
                if (!pop(gs.deltaBase))
                    return false;
                break;
            case 0x5f: // SDS
                if (!pop(a) || a < 0 || a > 6)
                    return false;
                gs.deltaShift = a;
                break;
            case 0x60: // ADD, SUB, DIV, MUL
            case 0x61:
            case 0x62:
            case 0x63: {
                if (!pop(b) || !pop(a))
                    return false;
                int64_t r = 0;
                if (op == 0x60)
                    r = int64_t(a) + b;
                else if (op == 0x61)
                    r = int64_t(a) - b;
                else if (op == 0x62)
                    r = b == 0 ? 0 : int64_t(a) * 64 / b;
                else
                    r = (int64_t(a) * b) / 64;
                if (!push(int32_t(r)))
                    return false;
                break;
            }
            case 0x64: // ABS, NEG, FLOOR, CEILING
            case 0x65:
            case 0x66:
            case 0x67:
                if (!pop(a))
                    return false;
                a = op == 0x64 ? std::abs(a) : op == 0x65 ? -a : op == 0x66 ? (a & ~63) : ((a + 63) & ~63);
                if (!push(a))
                    return false;
                break;
            case 0x68: // ROUND[ab], NROUND[ab]
            case 0x69:
            case 0x6a:
            case 0x6b:
            case 0x6c:
            case 0x6d:
            case 0x6e:
            case 0x6f:
                if (!pop(a) || !push(op <= 0x6b ? round(a) : a))
                    return false;
                break;
            case 0x76: // SROUND, S45ROUND
            case 0x77:
                if (!pop(a))
                    return false;
                setSuperRound(a, op == 0x77);
                break;
            case 0x78: // JROT, JROF
            case 0x79:
                if (!pop(b) || !pop(a))
                    return false;
                if ((b != 0) == (op == 0x78)) {
                    if (a == 0)
                        return false;
                    pc = size_t(int64_t(at) + a);
                    if (pc > end)
                        return false;
                }
                break;
            case 0x7a: // ROFF
                gs.round = Round::kOff;
                break;
            case 0x7c: // RUTG
                gs.round = Round::kUpToGrid;
                break;
            case 0x7d: // RDTG
                gs.round = Round::kDownToGrid;
                break;
            case 0x7e: // SANGW, AA: obsolete
            case 0x7f:
            case 0x85: // SCANCTRL
            case 0x8d: // SCANTYPE
                if (!pop(a))
                    return false;
                break;
            case 0x80: { // FLIPPT
                Zone *z = zone(gs.zp0);
                for (; gs.loop > 0; gs.loop--) {
                    if (!pop(a) || !valid(z, a))
                        return false;
                    z->onCurve[size_t(a)] = !z->onCurve[size_t(a)];
                }
                gs.loop = 1;
                break;
            }
            case 0x81: // FLIPRGON, FLIPRGOFF
            case 0x82: {
                if (!pop(b) || !pop(a))
                    return false;
                Zone *z = zone(gs.zp0);
                if (!valid(z, a) || !valid(z, b))
                    return false;
                for (int32_t p = a; p <= b; p++)
                    z->onCurve[size_t(p)] = op == 0x81;
                break;
            }
            case 0x86: // SDPVTL: the dual vector from the originals
            case 0x87:
                if (!pop(a) || !pop(b) || !lineVector(a, b, op == 0x87, true, gs.dv) ||
                    !lineVector(a, b, op == 0x87, false, gs.pv))
                    return false;
                break;
            case 0x88: { // GETINFO
                if (!pop(a))
                    return false;
                // As Windows' rasterizer of the time reports itself
                // (version 35), drawing in grey levels (bit 12).
                int32_t r = 0;
                if (a & 1)
                    r |= 35;
                if (a & 32)
                    r |= 1 << 12;
                if (!push(r))
                    return false;
                break;
            }
            case 0x89: { // IDEF
                if (!_definitions || !pop(a))
                    return false;
                const size_t start = pc;
                if (!skipToEndf(code, pc, end))
                    return false;
                _size.idefs[a] = {codePtr, start, pc};
                pc += 1;
                break;
            }
            case 0x8a: // ROLL
                if (!pop(a) || !pop(b) || !pop(c) || !push(b) || !push(a) || !push(c))
                    return false;
                break;
            case 0x8b: // MAX, MIN
            case 0x8c:
                if (!pop(b) || !pop(a) || !push(op == 0x8b ? std::max(a, b) : std::min(a, b)))
                    return false;
                break;
            case 0x8e: // INSTCTRL
                if (!pop(a) || !pop(b))
                    return false;
                if (a >= 1 && a <= 3)
                    gs.instructControl = (gs.instructControl & ~(1 << (a - 1))) | (b ? 1 << (a - 1) : 0);
                break;
            default:
                if (op >= 0xb0 && op <= 0xbf) { // PUSHB, PUSHW
                    const bool words = op >= 0xb8;
                    const size_t n = size_t(op - (words ? 0xb8 : 0xb0) + 1);
                    if (pc + n * (words ? 2 : 1) > end)
                        return false;
                    for (size_t k = 0; k < n; k++)
                        if (!push(words ? int32_t(s16(&code[pc + 2 * k])) : int32_t(code[pc + k])))
                            return false;
                    pc += n * (words ? 2 : 1);
                } else if (op >= 0xc0) { // MDRP, MIRP
                    if (!moveRelative(op))
                        return false;
                } else if (auto it = _size.idefs.find(op); it != _size.idefs.end()) {
                    if (!execute(it->second.code, it->second.start, it->second.end, depth + 1))
                        return false;
                } else {
                    return false;
                }
                break;
            }
        }
        return true;
    }

    // MDRP[abcde] and MIRP[abcde]: the point at a distance from rp0, its
    // original's or the control value's, kept to the minimum, rounded,
    // made rp0 after (a), its single width and cut-in as the spec says.
    bool moveRelative(uint8_t op) {
        const bool indirect = op >= 0xe0;
        const bool setRp0 = (op & 0x10) != 0;
        const bool keepMin = (op & 0x08) != 0;
        const bool roundIt = (op & 0x04) != 0;
        int32_t cvtIndex = 0;
        int32_t p = 0;
        if ((indirect && !pop(cvtIndex)) || !pop(p))
            return false;
        Zone *z = zone(gs.zp1);
        Zone *r = zone(gs.zp0);
        if (!valid(z, p) || !valid(r, gs.rp0))
            return false;
        const size_t i = size_t(p);
        const size_t r0 = size_t(gs.rp0);
        int32_t cvtDistance = 0;
        if (indirect) {
            if (cvtIndex < 0 || size_t(cvtIndex) >= _cvt.size())
                return false;
            cvtDistance = _cvt[size_t(cvtIndex)];
            if (std::abs(std::abs(cvtDistance) - gs.singleWidth) < gs.singleWidthCutIn)
                cvtDistance = cvtDistance >= 0 ? gs.singleWidth : -gs.singleWidth;
            if (gs.zp1 == 0) {
                z->ox[i] = r->ox[r0] + mul14(cvtDistance, gs.fv.x);
                z->oy[i] = r->oy[r0] + mul14(cvtDistance, gs.fv.y);
                z->x[i] = z->ox[i];
                z->y[i] = z->oy[i];
            }
        }
        const int32_t orgDistance = dualProject(int64_t(z->ox[i]) - r->ox[r0], int64_t(z->oy[i]) - r->oy[r0]);
        const int32_t curDistance = project(int64_t(z->x[i]) - r->x[r0], int64_t(z->y[i]) - r->y[r0]);
        int32_t d = 0;
        if (indirect) {
            if (gs.autoFlip && (orgDistance ^ cvtDistance) < 0)
                cvtDistance = -cvtDistance;
            if (roundIt) {
                if (std::abs(cvtDistance - orgDistance) > gs.cvtCutIn)
                    cvtDistance = orgDistance;
                d = round(cvtDistance);
            } else {
                d = cvtDistance;
            }
        } else {
            d = orgDistance;
            if (std::abs(std::abs(d) - gs.singleWidth) < gs.singleWidthCutIn)
                d = d >= 0 ? gs.singleWidth : -gs.singleWidth;
            if (roundIt)
                d = round(d);
        }
        if (keepMin) {
            if (orgDistance >= 0)
                d = std::max(d, gs.minDistance);
            else
                d = std::min(d, -gs.minDistance);
        }
        move(*z, p, d - curDistance);
        gs.rp1 = gs.rp0;
        gs.rp2 = p;
        if (setRp0)
            gs.rp0 = p;
        return true;
    }
};

// A glyph's points read from 'glyf', scaled, with its instructions;
// composites put together from their parts, each hinted on its own.
struct Outline {
    std::vector<int32_t> x; // 26.6
    std::vector<int32_t> y;
    std::vector<int32_t> ox;
    std::vector<int32_t> oy;
    std::vector<bool> onCurve;
    std::vector<int> contourEnds;
    std::vector<uint8_t> instructions;
    int32_t pp1 = 0; // the phantom points' x, scaled (rounded to the grid)
    int32_t pp2 = 0;
    int32_t pp1Original = 0; // and before rounding
    int32_t pp2Original = 0;
};

} // namespace

Hinter::Hinter() = default;
Hinter::~Hinter() = default;

bool Hinter::load(const std::vector<uint8_t> &font) {
    _tables.reset();
    _sizes.clear();
    if (font.size() < 12)
        return false;
    auto t = std::make_unique<Tables>();
    t->data = font.data();
    t->size = font.size();
    const size_t numTables = u16(&font[4]);
    const uint8_t *head = nullptr;
    const uint8_t *maxp = nullptr;
    const uint8_t *hhea = nullptr;
    size_t maxpSize = 0;
    for (size_t k = 0; k < numTables; k++) {
        const size_t rec = 12 + 16 * k;
        if (rec + 16 > font.size())
            return false;
        const std::string tag(reinterpret_cast<const char *>(&font[rec]), 4);
        const size_t off = u32(&font[rec + 8]);
        const size_t len = u32(&font[rec + 12]);
        if (off > font.size() || len > font.size() - off)
            continue;
        const uint8_t *p = &font[off];
        if (tag == "head" && len >= 54)
            head = p;
        else if (tag == "maxp" && len >= 6)
            maxp = p, maxpSize = len;
        else if (tag == "hhea" && len >= 36)
            hhea = p;
        else if (tag == "glyf")
            t->glyf = p, t->glyfSize = len;
        else if (tag == "loca")
            t->loca = p, t->locaSize = len;
        else if (tag == "hmtx")
            t->hmtx = p, t->hmtxSize = len;
        else if (tag == "cvt ")
            for (size_t i = 0; i + 1 < len; i += 2)
                t->cvt.push_back(s16(p + i));
        else if (tag == "fpgm")
            t->fpgm.assign(p, p + len);
        else if (tag == "prep")
            t->prep.assign(p, p + len);
    }
    if (!head || !maxp || !hhea || !t->glyf || !t->loca || !t->hmtx)
        return false;
    _unitsPerEm = u16(head + 18);
    t->longLoca = s16(head + 50) != 0;
    t->numGlyphs = u16(maxp + 4);
    if (maxpSize >= 32) {
        t->maxTwilightPoints = u16(maxp + 16);
        t->maxStorage = u16(maxp + 18);
        t->maxFunctionDefs = u16(maxp + 20);
    }
    t->numberOfHMetrics = u16(hhea + 34);
    if (_unitsPerEm <= 0)
        return false;
    _tables = std::move(t);
    return true;
}

bool Hinter::hasPrograms() const {
    return _tables && (!_tables->fpgm.empty() || !_tables->prep.empty());
}

Hinter::Size &Hinter::size(int ppem) {
    auto &slot = _sizes[ppem];
    if (slot)
        return *slot;
    slot = std::make_unique<Size>();
    Size &s = *slot;
    const Tables &t = *_tables;
    s.ppem = ppem;
    s.unitsPerEm = _unitsPerEm;
    s.cvt.resize(t.cvt.size());
    for (size_t i = 0; i < t.cvt.size(); i++)
        s.cvt[i] = s.scaled(t.cvt[i]);
    s.storage.assign(size_t(std::max(t.maxStorage, 64)), 0);
    s.functions.resize(size_t(std::max(t.maxFunctionDefs, 0)));
    Zone twilight;
    twilight.resize(size_t(t.maxTwilightPoints));
    Zone none;
    // The font program defines the functions; the control value program
    // sets this size's control values and the glyphs' graphics state.
    {
        Exec e(s, twilight, none, s.cvt, s.storage, true);
        s.ok = t.fpgm.empty() || e.run(t.fpgm);
    }
    if (s.ok) {
        Exec e(s, twilight, none, s.cvt, s.storage, true);
        s.ok = t.prep.empty() || e.run(t.prep);
        s.gs = e.gs;
        s.gs.rp0 = s.gs.rp1 = s.gs.rp2 = 0;
        s.gs.zp0 = s.gs.zp1 = s.gs.zp2 = 1;
        s.gs.loop = 1;
        s.gs.pv = s.gs.fv = s.gs.dv = Vec{};
    }
    return s;
}

namespace {

bool glyphBytes(const Hinter::Tables &t, int index, const uint8_t *&data, size_t &len) {
    if (index < 0 || index >= t.numGlyphs)
        return false;
    const size_t i = size_t(index);
    size_t a = 0;
    size_t b = 0;
    if (t.longLoca) {
        if ((i + 2) * 4 > t.locaSize)
            return false;
        a = u32(t.loca + 4 * i);
        b = u32(t.loca + 4 * (i + 1));
    } else {
        if ((i + 2) * 2 > t.locaSize)
            return false;
        a = size_t(u16(t.loca + 2 * i)) * 2;
        b = size_t(u16(t.loca + 2 * (i + 1))) * 2;
    }
    if (b < a || b > t.glyfSize)
        return false;
    data = t.glyf + a;
    len = b - a;
    return true;
}

void metrics(const Hinter::Tables &t, int index, int &advance, int &lsb) {
    const int n = std::max(1, t.numberOfHMetrics);
    const size_t i = size_t(std::min(index, n - 1));
    advance = 4 * i + 2 <= t.hmtxSize ? u16(t.hmtx + 4 * i) : 0;
    if (index < n)
        lsb = 4 * size_t(index) + 4 <= t.hmtxSize ? s16(t.hmtx + 4 * size_t(index) + 2) : 0;
    else {
        const size_t at = 4 * size_t(n) + 2 * size_t(index - n);
        lsb = at + 2 <= t.hmtxSize ? s16(t.hmtx + at) : 0;
    }
}

} // namespace

bool Hinter::glyph(int index, int ppem, HintedGlyph &out) {
    out = {};
    if (!_tables || ppem <= 0)
        return false;
    Size &s = size(ppem);
    const Tables &t = *_tables;
    const bool hintGlyphs = s.ok && !(s.gs.instructControl & 1);

    // Runs the glyph's instructions over its points and the phantom
    // points (the origin, and the advance's end, rounded to the grid).
    auto hint = [&](Outline &o) -> bool {
        if (!hintGlyphs || o.instructions.empty())
            return true;
        Zone twilight;
        twilight.resize(size_t(t.maxTwilightPoints));
        Zone z;
        const size_t n = o.x.size();
        z.resize(n + 4);
        for (size_t k = 0; k < n; k++) {
            z.x[k] = o.x[k];
            z.y[k] = o.y[k];
            z.ox[k] = o.ox[k];
            z.oy[k] = o.oy[k];
            z.onCurve[k] = o.onCurve[k];
        }
        z.contourEnds = o.contourEnds;
        // The phantom points: rounded where they are, as scaled where
        // they were (what IP measures by).
        z.x[n] = o.pp1;
        z.x[n + 1] = o.pp2;
        z.ox[n] = o.pp1Original;
        z.ox[n + 1] = o.pp2Original;
        std::vector<int32_t> cvt = s.cvt;
        std::vector<int32_t> storage = s.storage;
        Exec e(s, twilight, z, cvt, storage, false);
        e.gs = s.gs;
        if (!e.run(o.instructions))
            return true; // as scaled, as the rasterizer does with a program that fails
        for (size_t k = 0; k < n; k++) {
            o.x[k] = z.x[k];
            o.y[k] = z.y[k];
            o.onCurve[k] = z.onCurve[k];
        }
        o.pp1 = z.x[n];
        o.pp2 = z.x[n + 1];
        out.hinted = true;
        return true;
    };

    // Reads and hints a glyph (recursively for a composite's parts).
    std::function<bool(int, int, Outline &)> load = [&](int glyphIndex, int depth, Outline &o) -> bool {
        if (depth > 8)
            return false;
        const uint8_t *g = nullptr;
        size_t len = 0;
        if (!glyphBytes(t, glyphIndex, g, len))
            return false;
        int advance = 0;
        int lsb = 0;
        metrics(t, glyphIndex, advance, lsb);
        int xMin = 0;
        int contours = 0;
        if (len >= 10) {
            contours = s16(g);
            xMin = s16(g + 2);
        }
        auto scaled = [&](int v) { return s.scaled(v); };
        o.pp1Original = scaled(xMin - lsb);
        o.pp2Original = o.pp1Original + scaled(advance);
        o.pp1 = (o.pp1Original + 32) & ~63;
        o.pp2 = (o.pp2Original + 32) & ~63;
        if (len < 10)
            return true; // no outline: a space
        if (contours >= 0) {
            const uint8_t *p = g + 10;
            const uint8_t *end = g + len;
            if (p + 2 * size_t(contours) + 2 > end)
                return false;
            for (int k = 0; k < contours; k++)
                o.contourEnds.push_back(u16(p + 2 * k));
            p += 2 * contours;
            const size_t n = contours ? size_t(o.contourEnds.back()) + 1 : 0;
            const size_t ins = u16(p);
            p += 2;
            if (p + ins > end)
                return false;
            o.instructions.assign(p, p + ins);
            p += ins;
            std::vector<uint8_t> flags;
            while (flags.size() < n) {
                if (p >= end)
                    return false;
                const uint8_t f = *p++;
                flags.push_back(f);
                if (f & 8) {
                    if (p >= end)
                        return false;
                    for (int r = *p++; r > 0 && flags.size() < n; r--)
                        flags.push_back(f);
                }
            }
            auto coords = [&](uint8_t shortBit, uint8_t sameBit, std::vector<int32_t> &v) {
                int value = 0;
                for (size_t k = 0; k < n; k++) {
                    const uint8_t f = flags[k];
                    if (f & shortBit) {
                        if (p >= end)
                            return false;
                        value += (f & sameBit) ? *p : -*p;
                        p++;
                    } else if (!(f & sameBit)) {
                        if (p + 2 > end)
                            return false;
                        value += s16(p);
                        p += 2;
                    }
                    v.push_back(value);
                }
                return true;
            };
            std::vector<int32_t> fx;
            std::vector<int32_t> fy;
            if (!coords(2, 16, fx) || !coords(4, 32, fy))
                return false;
            for (size_t k = 0; k < n; k++) {
                o.ox.push_back(scaled(fx[k]));
                o.oy.push_back(scaled(fy[k]));
                o.onCurve.push_back((flags[k] & 1) != 0);
            }
            o.x = o.ox;
            o.y = o.oy;
            return hint(o);
        }
        // A composite: its parts, each placed by its offset (rounded to
        // the grid if it says so) or by matching points.
        const uint8_t *p = g + 10;
        const uint8_t *end = g + len;
        bool more = true;
        bool hasInstructions = false;
        while (more) {
            if (p + 4 > end)
                return false;
            const uint16_t flags = u16(p);
            const int part = u16(p + 2);
            p += 4;
            int32_t arg1 = 0;
            int32_t arg2 = 0;
            if (flags & 1) {
                if (p + 4 > end)
                    return false;
                arg1 = (flags & 2) ? s16(p) : u16(p);
                arg2 = (flags & 2) ? s16(p + 2) : u16(p + 2);
                p += 4;
            } else {
                if (p + 2 > end)
                    return false;
                arg1 = (flags & 2) ? int8_t(p[0]) : p[0];
                arg2 = (flags & 2) ? int8_t(p[1]) : p[1];
                p += 2;
            }
            int32_t m[4] = {kOne14, 0, 0, kOne14}; // 2.14
            if (flags & 8) {
                if (p + 2 > end)
                    return false;
                m[0] = m[3] = s16(p);
                p += 2;
            } else if (flags & 0x40) {
                if (p + 4 > end)
                    return false;
                m[0] = s16(p);
                m[3] = s16(p + 2);
                p += 4;
            } else if (flags & 0x80) {
                if (p + 8 > end)
                    return false;
                for (int k = 0; k < 4; k++)
                    m[k] = s16(p + 2 * k);
                p += 8;
            }
            Outline c;
            if (!load(part, depth + 1, c))
                return false;
            const bool transformed = m[0] != kOne14 || m[1] != 0 || m[2] != 0 || m[3] != kOne14;
            if (transformed)
                for (size_t k = 0; k < c.x.size(); k++) {
                    const int64_t x = c.x[k];
                    const int64_t y = c.y[k];
                    c.x[k] = int32_t(divRound(x * m[0] + y * m[2], kOne14));
                    c.y[k] = int32_t(divRound(x * m[1] + y * m[3], kOne14));
                    const int64_t ox = c.ox[k];
                    const int64_t oy = c.oy[k];
                    c.ox[k] = int32_t(divRound(ox * m[0] + oy * m[2], kOne14));
                    c.oy[k] = int32_t(divRound(ox * m[1] + oy * m[3], kOne14));
                }
            int32_t dx = 0;
            int32_t dy = 0;
            if (flags & 2) {
                dx = scaled(arg1);
                dy = scaled(arg2);
                if (flags & 4)
                    dx = (dx + 32) & ~63, dy = (dy + 32) & ~63;
            } else {
                // Point arg1 of what's there so far on point arg2 of this part.
                if (arg1 < 0 || size_t(arg1) >= o.x.size() || arg2 < 0 || size_t(arg2) >= c.x.size())
                    return false;
                dx = o.x[size_t(arg1)] - c.x[size_t(arg2)];
                dy = o.y[size_t(arg1)] - c.y[size_t(arg2)];
            }
            const int base = int(o.x.size());
            for (size_t k = 0; k < c.x.size(); k++) {
                o.x.push_back(c.x[k] + dx);
                o.y.push_back(c.y[k] + dy);
                o.ox.push_back(c.ox[k] + dx);
                o.oy.push_back(c.oy[k] + dy);
                o.onCurve.push_back(c.onCurve[k]);
            }
            for (int e : c.contourEnds)
                o.contourEnds.push_back(e + base);
            if (flags & 0x200) { // USE_MY_METRICS
                o.pp1 = c.pp1 + dx;
                o.pp2 = c.pp2 + dx;
                o.pp1Original = c.pp1Original + dx;
                o.pp2Original = c.pp2Original + dx;
            }
            hasInstructions = hasInstructions || (flags & 0x100);
            more = (flags & 0x20) != 0;
        }
        if (hasInstructions && p + 2 <= end) {
            const size_t ins = u16(p);
            if (p + 2 + ins <= end)
                o.instructions.assign(p + 2, p + 2 + ins);
        }
        // The parts are hinted already: the composite's own program runs
        // on them where they are now.
        o.ox = o.x;
        o.oy = o.y;
        return hint(o);
    };

    Outline o;
    if (!load(index, 0, o))
        return false;
    const int32_t origin = o.pp1;
    out.x.resize(o.x.size());
    out.y = o.y;
    for (size_t k = 0; k < o.x.size(); k++)
        out.x[k] = o.x[k] - origin;
    out.onCurve = o.onCurve;
    out.contourEnds = o.contourEnds;
    out.advance = int((o.pp2 - o.pp1 + 32) >> 6);
    return true;
}

} // namespace reluva::ttf
