// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's New Worlds commands for its models (.geo, .mdl).

#include "tools/tool.h"

#include "common/font.h"
#include "engines/newworlds/cat_archive.h"
#include "engines/newworlds/geo.h"
#include "engines/newworlds/mdl.h"
#include "engines/newworlds/model_render.h"
#include "engines/newworlds/model_textures.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace reluva::tool;

bool writeObj(const reluva::newworlds::GeoModel& m, const std::string& path) {
    std::ofstream f(path);
    f << "# exported by reluva-tool newworlds geo-info\n";
    // OBJ numbers vertices across the whole file, so offset each part's.
    std::vector<size_t> base;
    size_t next = 1;
    for (const auto& part : m.parts) {
        base.push_back(next);
        next += part.vertices.size();
        for (const auto& v : part.vertices)
            f << "v " << v.x << ' ' << v.y << ' ' << v.z << '\n';
    }
    size_t vt = 1;
    for (size_t pi = 0; pi < m.parts.size(); ++pi) {
        f << "g part" << pi << '\n';
        uint32_t slot = ~0u;
        for (const auto& face : m.faces) {
            if (face.part != pi)
                continue;
            // The face's texture number, as a material name.
            if (face.corners.back().extra[4] != slot) {
                slot = face.corners.back().extra[4];
                f << "usemtl slot" << slot << '\n';
            }
            for (const auto& c : face.corners)
                f << "vt " << c.u << ' ' << 1.0f - c.v << '\n';
            f << 'f';
            for (const auto& c : face.corners)
                f << ' ' << base[pi] + c.vertex << '/' << vt++;
            f << '\n';
        }
    }
    return bool(f);
}

int cmdGeoInfo(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool newworlds geo-info <file.geo> [out.obj]\n");
        return 2;
    }
    Bytes d;
    if (!readFile(argv[2], d))
        return 1;
    reluva::newworlds::GeoModel m;
    if (reluva::Error err = reluva::newworlds::parseGeo(d, m)) {
        std::printf("doesn't decode: %s\n", err.message.c_str());
        return 1;
    }
    size_t corners = 0;
    for (const auto& face : m.faces)
        corners += face.corners.size();
    std::printf("%zu faces (%zu corners), %zu part(s)\n", m.faces.size(), corners, m.parts.size());
    for (size_t pi = 0; pi < m.parts.size(); ++pi) {
        const auto& part = m.parts[pi];
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (const auto& v : part.vertices) {
            const float c[3] = {v.x, v.y, v.z};
            for (int i = 0; i < 3; ++i) {
                lo[i] = std::min(lo[i], c[i]);
                hi[i] = std::max(hi[i], c[i]);
            }
        }
        size_t faces = 0;
        for (const auto& face : m.faces)
            faces += face.part == pi;
        std::string lists;
        for (const auto& list : part.normalLists)
            lists += (lists.empty() ? "" : "+") + std::to_string(list.size());
        std::printf("  part %zu (%u): %zu faces, %zu vertices, %zu edges, normal lists of %s; x %g..%g, y %g..%g, "
                    "z %g..%g\n",
                    pi, part.node, faces, part.vertices.size(), part.edges.size(),
                    lists.empty() ? "none" : lists.c_str(), double(lo[0]),
                    double(hi[0]), double(lo[1]), double(hi[1]), double(lo[2]), double(hi[2]));
    }
    std::printf("centre %g, %g; radius %g\n", double(m.centreX), double(m.centreY), double(m.radius));
    for (size_t i = 0; i < m.attachments.size(); ++i) {
        const auto& a = m.attachments[i];
        std::printf("  attachment %zu at (%g, %g, %g); frame", i, double(a.position.x), double(a.position.y),
                    double(a.position.z));
        for (float f : a.frame)
            std::printf(" %.3g", double(f));
        std::printf("\n");
    }
    // How often each value of the faces' second header word occurs (face 0's
    // header is the file header, so it's left out).
    std::map<uint32_t, unsigned> seconds;
    for (size_t i = 1; i < m.faces.size(); ++i)
        ++seconds[m.faces[i].groupB];
    std::printf("faces' second header value (value x count):");
    for (const auto& [value, n] : seconds)
        std::printf(" %u x%u", value, n);
    std::printf("\n");
    // The last value of each face's last corner: a texture number?
    std::map<uint32_t, unsigned> tails;
    for (const auto& face : m.faces)
        ++tails[face.corners.back().extra[4]];
    std::printf("last value of each face's last corner (value x count):");
    for (const auto& [value, n] : tails)
        std::printf(" %u x%u", value, n);
    std::printf("\n");

    // Test: is the second value an index into the face's part's first normal
    // list, giving the face's normal? Compare with the normal worked out from
    // the corners (Newell's method), either way round.
    unsigned same = 0, opposite = 0, other = 0, outOfRange = 0;
    for (size_t i = 1; i < m.faces.size(); ++i) {
        const auto& face = m.faces[i];
        const auto& part = m.parts[face.part];
        if (part.normalLists.empty() || face.groupB >= part.normalLists[0].size()) {
            ++outOfRange;
            continue;
        }
        double n[3] = {0, 0, 0};
        for (size_t k = 0; k < face.corners.size(); ++k) {
            const auto& a = part.vertices[face.corners[k].vertex];
            const auto& b = part.vertices[face.corners[(k + 1) % face.corners.size()].vertex];
            n[0] += double(a.y - b.y) * double(a.z + b.z);
            n[1] += double(a.z - b.z) * double(a.x + b.x);
            n[2] += double(a.x - b.x) * double(a.y + b.y);
        }
        const double len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        const auto& s = part.normalLists[0][face.groupB];
        const double d = len > 0 ? (n[0] * s.x + n[1] * s.y + n[2] * s.z) / len : 0;
        if (d > 0.99)
            ++same;
        else if (d < -0.99)
            ++opposite;
        else
            ++other;
    }
    std::printf("second value as an index into the part's first normal list: %u match the face's normal, "
                "%u the opposite, %u neither, %u out of range\n",
                same, opposite, other, outOfRange);
    if (argc > 3) {
        if (!writeObj(m, argv[3])) {
            std::fprintf(stderr, "cannot write %s\n", argv[3]);
            return 1;
        }
        std::printf("wrote %s\n", argv[3]);
    }
    return 0;
}

