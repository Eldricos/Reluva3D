// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's New Worlds commands for its textures (.lbp).

#include "tools/tool.h"

#include "engines/newworlds/cat_archive.h"
#include "engines/newworlds/geo.h"
#include "engines/newworlds/ground_textures.h"
#include "engines/newworlds/model_textures.h"
#include "engines/newworlds/texture.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace reluva::tool;

// 8-bit paletted BMP, rows top-down. swapRB picks which palette byte is red.
bool writeBmp(const std::string& path, int w, int h, const uint8_t* palette, int paletteCount, const uint8_t* pixels,
              bool swapRB) {
    const int stride = (w + 3) & ~3;
    const uint32_t paletteBytes = 256 * 4, dataOffset = 14 + 40 + paletteBytes;
    const uint32_t fileSize = dataOffset + uint32_t(stride * h);
    Bytes out;
    auto p16 = [&](uint32_t v) { out.push_back(uint8_t(v)); out.push_back(uint8_t(v >> 8)); };
    auto p32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i))); };
    out.push_back('B');
    out.push_back('M');
    p32(fileSize); p32(0); p32(dataOffset);
    p32(40); p32(uint32_t(w)); p32(uint32_t(-h)); p16(1); p16(8); p32(0); p32(uint32_t(stride * h));
    p32(2835); p32(2835); p32(256); p32(0);
    for (int i = 0; i < 256; ++i) {
        uint8_t c0 = 0, c1 = 0, c2 = 0;
        if (i < paletteCount) {
            c0 = palette[i * 4];
            c1 = palette[i * 4 + 1];
            c2 = palette[i * 4 + 2];
        }
        // BMP palettes are stored blue, green, red, 0.
        uint8_t r = swapRB ? c2 : c0, b = swapRB ? c0 : c2;
        out.push_back(b);
        out.push_back(c1);
        out.push_back(r);
        out.push_back(0);
    }
    for (int y = 0; y < h; ++y) {
        out.insert(out.end(), pixels + size_t(y) * size_t(w), pixels + size_t(y + 1) * size_t(w));
        out.resize(out.size() + size_t(stride - w), 0);
    }
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
    return bool(f);
}

