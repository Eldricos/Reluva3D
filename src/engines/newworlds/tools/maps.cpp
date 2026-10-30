// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's New Worlds commands for its maps (.gmf) and missions.

#include "tools/tool.h"

#include "engines/newworlds/cat_archive.h"
#include "engines/newworlds/geo.h"
#include "engines/newworlds/gmf.h"
#include "engines/newworlds/levels.h"
#include "engines/newworlds/model_textures.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace reluva::tool;

int cmdLevelInfo(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool level-info <level.dat> [file.cat...]\n");
        return 2;
    }
    namespace nw = reluva::newworlds;
    Bytes d;
    if (!readFile(argv[2], d))
        return 1;
    std::vector<nw::Level> levels;
    if (reluva::Error err = nw::parseLevelList(std::string_view(reinterpret_cast<const char*>(d.data()), d.size()),
                                               levels)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    // Which of the given archives holds each level's map.
    std::vector<std::pair<std::string, std::string>> maps; // map name, archive
    for (int a = 3; a < argc; ++a) {
        nw::CatArchive cat;
        if (reluva::Error err = cat.open(argv[a])) {
            std::fprintf(stderr, "%s: %s\n", argv[a], err.message.c_str());
            continue;
        }
        for (const auto& e : cat.entries())
            if (e.tag == nw::cattag::Gmf)
                maps.emplace_back(e.name, std::filesystem::path(argv[a]).filename().string());
    }
    for (const auto& l : levels) {
        std::string where;
        for (const auto& [map, archive] : maps)
            if (nw::levelForMap(levels, map) == &l)
                where += (where.empty() ? "" : ", ") + map + " in " + archive;
        std::printf("%-5s %d  %-28s %s%s\n", l.shortName.c_str(), l.maxPlayers, l.title.c_str(),
                    l.commentedOut ? "(commented out) " : "", where.c_str());
    }
    std::printf("%zu levels\n", levels.size());
    return 0;
}

