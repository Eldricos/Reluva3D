// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/curves.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <string>

// The arithmetic follows granny2.dll's: x87 at 53-bit precision, so in
// doubles, rounded to float where the DLL stores a float. CMakeLists.txt
// builds this file without fused multiply-adds, which would round once
// where the DLL rounds twice.

namespace reluva::awe {

namespace {

const char* const kFormatNames[] = {
    "DaKeyframes32f", "DaK32fC32f",  "DaIdentity",   "DaConstant32f", "D3Constant32f", "D4Constant32f",
    "DaK16uC16u",     "DaK8uC8u",    "D4nK16uC15u",  "D4nK8uC7u",     "D3K16uC16u",    "D3K8uC8u",
    "D9I1K16uC16u",   "D9I3K16uC16u", "D9I1K8uC8u",  "D9I3K8uC8u"};

// A member's floats: an inline array (real32[3]) or a reference to an
// array of Real32 objects.
std::vector<float> readFloats(const Gr2File& file, const Gr2Object& o, const char* name) {
    std::vector<float> out;
    const Gr2Member* m = o.member(name);
    if (!m)
        return out;
    if (m->type == Gr2Type::Real32) {
        for (uint32_t i = 0; i < std::max<uint32_t>(1, m->arrayWidth); i++)
            out.push_back(float(o.number(name, i)));
        return out;
    }
    const Gr2Object::Array a = o.array(name);
    if (a.stride == 4)
        for (uint32_t i = 0; i < a.count; i++)
            out.push_back(file.readFloat(a.address + 4 * i));
    return out;
}

// The quantised quaternions' ranges (D4n*): a scale and an offset
// for each of 16 entries, all multiples of √½ (the most a quaternion's
// three smaller components can be); entries 8-15 are 0-7 negated. These
// are the table at 0x500551d8 in granny2.dll.
struct Range {
    float scale, offset;
};
Range quaternionRange(unsigned entry) {
    const float h = 0.70710677f; // √½ as a float, 0x3f3504f3
    static const Range ranges[8] = {{2 * h, -h},     {h, -h / 2},     {h / 2, -h * 0.75f}, {h / 2, -h / 4},
                                    {h / 2, h / 4},  {h / 4, -h / 4}, {h / 4, -h / 8},     {h / 4, 0}};
    const Range r = ranges[entry & 7];
    return entry & 8 ? Range{-r.scale, -r.offset} : r;
}

// KnotsControls: n knots then the controls, in one array of 8- or 16-bit
// values (as the format's name says).
Error readKnotsControls(const Gr2File& file, const Gr2Object& data, CurveData& out) {
    const CurveFormat f = CurveFormat(out.format);
    const uint32_t size = f == CurveFormat::D4nK16uC15u || f == CurveFormat::D3K16uC16u ? 2 : 1;
    const Gr2Object::Array a = data.array("KnotsControls");
    if (a.count && a.stride != size)
        return Error::fail(std::string(curveFormatName(out.format)) + "'s KnotsControls aren't " +
                           std::to_string(8 * size) + "-bit");
    out.knotsControls.resize(a.count);
    for (uint32_t i = 0; i < a.count; i++)
        out.knotsControls[i] = size == 2 ? file.read16(a.address + 2 * i) : file.read8(a.address + i);
    return Error::success();
}

} // namespace

const char* curveFormatName(uint8_t format) {
    return format < std::size(kFormatNames) ? kFormatNames[format] : "";
}

Error readCurveData(const Gr2File& file, const Gr2Object& curve, CurveData& out) {
    out = CurveData();
    const Gr2Object data = curve.object("CurveData");
    if (!data.valid() || data.members().empty() || data.members()[0].type != Gr2Type::Inline)
        return Error::fail("no CurveData");
    // Its first member is the header every format starts with.
    const Gr2Object header = data.object(data.members()[0].name);
    out.format = uint8_t(header.number("Format"));
    out.degree = uint8_t(header.number("Degree"));
    switch (CurveFormat(out.format)) {
    case CurveFormat::DaIdentity:
        out.dimension = int(data.number("Dimension"));
        break;
    case CurveFormat::DaConstant32f:
    case CurveFormat::D3Constant32f:
        out.controls = readFloats(file, data, "Controls");
        break;
    case CurveFormat::DaK32fC32f:
        out.knots = readFloats(file, data, "Knots");
        out.controls = readFloats(file, data, "Controls");
        break;
    case CurveFormat::D4nK16uC15u:
    case CurveFormat::D4nK8uC7u:
        out.packed = uint16_t(data.number("ScaleOffsetTableEntries"));
        out.oneOverKnotScale = float(data.number("OneOverKnotScale"));
        if (Error e = readKnotsControls(file, data, out))
            return e;
        break;
    case CurveFormat::D3K16uC16u:
    case CurveFormat::D3K8uC8u:
        out.packed = uint16_t(data.number("OneOverKnotScaleTrunc"));
        for (uint32_t c = 0; c < 3; c++) {
            out.controlScales[c] = float(data.number("ControlScales", c));
            out.controlOffsets[c] = float(data.number("ControlOffsets", c));
        }
        if (Error e = readKnotsControls(file, data, out))
            return e;
        break;
    default:
        return Error::fail(std::string("curves in format ") + std::to_string(out.format) + " (" +
                           curveFormatName(out.format) + ") aren't read");
    }
    return Error::success();
}

Error expandCurve(const CurveData& data, const std::vector<float>& identity, Curve& out) {
    out = Curve();
    out.degree = data.degree;
    switch (CurveFormat(data.format)) {
    case CurveFormat::DaIdentity:
        // The DLL copies its Dimension values of the identity it's given.
        out.knots = {0};
        out.controls = identity;
        out.controls.resize(size_t(std::max(data.dimension, 0)), 0.0f);
        break;
    case CurveFormat::DaConstant32f:
    case CurveFormat::D3Constant32f:
        out.knots = {0};
        out.controls = data.controls;
        break;
    case CurveFormat::DaK32fC32f:
        // As many values a knot as there are whole sets of controls.
        out.knots = data.knots;
        if (!out.knots.empty()) {
            out.controls = data.controls;
            out.controls.resize(data.controls.size() / out.knots.size() * out.knots.size());
        }
        break;
    case CurveFormat::D4nK16uC15u:
    case CurveFormat::D4nK8uC7u: {
        // n knots, then 3 values a knot: a unit quaternion's three smaller
        // components, 15 or 7 bits each; the top bits say which component
        // was left out (and its sign), rebuilt as the square root of what
        // the others leave of 1.
        const bool wide = CurveFormat(data.format) == CurveFormat::D4nK16uC15u;
        const unsigned bits = wide ? 15 : 7, mask = (1u << bits) - 1;
        const std::vector<uint16_t>& kc = data.knotsControls;
        const size_t n = kc.size() / 4;
        const float knotScale = float(1.0 / double(data.oneOverKnotScale));
        // Each component's range: four 4-bit entries of the table, x's in
        // the low bits; the scale spread over the values' steps (1/127 or
        // 1/32767, at 0x50055270 and 0x50055268 in the DLL).
        double scale[4], offset[4];
        for (unsigned c = 0; c < 4; c++) {
            const Range r = quaternionRange((data.packed >> (4 * c)) & 15);
            scale[c] = float(double(r.scale) * double(1.0f / float(mask)));
            offset[c] = r.offset;
        }
        out.knots.resize(n);
        out.controls.resize(n * 4);
        for (size_t k = 0; k < n; k++) {
            out.knots[k] = float(double(kc[k]) * double(knotScale));
            const uint16_t* v = &kc[n + 3 * k];
            const unsigned missing = ((v[1] >> bits) << 1) | (v[2] >> bits);
            float* q = &out.controls[4 * k];
            double sum = 0;
            unsigned c = missing;
            for (int i = 0; i < 3; i++) {
                c = (c + 1) & 3;
                const double x = double(v[i] & mask) * scale[c] + offset[c];
                sum += x * x;
                q[c] = float(x);
            }
            const double w = std::sqrt(1.0 - sum);
            q[missing] = float(v[0] >> bits ? -w : w);
        }
        break;
    }
    case CurveFormat::D3K16uC16u:
    case CurveFormat::D3K8uC8u: {
        // n knots, then 3 values a knot, each scaled and offset. The
        // knots' scale is stored as the top 16 bits of a float.
        const std::vector<uint16_t>& kc = data.knotsControls;
        const size_t n = kc.size() / 4;
        const uint32_t bits = uint32_t(data.packed) << 16;
        float oneOverKnotScale;
        std::memcpy(&oneOverKnotScale, &bits, 4);
        const float knotScale = float(1.0 / double(oneOverKnotScale));
        out.knots.resize(n);
        out.controls.resize(n * 3);
        for (size_t k = 0; k < n; k++) {
            out.knots[k] = float(double(kc[k]) * double(knotScale));
            for (size_t c = 0; c < 3; c++)
                out.controls[3 * k + c] =
                    float(double(kc[n + 3 * k + c]) * double(data.controlScales[c]) + double(data.controlOffsets[c]));
        }
        break;
    }
    default:
        return Error::fail(std::string("curves in format ") + std::to_string(data.format) + " (" +
                           curveFormatName(data.format) + ") aren't read");
    }
    return Error::success();
}

} // namespace reluva::awe