int cmdLbpInfo(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool newworlds lbp-info <file> [output prefix]\n");
        return 2;
    }
    Bytes d;
    if (!readFile(argv[2], d))
        return 1;
    reluva::newworlds::Texture tex;
    if (reluva::Error err = reluva::newworlds::decodeTexture(d, tex)) {
        std::printf("doesn't decode: %s\nfirst bytes:\n", err.message.c_str());
        hexdump(d, 0, 64);
        return 1;
    }
    const uint32_t paletteCount = rd32(d, 4);
    const size_t header = 8 + size_t(paletteCount) * 4;
    const size_t pixels = size_t(tex.width) * size_t(tex.height);
    const size_t indices = header + (tex.colourKeyed ? 16 : 12);
    auto colour = [&](unsigned i) { // palette entries are blue, green, red, 0
        char buf[32];
        const uint8_t* e = &d[8 + size_t(i) * 4];
        std::snprintf(buf, sizeof buf, "#%02x%02x%02x", e[2], e[1], e[0]);
        return std::string(buf);
    };

    std::printf("format %u, %d x %d, palette %u entries\n", tex.format, tex.width, tex.height, paletteCount);
    if (tex.colourKeyed)
        std::printf("colour key: index %u = %s\n", tex.keyIndex,
                    tex.keyIndex < paletteCount ? colour(tex.keyIndex).c_str() : "(outside the palette)");
    std::printf("header after the palette:\n");
    hexdump(d, header, tex.colourKeyed ? 16 : 12);

    unsigned hist[256] = {};
    for (size_t i = 0; i < pixels; ++i)
        ++hist[d[indices + i]];
    std::vector<unsigned> order;
    for (unsigned i = 0; i < 256; ++i)
        if (hist[i])
            order.push_back(i);
    std::sort(order.begin(), order.end(), [&](unsigned x, unsigned y) { return hist[x] > hist[y]; });
    std::printf("most used indices:\n");
    for (size_t k = 0; k < order.size() && k < 6; ++k)
        std::printf("  index %3u %s  %6u pixels%s\n", order[k], order[k] < paletteCount ? colour(order[k]).c_str() : "?",
                    hist[order[k]], tex.colourKeyed && order[k] == tex.keyIndex ? "  <- key" : "");

    std::printf("palette entries that look like key colours (magenta or bright green):\n ");
    for (unsigned i = 0; i < paletteCount; ++i) {
        const uint8_t* e = &d[8 + size_t(i) * 4];
        const uint8_t r = e[2], g = e[1], b = e[0];
        if ((r > 200 && b > 200 && g < 60) || (g > 150 && r < 40 && b < 40))
            std::printf(" %u=%s(%u px)", i, colour(i).c_str(), hist[i]);
    }
    std::printf("\n");

    if (argc > 3) {
        const std::string prefix = argv[3];
        Bytes alpha(pixels);
        for (size_t i = 0; i < pixels; ++i)
            alpha[i] = tex.alpha.empty() ? tex.rgba[i * 4 + 3] : tex.alpha[i];
        uint8_t grey[256 * 4];
        for (int i = 0; i < 256; ++i)
            grey[i * 4] = grey[i * 4 + 1] = grey[i * 4 + 2] = uint8_t(i), grey[i * 4 + 3] = 0;
        const bool ok = writeBmp(prefix + ".bmp", tex.width, tex.height, d.data() + 8, int(paletteCount),
                                 d.data() + indices, true) &&
                        writeBmp(prefix + "_alpha.bmp", tex.width, tex.height, grey, 256, alpha.data(), false);
        std::printf(ok ? "wrote %s.bmp and %s_alpha.bmp\n" : "failed writing %s.bmp\n", prefix.c_str(), prefix.c_str());
        return ok ? 0 : 1;
    }
    return 0;
}