int cmdGeoFaces(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool newworlds geo-faces <file.geo>\n");
        return 2;
    }
    Bytes d;
    if (!readFile(argv[2], d))
        return 1;
    // Same walk as parseGeo, but any fourth header word is accepted and shown.
    const uint32_t faceCount = rd32(d, 4);
    std::printf("%zu bytes, %u faces\n", d.size(), faceCount);
    size_t p = 0;
    for (uint32_t i = 0; i < faceCount; ++i) {
        const uint32_t n = rd32(d, p + 8);
        if (n < 3 || n > 256 || p + 16 + size_t(n) * 32 > d.size()) {
            std::printf("face %u at 0x%zx: bad header (%u, %u, %u, %u); bytes around it:\n", i, p, rd32(d, p),
                        rd32(d, p + 4), n, rd32(d, p + 12));
            hexdump(d, p >= 0x60 ? p - 0x60 : 0, 0x160);
            return 1;
        }
        std::printf("face %u at 0x%zx: (%u, %u, %u, %u)\n", i, p, rd32(d, p), rd32(d, p + 4), n, rd32(d, p + 12));
        p += 16;
        for (uint32_t k = 0; k < n; ++k, p += 32) {
            float u, v;
            const uint32_t ub = rd32(d, p + 4), vb = rd32(d, p + 8);
            std::memcpy(&u, &ub, 4);
            std::memcpy(&v, &vb, 4);
            std::printf("    v %-4u uv %9.4f %9.4f  rest %u %u %u %u %u\n", rd32(d, p), double(u / 65536),
                        double(v / 65536), rd32(d, p + 12), rd32(d, p + 16), rd32(d, p + 20), rd32(d, p + 24),
                        rd32(d, p + 28));
        }
    }
    std::printf("faces end at 0x%zx; next bytes:\n", p);
    hexdump(d, p, 0x100);
    return 0;
}

