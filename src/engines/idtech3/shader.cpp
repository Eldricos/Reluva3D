// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/shader.h"
#include "engines/idtech3/lexer.h"

#include "common/fs.h"

#include <algorithm>
#include <cstdlib>

namespace reluva::idtech3 {

namespace {

// The keyword and the rest of its line.
Directive readDirective(Lexer& lex, std::string_view keyword) {
    Directive d{fs::toLower(keyword), {}};
    while (auto arg = lex.next(false))
        d.args.emplace_back(*arg);
    return d;
}

std::string lower(const std::string& s) { return fs::toLower(s); }

void applyStageDirective(ShaderStage& stage, const Directive& d) {
    if ((d.keyword == "map" || d.keyword == "clampmap") && !d.args.empty()) {
        const std::string arg = lower(d.args[0]);
        stage.clamp = d.keyword == "clampmap";
        if (arg == "$lightmap")
            stage.lightmap = true;
        else if (arg == "$whiteimage")
            stage.whiteImage = true;
        else
            stage.maps = {d.args[0]};
    } else if (d.keyword == "animmap" && d.args.size() >= 2) {
        stage.animFps = std::strtof(d.args[0].c_str(), nullptr);
        stage.maps.assign(d.args.begin() + 1, d.args.end());
    } else if (d.keyword == "blendfunc" && !d.args.empty()) {
        // Shorthands, and the explicit two-factor form.
        const std::string a = lower(d.args[0]);
        if (a == "add") {
            stage.blendSrc = "gl_one";
            stage.blendDst = "gl_one";
        } else if (a == "filter") {
            stage.blendSrc = "gl_dst_color";
            stage.blendDst = "gl_zero";
        } else if (a == "blend") {
            stage.blendSrc = "gl_src_alpha";
            stage.blendDst = "gl_one_minus_src_alpha";
        } else if (d.args.size() >= 2) {
            stage.blendSrc = a;
            stage.blendDst = lower(d.args[1]);
        }
    }
}

} // namespace

bool ShaderDef::hasSurfaceParm(std::string_view parm) const {
    const std::string p = fs::toLower(parm);
    return std::find(surfaceParms.begin(), surfaceParms.end(), p) != surfaceParms.end();
}

Error parseShaderFile(std::string_view text, const std::string& file, std::vector<ShaderDef>& out) {
    Lexer lex(text);
    auto where = [&] { return file + " line " + std::to_string(lex.line()); };
    while (auto name = lex.next()) {
        ShaderDef def;
        def.name = std::string(*name);
        def.file = file;
        auto open = lex.next();
        if (!open || *open != "{")
            return Error::fail(where() + ": expected '{' after shader " + def.name);
        bool closed = false;
        while (auto tok = lex.next()) {
            if (*tok == "}") {
                closed = true;
                break;
            }
            if (*tok == "{") {
                ShaderStage stage;
                bool stageClosed = false;
                while (auto st = lex.next()) {
                    if (*st == "}") {
                        stageClosed = true;
                        break;
                    }
                    if (*st == "{") { // no nesting inside stages; skip it whole
                        lex.skipBlock();
                        continue;
                    }
                    stage.directives.push_back(readDirective(lex, *st));
                    applyStageDirective(stage, stage.directives.back());
                }
                if (!stageClosed)
                    return Error::fail(where() + ": shader " + def.name + " ends inside a stage");
                def.stages.push_back(std::move(stage));
                continue;
            }
            Directive d = readDirective(lex, *tok);
            if (d.keyword == "surfaceparm" && !d.args.empty())
                def.surfaceParms.push_back(lower(d.args[0]));
            else if (d.keyword == "skyparms")
                def.sky = true;
            def.directives.push_back(std::move(d));
        }
        if (!closed)
            return Error::fail(where() + ": shader " + def.name + " is not closed");
        out.push_back(std::move(def));
    }
    return Error::success();
}

void ShaderLibrary::add(ShaderDef def) {
    if (byName_.try_emplace(normalizeArchiveName(def.name), shaders_.size()).second)
        shaders_.push_back(std::move(def));
}

const ShaderDef* ShaderLibrary::find(std::string_view name) const {
    auto it = byName_.find(normalizeArchiveName(name));
    return it == byName_.end() ? nullptr : &shaders_[it->second];
}

void ShaderLibrary::load(const SearchSet& files, std::vector<std::string>* warnings) {
    shaders_.clear();
    byName_.clear();
    std::vector<uint8_t> data;
    for (const std::string& name : files.list()) {
        const std::string key = normalizeArchiveName(name);
        if (key.rfind("scripts/", 0) != 0 || key.size() < 7 || key.substr(key.size() - 7) != ".shader")
            continue;
        if (Error err = files.read(name, data)) {
            if (warnings)
                warnings->push_back(err.message);
            continue;
        }
        std::vector<ShaderDef> defs;
        Error err = parseShaderFile(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()), name,
                                    defs);
        if (err && warnings)
            warnings->push_back(err.message);
        // Keep what parsed before a problem.
        for (ShaderDef& d : defs)
            add(std::move(d));
    }
}

} // namespace reluva::idtech3
