// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's id Tech 3 commands for maps (.bsp) and their entities.

#include "tools/tool.h"
#include "engines/idtech3/tools/game_folder.h"

#include "common/fs.h"
#include "engines/idtech3/bsp.h"
#include "engines/idtech3/entities.h"
#include "engines/idtech3/shader.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace reluva::tool;
using namespace reluva::idtech3::tool;

int cmdBspCheck(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: reluva-tool bsp-check <game folder>\n");
        return 2;
    }
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set))
        return 1;
    int maps = 0, failed = 0, corrected = 0;
    Bytes data;
    for (const std::string& name : set.list()) {
        const std::string key = reluva::normalizeArchiveName(name);
        if (key.rfind("maps/", 0) != 0 || key.size() < 4 || key.compare(key.size() - 4, 4, ".bsp") != 0)
            continue;
        ++maps;
        reluva::idtech3::Bsp bsp;
        reluva::Error err = set.read(name, data);
        if (!err)
            err = reluva::idtech3::parseBsp(data, bsp);
        if (err) {
            ++failed;
            std::printf("FAIL %s: %s\n", name.c_str(), err.message.c_str());
            continue;
        }
        const auto& f = bsp.fixes;
        if (f.total())
            ++corrected;
        std::printf("ok   %s: %zu surfaces, %zu lightmaps", name.c_str(), bsp.surfaces.size(), bsp.lightmapCount());
        if (f.total())
            std::printf("; corrected: %d lightmap, %d fog, %d flare/bad, %d type", f.lightmaps, f.fogs, f.flares,
                        f.types);
        std::printf("\n");
    }
    std::printf("%d maps: %d failed, %d loaded with corrections\n", maps, failed, corrected);
    return failed ? 1 : 0;
}

// A map's entities: all, or those with any key or value containing one of
// the texts given (ignoring case), each followed by the entities that
// target it (what triggers it).
int cmdBspEntities(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool bsp-entities <game folder> <map> [text]...\n");
        return 2;
    }
    namespace q3 = reluva::idtech3;
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set))
        return 1;
    std::string map = argv[3];
    if (map.size() > 4 && map.compare(map.size() - 4, 4, ".bsp") == 0)
        map.resize(map.size() - 4);
    if (map.rfind("maps/", 0) == 0)
        map = map.substr(5);
    Bytes data;
    q3::Bsp bsp;
    if (reluva::Error err = set.read("maps/" + map + ".bsp", data); err || (err = q3::parseBsp(data, bsp))) {
        std::fprintf(stderr, "maps/%s.bsp: %s\n", map.c_str(), err.message.c_str());
        return 1;
    }
    std::vector<q3::Entity> entities;
    if (reluva::Error err = q3::parseEntities(bsp.entities, entities)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    std::vector<std::string> wanted;
    for (int i = 4; i < argc; ++i)
        wanted.push_back(reluva::fs::toLower(argv[i]));
    auto print = [&](size_t n, const char* indent) {
        std::printf("%s#%zu", indent, n);
        for (const auto& [k, v] : entities[n].pairs)
            std::printf(" %s=\"%s\"", k.c_str(), v.c_str());
        std::printf("\n");
    };
    for (size_t n = 0; n < entities.size(); ++n) {
        bool match = wanted.empty();
        for (const auto& [k, v] : entities[n].pairs)
            for (const std::string& w : wanted)
                match = match || reluva::fs::toLower(k).find(w) != std::string::npos ||
                        reluva::fs::toLower(v).find(w) != std::string::npos;
        if (!match)
            continue;
        print(n, "");
        if (wanted.empty())
            continue;
        const std::string name = entities[n].get("targetname");
        if (name.empty())
            continue;
        for (size_t m = 0; m < entities.size(); ++m)
            for (const auto& [k, v] : entities[m].pairs)
                if ((k == "target" || k == "target2" || k == "target3" || k == "target4") &&
                    reluva::fs::toLower(v) == reluva::fs::toLower(name)) {
                    print(m, "    targeted by ");
                    break;
                }
    }
    return 0;
}