// Per texture number of each model: how many faces use it, their area, how
// they face (share of area facing up, down or sideways) and the range of
// their texture coordinates, to tell what kind of texture each must be.
int cmdGeoSlots(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool newworlds geo-slots <file.geo>...\n");
        return 2;
    }
    namespace nw = reluva::newworlds;
    for (int a = 2; a < argc; ++a) {
        Bytes d;
        nw::GeoModel geo;
        if (!readFile(argv[a], d) || nw::parseGeo(d, geo)) {
            std::printf("%s: doesn't read\n", argv[a]);
            continue;
        }
        struct Slot { unsigned faces = 0; double area = 0, up = 0, down = 0, tilt = 0; float u0 = 1e9f, u1 = -1e9f, v0 = 1e9f, v1 = -1e9f; };
        std::map<uint32_t, Slot> slots;
        for (const auto& f : geo.faces) {
            Slot& sl = slots[f.corners.back().extra[4]];
            ++sl.faces;
            const auto& vs = geo.parts[f.part].vertices;
            double nx = 0, ny = 0, nz = 0;
            for (size_t i = 0; i < f.corners.size(); ++i) {
                const auto& p = vs[f.corners[i].vertex];
                const auto& q = vs[f.corners[(i + 1) % f.corners.size()].vertex];
                nx += (p.y - q.y) * (p.z + q.z);
                ny += (p.z - q.z) * (p.x + q.x);
                nz += (p.x - q.x) * (p.y + q.y);
                sl.u0 = std::min(sl.u0, f.corners[i].u), sl.u1 = std::max(sl.u1, f.corners[i].u);
                sl.v0 = std::min(sl.v0, f.corners[i].v), sl.v1 = std::max(sl.v1, f.corners[i].v);
            }
            const double area = std::sqrt(nx * nx + ny * ny + nz * nz) / 2;
            sl.area += area;
            if (area > 0) {
                const double c = nz / (2 * area); // faces are clockwise from the front, so -c is up
                sl.tilt += std::fabs(c) * area;  // |cos| of the angle from vertical axis, area-weighted
                if (-c > 0.7)
                    sl.up += area;
                else if (c > 0.7)
                    sl.down += area;
            }
        }
        std::printf("%s:\n", argv[a]);
        for (const auto& [n, sl] : slots)
            std::printf("  %u: %3u faces, area %9.0f, up %3.0f%% down %3.0f%%, u %5.2f..%5.2f v %5.2f..%5.2f, tilt %.2f\n", n,
                        sl.faces, sl.area, sl.area ? 100 * sl.up / sl.area : 0, sl.area ? 100 * sl.down / sl.area : 0,
                        sl.u0, sl.u1, sl.v0, sl.v1, sl.area ? sl.tilt / sl.area : 0);
    }
    return 0;
}