// Map (.gmf) layout as far as known: u16 counts of type-21, type-20 and
// type-19 records, u16 grid width and height; those records (u16 type, u16
// length, u16 ?, then `length` bytes) in that order; then data of unknown
// layout. See docs/engines/newworlds/gmf.md.
int cmdGmfInfo(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool gmf-info <file.gmf> [record number]\n");
        return 2;
    }
    Bytes d;
    if (!readFile(argv[2], d))
        return 1;
    const long want = argc > 3 ? std::strtol(argv[3], nullptr, 0) : -1;
    const unsigned counts[3] = {rd16(d, 0), rd16(d, 2), rd16(d, 4)};
    const uint16_t types[3] = {21, 20, 19};
    std::printf("%zu bytes; %u type-21, %u type-20 and %u type-19 records; grid %u x %u\n", d.size(), counts[0],
                counts[1], counts[2], rd16(d, 6), rd16(d, 8));
    {
        reluva::newworlds::GmfMap map;
        const reluva::Error err = reluva::newworlds::parseGmf(d, map);
        std::printf("engine's reader: %s\n", err ? err.message.c_str() : "reads it");
        if (!err) {
            std::map<int, size_t> types;
            for (const auto& o : map.scenery)
                ++types[o.type];
            std::printf("placed objects: %zu;", map.scenery.size());
            for (const auto& [type, n] : types)
                std::printf(" type %d x%zu", type, n);
            std::printf("\n");
        }
    }

    struct Record {
        size_t offset;
        uint16_t type, length, third;
    };
    std::vector<Record> records;
    size_t p = 10;
    for (int k = 0; k < 3; ++k) {
        std::map<uint16_t, unsigned> lengths, thirds;
        const size_t start = p;
        for (unsigned i = 0; i < counts[k]; ++i) {
            const Record r{p, rd16(d, p), rd16(d, p + 2), rd16(d, p + 4)};
            if (p + 6 > d.size() || r.type != types[k] || p + 6 + r.length > d.size()) {
                std::printf("type-%u record %u at 0x%zx doesn't fit (type %u, length %u):\n", types[k], i, p, r.type,
                            r.length);
                hexdump(d, p, 0x40);
                return 1;
            }
            records.push_back(r);
            ++lengths[r.length];
            ++thirds[r.third];
            p += 6 + r.length;
        }
        std::printf("type %u: 0x%zx..0x%zx; lengths", types[k], start, p);
        for (const auto& [v, n] : lengths)
            std::printf(" %u x%u", v, n);
        std::printf("; third values");
        for (const auto& [v, n] : thirds)
            std::printf(" %u x%u", v, n);
        std::printf("\n");
    }
    std::printf("after the records: 0x%zx, %zu bytes (%.2f per grid cell)\n", p, d.size() - p,
                double(d.size() - p) / std::max(1, rd16(d, 6) * rd16(d, 8)));

    if (want >= 0 && size_t(want) < records.size()) {
        const Record& r = records[size_t(want)];
        std::printf("record %ld at 0x%zx: type %u, length %u, third %u\n", want, r.offset, r.type, r.length, r.third);
        hexdump(d, r.offset, 6 + size_t(r.length));
        return 0;
    }
    // Type 19: four u32 indices after 6 bytes, presumably into the tiles
    // (the type-21/20 records). Check they're in range.
    {
        const size_t first = counts[0] + counts[1];
        uint32_t maxIndex = 0;
        for (size_t i = first; i < records.size(); ++i)
            for (int k = 0; k < 4; ++k)
                maxIndex = std::max(maxIndex, rd32(d, records[i].offset + 6 + 6 + 4 * size_t(k)));
        std::printf("type-19 indices: highest %u (%u tiles)\n", maxIndex, counts[0]);
        // Type 20: u32 own number, u32, u32, u32, u16, then 8 x 8 byte grids.
        unsigned numbered = 0;
        for (size_t i = counts[0]; i < first; ++i)
            numbered += rd32(d, records[i].offset + 6) == i - counts[0];
        std::printf("type-20 records whose first value is their own number: %u of %u\n", numbered, counts[1]);
    }

    // Each tile's type-20 header values: own number, 0, model?, flags?, u16.
    std::printf("tiles (number: type-20 values 2-5):");
    for (size_t i = counts[0]; i < size_t(counts[0]) + counts[1]; ++i) {
        const size_t o = records[i].offset + 6;
        std::printf("%s %zu: %u %u 0x%x %u", (i - counts[0]) % 6 ? ";" : "\n ", i - counts[0], rd32(d, o + 4),
                    rd32(d, o + 8), rd32(d, o + 12), rd16(d, o + 16));
    }
    std::printf("\n");

    // After the records: a u32 count, then that many u32 entries (lists ending
    // ffffffff), then one 10-byte entry per grid cell (u16 block, u16 ?, u16 ?,
    // u32 where its list starts), then 1024 more bytes.
    {
        const uint32_t n = rd32(d, p);
        size_t separators = 0;
        const size_t end = p + 4 + size_t(n) * 4;
        if (end <= d.size()) {
            for (size_t o = p + 4; o < end; o += 4)
                separators += rd32(d, o) == 0xffffffffu;
            std::printf("after the records: u32 %u; %u u32s from there hold %zu ffffffff separators and end at 0x%zx, "
                        "leaving %zu bytes (%.2f per grid cell)\n",
                        n, n, separators, end, d.size() - end,
                        double(d.size() - end) / std::max(1, rd16(d, 6) * rd16(d, 8)));
            const unsigned w = rd16(d, 6), h = rd16(d, 8);
            if (d.size() - end >= size_t(w) * h * 10) {
                std::map<uint16_t, unsigned> seconds;
                std::printf("blocks on the grid (rows of %u; 0-9 then a-z, '+' for higher):\n", w);
                for (unsigned y = 0; y < h; ++y) {
                    std::printf("  ");
                    for (unsigned x = 0; x < w; ++x) {
                        const size_t e = end + (size_t(y) * w + x) * 10;
                        const unsigned b = rd16(d, e);
                        ++seconds[rd16(d, e + 2)];
                        std::printf("%c", b < 10 ? char('0' + b) : b < 36 ? char('a' + b - 10) : '+');
                    }
                    std::printf("\n");
                }
                std::printf("second values of the grid entries:");
                for (const auto& [v, n] : seconds)
                    std::printf(" %u x%u", v, n);
                std::printf("\nlast 1024 bytes start:\n");
                hexdump(d, end + size_t(w) * h * 10, 0x40);
            } else {
                std::printf("start of what's left:\n");
                hexdump(d, end, 0x80);
            }
        } else {
            std::printf("after the records: u32 %u (too many for the file to be that many u32s)\n", n);
        }
    }

    // A sample of each kind, and the start of what follows.
    for (int k = 0; k < 3; ++k) {
        size_t first = 0;
        for (int j = 0; j < k; ++j)
            first += counts[j];
        if (counts[k] == 0)
            continue;
        const size_t pick = first + counts[k] / 2;
        const Record& r = records[pick];
        std::printf("record %zu (a type-%u one from the middle) at 0x%zx:\n", pick, r.type, r.offset);
        if (r.type == 20) { // header values, then the 8 x 8 grids
            std::printf("  values %u %u %u %u %u; grids:\n", rd32(d, r.offset + 6), rd32(d, r.offset + 10),
                        rd32(d, r.offset + 14), rd32(d, r.offset + 18), rd16(d, r.offset + 22));
            const size_t grids = r.offset + 6 + 18;
            for (unsigned row = 0; row < 8; ++row) {
                std::printf("   ");
                for (size_t g = 0; g * 64 + 64 <= size_t(r.length) - 18; ++g) {
                    for (unsigned c = 0; c < 8; ++c)
                        std::printf(" %02x", d[grids + g * 64 + row * 8 + c]);
                    std::printf("  ");
                }
                std::printf("\n");
            }
            const size_t rest = grids + ((size_t(r.length) - 18) / 64) * 64;
            if (rest < r.offset + 6 + r.length) {
                std::printf("  after the grids:\n");
                hexdump(d, rest, r.offset + 6 + r.length - rest);
            }
            continue;
        }
        hexdump(d, r.offset, std::min<size_t>(6 + size_t(r.length), 0x80));
    }
    if (counts[2] > 1) {
        std::printf("first type-19 records, one per line:\n");
        const size_t first = counts[0] + counts[1];
        for (size_t i = first; i < first + std::min<size_t>(counts[2], 12); ++i)
            hexdump(d, records[i].offset, 6 + size_t(records[i].length));
    }
    std::printf("after the records:\n");
    hexdump(d, p, 0x80);
    return 0;
}