int cmdEntitySurvey(int argc, char** argv) {
    // Every entity class and key in every map of a game folder: how many
    // entities and maps use each, a few of the values each key takes, and
    // the spawnflags bits each class uses; written to a file as plain text.
    if (argc < 3 || argc > 4) {
        std::fprintf(stderr, "usage: reluva-tool entity-survey <game folder> [output file]\n");
        return 2;
    }
    namespace q3 = reluva::idtech3;
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set))
        return 1;
    const std::string outPath = argc == 4 ? argv[3] : "entity-survey.txt";

    struct KeyStats {
        int uses = 0;
        std::set<std::string> maps;
        std::map<std::string, int> values; // up to kMaxValues distinct, counted
        bool moreValues = false;
    };
    struct ClassStats {
        int count = 0;
        std::set<std::string> maps;
        std::map<std::string, KeyStats> keys;
        uint32_t flagBits = 0;
        std::map<long, int> flagValues;
    };
    constexpr size_t kMaxValues = 400;
    std::map<std::string, ClassStats> classes;
    std::map<std::string, std::map<std::string, int>> keyClasses; // key -> class -> uses
    std::vector<std::pair<std::string, size_t>> mapCounts;
    std::vector<std::string> failures;
    size_t total = 0;
    auto lower = [](std::string t) { return reluva::fs::toLower(t); };
    Bytes data;
    for (const std::string& name : set.list()) {
        const std::string file = reluva::normalizeArchiveName(name);
        if (file.rfind("maps/", 0) != 0 || file.size() < 4 || file.compare(file.size() - 4, 4, ".bsp") != 0)
            continue;
        const std::string map = file.substr(5, file.size() - 9);
        q3::Bsp bsp;
        std::vector<q3::Entity> entities;
        reluva::Error err = set.read(name, data);
        if (!err)
            err = q3::parseBsp(data, bsp);
        if (!err)
            err = q3::parseEntities(bsp.entities, entities);
        if (err) {
            failures.push_back(map + ": " + err.message);
            continue;
        }
        mapCounts.emplace_back(map, entities.size());
        total += entities.size();
        for (const q3::Entity& e : entities) {
            const std::string cls = e.classname().empty() ? "(no classname)" : lower(e.classname());
            ClassStats& c = classes[cls];
            ++c.count;
            c.maps.insert(map);
            for (const auto& [k, v] : e.pairs) {
                const std::string key = lower(k);
                if (key == "classname")
                    continue;
                KeyStats& ks = c.keys[key];
                ++ks.uses;
                ks.maps.insert(map);
                if (ks.values.count(v) || ks.values.size() < kMaxValues)
                    ++ks.values[v];
                else
                    ks.moreValues = true;
                ++keyClasses[key][cls];
                if (key == "spawnflags") {
                    const long flags = std::strtol(v.c_str(), nullptr, 10);
                    c.flagBits |= uint32_t(flags);
                    ++c.flagValues[flags];
                }
            }
        }
    }

    // Text as it is, but quotes and anything unprintable escaped, and long
    // values shortened.
    auto shown = [](const std::string& v) {
        std::string out;
        for (unsigned char ch : v.substr(0, 80)) {
            if (ch == '"' || ch == '\\')
                out += '\\', out += char(ch);
            else if (ch < 32 || ch > 126) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\x%02x", ch);
                out += buf;
            } else
                out += char(ch);
        }
        return v.size() > 80 ? out + "..." : out;
    };
    auto mapList = [](const std::set<std::string>& maps, size_t limit) {
        std::string out;
        size_t n = 0;
        for (const std::string& m : maps) {
            if (n++ == limit) {
                out += " +" + std::to_string(maps.size() - limit) + " more";
                break;
            }
            out += " " + m;
        }
        return out;
    };

    std::ofstream out(outPath, std::ios::binary);
    if (!out) {
        std::fprintf(stderr, "entity-survey: can't write %s\n", outPath.c_str());
        return 1;
    }
    out << "Reluva entity survey of " << argv[2] << "\n";
    out << mapCounts.size() << " maps, " << total << " entities, " << classes.size() << " classes, "
        << keyClasses.size() << " keys\n";
    for (const std::string& f : failures)
        out << "FAILED " << f << "\n";
    out << "\n== Maps (entities) ==\n";
    for (const auto& [m, n] : mapCounts)
        out << m << " " << n << "\n";

    std::vector<const std::pair<const std::string, ClassStats>*> order;
    for (const auto& c : classes)
        order.push_back(&c);
    std::sort(order.begin(), order.end(), [](auto* a, auto* b) {
        return a->second.maps.size() != b->second.maps.size() ? a->second.maps.size() > b->second.maps.size()
                                                                : a->first < b->first;
    });
    out << "\n== Classes (by how many maps use them) ==\n";
    for (const auto* entry : order) {
        const auto& [cls, c] = *entry;
        out << "\n" << cls << ": " << c.count << " entities in " << c.maps.size() << " maps:" << mapList(c.maps, 12)
            << "\n";
        if (c.flagBits) {
            out << "  spawnflags bits:";
            for (int b = 0; b < 32; ++b)
                if (c.flagBits & (1u << b))
                    out << " " << (1u << b);
            out << "; values:";
            for (const auto& [v, n] : c.flagValues)
                out << " " << v << "(" << n << ")";
            out << "\n";
        }
        std::vector<const std::pair<const std::string, KeyStats>*> keys;
        for (const auto& k : c.keys)
            keys.push_back(&k);
        std::sort(keys.begin(), keys.end(), [](auto* a, auto* b) {
            return a->second.uses != b->second.uses ? a->second.uses > b->second.uses : a->first < b->first;
        });
        for (const auto* ke : keys) {
            const auto& [key, ks] = *ke;
            out << "  " << key << ": " << ks.uses << " of " << c.count << ", " << ks.values.size()
                << (ks.moreValues ? "+" : "") << " distinct";
            // The most common values first, up to six.
            std::vector<std::pair<std::string, int>> vals(ks.values.begin(), ks.values.end());
            std::stable_sort(vals.begin(), vals.end(), [](auto& a, auto& b) { return a.second > b.second; });
            for (size_t i = 0; i < vals.size() && i < 6; ++i)
                out << (i ? " | " : ": ") << "\"" << shown(vals[i].first) << "\""
                    << (vals[i].second > 1 ? "x" + std::to_string(vals[i].second) : "");
            out << "\n";
        }
    }

    out << "\n== Keys (the classes using each) ==\n";
    for (const auto& [key, users] : keyClasses) {
        int uses = 0;
        for (const auto& [cls, n] : users)
            uses += n;
        out << key << ": " << uses << " uses;";
        for (const auto& [cls, n] : users)
            out << " " << cls << "(" << n << ")";
        out << "\n";
    }
    out.close();
    std::printf("%zu maps, %zu entities, %zu classes, %zu keys", mapCounts.size(), total, classes.size(),
                keyClasses.size());
    if (!failures.empty())
        std::printf(", %zu maps failed", failures.size());
    std::printf("\nwritten to %s\n", outPath.c_str());
    return failures.empty() ? 0 : 1;
}