// Decode every texture (tag 0x16) in the given archives and group them by
// what their header holds, to find the layouts the decoder doesn't handle.
int cmdTexSurvey(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool newworlds tex-survey <file.cat>...\n");
        return 2;
    }
    struct Group {
        unsigned count = 0;
        std::map<std::string, unsigned> extensions;
        std::vector<std::string> examples;
        Bytes firstBytes;
    };
    std::map<std::string, Group> groups;
    unsigned total = 0, decoded = 0;
    std::vector<uint8_t> data;
    for (int a = 2; a < argc; ++a) {
        reluva::newworlds::CatArchive cat;
        if (reluva::Error err = cat.open(argv[a])) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 1;
        }
        for (const auto& e : cat.entries()) {
            if (e.tag != reluva::newworlds::cattag::Texture)
                continue;
            if (cat.read(e, data))
                continue;
            ++total;
            reluva::newworlds::Texture tex;
            const bool ok = !reluva::newworlds::decodeTexture(data, tex);
            decoded += ok;

            // Describe the header: format, palette count, the three u32s
            // after the palette, and data size relative to width x height.
            char key[160];
            const uint32_t format = data.size() >= 4 ? rd32(data, 0) : 0;
            const uint32_t pal = data.size() >= 8 ? rd32(data, 4) : 0;
            const size_t hdr = 8 + size_t(pal) * 4;
            if (pal > 256 || hdr + 12 > data.size()) {
                std::snprintf(key, sizeof key, "%s format %u, u32@4 %u: palette doesn't fit", ok ? "OK  " : "FAIL",
                              format, pal);
            } else {
                const uint32_t f0 = rd32(data, hdr), w = rd32(data, hdr + 4), h = rd32(data, hdr + 8);
                if (f0 == 1 && hdr + 16 <= data.size()) {
                    // Colour-keyed: u32 1, key index, width, height.
                    const uint32_t kw = rd32(data, hdr + 8), kh = rd32(data, hdr + 12);
                    const size_t layers = format == 6 ? 2 : 1;
                    const bool fits = size_t(kw) * kh * layers == data.size() - hdr - 16;
                    std::snprintf(key, sizeof key, "%s format %u, palette %u, colour-keyed, size %s",
                                  ok ? "OK  " : "FAIL", format, pal, fits ? "fits" : "doesn't fit");
                    goto grouped;
                }
                const size_t rest = data.size() - hdr - 12;
                char ratio[48];
                if (w && h && w <= 4096 && h <= 4096 && rest % (size_t(w) * h) == 0)
                    std::snprintf(ratio, sizeof ratio, "data = %zu x w*h", rest / (size_t(w) * h));
                else if (w && h && w <= 4096 && h <= 4096)
                    std::snprintf(ratio, sizeof ratio, "data = %.3f x w*h", double(rest) / (double(w) * h));
                else
                    std::snprintf(ratio, sizeof ratio, "w/h not plausible");
                std::snprintf(key, sizeof key, "%s format %u, palette %u, u32 after palette %u, %s", ok ? "OK  " : "FAIL",
                              format, pal, f0, ratio);
            }
        grouped:
            Group& g = groups[key];
            ++g.count;
            ++g.extensions[extensionOf(e.name)];
            if (g.examples.size() < 3)
                g.examples.push_back(std::string(argv[a]) + ": " + e.name + " (" + std::to_string(data.size()) + " bytes)");
            if (g.firstBytes.empty())
                g.firstBytes.assign(data.begin(), data.begin() + std::min<size_t>(data.size(), 48));
        }
    }
    std::printf("%u textures, %u decode with the current rules\n\n", total, decoded);
    for (const auto& [key, g] : groups) {
        std::printf("%5u  %s\n       extensions:", g.count, key.c_str());
        for (const auto& [ext, n] : g.extensions)
            std::printf(" %s x%u", ext.empty() ? "(none)" : ext.c_str(), n);
        std::printf("\n");
        for (const std::string& ex : g.examples)
            std::printf("       e.g. %s\n", ex.c_str());
        if (key.rfind("FAIL", 0) == 0)
            hexdump(g.firstBytes, 0, g.firstBytes.size());
        std::printf("\n");
    }
    return 0;
}

// For every colour-keyed texture, compare the key index with the corner
// pixels, which in sprites and interface images are nearly always background.
int cmdTexKeys(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool newworlds tex-keys <file.cat>...\n");
        return 2;
    }
    unsigned total = 0, keyIsCorner = 0, cornerLooksKey = 0;
    std::vector<uint8_t> d;
    std::printf("%-44s %3s %4s %-8s %6s  %-14s %s\n", "texture", "fmt", "key", "colour", "px", "corners", "corner colour");
    for (int a = 2; a < argc; ++a) {
        reluva::newworlds::CatArchive cat;
        if (reluva::Error err = cat.open(argv[a])) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 1;
        }
        for (const auto& e : cat.entries()) {
            if (e.tag != reluva::newworlds::cattag::Texture || cat.read(e, d))
                continue;
            reluva::newworlds::Texture tex;
            if (reluva::newworlds::decodeTexture(d, tex) || !tex.colourKeyed)
                continue;
            ++total;
            const uint32_t pal = rd32(d, 4);
            const size_t idx = 8 + size_t(pal) * 4 + 16;
            const size_t w = size_t(tex.width), h = size_t(tex.height);
            const uint8_t corners[4] = {d[idx], d[idx + w - 1], d[idx + (h - 1) * w], d[idx + h * w - 1]};
            auto colour = [&](unsigned i) {
                char buf[16];
                if (i >= pal)
                    return std::string("?");
                const uint8_t* c = &d[8 + size_t(i) * 4];
                std::snprintf(buf, sizeof buf, "#%02x%02x%02x", c[2], c[1], c[0]);
                return std::string(buf);
            };
            unsigned keyPixels = 0;
            for (size_t i = 0; i < w * h; ++i)
                keyPixels += d[idx + i] == tex.keyIndex;
            const bool allSame = corners[0] == corners[1] && corners[0] == corners[2] && corners[0] == corners[3];
            const bool match = allSame && corners[0] == tex.keyIndex;
            keyIsCorner += match;
            const uint8_t* cc = corners[0] < pal ? &d[8 + size_t(corners[0]) * 4] : nullptr;
            const bool looksKey = cc && ((cc[2] > 200 && cc[0] > 200 && cc[1] < 60) || (cc[1] > 150 && cc[2] < 40 && cc[0] < 40));
            cornerLooksKey += looksKey;
            char cornerText[24];
            std::snprintf(cornerText, sizeof cornerText, "%u %u %u %u", corners[0], corners[1], corners[2], corners[3]);
            std::string name = e.name.size() > 44 ? "..." + e.name.substr(e.name.size() - 41) : e.name;
            std::printf("%-44s %3u %4u %-8s %6u  %-14s %s%s%s\n", name.c_str(), tex.format, tex.keyIndex,
                        colour(tex.keyIndex).c_str(), keyPixels, cornerText, colour(corners[0]).c_str(),
                        match ? "  key = corners" : "", looksKey ? "  (key-like colour)" : "");
        }
    }
    std::printf("\n%u colour-keyed textures: key index = all four corners in %u; corner colour is magenta/green in %u\n",
                total, keyIsCorner, cornerLooksKey);
    return 0;
}

