// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's commands for LEGO Alpha Team's files
// (docs/engines/netimmerse/README.md): its .pac archives and .FIN models.
//
//   reluva-tool netimmerse pac-list <file.pac>
//   reluva-tool netimmerse pac-extract <file.pac> <output dir> [name filter]
//   reluva-tool netimmerse fin-check <Fin.pac> [file.pac...]

#include "common/fs.h"
#include "engines/netimmerse/fin.h"
#include "engines/netimmerse/pac_archive.h"
#include "devtools/tool.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

// Unix time -> "YYYY-MM-DD HH:MM" (UTC), without platform time functions.
std::string formatTime(uint32_t t) {
    int64_t days = t / 86400, secs = t % 86400;
    int64_t z = days + 719468, era = z / 146097, doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    int64_t d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9, y = yoe + era * 400 + (m <= 2);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d", int(y), int(m), int(d),
                  int(secs / 3600), int(secs / 60 % 60));
    return buf;
}

int cmdPacList(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool netimmerse pac-list <file.pac>\n");
        return 2;
    }
    reluva::netimmerse::PacArchive pac;
    if (reluva::Error err = pac.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    uint64_t total = 0;
    for (const auto& e : pac.entries()) {
        const auto t = reluva::netimmerse::PacArchive::unixTime(e.modified);
        std::printf("%10llu %9u  %-16s %s\n", static_cast<unsigned long long>(e.offset), e.size,
                    formatTime(uint32_t(t)).c_str(), e.name.c_str());
        total += e.size;
    }
    std::printf("%zu files, %llu bytes, data from %u\n", pac.entries().size(),
                static_cast<unsigned long long>(total), pac.dataStart());
    return 0;
}

int cmdPacExtract(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool netimmerse pac-extract <file.pac> <output dir> [name filter]\n");
        return 2;
    }
    reluva::netimmerse::PacArchive pac;
    if (reluva::Error err = pac.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const std::filesystem::path outDir = argv[3];
    const std::string filter = argc > 4 ? reluva::fs::toLower(argv[4]) : "";
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    std::vector<uint8_t> data;
    unsigned written = 0;
    for (const auto& e : pac.entries()) {
        if (reluva::fs::toLower(e.name).find(filter) == std::string::npos)
            continue;
        // Names come from the archive: refuse anything that could escape outDir.
        const std::filesystem::path rel(e.name);
        bool safe = !rel.is_absolute() && !rel.has_root_name() && rel.has_filename();
        for (const auto& part : rel)
            safe &= part != "..";
        if (!safe) {
            std::fprintf(stderr, "skipping unsafe name %s\n", e.name.c_str());
            continue;
        }
        if (reluva::Error err = pac.read(e, data)) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 1;
        }
        const std::filesystem::path out = outDir / rel;
        std::filesystem::create_directories(out.parent_path(), ec);
        std::ofstream f(out, std::ios::binary);
        f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        if (!f) {
            std::fprintf(stderr, "cannot write %s\n", out.string().c_str());
            return 1;
        }
        ++written;
    }
    std::printf("extracted %u files to %s\n", written, outDir.string().c_str());
    return 0;
}