// For each model number a planet's maps use, how well each ground model would
// fit there: the share of the number's tile sides that meet their neighbours
// (all 5 heights within 80), the neighbours taking their models from
// tileModelOrder. The model the numbering gives should fit best.
int cmdGmfFit(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool gmf-fit [-a] <planet.cat> [map]\n");
        return 2;
    }
    namespace nw = reluva::newworlds;
    bool archiveOrder = false, table = false, layersMode = false, heightMode = false, jointMode = false,
         summaryMode = false, rotationScan = false;
    const char* orderFile = nullptr;
    while (argc >= 3 && argv[2][0] == '-' && argv[2][1] != 0 && argv[2][2] == 0) {
        if (argv[2][1] == 'o' && argc >= 4) {
            orderFile = argv[3];
            argv += 2, argc -= 2;
            continue;
        }
        archiveOrder |= argv[2][1] == 'a';
        table |= argv[2][1] == 't';
        layersMode |= argv[2][1] == 'l';
        heightMode |= argv[2][1] == 'h';
        jointMode |= argv[2][1] == 'j';
        summaryMode |= argv[2][1] == 'm';
        rotationScan |= argv[2][1] == 'r';
        ++argv, --argc;
    }
    if (argc < 3)
        return 2;
    nw::CatArchive cat;
    if (reluva::Error err = cat.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    std::vector<std::string> geoNames;
    std::vector<const nw::CatEntry*> geoEntries, maps;
    for (const auto& e : cat.entries()) {
        if (e.tag == nw::cattag::Geometry)
            geoNames.push_back(e.name), geoEntries.push_back(&e);
        if (e.tag == nw::cattag::Gmf && (argc < 4 || e.name.find(argv[3]) != std::string::npos))
            maps.push_back(&e);
    }
    // -a: number the models in plain archive order instead; -o file: the
    // numbering from a file of "number model-stem" lines.
    std::vector<size_t> order = nw::tileModelOrder(geoNames);
    if (archiveOrder)
        for (size_t i = 0; i < order.size() || i < geoNames.size(); ++i)
            order.resize(geoNames.size()), order[i] = i;
    if (orderFile) {
        order.assign(order.size(), nw::kNoModel);
        std::ifstream in(orderFile);
        unsigned n;
        std::string stem;
        while (in >> n >> stem) {
            if (n >= order.size())
                order.resize(n + 1, nw::kNoModel);
            for (size_t g = 0; g < geoNames.size(); ++g)
                if (geoNames[g].find("/lod/") == std::string::npos &&
                    geoNames[g].size() > stem.size() + 5 &&
                    geoNames[g].compare(geoNames[g].size() - stem.size() - 4, stem.size() + 4, stem + ".geo") == 0 &&
                    geoNames[g][geoNames[g].size() - stem.size() - 5] == '/')
                    order[n] = g;
        }
    }
    // Each ground model's sides at each rotation (lod/ and scenery models
    // aren't candidates).
    std::vector<std::array<nw::TileSides, 4>> sides(geoNames.size());
    std::vector<bool> candidate(geoNames.size());
    Bytes d;
    for (size_t g = 0; g < geoNames.size(); ++g) {
        nw::GeoModel geo;
        if (cat.read(*geoEntries[g], d) || nw::parseGeo(d, geo))
            continue;
        candidate[g] = true;
        for (int r = 0; r < 4; ++r)
            sides[g][size_t(r)] = nw::tileSides(geo, r);
    }
    // -s: each model's sides instead, as heights in levels of 1000 (east,
    // north, west, south; 5 points each, low coordinate first).
    if (argc >= 4 && std::strcmp(argv[3], "-s") == 0) {
        for (size_t g = 0; g < geoNames.size(); ++g) {
            if (!candidate[g] || geoNames[g].find("/lod/") != std::string::npos)
                continue;
            std::printf("%2zu %-24s", g, geoNames[g].c_str());
            const auto& t = sides[g][0];
            for (int sd = 0; sd < 4; ++sd) {
                std::printf(" ");
                for (int k = 0; k < 5; ++k)
                    std::printf(t.present[sd] ? "%2d" : " .", int(std::lround(t.z[sd][k] / 1000)));
            }
            std::printf("\n");
        }
        return 0;
    }
    // Each model's surface height at the centre of each cell of its 8 x 8
    // grid, in the maps' terrain-layer units (1 at 0, 3 at 3264, 5 at 7360:
    // 1 + z / 1840), for each rotation, cells in map orientation (row 0 south,
    // column 0 west).
    std::vector<std::array<std::array<int, 64>, 4>> levels(geoNames.size());
    for (size_t g = 0; g < geoNames.size(); ++g) {
        nw::GeoModel geo;
        if (!candidate[g] || cat.read(*geoEntries[g], d) || nw::parseGeo(d, geo))
            continue;
        auto heightAt = [&](float x, float y) {
            float best = -1e9f;
            for (const auto& f : geo.faces) {
                const auto& vs = geo.parts[f.part].vertices;
                for (size_t i = 1; i + 1 < f.corners.size(); ++i) {
                    const auto& a = vs[f.corners[0].vertex];
                    const auto& b = vs[f.corners[i].vertex];
                    const auto& c = vs[f.corners[i + 1].vertex];
                    const float det = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
                    if (std::fabs(det) < 1e-3f)
                        continue;
                    const float l1 = ((b.y - c.y) * (x - c.x) + (c.x - b.x) * (y - c.y)) / det;
                    const float l2 = ((c.y - a.y) * (x - c.x) + (a.x - c.x) * (y - c.y)) / det;
                    const float l3 = 1 - l1 - l2;
                    if (l1 < -1e-4f || l2 < -1e-4f || l3 < -1e-4f)
                        continue;
                    best = std::max(best, l1 * a.z + l2 * b.z + l3 * c.z);
                }
            }
            return best;
        };
        for (int r = 0; r < 4; ++r)
            for (int row = 0; row < 8; ++row)
                for (int col = 0; col < 8; ++col) {
                    // The cell's centre in map orientation, turned back into the model's frame.
                    float x = -nw::kGroundTileSize / 2 + (float(col) + 0.5f) * nw::kGroundTileSize / 8;
                    float y = -nw::kGroundTileSize / 2 + (float(row) + 0.5f) * nw::kGroundTileSize / 8;
                    for (int k = 0; k < r; ++k) {
                        const float t = x;
                        x = y;
                        y = -t;
                    }
                    const float z = heightAt(x, y);
                    // In tenths of a level, so slopes compare within a level.
                    levels[g][size_t(r)][size_t(row * 8 + col)] = z < -1e8f ? -100 : int(std::lround(10 + z / 184));
                }
    }
    auto layerCount = [](const nw::GmfMap::Tile& t, int cell) {
        int n = 0;
        for (int l = 0; l < nw::GmfMap::kLayers; ++l)
            n += t.layers[size_t(l * 64 + cell)] != 0;
        return n;
    };
    auto shortName = [&](size_t g) {
        if (g == nw::kNoModel)
            return std::string("-");
        const std::string& n = geoNames[g];
        return n.substr(n.rfind('/') + 1);
    };
    auto meets = [](const nw::TileSides& a, int sa, const nw::TileSides& b, int sb) {
        for (int k = 0; k < 5; ++k)
            if (std::fabs(a.z[sa][k] - b.z[sb][k]) >= 80)
                return false;
        return true;
    };
    if (jointMode) {
        // -j: search for the numbering that best fits the planet's maps:
        // each number may take only models whose heights fit its tiles'
        // terrain layers (within 10 points of the best: the layers only roughly
        // follow slopes), and among those, swaps
        // between numbers are kept while more tile edges meet.
        struct Pair { uint32_t na, nb; int ra, rb, side; };
        std::vector<Pair> pairs;
        std::map<uint32_t, std::vector<std::pair<unsigned, unsigned>>> heightTally; // number -> per model (met, seen)
        std::map<uint32_t, unsigned> uses;
        for (const nw::CatEntry* me : maps) {
            nw::GmfMap map;
            if (cat.read(*me, d) || nw::parseGmf(d, map))
                continue;
            for (int ty = 0; ty < 2 * map.height; ++ty)
                for (int tx = 0; tx < 2 * map.width; ++tx)
                    for (int sd = 0; sd < 2; ++sd) {
                        const int a = nw::tileAt(map, tx, ty), b = nw::tileAt(map, tx + (sd == 0), ty + (sd == 1));
                        if (a < 0 || b < 0)
                            continue;
                        const auto& ta = map.tiles[size_t(a)];
                        const auto& tb = map.tiles[size_t(b)];
                        pairs.push_back({ta.model, tb.model, ta.rotation, tb.rotation, sd});
                    }
            for (const auto& t : map.tiles) {
                ++uses[t.model];
                auto& tally = heightTally[t.model];
                tally.resize(geoNames.size());
                for (size_t g = 0; g < geoNames.size(); ++g) {
                    if (!candidate[g])
                        continue;
                    for (int cell = 0; cell < 64; ++cell) {
                        ++tally[g].second;
                        tally[g].first += std::abs(levels[g][size_t(t.rotation)][size_t(cell)] - 10 * layerCount(t, cell)) < 10;
                    }
                }
            }
        }
        auto isMain = [&](size_t g) { return candidate[g] && geoNames[g].find("/lod/") == std::string::npos; };
        std::map<uint32_t, std::vector<bool>> allowed;
        std::map<uint32_t, std::vector<double>> heightScore;
        for (auto& [n, tally] : heightTally) {
            double best = 0;
            auto& hs = heightScore[n];
            hs.assign(geoNames.size(), 0);
            for (size_t g = 0; g < geoNames.size(); ++g)
                if (isMain(g) && tally[g].second)
                    hs[g] = double(tally[g].first) / tally[g].second, best = std::max(best, hs[g]);
            auto& al = allowed[n];
            al.assign(geoNames.size(), false);
            for (size_t g = 0; g < geoNames.size(); ++g)
                al[g] = isMain(g) && hs[g] >= best - 0.10;
        }
        std::map<uint32_t, size_t> assign;
        for (const auto& [n, c] : uses)
            assign[n] = n < order.size() ? order[n] : nw::kNoModel;
        const std::map<uint32_t, size_t> start = assign;
        auto pairMet = [&](const Pair& p) -> int {
            const size_t ga = assign[p.na], gb = assign[p.nb];
            if (ga == nw::kNoModel || gb == nw::kNoModel || !candidate[ga] || !candidate[gb])
                return 0;
            const auto& sa = sides[ga][size_t(p.ra)];
            const auto& sb = sides[gb][size_t(p.rb)];
            if (!sa.present[p.side] || !sb.present[p.side + 2])
                return 0;
            return meets(sa, p.side, sb, p.side + 2) ? 1 : 0;
        };
        // The pairs touching each number, for scoring a change quickly.
        std::map<uint32_t, std::vector<size_t>> touching;
        for (size_t i = 0; i < pairs.size(); ++i) {
            touching[pairs[i].na].push_back(i);
            if (pairs[i].nb != pairs[i].na)
                touching[pairs[i].nb].push_back(i);
        }
        auto localScore = [&](uint32_t a, uint32_t b) {
            int total = 0;
            for (size_t i : touching[a])
                total += pairMet(pairs[i]);
            for (size_t i : touching[b])
                if (pairs[i].na != a && pairs[i].nb != a)
                    total += pairMet(pairs[i]);
            return total;
        };
        auto fits = [&](uint32_t n, size_t g) { return g == nw::kNoModel ? false : allowed[n][g]; };
        // Numbers whose model doesn't fit their heights take any fitting model first.
        auto inUse = [&](size_t c) {
            for (const auto& [m, gm] : assign)
                if (gm == c)
                    return true;
            return false;
        };
        for (auto& [n, g] : assign)
            if (!fits(n, g))
                for (size_t c = 0; c < geoNames.size(); ++c)
                    if (allowed[n][c] && !inUse(c)) {
                        g = c;
                        break;
                    }
        for (int pass = 0; pass < 20; ++pass) {
            bool improved = false;
            for (auto& [n, g] : assign) {
                for (size_t c = 0; c < geoNames.size(); ++c) {
                    if (c == g || !fits(n, c))
                        continue;
                    // The number using c now, if any, takes n's model (if it fits there).
                    uint32_t other = n;
                    for (const auto& [m, gm] : assign)
                        if (gm == c && m != n)
                            other = m;
                    if (other != n && !fits(other, g))
                        continue;
                    const int before = localScore(n, other);
                    const size_t old = g;
                    g = c;
                    if (other != n)
                        assign[other] = old;
                    const int after = localScore(n, other);
                    if (after > before) {
                        improved = true;
                    } else {
                        g = old;
                        if (other != n)
                            assign[other] = c;
                    }
                }
            }
            if (!improved)
                break;
        }
        int met = 0, seen = 0;
        for (const auto& p : pairs) {
            met += pairMet(p);
            const size_t ga = assign[p.na], gb = assign[p.nb];
            seen += ga != nw::kNoModel && gb != nw::kNoModel && candidate[ga] && candidate[gb];
        }
        std::printf("%zu maps, %zu tile pairs, %.1f%% of edges meet\n", maps.size(), pairs.size(),
                    seen ? 100.0 * met / seen : 0.0);
        for (const auto& [n, g] : assign) {
            const size_t s0 = start.at(n);
            std::printf("  %3u x%-5u %-14s height %5.1f%%%s\n", n, uses[n], shortName(g).c_str(),
                        g == nw::kNoModel ? 0.0 : 100 * heightScore[n][g],
                        g == s0 ? "" : ("   (table: " + shortName(s0) + ")").c_str());
        }
        return 0;
    }
    if (summaryMode) {
        // -m: only the share of tile edges that meet with this numbering, and
        // of tile cells whose terrain layers match the model's height.
        unsigned met = 0, seen = 0, cellsOk = 0, cells = 0;
        for (const nw::CatEntry* me : maps) {
            nw::GmfMap map;
            if (cat.read(*me, d) || nw::parseGmf(d, map))
                continue;
            auto modelOf = [&](uint32_t number) { return number < order.size() ? order[number] : nw::kNoModel; };
            for (int ty = 0; ty < 2 * map.height; ++ty)
                for (int tx = 0; tx < 2 * map.width; ++tx)
                    for (int sd = 0; sd < 2; ++sd) {
                        const int a = nw::tileAt(map, tx, ty), b = nw::tileAt(map, tx + (sd == 0), ty + (sd == 1));
                        if (a < 0 || b < 0)
                            continue;
                        const auto& ta = map.tiles[size_t(a)];
                        const auto& tb = map.tiles[size_t(b)];
                        const size_t ga = modelOf(ta.model), gb = modelOf(tb.model);
                        ++seen;
                        if (ga == nw::kNoModel || gb == nw::kNoModel || !candidate[ga] || !candidate[gb])
                            continue;
                        const auto& sa = sides[ga][size_t(ta.rotation)];
                        const auto& sb = sides[gb][size_t(tb.rotation)];
                        met += sa.present[sd] && sb.present[sd + 2] && meets(sa, sd, sb, sd + 2);
                    }
            for (const auto& t : map.tiles) {
                const size_t g = modelOf(t.model);
                for (int cell = 0; cell < 64; ++cell) {
                    ++cells;
                    cellsOk += g != nw::kNoModel && candidate[g] &&
                               std::abs(levels[g][size_t(t.rotation)][size_t(cell)] - 10 * layerCount(t, cell)) < 10;
                }
            }
        }
        std::printf("%zu maps: %.1f%% of tile edges meet, %.1f%% of cells match the terrain layers\n", maps.size(),
                    seen ? 100.0 * met / seen : 0.0, cells ? 100.0 * cellsOk / cells : 0.0);
        return 0;
    }
    for (const nw::CatEntry* me : maps) {
        nw::GmfMap map;
        if (cat.read(*me, d) || nw::parseGmf(d, map)) {
            std::printf("%s: doesn't read\n", me->name.c_str());
            continue;
        }
        // Neighbouring tile pairs: (tile, side 0 east or 1 north, neighbour).
        struct Pair { int a, side, b; };
        std::vector<Pair> pairs;
        for (int ty = 0; ty < 2 * map.height; ++ty)
            for (int tx = 0; tx < 2 * map.width; ++tx)
                for (int s = 0; s < 2; ++s) {
                    const int a = nw::tileAt(map, tx, ty), b = nw::tileAt(map, tx + (s == 0), ty + (s == 1));
                    if (a >= 0 && b >= 0)
                        pairs.push_back({a, s, b});
                }
        std::map<uint32_t, unsigned> uses;
        for (const auto& t : map.tiles)
            ++uses[t.model];
        if (rotationScan) {
            // -r: per number, each tile's best-fitting models and rotations by
            // heights; tallies of (model, stored rotation - fitted rotation).
            for (const auto& [number, count] : uses) {
                std::map<std::string, unsigned> tally;
                for (const auto& t : map.tiles) {
                    if (t.model != number)
                        continue;
                    // The best-fitting models and rotations for this tile (ties all count).
                    int bestOk = -1;
                    std::vector<std::string> best;
                    for (size_t g = 0; g < geoNames.size(); ++g) {
                        if (!candidate[g] || geoNames[g].find("/lod/") != std::string::npos)
                            continue;
                        for (int r = 0; r < 4; ++r) {
                            int ok = 0;
                            for (int cell = 0; cell < 64; ++cell)
                                ok += std::abs(levels[g][size_t(r)][size_t(cell)] - 10 * layerCount(t, cell)) < 10;
                            const std::string key = shortName(g) + " d" + std::to_string((t.rotation - r + 4) % 4);
                            if (ok > bestOk)
                                bestOk = ok, best = {key};
                            else if (ok == bestOk)
                                best.push_back(key);
                        }
                    }
                    for (const auto& k : best)
                        ++tally[k];
                }
                std::vector<std::pair<unsigned, std::string>> v;
                for (auto& [k, c] : tally)
                    v.push_back({c, k});
                std::sort(v.rbegin(), v.rend());
                std::printf("%3u x%-3u", number, count);
                for (size_t i = 0; i < v.size() && i < 6; ++i)
                    std::printf("  %s:%u", v[i].second.c_str(), v[i].first);
                std::printf("\n");
            }
            continue;
        }
        if (layersMode) {
            // -l: per number, a rotation-0 tile's count of non-zero terrain
            // layers per cell (row 7 = north first), beside the assigned
            // model's heights per cell (vertex mean, in levels of 1000).
            for (const auto& [number, count] : uses) {
                const nw::GmfMap::Tile* sample = nullptr;
                for (const auto& t : map.tiles)
                    if (t.model == number && (!sample || t.rotation == 0))
                        sample = sample && sample->rotation == 0 ? sample : &t;
                const size_t g = number < order.size() ? order[number] : nw::kNoModel;
                std::printf("%u (x%u, rotation %d) %s\n", number, count, sample->rotation,
                            g == nw::kNoModel ? "-" : geoNames[g].c_str());
                nw::GeoModel geo;
                const bool haveGeo = g != nw::kNoModel && !cat.read(*geoEntries[g], d) && !nw::parseGeo(d, geo);
                for (int row = 7; row >= 0; --row) {
                    std::printf("   ");
                    for (int col = 0; col < 8; ++col) {
                        int n = 0;
                        for (int l = 0; l < nw::GmfMap::kLayers; ++l)
                            n += sample->layers[size_t(l * 64 + row * 8 + col)] != 0;
                        std::printf("%d", n);
                    }
                    std::printf("   ");
                    for (int col = 0; col < 8 && haveGeo; ++col) {
                        const float cell = nw::kGroundTileSize / 8, x0 = -nw::kGroundTileSize / 2 + col * cell,
                                    y0 = -nw::kGroundTileSize / 2 + row * cell;
                        double z = 0;
                        int k = 0;
                        for (const auto& part : geo.parts)
                            for (const auto& v : part.vertices)
                                if (v.x >= x0 - 1 && v.x <= x0 + cell + 1 && v.y >= y0 - 1 && v.y <= y0 + cell + 1)
                                    z += v.z, ++k;
                        if (k)
                            std::printf("%2d", int(std::lround(z / k / 1000)));
                        else
                            std::printf(" .");
                    }
                    std::printf("\n");
                }
            }
            continue;
        }
        if (!table)
            std::printf("%s (%zu tiles):\n", me->name.c_str(), map.tiles.size());
        auto modelOf = [&](uint32_t number) { return number < order.size() ? order[number] : nw::kNoModel; };
        for (const auto& [number, count] : uses) {
            const size_t given = modelOf(number);
            // Score every candidate in this number's place.
            std::vector<std::pair<double, size_t>> scores;
            unsigned sidesSeen = 0;
            for (size_t g = 0; g < geoNames.size() && heightMode; ++g) {
                if (!candidate[g])
                    continue;
                // -h: the share of the number's tiles' cells whose terrain
                // layer count is the model's height there.
                unsigned met = 0, seen = 0;
                for (const auto& t : map.tiles) {
                    if (t.model != number)
                        continue;
                    for (int cell = 0; cell < 64; ++cell) {
                        ++seen;
                        met += std::abs(levels[g][size_t(t.rotation)][size_t(cell)] - 10 * layerCount(t, cell)) < 10;
                    }
                }
                sidesSeen = std::max(sidesSeen, seen);
                scores.push_back({seen ? double(met) / seen : 0, g});
            }
            for (size_t g = 0; g < geoNames.size() && !heightMode; ++g) {
                if (!candidate[g])
                    continue;
                unsigned met = 0, seen = 0;
                for (const Pair& p : pairs) {
                    const auto& ta = map.tiles[size_t(p.a)];
                    const auto& tb = map.tiles[size_t(p.b)];
                    const bool ina = ta.model == number, inb = tb.model == number;
                    if (ina == inb)
                        continue; // only sides against other numbers
                    const size_t ga = ina ? g : modelOf(ta.model), gb = inb ? g : modelOf(tb.model);
                    if (ga == nw::kNoModel || gb == nw::kNoModel || !candidate[ga] || !candidate[gb])
                        continue;
                    const auto& sa = sides[ga][size_t(ta.rotation)];
                    const auto& sb = sides[gb][size_t(tb.rotation)];
                    const int oa = p.side, ob = p.side + 2;
                    if (!sa.present[oa] || !sb.present[ob])
                        continue;
                    ++seen;
                    met += meets(sa, oa, sb, ob);
                }
                sidesSeen = std::max(sidesSeen, seen);
                scores.push_back({seen ? double(met) / seen : 0, g});
            }
            std::sort(scores.begin(), scores.end(), [](auto& x, auto& y) { return x.first > y.first; });
            if (table) {
                // -t: every candidate, for analysis: map number uses sides model score
                for (const auto& [sc, g] : scores)
                    std::printf("T %s %u %u %u %s %.4f\n", me->name.c_str(), number, count, sidesSeen,
                                shortName(g).c_str(), sc);
                continue;
            }
            double givenScore = -1;
            for (const auto& [sc, g] : scores)
                if (g == given)
                    givenScore = sc;
            const bool best = !scores.empty() && (scores[0].second == given || scores[0].first <= givenScore);
            std::printf("  %3u x%-4u %-10s %5.1f%%   best %-10s %5.1f%%, %-10s %5.1f%%  (%u sides)%s\n", number, count,
                        shortName(given).c_str(), givenScore < 0 ? 0.0 : 100 * givenScore,
                        scores.size() > 0 ? shortName(scores[0].second).c_str() : "", scores.size() > 0 ? 100 * scores[0].first : 0.0,
                        scores.size() > 1 ? shortName(scores[1].second).c_str() : "", scores.size() > 1 ? 100 * scores[1].first : 0.0,
                        sidesSeen, best ? "" : "  <-");
        }
    }
    return 0;
}