int cmdGroundTextures(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: reluva-tool newworlds ground-textures <planet.cat>\n");
        return 2;
    }
    namespace nw = reluva::newworlds;
    nw::CatArchive cat;
    if (reluva::Error err = cat.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    std::vector<uint8_t> data;
    for (const auto& e : cat.entries()) {
        std::string folder, stem, ext;
        nw::splitName(e.name, folder, stem, ext);
        static const char* const kScenery[] = {"tree", "tree_s", "rock", "frn_spec", "bush"};
        if (e.tag != nw::cattag::Geometry || e.name.find("/map/") == std::string::npos ||
            e.name.find("/lod/") != std::string::npos ||
            std::any_of(std::begin(kScenery), std::end(kScenery), [&](const char* n) { return stem == n; }))
            continue;
        std::vector<std::string> planet;
        for (const auto& t : cat.entries()) {
            std::string tf, ts, te;
            nw::splitName(t.name, tf, ts, te);
            if (t.tag == nw::cattag::Texture && tf == folder && (te == "mp0" || te == "lbp"))
                planet.push_back(ts);
        }
        nw::GeoModel m;
        if (cat.read(e, data) || nw::parseGeo(data, m)) {
            std::printf("%-12s doesn't decode\n", stem.c_str());
            continue;
        }
        const auto picks = nw::chooseGroundTextures(stem, m, planet);
        std::printf("%-12s", stem.c_str());
        for (size_t s = 0; s < picks.size(); ++s) {
            size_t faces = 0;
            for (const auto& f : m.faces)
                faces += f.corners.back().extra[4] == s;
            std::printf("  %zu: %s (%zu)", s, picks[s].empty() ? "?" : picks[s].c_str(), faces);
        }
        std::printf("\n");
    }
    return 0;
}

const Commands commands("newworlds", {
    {"lbp-info", "<file> [output prefix]",
     "decode a texture, show its key and colours, write <out>.bmp and <out>_alpha.bmp",
     cmdLbpInfo},
    {"tex-survey", "<file.cat>...",
     "group every texture by header layout",
     cmdTexSurvey},
    {"tex-keys", "<file.cat>...",
     "compare each colour key with the image corners",
     cmdTexKeys},
    {"ground-textures", "<planet.cat>",
     "show the textures the map viewer gives each ground tile model's slots, with each slot's face "
     "count",
     cmdGroundTextures},
});

} // namespace
