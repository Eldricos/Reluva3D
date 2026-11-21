// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's LEGO Island commands for the worlds' *INF.DTA files
// (docs/engines/legoisland/formats/dta.md).

#include "common/fs.h"
#include "devtools/tool.h"
#include "engines/legoisland/formats/dta.h"
#include "engines/legoisland/formats/si.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace reluva;
using namespace reluva::legoisland;
using namespace reluva::tool;

// Each animation, and its models and where they start.
int cmdDtaList(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool legoisland dta-list <file.dta>\n");
        return 2;
    }
    DtaFile dta;
    if (Error e = dta.read(argv[2])) {
        std::fprintf(stderr, "%s\n", e.message.c_str());
        return 1;
    }
    std::printf("version %u, %zu animations\n", dta.version, dta.animations.size());
    for (const DtaAnimation &a : dta.animations) {
        std::printf("%s %u  spot %d  flags %u %u %#04x %u  at (%g, %g, %g) r %g\n", a.name.c_str(), a.id,
                    a.spot == 0xFFFF ? -1 : int(a.spot), a.flags[0], a.flags[1], a.flags[2], a.flags[3], a.center[0],
                    a.center[1], a.center[2], a.radius);
        for (const DtaModel &m : a.models)
            std::printf("  %-12s kind %u  at (%g, %g, %g) facing (%g, %g, %g) up (%g, %g, %g) %u\n", m.name.c_str(),
                        m.kind, m.location[0], m.location[1], m.location[2], m.direction[0], m.direction[1],
                        m.direction[2], m.up[0], m.up[1], m.up[2], m.flag);
    }
    return 0;
}

// Every *INF.DTA in a game's folder read, and each one's animations found
// in the SI files: which file, and how its ids differ from theirs.
int cmdDtaCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool legoisland dta-check <game folder>\n");
        return 2;
    }
    const std::filesystem::path dir = argv[2];
    std::vector<std::filesystem::path> dtas;
    std::vector<std::filesystem::path> sis;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        const std::string name = fs::toLower(it->path().filename().string());
        if (name.size() > 7 && name.ends_with("inf.dta"))
            dtas.push_back(it->path());
        else if (name.ends_with(".si"))
            sis.push_back(it->path());
    }
    std::sort(dtas.begin(), dtas.end());
    // Each SI file's "<name>_RunAnim" objects' ids, by name.
    std::map<std::string, std::map<std::string, uint32_t>> runAnims;
    for (const std::filesystem::path &p : sis) {
        SiFile si;
        if (si.open(p))
            continue;
        for (const SiObject *o : si.all())
            if (o->name.size() > 8 && o->name.ends_with("_RunAnim"))
                runAnims[p.filename().string()][o->name.substr(0, o->name.size() - 8)] = o->id;
    }
    int failed = 0;
    for (const std::filesystem::path &p : dtas) {
        DtaFile dta;
        if (Error e = dta.read(p)) {
            std::fprintf(stderr, "%s\n", e.message.c_str());
            failed++;
            continue;
        }
        // The SI file with the most of its animations at the same ids, else
        // with the most of them at all, and by how much the ids differ.
        std::string best;
        size_t bestFound = 0;
        int bestSame = -1;
        std::map<int64_t, int> offsets;
        for (const auto &[file, names] : runAnims) {
            std::map<int64_t, int> diff;
            size_t found = 0;
            for (const DtaAnimation &a : dta.animations)
                if (const auto it = names.find(a.name); it != names.end()) {
                    found++;
                    diff[int64_t(a.id) - int64_t(it->second)]++;
                }
            const int same = diff.count(0) ? diff[0] : 0;
            if (found > 0 && (same > bestSame || (same == bestSame && found > bestFound))) {
                bestSame = same;
                bestFound = found;
                best = file;
                offsets = diff;
            }
        }
        // How many of its models start within its circle.
        int inside = 0;
        int models = 0;
        for (const DtaAnimation &a : dta.animations)
            for (const DtaModel &m : a.models) {
                models++;
                const float dx = m.location[0] - a.center[0];
                const float dz = m.location[2] - a.center[2];
                inside += std::sqrt(dx * dx + dz * dz) <= a.radius;
            }
        std::printf("%-14s %3zu animations, %4d models (%d inside their circles)", p.filename().string().c_str(),
                    dta.animations.size(), models, inside);
        if (!best.empty()) {
            std::printf(": %zu in %s, ids", bestFound, best.c_str());
            for (const auto &[d, n] : offsets)
                std::printf(" %+lld (%d)", static_cast<long long>(d), n);
        }
        std::printf("\n");
    }
    return failed ? 1 : 0;
}

const Commands kCommands(
    "legoisland",
    {
        {"dta-list", "<file.dta>", "a world's *INF.DTA: its animations and their models", cmdDtaList},
        {"dta-check", "<game folder>", "every *INF.DTA read, each one's animations found in the SI files", cmdDtaCheck},
    });

} // namespace
