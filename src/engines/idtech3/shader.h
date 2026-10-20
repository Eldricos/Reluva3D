// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reluva::idtech3 {

// One directive inside a shader or stage: its keyword (lower case) and the
// rest of its line's tokens as written.
struct Directive {
    std::string keyword;
    std::vector<std::string> args;
};

// A rendering pass ("stage") of a shader. The fields pick out what the
// renderer needs most; every directive is kept in `directives` for the
// rest. See docs/formats/idtech3/shader.md.
struct ShaderStage {
    std::vector<std::string> maps; // one image, or animMap's frames
    float animFps = 0;             // animMap's rate; 0 = not animated
    bool clamp = false;            // clampMap
    bool lightmap = false;         // map $lightmap
    bool whiteImage = false;       // map $whiteimage
    std::string blendSrc, blendDst; // GL_* names, lower case; empty = opaque
    std::vector<Directive> directives;
};

struct ShaderDef {
    std::string name;   // as written, e.g. "textures/borg/borgfloor1"
    std::string file;   // the .shader file it came from
    std::vector<std::string> surfaceParms; // lower case
    std::vector<ShaderStage> stages;
    std::vector<Directive> directives; // the shader-level ones
    bool sky = false; // has skyParms

    bool hasSurfaceParm(std::string_view parm) const;
};

// Parse every shader in one .shader file's text, appending to `out`.
// `file` is recorded in each definition and used in messages.
Error parseShaderFile(std::string_view text, const std::string& file, std::vector<ShaderDef>& out);

// Every shader of a game: all scripts/*.shader files in a search set. When
// two files define a name, the first parsed wins; files are parsed in name
// order. Problems with single files are collected, not fatal.
class ShaderLibrary {
public:
    void load(const SearchSet& files, std::vector<std::string>* warnings = nullptr);
    void add(ShaderDef def);

    // By name, ignoring case and slash direction; nullptr if undefined
    // (then the engine uses the image of the same name, see shader.md).
    const ShaderDef* find(std::string_view name) const;
    size_t size() const { return shaders_.size(); }
    const std::vector<ShaderDef>& all() const { return shaders_; }

private:
    std::vector<ShaderDef> shaders_;
    std::unordered_map<std::string, size_t> byName_;
};

} // namespace reluva::idtech3
