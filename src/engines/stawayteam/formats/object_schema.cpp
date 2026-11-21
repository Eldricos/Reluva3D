// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/formats/object_schema.h"

#include <algorithm>
#include <cstring>

namespace reluva::stawayteam {

const ClassSchema *findClass(std::string_view name) {
    const auto classes = allClasses();
    const auto it =
        std::lower_bound(classes.begin(), classes.end(), name,
                         [](const ClassSchema &c, std::string_view n) { return std::string_view(c.name) < n; });
    return it != classes.end() && it->name == name ? &*it : nullptr;
}

std::vector<const ClassSchema *> classesOfVersion(float version) {
    std::vector<const ClassSchema *> out;
    for (const ClassSchema &c : allClasses())
        if (c.version == version)
            out.push_back(&c);
    return out;
}

std::vector<const PropertySchema *> propertiesOf(const ClassSchema &cls) {
    std::vector<const PropertySchema *> out;
    if (cls.parent >= 0)
        out = propertiesOf(allClasses()[size_t(cls.parent)]);
    const auto props = allProperties().subspan(cls.first, cls.count);
    for (const PropertySchema &p : props)
        out.push_back(&p);
    return out;
}

OwnData ownDataOf(const ClassSchema &cls) {
    const ClassSchema *c = &cls;
    while (c->own == OwnData::kNone && c->parent >= 0)
        c = &allClasses()[size_t(c->parent)];
    return c->own;
}

} // namespace reluva::stawayteam