// Renders models to a contact sheet (a PPM image), textured as the model
// viewer shows them, two views each with its name: every main model (not
// lod/ or a variant) whose name holds the filter, from the
// archives given. Textures are found across all of them.
int cmdGeoSheet(int argc, char** argv) {
    // -l: larger cells, four views each, a model a row; -t: eight views
    // round it at the briefings' height.
    const bool turntable = argc >= 3 && std::strcmp(argv[2], "-t") == 0;
    const bool large = turntable || (argc >= 3 && std::strcmp(argv[2], "-l") == 0);
    if (large)
        --argc, ++argv;
    if (argc < 5) {
        std::fprintf(stderr, "usage: reluva-tool newworlds geo-sheet [-l|-t] <out.ppm> <name filter> <file.cat>...\n");
        return 2;
    }
    namespace nw = reluva::newworlds;
    nw::Archives archives;
    for (int i = 4; i < argc; ++i) {
        auto cat = std::make_unique<nw::CatArchive>();
        if (reluva::Error err = cat->open(argv[i])) {
            std::fprintf(stderr, "%s: %s\n", argv[i], err.message.c_str());
            return 1;
        }
        archives.push_back(std::move(cat));
    }
    const std::vector<nw::TextureFile> textures = nw::listTextureFiles(archives);
    const std::string filter = argv[3];
    std::vector<std::string> names;
    for (const auto& a : archives)
        for (const auto& e : a->entries()) {
            std::string folder, stem, ext;
            nw::splitName(e.name, folder, stem, ext);
            // Variants: damaged (_d, _ad), low detail (_l), shadow (_s) and
            // placement outline (_p).
            const bool variant = stem.size() > 2 && (stem.ends_with("_d") || stem.ends_with("_l") || stem.ends_with("_s") ||
                                                     stem.ends_with("_p") ||
                                                     (stem.ends_with("d") && stem[stem.size() - 3] == '_'));
            if (e.tag == nw::cattag::Geometry && e.name.find("/lod/") == std::string::npos && !variant &&
                e.name.find(filter) != std::string::npos)
                names.push_back(e.name);
        }
    if (names.empty()) {
        std::fprintf(stderr, "no models match %s\n", filter.c_str());
        return 1;
    }
    const int kCellW = large ? 320 : 200, kCellH = large ? 240 : 150, kPerRow = large ? 1 : 4, kViews = turntable ? 8 : large ? 4 : 2;
    const int rows = int((names.size() + size_t(kPerRow) - 1) / size_t(kPerRow));
    reluva::Surface sheet;
    sheet.create(kPerRow * kViews * kCellW, rows * kCellH, reluva::PixelFormat::RGBA32);
    reluva::Surface cell;
    cell.create(kCellW, kCellH, reluva::PixelFormat::RGBA32);
    nw::Rasterizer raster;
    for (size_t i = 0; i < names.size(); ++i) {
        nw::TexturedModel m;
        const bool ok = nw::loadTexturedModel(archives, textures, names[i], m);
        for (int view = 0; view < kViews; ++view) {
            for (size_t p = 0; p < cell.pixels.size(); p += 4)
                cell.pixels[p] = 0, cell.pixels[p + 1] = 0, cell.pixels[p + 2] = 24, cell.pixels[p + 3] = 255;
            static constexpr float kYaw[4] = {0.6f, 2.7f, 1.6f, 4.2f}, kPitch[4] = {0.35f, 0.9f, 0.15f, 0.35f};
            if (ok)
                nw::drawModelView(cell, raster, m, turntable ? float(view) * 0.7854f : kYaw[view],
                                  turntable ? 0.35f : kPitch[view]);
            if (view == 0) {
                std::string folder, stem, ext;
                nw::splitName(names[i], folder, stem, ext);
                reluva::font::drawText(cell, 3, 3, ok ? stem : stem + " ?", {255, 230, 80});
            }
            const int x0 = int(i % size_t(kPerRow)) * kViews * kCellW + view * kCellW, y0 = int(i / size_t(kPerRow)) * kCellH;
            for (int y = 0; y < kCellH; ++y)
                std::memcpy(sheet.row(y0 + y) + x0 * 4, cell.row(y), size_t(kCellW) * 4);
        }
    }
    std::ofstream out(argv[2], std::ios::binary);
    out << "P6\n" << sheet.w << ' ' << sheet.h << "\n255\n";
    for (int y = 0; y < sheet.h; ++y)
        for (int x = 0; x < sheet.w; ++x)
            out.write(reinterpret_cast<const char*>(sheet.row(y) + x * 4), 3);
    std::printf("%zu models drawn to %s\n", names.size(), argv[2]);
    return out ? 0 : 1;
}

