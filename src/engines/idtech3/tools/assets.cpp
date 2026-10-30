// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's id Tech 3 commands for shaders, images and models.

#include "tools/tool.h"
#include "engines/idtech3/tools/game_folder.h"

#include "common/fs.h"
#include "engines/idtech3/bsp.h"
#include "engines/idtech3/entities.h"
#include "engines/idtech3/md3.h"
#include "engines/idtech3/shader.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace reluva::tool;
using namespace reluva::idtech3::tool;

int cmdImageCheck(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        std::fprintf(stderr, "usage: reluva-tool image-check <game folder> [map name filter]\n");
        return 2;
    }
    namespace q3 = reluva::idtech3;
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set))
        return 1;
    std::vector<std::string> warnings;
    q3::ShaderLibrary shaders;
    shaders.load(set, &warnings);
    for (const std::string& w : warnings)
        std::fprintf(stderr, "warning: %s\n", w.c_str());
    const std::string filter = argc == 4 ? reluva::normalizeArchiveName(argv[3]) : std::string();

    std::map<std::string, int> missingEverywhere; // image -> maps missing it
    int maps = 0;
    Bytes data;
    for (const std::string& name : set.list()) {
        const std::string key = reluva::normalizeArchiveName(name);
        if (key.rfind("maps/", 0) != 0 || key.size() < 4 || key.compare(key.size() - 4, 4, ".bsp") != 0 ||
            key.find(filter) == std::string::npos)
            continue;
        q3::Bsp bsp;
        if (set.read(name, data) || q3::parseBsp(data, bsp))
            continue;
        ++maps;
        // Every image each shader names: its stages' maps, or for a shader
        // without a script, the image of its own name.
        std::map<std::string, std::string> missing; // image -> a shader wanting it
        for (const q3::BspShader& s : bsp.shaders) {
            std::vector<std::string> images;
            const q3::ShaderDef* def = shaders.find(s.name);
            if (!def)
                images.push_back(s.name);
            else
                for (const q3::ShaderStage& st : def->stages)
                    for (const std::string& m : st.maps)
                        if (!m.empty() && m[0] != '$')
                            images.push_back(m);
            for (const std::string& img : images)
                if (q3::findImage(set, img).empty())
                    missing.emplace(reluva::normalizeArchiveName(img), std::string(def ? "script " : "") + s.name);
        }
        std::printf("%s: %zu shaders, %zu missing images\n", name.c_str(), bsp.shaders.size(), missing.size());
        for (const auto& [img, shader] : missing) {
            std::printf("    %-56s (shader %s)\n", img.c_str(), shader.c_str());
            ++missingEverywhere[img];
        }
    }
    std::printf("%d maps, %zu distinct missing images\n", maps, missingEverywhere.size());
    return 0;
}

