// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/awe/gr2.h"
#include "engines/awe/model.h"

#include <cstdint>
#include <vector>

// Granny's compressed curve formats: a curve's CurveData variant, whose
// header's Format byte names how its knots and controls are stored. They
// are expanded here to the plain B-spline curves Reluva evaluates, giving
// the same values as granny2.dll's GrannyCurveExtractKnotValues, bit for
// bit. See docs/formats/awe/gr2.md ("Compressed curves").

namespace reluva::awe {

// The Format byte: the index into granny2.dll's table of curve formats.
enum class CurveFormat : uint8_t {
    DaKeyframes32f = 0,
    DaK32fC32f = 1,
    DaIdentity = 2,
    DaConstant32f = 3,
    D3Constant32f = 4,
    D4Constant32f = 5,
    DaK16uC16u = 6,
    DaK8uC8u = 7,
    D4nK16uC15u = 8,
    D4nK8uC7u = 9,
    D3K16uC16u = 10,
    D3K8uC8u = 11,
    D9I1K16uC16u = 12,
    D9I3K16uC16u = 13,
    D9I1K8uC8u = 14,
    D9I3K8uC8u = 15,
};
// "D4nK8uC7u"..., or "" past the table.
const char* curveFormatName(uint8_t format);

// A compressed curve's data as the file stores it. Each format uses some
// of these members.
struct CurveData {
    uint8_t format = 0, degree = 0;
    int dimension = 0;            // DaIdentity
    // D4n*: ScaleOffsetTableEntries; D3K*: OneOverKnotScaleTrunc.
    uint16_t packed = 0;
    float oneOverKnotScale = 0;   // D4n*
    float controlScales[3] = {0, 0, 0}, controlOffsets[3] = {0, 0, 0}; // D3K*
    std::vector<float> knots;     // DaK32fC32f
    std::vector<float> controls;  // DaK32fC32f, DaConstant32f, D3Constant32f
    // D4n* and D3K*: the knots, then the controls (8- or 16-bit values).
    std::vector<uint16_t> knotsControls;
};

// Reads a curve (a Curve2 object: PositionCurve...) whose CurveData is
// one of the formats expandCurve() knows.
Error readCurveData(const Gr2File& file, const Gr2Object& curve, CurveData& out);

// Its knots and controls, as GrannyCurveExtractKnotValues gives them: an
// identity curve is `identity` at one knot (time 0), a constant one its
// value at one knot.
Error expandCurve(const CurveData& data, const std::vector<float>& identity, Curve& out);

} // namespace reluva::awe