int cmdGeoTextures(int argc, char** argv) {
    int first = 2;
    const bool all = argc > 2 && std::strcmp(argv[2], "-a") == 0;
    if (all)
        ++first;
    if (argc <= first) {
        std::fprintf(stderr, "usage: reluva-tool newworlds geo-textures [-a] <file.cat>...\n");
        return 2;
    }
    namespace nw = reluva::newworlds;
    nw::Archives archives;
    for (int a = first; a < argc; ++a) {
        auto cat = std::make_unique<nw::CatArchive>();
        if (reluva::Error err = cat->open(argv[a])) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 1;
        }
        archives.push_back(std::move(cat));
    }
    const std::vector<nw::TextureFile> textures = nw::listTextureFiles(archives);
    unsigned models = 0, without = 0, short_ = 0;
    std::vector<uint8_t> data;
    for (const auto& cat : archives)
        for (const auto& e : cat->entries()) {
            if (e.tag != nw::cattag::Geometry)
                continue;
            ++models;
            // How many textures the faces number: the highest number + 1.
            uint32_t needed = 0;
            nw::GeoModel m;
            if (!cat->read(e, data) && !nw::parseGeo(data, m))
                for (const auto& face : m.faces)
                    needed = std::max(needed, face.corners.back().extra[4] + 1);
            auto slots = nw::findModelTextures(e.name, textures, needed);
            nw::addDoorTextures(m, e.name, textures, slots);
            nw::addPadTextures(m, e.name, textures, slots);
            const size_t found = size_t(std::count_if(slots.begin(), slots.end(), [](auto* t) { return t; }));
            const bool tooFew = found < needed;
            if (found == 0)
                ++without;
            else if (tooFew)
                ++short_;
            if (!all && !tooFew)
                continue;
            std::printf("%s: needs %u, found", e.name.c_str(), needed);
            for (const auto* t : slots)
                std::printf(" %s", t ? t->stem.c_str() : "?");
            if (slots.empty())
                std::printf(" none");
            if (tooFew) {
                // Textures in the same folder whose names start like the model's.
                std::string folder, stem, ext;
                nw::splitName(e.name, folder, stem, ext);
                const size_t prefix = std::min<size_t>(3, stem.size());
                std::printf("; similar:");
                unsigned shown = 0;
                for (const auto& t : textures)
                    if (t.folder == folder && t.stem.compare(0, prefix, stem, 0, prefix) == 0 && shown++ < 10)
                        std::printf(" %s", t.stem.c_str());
                if (!shown)
                    std::printf(" -");
            }
            std::printf("\n");
        }
    std::printf("%u models: %u without textures, %u with fewer than their faces number\n", models, without,
                short_);
    return 0;
}

int cmdGeoSurvey(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool newworlds geo-survey <file.cat>...\n");
        return 2;
    }
    std::map<std::string, std::vector<std::string>> failures;
    unsigned total = 0, ok = 0;
    std::vector<uint8_t> d;
    for (int a = 2; a < argc; ++a) {
        reluva::newworlds::CatArchive cat;
        if (reluva::Error err = cat.open(argv[a])) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 1;
        }
        for (const auto& e : cat.entries()) {
            if (e.tag != reluva::newworlds::cattag::Geometry || cat.read(e, d))
                continue;
            ++total;
            reluva::newworlds::GeoModel m;
            reluva::Error err = reluva::newworlds::parseGeo(d, m);
            if (!err) {
                ++ok;
                continue;
            }
            // Group by message with numbers (decimal or 0x hex) removed.
            std::string key;
            const std::string& msg = err.message;
            for (size_t i = 0; i < msg.size();) {
                if (msg[i] >= '0' && msg[i] <= '9') {
                    if (msg.compare(i, 2, "0x") == 0)
                        i += 2;
                    while (i < msg.size() && std::isxdigit(static_cast<unsigned char>(msg[i])))
                        ++i;
                    key += '#';
                } else {
                    key += msg[i++];
                }
            }
            failures[key].push_back(std::string(argv[a]) + ": " + e.name + " (" + std::to_string(d.size()) +
                                    " bytes): " + err.message);
        }
    }
    std::printf("%u models, %u decode\n\n", total, ok);
    for (const auto& [key, list] : failures) {
        std::printf("%5zu  %s\n", list.size(), key.c_str());
        for (size_t i = 0; i < list.size() && i < 3; ++i)
            std::printf("       e.g. %s\n", list[i].c_str());
    }
    return 0;
}

