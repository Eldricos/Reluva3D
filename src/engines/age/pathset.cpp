// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Worked out from the 57 retail files; see docs/formats/age/pathset.md.

#include "engines/age/pathset.h"

#include <cctype>
#include <cstring>

namespace reluva::age {

namespace {

uint32_t u32(const uint8_t *p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
         uint32_t(p[3]) << 24;
}

float f32(const uint8_t *p) {
  const uint32_t v = u32(p);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}

} // namespace

Error readPathset(const std::vector<uint8_t> &data,
                  std::vector<PathsetPath> &out) {
  out.clear();
  if (data.size() < 12 || std::memcmp(data.data(), "PTH1", 4) != 0)
    return Error::fail("not a path set (no PTH1)");
  const uint32_t count = u32(data.data() + 4);
  size_t at = 12; // after the count and a word not known
  for (uint32_t i = 0; i < count; ++i) {
    if (at + 40 > data.size())
      break;
    PathsetPath p;
    const char *name = reinterpret_cast<const char *>(data.data() + at);
    p.name.assign(name, strnlen(name, 32));
    at += 32;
    const uint32_t n = u32(data.data() + at);
    p.start = u32(data.data() + at + 4);
    at += 8;
    if (n > 100000 || at + uint64_t(n) * 16 + 4 > data.size())
      break;
    for (uint32_t k = 0; k < n; ++k, at += 16) {
      const uint8_t *q = data.data() + at;
      p.ids.push_back(u32(q));
      p.points.push_back({f32(q + 4), f32(q + 8), f32(q + 12)});
    }
    p.end = u32(data.data() + at);
    at += 4;
    p.model = p.name;
    const size_t colon = p.name.find(':');
    if (colon != std::string::npos) {
      std::string prefix = p.name.substr(0, colon);
      for (char &c : prefix)
        c = char(std::tolower(static_cast<unsigned char>(c)));
      p.open = prefix == "open";
      p.inactive = prefix == "inactive";
      p.model = p.name.substr(colon + 1);
    }
    out.push_back(std::move(p));
  }
  if (out.empty() && count > 0)
    return Error::fail("no whole paths");
  return Error::success();
}

} // namespace reluva::age