int cmdModelCheck(int argc, char** argv) {
    if (argc < 3 || argc > 5) {
        std::fprintf(stderr, "usage: reluva-tool model-check <game folder> [name filter] [-v]\n");
        return 2;
    }
    namespace q3 = reluva::idtech3;
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set))
        return 1;
    std::string filter;
    bool verbose = false;
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "-v") == 0)
            verbose = true;
        else
            filter = reluva::normalizeArchiveName(argv[i]);
    }
    std::vector<std::string> warnings;
    q3::ShaderLibrary shaders;
    shaders.load(set, &warnings);

    // Every image by its file name without folder or extension, to say
    // where a missing one might be.
    auto stem = [](const std::string& path) {
        std::string s = reluva::normalizeArchiveName(path);
        if (const size_t slash = s.find_last_of('/'); slash != std::string::npos)
            s = s.substr(slash + 1);
        if (const size_t dot = s.find_last_of('.'); dot != std::string::npos)
            s.resize(dot);
        return s;
    };
    auto folder = [](const std::string& path) {
        const std::string s = reluva::normalizeArchiveName(path);
        const size_t slash = s.find_last_of('/');
        return slash == std::string::npos ? std::string() : s.substr(0, slash);
    };
    Bytes data;
    std::multimap<std::string, std::string> imagesByStem;
    std::vector<std::string> models;
    std::set<std::string> mapShadersBare, mapModels; // what the maps use: shader names without extension, models
    std::vector<std::string> maps;
    for (const std::string& n : set.list()) {
        const std::string key = reluva::normalizeArchiveName(n);
        if (key.size() > 4) {
            const std::string ext = key.substr(key.size() - 4);
            if (ext == ".tga" || ext == ".jpg" || ext == ".png")
                imagesByStem.emplace(stem(key), key);
            else if ((ext == ".md3" || ext == ".mdr") && key.find(filter) != std::string::npos)
                models.push_back(n);
            else if (ext == ".bsp" && key.rfind("maps/", 0) == 0)
                maps.push_back(n);
        }
    }
    // A shader name as maps store it: without an extension.
    auto bare = [](const std::string& name) {
        std::string s = reluva::normalizeArchiveName(name);
        const size_t dot = s.find_last_of('.'), slash = s.find_last_of('/');
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
            s.resize(dot);
        return s;
    };
    for (const std::string& n : maps) {
        q3::Bsp bsp;
        if (set.read(n, data) || q3::parseBsp(data, bsp))
            continue;
        for (const q3::BspShader& s : bsp.shaders)
            mapShadersBare.insert(bare(s.name));
        std::vector<q3::Entity> entities;
        if (!q3::parseEntities(bsp.entities, entities))
            for (const q3::Entity& e : entities)
                for (const char* k : {"model", "model2"})
                    if (const std::string m = e.get(k); !m.empty() && m[0] != '*')
                        mapModels.insert(reluva::normalizeArchiveName(m));
    }


    // Kinds of result, with counts and a few examples.
    std::map<std::string, std::pair<int, std::vector<std::string>>> kinds;
    auto count = [&](const std::string& kind, const std::string& example) {
        auto& [n, ex] = kinds[kind];
        ++n;
        if (ex.size() < 3)
            ex.push_back(example);
    };
    int surfaces = 0, unreadable = 0;
    for (const std::string& path : models) {
        q3::Md3Model m;
        if (set.read(path, data) || q3::parseModel(data, m)) {
            ++unreadable;
            continue;
        }
        const std::string base = reluva::normalizeArchiveName(path).substr(0, path.size() - 4);
        // A skin next to the model, as players have, picks shaders first.
        std::string skins;
        std::map<std::string, std::string> skin;
        // Lower-detail copies (head_1.md3) use the main model's skins.
        std::string main = base;
        if (main.size() > 2 && main[main.size() - 2] == '_' && (main.back() == '1' || main.back() == '2'))
            main.resize(main.size() - 2);
        for (const std::string& n : {base + ".skin", base + "_default.skin", main + ".skin", main + "_default.skin"})
            if (set.has(n)) {
                skins += " " + n;
                if (skin.empty() && !set.read(n, data))
                    skin = q3::parseSkin(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
            }
        for (const q3::Md3Surface& s : m.surfaces) {
            ++surfaces;
            std::string name = s.shaders.empty() ? std::string() : s.shaders.front();
            std::string via;
            if (auto it = skin.find(reluva::fs::toLower(s.name)); it != skin.end()) {
                name = it->second;
                via = "skin: ";
            }
            const std::string where = path + " surface " + s.name + ": \"" + name + "\"";
            if (name.empty()) {
                count("no shader name" + std::string(skins.empty() ? "" : " (model has skins)"), where + skins);
                continue;
            }
            std::vector<std::string> wanted; // images it needs
            if (const q3::ShaderDef* def = shaders.find(name)) {
                for (const q3::ShaderStage& st : def->stages)
                    for (const std::string& img : st.maps)
                        if (!img.empty() && img[0] != '$')
                            wanted.push_back(img);
                if (wanted.empty()) {
                    count(via + "found: script without images", where);
                    continue;
                }
            } else {
                wanted.push_back(name);
            }
            bool allFound = true;
            for (const std::string& img : wanted) {
                if (!q3::findImage(set, img).empty())
                    continue;
                allFound = false;
                // Where files of that name are, if anywhere.
                std::string kind = via + (shaders.find(name) ? "missing image of a script" : "missing image");
                if (img.find('/') == std::string::npos && img.find('\\') == std::string::npos)
                    kind += ", name without a folder";
                const auto range = imagesByStem.equal_range(stem(img));
                std::string found;
                bool sameFolder = false;
                for (auto it = range.first; it != range.second; ++it) {
                    found += " " + it->second;
                    if (folder(it->second) == folder(path))
                        sameFolder = true;
                }
                kind += found.empty() ? ", no file of that name anywhere"
                                      : sameFolder ? ", a file of that name in the model's folder"
                                                   : ", a file of that name elsewhere";
                // Does a map show it: built into a map (whose surfaces keep
                // the model's shader names), or placed by an entity?
                const bool inMap = mapShadersBare.count(bare(name)) != 0;
                const bool placed = mapModels.count(reluva::normalizeArchiveName(path)) != 0;
                kind += inMap ? " [built into a map]" : placed ? " [placed by a map entity]" : " [not used by any map]";
                count(kind, where + (img != name ? " needs " + img : std::string()) +
                                (found.empty() ? std::string() : " ->" + found));
                if (verbose)
                    std::printf("MISSING %s%s ->%s\n", where.c_str(), img != name ? (" needs " + img).c_str() : "",
                                found.empty() ? " (none)" : found.c_str());
            }
            if (allFound)
                count(via + "found", where);
        }
    }
    std::printf("%zu models, %d surfaces, %d models unreadable\n", models.size(), surfaces, unreadable);
    for (const auto& [kind, info] : kinds) {
        std::printf("%6d  %s\n", info.first, kind.c_str());
        if (kind.find("found") == std::string::npos || kind.find("missing") != std::string::npos)
            for (const std::string& e : info.second)
                std::printf("          e.g. %s\n", e.c_str());
    }
    return 0;
}