int cmdPortalCheck(int argc, char** argv) {
    // Every map's portals and mirrors: the surfaces whose shader says
    // `portal`, and the entities that place their cameras (Quake III's
    // misc_portal_surface, aimed by its target, a misc_portal_camera).
    if (argc != 3) {
        std::fprintf(stderr, "usage: reluva-tool portal-check <game folder>\n");
        return 2;
    }
    namespace q3 = reluva::idtech3;
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set))
        return 1;
    q3::ShaderLibrary shaders;
    shaders.load(set, nullptr);
    Bytes data;
    for (const std::string& name : set.list()) {
        const std::string key = reluva::normalizeArchiveName(name);
        if (key.rfind("maps/", 0) != 0 || key.size() < 4 || key.compare(key.size() - 4, 4, ".bsp") != 0)
            continue;
        q3::Bsp bsp;
        if (set.read(name, data) || q3::parseBsp(data, bsp))
            continue;
        std::map<std::string, int> portalSurfaces;
        for (const q3::BspSurface& s : bsp.surfaces)
            if (s.shader >= 0 && size_t(s.shader) < bsp.shaders.size())
                if (const q3::ShaderDef* def = shaders.find(bsp.shaders[size_t(s.shader)].name); def && def->portal)
                    ++portalSurfaces[def->name];
        std::vector<q3::Entity> entities;
        q3::parseEntities(bsp.entities, entities);
        std::vector<const q3::Entity*> portalEntities;
        for (const q3::Entity& e : entities)
            if (e.classname().rfind("misc_portal", 0) == 0)
                portalEntities.push_back(&e);
        if (portalSurfaces.empty() && portalEntities.empty())
            continue;
        std::printf("%s:\n", key.c_str());
        for (const auto& [shader, count] : portalSurfaces)
            std::printf("  %d surface%s with %s\n", count, count == 1 ? "" : "s", shader.c_str());
        for (const q3::Entity* e : portalEntities) {
            std::printf("  %s", e->classname().c_str());
            for (const auto& [k, v] : e->pairs)
                if (k != "classname")
                    std::printf(" %s \"%s\"", k.c_str(), v.c_str());
            std::printf("\n");
        }
    }
    return 0;
}

const Commands commands("idtech3", {
    {"bsp-check", "<game folder, e.g. .../BaseEF>",
     "load every maps/*.bsp of a game folder",
     cmdBspCheck},
    {"bsp-entities", "<game folder> <map> [text]...",
     "a map's entities, or those with a key or value containing one of the texts, each followed by "
     "those that target it",
     cmdBspEntities},
    {"entity-survey", "<game folder> [output file]",
     "every entity class and key in the game's maps, written to a file; report failures and "
     "corrections",
     cmdEntitySurvey},
    {"portal-check", "<game folder>",
     "each map's portal and mirror surfaces and portal entities",
     cmdPortalCheck},
});

} // namespace