int cmdMdlInfo(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: reluva-tool newworlds mdl-info <file.mdl | file.cat>\n");
        return 2;
    }
    namespace nw = reluva::newworlds;
    auto list = [](const char* what, const std::vector<nw::MdlModel::Name>& names) {
        if (names.empty())
            return;
        std::printf("  %s:", what);
        for (const auto& n : names)
            std::printf(" %s=%d", n.name.c_str(), n.value);
        std::printf("\n");
    };
    const std::string path = argv[2];
    if (path.size() > 4 && path.compare(path.size() - 4, 4, ".cat") == 0) {
        nw::CatArchive cat;
        if (reluva::Error err = cat.open(path)) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 1;
        }
        std::map<std::string, unsigned> states;
        unsigned count = 0, failed = 0;
        std::vector<uint8_t> data;
        for (const auto& e : cat.entries()) {
            if (e.tag != nw::cattag::Mdl)
                continue;
            ++count;
            nw::MdlModel m;
            reluva::Error err = cat.read(e, data);
            if (!err)
                err = nw::parseMdl(data, m);
            if (err) {
                std::printf("%s: %s\n", e.name.c_str(), err.message.c_str());
                ++failed;
                continue;
            }
            std::string key;
            for (const auto& s : m.states)
                key += (key.empty() ? "" : ", ") + (s.empty() ? std::string("(unnamed)") : s);
            ++states[key.empty() ? "(none listed)" : key];
        }
        std::printf("%u metamodels, %u decode\nstates:\n", count, count - failed);
        for (const auto& [k, n] : states)
            std::printf("  %4u  %s\n", n, k.c_str());
        return failed ? 1 : 0;
    }
    std::vector<uint8_t> data;
    if (!readFile(path.c_str(), data)) {
        std::fprintf(stderr, "cannot open %s\n", path.c_str());
        return 1;
    }
    nw::MdlModel m;
    if (reluva::Error err = nw::parseMdl(data, m)) {
        std::printf("%s\n", err.message.c_str());
        return 1;
    }
    std::printf("\"%s\"\n", m.title.c_str());
    list("parts", m.parts);
    list("points", m.points);
    list("places", m.places);
    list("animated", m.animated);
    std::printf("  states:");
    for (const auto& s : m.states)
        std::printf(" \"%s\"", s.c_str());
    std::printf("\n");
    for (size_t i = 0; i < m.reference.size(); ++i) {
        const auto& t = m.reference[i];
        std::string name = "#" + std::to_string(i);
        for (const auto& a : m.animated)
            if (a.value == int32_t(i))
                name = a.name;
        std::printf("  reference pose %zu %-16s at (%g, %g, %g)\n", i, name.c_str(), double(t.position.x),
                    double(t.position.y), double(t.position.z));
    }
    return 0;
}

const Commands commands("newworlds", {
    {"geo-info", "<file.geo> [out.obj]",
     "decode a model; optionally export it as OBJ",
     cmdGeoInfo},
    {"geo-faces", "<file.geo>",
     "walk a model's faces, showing each header and corner, and dump the bytes where the walk stops",
     cmdGeoFaces},
    {"geo-slots", "<file.geo>...",
     "per texture number: faces, area, facing, texture coordinates",
     cmdGeoSlots},
    {"geo-sheet", "[-l|-t] <out.ppm> <name filter> <file.cat>...",
     "draw models to a contact sheet, two views each (-l: larger, four; -t: eight round it)",
     cmdGeoSheet},
    {"geo-textures", "[-a] <file.cat>...",
     "show which textures each model gets (by default only models with too few, with similar names)",
     cmdGeoTextures},
    {"geo-survey", "<file.cat>...",
     "decode every model, group failures by reason",
     cmdGeoSurvey},
    {"mdl-info", "<file.mdl | file.cat>",
     "show a metamodel's names and states, or survey every one in an archive",
     cmdMdlInfo},
});

} // namespace