int cmdShaderShow(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: reluva-tool shader-show <game folder> <shader | model.md3>\n");
        return 2;
    }
    namespace q3 = reluva::idtech3;
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set))
        return 1;
    std::vector<std::string> names;
    const std::string what = argv[3];
    const std::string whatExt = what.size() > 4 ? reluva::fs::toLower(what.substr(what.size() - 4)) : "";
    if (whatExt == ".md3" || whatExt == ".mdr") {
        // A model: each surface's shader.
        Bytes data;
        q3::Md3Model model;
        if (reluva::Error err = set.read(what, data); err || (err = q3::parseModel(data, model))) {
            std::fprintf(stderr, "%s: %s\n", what.c_str(), err.message.c_str());
            return 1;
        }
        for (const q3::Md3Surface& surface : model.surfaces) {
            const std::string shader = surface.shaders.empty() ? std::string() : surface.shaders.front();
            std::printf("surface %s: %d vertices, %zu triangles, shader %s\n", surface.name.c_str(),
                        surface.vertexCount, surface.indices.size() / 3, shader.empty() ? "(none)" : shader.c_str());
            if (!shader.empty() && std::find(names.begin(), names.end(), shader) == names.end())
                names.push_back(shader);
        }
        std::printf("%zu frames, %d tags each:", model.frames.size(), model.tagCount);
        for (int t = 0; t < model.tagCount; ++t)
            std::printf(" %s", model.tags[size_t(t)].name.c_str());
        std::printf("\n");
    } else {
        names.push_back(what);
    }
    // Each shader's text, as its script has it: the name, then the block
    // from its brace to the matching one.
    for (const std::string& name : names) {
        std::string base = reluva::fs::toLower(name);
        if (base.size() > 4 && (base.substr(base.size() - 4) == ".tga" || base.substr(base.size() - 4) == ".jpg"))
            base.resize(base.size() - 4);
        bool found = false;
        for (const std::string& file : set.list()) {
            const std::string key = reluva::normalizeArchiveName(file);
            if (key.rfind("scripts/", 0) != 0 || key.size() < 7 || key.compare(key.size() - 7, 7, ".shader") != 0)
                continue;
            Bytes data;
            if (set.read(file, data))
                continue;
            const std::string text(data.begin(), data.end());
            const std::string lower = reluva::fs::toLower(text);
            for (size_t at = lower.find(base); at != std::string::npos && !found; at = lower.find(base, at + 1)) {
                // A whole token at the start of a line, followed by a brace.
                const bool startsToken = at == 0 || static_cast<unsigned char>(lower[at - 1]) <= ' ';
                const size_t after = at + base.size();
                if (!startsToken || (after < lower.size() && static_cast<unsigned char>(lower[after]) > ' '))
                    continue;
                const size_t open = lower.find_first_not_of(" \t\r\n", after);
                if (open == std::string::npos || lower[open] != '{')
                    continue;
                int depth = 0;
                size_t end = open;
                for (; end < text.size(); ++end) {
                    if (text[end] == '{')
                        ++depth;
                    else if (text[end] == '}' && --depth == 0)
                        break;
                }
                std::printf("--- %s, in %s:\n%s\n", name.c_str(), file.c_str(),
                            text.substr(at, std::min(end + 1, text.size()) - at).c_str());
                found = true;
            }
            if (found)
                break;
        }
        if (!found)
            std::printf("--- %s: no script defines it (the image itself, drawn the default way)\n", name.c_str());
    }
    return 0;
}

