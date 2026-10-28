// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/materials.h"

#include "common/fs.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace reluva::age {

namespace {

std::string lower(std::string s) { return fs::toLower(s); }

// "s_thames-0009" -> "s_thames".
std::string withoutFrame(std::string s) {
    if (s.size() > 5 && s[s.size() - 5] == '-' && s.find_first_not_of("0123456789", s.size() - 4) == std::string::npos)
        s.resize(s.size() - 5);
    return s;
}

} // namespace

Error PhysicsMaterials::load(const SearchSet& files) {
    std::vector<uint8_t> mtl, csv;
    if (Error err = files.read("city/materials.mtl", mtl))
        return err;
    files.read("city/materials.csv", csv);
    return parse(std::string_view(reinterpret_cast<const char*>(mtl.data()), mtl.size()),
                 std::string_view(reinterpret_cast<const char*>(csv.data()), csv.size()));
}

Error PhysicsMaterials::parse(std::string_view mtl, std::string_view csv) {
    materials_.clear();
    byName_.clear();
    textures_.clear();
    std::istringstream in{std::string(mtl)};
    std::string line;
    PhysicsMaterial* m = nullptr;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        std::string word;
        if (!(words >> word))
            continue;
        if (word == "mtl") {
            materials_.push_back({});
            m = &materials_.back();
            words >> m->name;
        } else if (word == "}") {
            m = nullptr;
        } else if (m) {
            float v = 0;
            words >> v;
            if (word == "elasticity:")
                m->elasticity = v;
            else if (word == "friction:")
                m->friction = v;
            else if (word == "sound:")
                m->sound = int(v);
            else if (word == "drag:")
                m->drag = v;
            else if (word == "width:")
                m->width = v;
            else if (word == "height:")
                m->height = v;
            else if (word == "depth:")
                m->depth = v;
            // effect:, ptxindex:, ptxthreshold: (particles) aren't used.
        }
    }
    if (m)
        return Error::fail("materials: a material isn't closed");
    for (size_t i = 0; i < materials_.size(); ++i)
        byName_[lower(materials_[i].name)] = i;
    // "_default" is what textures not listed (or listed "none") are.
    if (auto it = byName_.find("_default"); it != byName_.end())
        default_ = materials_[it->second];
    std::istringstream rows{std::string(csv)};
    bool header = true;
    while (std::getline(rows, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const size_t comma = line.find(',');
        if (header || comma == std::string::npos) {
            header = false;
            continue;
        }
        const std::string physics = lower(line.substr(comma + 1));
        if (physics != "none" && byName_.count(physics))
            textures_[lower(line.substr(0, comma))] = physics;
    }
    return Error::success();
}

const PhysicsMaterial& PhysicsMaterials::byName(const std::string& name) const {
    const auto it = byName_.find(lower(name));
    return it == byName_.end() ? default_ : materials_[it->second];
}

const PhysicsMaterial& PhysicsMaterials::forTexture(const std::string& texture) const {
    const std::string key = lower(texture);
    auto it = textures_.find(key);
    if (it == textures_.end())
        it = textures_.find(withoutFrame(key));
    return it == textures_.end() ? default_ : byName(it->second);
}

CollisionSurface PhysicsMaterials::surface(const PhysicsMaterial& m) const {
    // Relative to the default material (0.9, the roads'), so roads stay
    // what Reluva's driving was tuned on.
    CollisionSurface s;
    s.friction *= m.friction / std::max(0.01f, default_.friction);
    s.sound = uint8_t(std::max(0, m.sound));
    s.drag = m.drag;
    s.depth = m.depth;
    return s;
}

} // namespace reluva::age