// Every map in a planet archive as CSV files for study: <map>_tiles.csv
// (each tile position's model number and name, flags), <map>_cells.csv
// (each block's grid entry) and <map>_objects.csv (placed objects).
int cmdGmfDump(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: reluva-tool gmf-dump <planet.cat> <output dir>\n");
        return 2;
    }
    namespace nw = reluva::newworlds;
    nw::CatArchive cat;
    if (reluva::Error err = cat.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    std::vector<std::string> geoNames;
    for (const auto& e : cat.entries())
        if (e.tag == nw::cattag::Geometry)
            geoNames.push_back(e.name);
    const std::vector<size_t> order = nw::tileModelOrder(geoNames);
    const std::filesystem::path out = argv[3];
    std::filesystem::create_directories(out);
    std::vector<uint8_t> data;
    int maps = 0;
    for (const auto& e : cat.entries()) {
        std::string folder, stem, ext;
        nw::splitName(e.name, folder, stem, ext);
        if (ext != "gmf")
            continue;
        nw::GmfMap map;
        if (cat.read(e, data) || nw::parseGmf(data, map)) {
            std::fprintf(stderr, "%s doesn't decode\n", e.name.c_str());
            continue;
        }
        ++maps;
        std::ofstream tiles(out / (stem + "_tiles.csv")), cells(out / (stem + "_cells.csv")),
            objects(out / (stem + "_objects.csv"));
        tiles << "tx,ty,record,model,name,flags\n";
        for (int ty = 0; ty < map.height * 2; ++ty)
            for (int tx = 0; tx < map.width * 2; ++tx) {
                const int t = nw::tileAt(map, tx, ty);
                if (t < 0)
                    continue;
                const auto& tile = map.tiles[size_t(t)];
                std::string name = "?";
                if (tile.model < order.size() && order[tile.model] != nw::kNoModel) {
                    std::string f, st, ex;
                    nw::splitName(geoNames[order[tile.model]], f, st, ex);
                    name = st;
                }
                tiles << tx << ',' << ty << ',' << t << ',' << tile.model << ',' << name << ',' << tile.flags << '\n';
            }
        cells << "bx,by,block,value\n";
        for (int by = 0; by < map.height; ++by)
            for (int bx = 0; bx < map.width; ++bx) {
                const auto& c = map.cells[size_t(by) * size_t(map.width) + size_t(bx)];
                cells << bx << ',' << by << ',' << c.block << ',' << c.value << '\n';
            }
        objects << "bx,by,x,y,type,turn,variant,extra\n";
        for (const auto& o : map.scenery)
            objects << o.bx << ',' << o.by << ',' << int(o.x) << ',' << int(o.y) << ',' << int(o.type) << ','
                    << int(o.turn) << ',' << int(o.variant) << ',' << int(o.extra) << '\n';
    }
    std::printf("%d maps written to %s\n", maps, argv[3]);
    return 0;
}

const Commands commands("newworlds", {
    {"level-info", "<level.dat> [file.cat...]",
     "list the missions, with the archives holding their maps",
     cmdLevelInfo},
    {"gmf-info", "<file.gmf> [record number]",
     "walk a map's records, summarise them, and dump one record (or where the walk stops)",
     cmdGmfInfo},
    {"gmf-fit", "[-m|-h|-l|-r|-j|-t|-a|-o file] <planet.cat> [map]",
     "check the model numbering by how tiles' edges meet",
     cmdGmfFit},
    {"gmf-dump", "<planet.cat> <output dir>",
     "write each map's tiles, grid and objects as CSV",
     cmdGmfDump},
});

} // namespace