int cmdShaderCheck(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: reluva-tool shader-check <game folder>\n");
        return 2;
    }
    namespace q3 = reluva::idtech3;
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set))
        return 1;
    std::vector<std::string> warnings;
    q3::ShaderLibrary shaders;
    shaders.load(set, &warnings);
    for (const std::string& w : warnings)
        std::printf("problem: %s\n", w.c_str());

    // Feature -> (count, a few shaders using it).
    std::map<std::string, std::pair<int, std::vector<std::string>>> found;
    auto add = [&](const std::string& what, const q3::ShaderDef& d) {
        auto& [n, examples] = found[what];
        ++n;
        if (examples.size() < 3 && std::find(examples.begin(), examples.end(), d.name) == examples.end())
            examples.push_back(d.name);
    };
    static const char* const deformNames[] = {"wave", "normal", "bulge", "move", "autosprite", "autosprite2",
                                              "text", "projectionShadow", "other"};
    for (const q3::ShaderDef& d : shaders.all()) {
        for (const std::string& u : d.unknown)
            add("unknown directive: " + u, d);
        for (const q3::ShaderDeform& df : d.deforms) {
            using T = q3::ShaderDeform::Type;
            if (df.type == T::Autosprite || df.type == T::Autosprite2)
                add(std::string("drawn on the level only, not on models: deformVertexes ") +
                        deformNames[size_t(df.type)],
                    d);
            else if (df.type != T::Wave && df.type != T::Bulge && df.type != T::Move && df.type != T::Normal)
                add(std::string("not drawn: deformVertexes ") + deformNames[size_t(df.type)], d);
        }
        if (d.deforms.size() > 2)
            add("not drawn: more than two deformVertexes", d);
        for (const q3::ShaderStage& st : d.stages)
            if (st.blendSrc == "gl_src_alpha_saturate")
                add("not drawn: blendFunc GL_SRC_ALPHA_SATURATE", d);
    }
    std::printf("%zu shaders\n", shaders.size());
    for (const auto& [what, info] : found) {
        std::printf("%5d  %s  (e.g.", info.first, what.c_str());
        for (const std::string& e : info.second)
            std::printf(" %s", e.c_str());
        std::printf(")\n");
    }
    return 0;
}

const Commands commands("idtech3", {
    {"image-check", "<game folder> [map name filter]",
     "list the images maps' shaders use that can't be found (only maps whose name contains map)",
     cmdImageCheck},
    {"model-check", "<game folder> [name filter] [-v]",
     "for every .md3 surface, the shader or image it names and whether it's found; where missing files "
     "of the same name are; -v lists every miss",
     cmdModelCheck},
    {"shader-show", "<game folder> <shader | model.md3>",
     "a shader's text as its script has it; for a model, its surfaces' shaders and theirs",
     cmdShaderShow},
    {"shader-check", "<game folder>",
     "count the shader directives the engine doesn't know",
     cmdShaderCheck},
});

} // namespace