int cmdFinCheck(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool netimmerse fin-check <Fin.pac> [file.pac...]\n");
        return 2;
    }
    using reluva::netimmerse::FinFile;
    using reluva::netimmerse::PacArchive;
    PacArchive fin;
    if (reluva::Error err = fin.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    // Where the images are looked for: the other archives given.
    reluva::SearchSet images;
    for (int i = 3; i < argc; ++i) {
        auto pac = std::make_unique<PacArchive>();
        if (reluva::Error err = pac->open(argv[i])) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 1;
        }
        images.add(std::move(pac), 0);
    }

    struct Count {
        unsigned total = 0, read = 0, exact = 0;
    };
    std::map<std::string, Count> counts;
    std::set<std::string> missingImages;
    unsigned files = 0, failed = 0, badLinks = 0, shapes = 0, triangles = 0;
    unsigned actorsWithTracks = 0, trackMismatches = 0, keysChecked = 0, keysMatching = 0;
    // Lights, by their settings: type, switch, attenuation (its switch,
    // distance and curve).
    std::map<std::string, unsigned> lightKinds;
    unsigned clipLists = 0, clipListsOut = 0;
    unsigned flipModes[4] = {}; // loop, reverse, clamp, other
    unsigned clipSounds = 0, soundLoop = 0, soundUnplaced = 0, soundMuted = 0, soundOdd = 0;
    std::vector<uint8_t> data;
    for (const auto& e : fin.entries()) {
        if (e.size == 0)
            continue;
        ++files;
        FinFile model;
        reluva::Error err = fin.read(e, data);
        if (!err)
            err = model.parse(data);
        if (err) {
            std::printf("%s: %s\n", e.name.c_str(), err.message.c_str());
            ++failed;
            continue;
        }
        for (const auto& o : model.objects()) {
            Count& c = counts[o.type];
            ++c.total;
            c.read += o.read;
            c.exact += o.exact;
        }
        // Links: children are nodes or shapes, properties are properties,
        // textures' frames are images.
        auto expect = [&](uint32_t id, bool ok, const char* what) {
            if (!ok) {
                if (++badLinks <= 10)
                    std::printf("%s: link %08x (%s) is not %s\n", e.name.c_str(), id, model.typeOf(id).c_str(), what);
            }
        };
        for (const auto& [id, node] : model.nodes) {
            for (uint32_t c : node.children)
                expect(c, model.nodes.count(c) || model.shapes.count(c) || model.coronas.count(c) || model.skins.count(c) ||
                              model.typeOf(c) == "NiLight" ||
                              model.typeOf(c).rfind("Ni3ds", 0) == 0,
                       "a node or shape");
            for (uint32_t p : node.properties)
                expect(p, model.typeOf(p).find("Property") != std::string::npos, "a property");
            if (node.actor)
                expect(node.actor, model.typeOf(node.actor) == "DDActorSharedData", "an actor's data");
        }
        for (const auto& [id, shape] : model.shapes) {
            ++shapes;
            triangles += unsigned(shape.triangles.size() / 3);
            for (uint32_t p : shape.properties)
                expect(p, model.typeOf(p).find("Property") != std::string::npos, "a property");
        }
        for (const auto& [id, skin] : model.skins)
            for (const auto& in : skin.influences)
                expect(in.bone, model.typeOf(in.bone) == "Ni3dsBone", "a bone");
        for (const auto& [id, frames] : model.textures)
            for (uint32_t f : frames)
                expect(f, model.images.count(f) != 0, "an image");
        for (const auto& [id, image] : model.multiTextures)
            expect(image, model.images.count(image) != 0, "an image");
        for (const auto& [id, flip] : model.flips) {
            expect(flip.target, model.typeOf(flip.target) == "NiTextureProperty", "a texture property");
            ++flipModes[std::min<uint32_t>(flip.outOfBounds, 3)];
        }
        for (const auto& [id, a] : model.materialAnimators)
            expect(a.target, model.typeOf(a.target) == "NiMaterialProperty", "a material");
        // An actor's tracks go to the 3ds nodes in depth-first order: there
        // are as many, and each track's first key is where its node was
        // saved.
        const std::vector<uint32_t> animated = model.animatedNodes();
        for (const auto& [id, actor] : model.actors) {
            if (actor.tracks.empty())
                continue;
            ++actorsWithTracks;
            if (actor.tracks.size() != animated.size()) {
                ++trackMismatches;
                std::printf("%s: %zu tracks for %zu 3ds nodes\n", e.name.c_str(), actor.tracks.size(), animated.size());
                continue;
            }
            for (size_t i = 0; i < animated.size(); ++i) {
                const auto& node = model.nodes.at(animated[i]);
                const auto& t = actor.tracks[i];
                if (!t.positions.empty()) {
                    ++keysChecked;
                    float d = 0;
                    for (size_t k = 0; k < 3; ++k)
                        d = std::max(d, std::fabs(t.positions.front().value[k] - node.translation[k]));
                    keysMatching += d < 1e-3f;
                }
                if (!t.rotations.empty()) {
                    // The quaternion's matrix, row by row, against the node's.
                    const auto [w, x, y, z] = t.rotations.front().value;
                    const float m[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - w * z),     2 * (x * z + w * y),
                                        2 * (x * y + w * z),     1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
                                        2 * (x * z - w * y),     2 * (y * z + w * x),     1 - 2 * (x * x + y * y)};
                    float d = 0;
                    for (size_t k = 0; k < 9; ++k)
                        d = std::max(d, std::fabs(m[k] - node.rotation[k]));
                    ++keysChecked;
                    keysMatching += d < 1e-3f;
                }
            }
        }
        // A clip's lists name tracks, flip-books and morph shapes by number.
        for (const auto& [id, actor] : model.actors)
            for (const auto& c : actor.clips) {
                if (!c.sound.empty()) {
                    ++clipSounds;
                    soundLoop += c.loop, soundUnplaced += c.unplaced, soundMuted += c.muteBeyondMax;
                    soundOdd += !c.soundNode.empty() || c.delay != 0;
                }
                const std::pair<const std::vector<uint16_t>*, size_t> lists[] = {
                    {&c.tracks, animated.size()}, {&c.flips, model.flips.size()}, {&c.morphs, model.morphs.size()}};
                for (const auto& [list, count] : lists) {
                    clipLists += !list->empty();
                    for (uint16_t v : *list)
                        if (v >= count) {
                            ++clipListsOut;
                            break;
                        }
                }
            }
        for (const auto& [id, l] : model.lights) {
            static const char* const kTypes[] = {"ambient", "directional", "point", "spot"};
            char key[160];
            std::snprintf(key, sizeof key, "%-11s %-3s attenuated %d, distance %g, curve %g",
                          l.type < 4 ? kTypes[l.type] : "?", l.on ? "on" : "off", int(l.attenuated),
                          double(l.attenuationDistance), double(l.attenuationCurve));
            ++lightKinds[key];
            for (uint32_t n : l.illuminated)
                expect(n, model.nodes.count(n) != 0, "a node");
        }
        if (images.size())
            for (const auto& [id, name] : model.images)
                if (!images.has(name))
                    missingImages.insert(name);
    }

    std::printf("%-24s %7s %7s %7s\n", "class", "objects", "read", "exact");
    unsigned notExact = 0;
    for (const auto& [type, c] : counts) {
        std::printf("%-24s %7u %7u %7u\n", type.c_str(), c.total, c.read, c.exact);
        notExact += c.read - c.exact;
    }
    std::printf("%u models, %u failed; %u shapes, %u triangles; %u read but not exactly; %u bad links\n", files,
                failed, shapes, triangles, notExact, badLinks);
    std::printf("%u clip lists (tracks, flip-books, morph shapes), %u naming more than the model has\n", clipLists,
                clipListsOut);
    std::printf("flip-books: %u loop, %u back and forth, %u clamp, %u other\n", flipModes[0], flipModes[1],
                flipModes[2], flipModes[3]);
    std::printf("%u clip sounds: %u loop, %u unplaced, %u muted beyond their maximum distance, %u with a node or a "
                "delay\n",
                clipSounds, soundLoop, soundUnplaced, soundMuted, soundOdd);
    if (!lightKinds.empty()) {
        std::printf("lights:\n");
        for (const auto& [key, n] : lightKinds)
            std::printf("  %4u %s\n", n, key.c_str());
    }
    std::printf("%u models with animation tracks, %u with a track count unlike their 3ds nodes'; %u of %u first keys "
                "where the node was saved\n",
                actorsWithTracks, trackMismatches, keysMatching, keysChecked);
    if (images.size()) {
        std::printf("%zu images not found in the archives given", missingImages.size());
        for (const std::string& m : missingImages)
            std::printf(" %s", m.c_str());
        std::printf("\n");
    }
    return failed || notExact || badLinks || !missingImages.empty() ? 1 : 0;
}

const reluva::tool::Commands commands("netimmerse", {
    {"pac-list", "<file.pac>", "list a .pac archive's files: offset, size, date and name", cmdPacList},
    {"pac-extract", "<file.pac> <output dir> [name filter]",
     "extract a .pac archive's files (those whose name contains the filter)", cmdPacExtract},
    {"fin-check", "<Fin.pac> [file.pac...]",
     "read every .FIN model in Fin.pac; count the objects read, and check their links and (in the other "
     "archives given) their images",
     cmdFinCheck},
});

} // namespace
