// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Harbinger commands for taking the game's data apart, for
// looking at it: STDS9H's harbinger_extract.py, in C++.
//
//   extract <out folder> <game folder> [options]
//       unpacks every .CAT/.LST/.ALL archive (DIALOGUE clips as .wav,
//       PHONEMES as .bin with a transcript, archives inside archives too)
//       and converts the assets: backgrounds (.CMP) and animations (.RLE,
//       a picture per frame) to .png, sounds (.RAW) to .wav, palettes
//       (.PAL) to swatches, the deck maps (MAPS/*.DAT) to .png; then writes
//       report.txt, cmp.csv, rle_frames.csv and raw.csv beside them.
//   probe-raw <out folder> <file.raw>
//       one sound in several formats and rates, to tell them apart by ear
//   map-palettes <out folder> <game folder>
//       each deck map drawn with every palette, as contact sheets
//
// The differences from the Python: it takes the game folder (a CD, both
// CDs side by side, an install: each part is "disc1", "disc2"... in the
// output, as the Python's disc roots were); it decodes with the engine's
// own readers (DCL, RLE, raw sound detection), so the RLE frames' report
// has their headers but not the row statistics that were used to work the
// format out; sounds are written as 16-bit WAV whatever their format; and
// it runs on one thread.

#include "audio/stream.h"
#include "audio/wav.h"
#include "common/bytes.h"
#include "common/dcl.h"
#include "common/fs.h"
#include "image/image.h"
#include "common/stream.h"
#include "common/text.h"
#include "engines/stharbinger/detection.h"
#include "engines/stharbinger/formats/archive.h"
#include "engines/stharbinger/formats/audio.h"
#include "engines/stharbinger/formats/graphics.h"
#include "devtools/tool.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace reluva;
using namespace reluva::tool;
namespace stdfs = std::filesystem;
using Rgb = std::vector<uint8_t>; // 256 x R, G, B

std::string upper(const std::string &s) {
    return text::toUpper(s);
}

std::string hexBytes(const uint8_t *d, size_t n, size_t max = 16) {
    std::string out;
    for (size_t i = 0; i < n && i < max; i++)
        out += text::format("%s%02X", i ? " " : "", d[i]);
    return out;
}

bool save(const stdfs::path &path, const std::vector<uint8_t> &data) {
    std::error_code ec;
    stdfs::create_directories(path.parent_path(), ec);
    return writeFile(path, data);
}

void saveText(const stdfs::path &path, const std::string &text) {
    save(path, std::vector<uint8_t>(text.begin(), text.end()));
}

// A picture of palette indices, in the palette's colours (opaque).
void savePng(const stdfs::path &path, int w, int h, const uint8_t *pixels, const Rgb &pal) {
    image::Image img;
    img.w = w;
    img.h = h;
    img.rgba.resize(size_t(w) * h * 4);
    for (size_t i = 0; i < size_t(w) * h; i++) {
        const uint8_t *c = pal.data() + pixels[i] * 3;
        img.rgba[i * 4] = c[0];
        img.rgba[i * 4 + 1] = c[1];
        img.rgba[i * 4 + 2] = c[2];
        img.rgba[i * 4 + 3] = 255;
    }
    save(path, image::encodePng(img));
}

audio::RawStream::Format rawFormat(const std::string &name) {
    if (name == "s8")
        return audio::RawStream::Format::S8;
    if (name == "s16")
        return audio::RawStream::Format::kS16LE;
    if (name == "s16be")
        return audio::RawStream::Format::kS16BE;
    return audio::RawStream::Format::U8;
}

std::string formatName(stharbinger::RawFormat f) {
    switch (f) {
    case stharbinger::kRawS8:
        return "s8";
    case stharbinger::kRawS16LE:
        return "s16";
    case stharbinger::kRawS16BE:
        return "s16be";
    default:
        return "u8";
    }
}

// Raw sound as a 16-bit mono WAV.
void saveWav(const stdfs::path &path, const std::vector<uint8_t> &data, int rate, const std::string &format) {
    audio::RawStream stream(data, rawFormat(format), rate);
    audio::Sound sound;
    sound.rate = rate;
    sound.channels = 1;
    sound.samples = audio::readAll(stream);
    save(path, audio::encodePcmWav(sound));
}

// 768 bytes of palette as 8-bit RGB: 6-bit VGA values (all 0..63) widened.
Rgb vgaPalette(const std::vector<uint8_t> &d, bool *sixBit = nullptr) {
    Rgb pal(768, 0);
    std::copy(d.begin(), d.begin() + std::min<size_t>(d.size(), 768), pal.begin());
    const bool six = *std::max_element(pal.begin(), pal.end()) <= 63;
    if (six)
        for (uint8_t &v : pal)
            v = uint8_t((v << 2) | (v >> 4));
    if (sixBit)
        *sixBit = six;
    return pal;
}

Rgb greyPalette() {
    Rgb pal(768);
    for (int i = 0; i < 768; i++)
        pal[size_t(i)] = uint8_t(i / 3);
    return pal;
}

// A distinct colour for each byte value, for map grids; 0 and 1 stay dark.
Rgb falseColourPalette() {
    Rgb pal;
    for (int i = 0; i < 256; i++) {
        if (i < 2) {
            pal.insert(pal.end(), 3, uint8_t(16 * i));
            continue;
        }
        const double h = std::fmod(i * 0.618033988749895, 1.0);
        const double s = 0.65;
        const double v = 0.95;
        const int k = int(h * 6);
        const double f = h * 6 - k;
        const double p = v * (1 - s);
        const double q = v * (1 - f * s);
        const double t = v * (1 - (1 - f) * s);
        const double rgb[6][3] = {{v, t, p}, {q, v, p}, {p, v, t}, {p, q, v}, {t, p, v}, {v, p, q}};
        for (double c : rgb[k % 6])
            pal.push_back(uint8_t(int(c * 255)));
    }
    return pal;
}

// --- Archives -------------------------------------------------------------------

std::optional<stdfs::path> sibling(const stdfs::path &path, const std::string &ext) {
    const std::string want = upper(path.stem().string() + ext);
    std::error_code ec;
    for (stdfs::directory_iterator it(path.parent_path(), ec), end; !ec && it != end; it.increment(ec))
        if (upper(it->path().filename().string()) == want)
            return it->path();
    return std::nullopt;
}

struct Entry {
    std::string name;
    uint32_t start, end;
};

// NAME.LST's offsets into NAME.ALL, with NAME.CAT's names; what doesn't
// add up in `issues`.
bool readArchive(const stdfs::path &lst, std::vector<uint8_t> &all, std::vector<Entry> &entries,
                 std::vector<std::string> &issues) {
    const auto allPath = sibling(lst, ".ALL");
    std::vector<uint8_t> raw;
    if (!allPath || !readFile(*allPath, all) || !readFile(lst, raw))
        return false;
    if (raw.size() % 4)
        issues.push_back(text::format("LST size %zu is not a multiple of 4", raw.size()));
    std::vector<uint32_t> bounds;
    for (size_t i = 0; i + 4 <= raw.size(); i += 4)
        bounds.push_back(readLE32(&raw[i]));
    if (bounds.empty() || bounds.back() != all.size()) {
        issues.push_back(text::format("last offset %s != ALL size %zu (no end sentinel?)",
                                      bounds.empty() ? "None" : std::to_string(bounds.back()).c_str(), all.size()));
        bounds.push_back(uint32_t(all.size()));
    }
    for (size_t i = 1; i < bounds.size(); i++)
        if (bounds[i] < bounds[i - 1] || bounds[i] > all.size()) {
            issues.push_back("offsets are not increasing");
            return false;
        }
    const size_t count = bounds.size() - 1;
    std::vector<std::string> names;
    bool haveNames = false;
    if (const auto cat = sibling(lst, ".CAT")) {
        std::vector<uint8_t> d;
        readFile(*cat, d);
        std::vector<std::string> lines;
        for (const std::string &l : text::split(std::string(d.begin(), d.end()), '\n')) {
            const std::string t = text::trim(l);
            if (!t.empty() && t != "\x1a")
                lines.push_back(t);
        }
        haveNames = true;
        if (!lines.empty() && lines[0].find_first_not_of("0123456789") == std::string::npos) {
            const size_t declared = size_t(std::atol(lines[0].c_str()));
            names.assign(lines.begin() + 1, lines.end());
            if (declared != count)
                issues.push_back(text::format("CAT count %zu != %zu LST entries", declared, count));
        } else {
            issues.push_back("CAT has no count line");
            names = lines;
        }
        if (names.size() != count)
            issues.push_back(text::format("CAT lists %zu names for %zu entries", names.size(), count));
    }
    for (size_t i = 0; i < count; i++) {
        const std::string name =
            haveNames && i < names.size() ? names[i] : text::format("%s_%03zu.bin", lst.stem().string().c_str(), i);
        entries.push_back({name, bounds[i], bounds[i + 1]});
    }
    return true;
}

bool hasPart(const stdfs::path &path, const char *part) {
    for (const auto &p : path)
        if (upper(p.string()) == part)
            return true;
    return false;
}

// A rough transcript of a lip-sync clip: the words between NULs in it.
std::string phonemeWords(const std::vector<uint8_t> &clip) {
    std::string out;
    for (size_t i = 1; i < clip.size(); i++) {
        if (clip[i - 1] != 0 || !std::isalpha(clip[i]))
            continue;
        size_t j = i;
        while (j < clip.size() && (std::isalpha(clip[j]) || clip[j] == '\'' || clip[j] == '-'))
            j++;
        if (j < clip.size() && clip[j] == 0) {
            out += (out.empty() ? "" : " ") +
                   std::string(clip.begin() + std::ptrdiff_t(i), clip.begin() + std::ptrdiff_t(j));
            i = j;
        }
    }
    return out;
}

// An LE header: "LE", byte and word order 0, format level 0, CPU and OS 1..4.
long findLeHeader(const std::vector<uint8_t> &d) {
    for (size_t o = 0; o + 12 <= d.size(); o++)
        if (d[o] == 'L' && d[o + 1] == 'E' && !std::memcmp(&d[o + 2], "\0\0\0\0\0\0", 6)) {
            const int cpu = readLE16(&d[o + 8]);
            const int os = readLE16(&d[o + 10]);
            if (cpu >= 1 && cpu <= 4 && os >= 1 && os <= 4)
                return long(o);
        }
    return -1;
}

std::string identifyExe(const std::vector<uint8_t> &d) {
    if (d.size() < 2 || !((d[0] == 'M' && d[1] == 'Z') || (d[0] == 'Z' && d[1] == 'M')))
        return {};
    std::string kind = "MZ (16-bit DOS / stub)";
    if (d.size() >= 0x40) {
        const uint32_t off = readLE32(&d[0x3C]);
        if (off > 0 && off + 2 < d.size()) {
            const std::string sig(d.begin() + off, d.begin() + off + 2);
            if (sig == "PE")
                kind = "PE (32-bit Windows)";
            else if (sig == "LE")
                kind = "LE (32-bit DOS, DOS/4GW style)";
            else if (sig == "LX")
                kind = "LX (32-bit OS/2 or DOS extender)";
            else if (sig == "NE")
                kind = "NE (16-bit Windows)";
        }
    }
    if (kind.rfind("MZ", 0) == 0) {
        const long le = findLeHeader(d);
        if (le >= 0)
            kind = text::format("MZ stub + LE at 0x%lX (32-bit DOS, bound to extender)", le);
    }
    const std::string all(d.begin(), d.end());
    if (all.find("DOS/4G") != std::string::npos)
        kind += ", mentions DOS/4G";
    if (upper(all).find("WATCOM") != std::string::npos)
        kind += ", Watcom runtime";
    return kind;
}

// --- Sound statistics -----------------------------------------------------------

double entropy(const std::vector<uint8_t> &d, size_t start = 0, size_t step = 1) {
    size_t counts[256] = {};
    size_t n = 0;
    for (size_t i = start; i < d.size(); i += step, n++)
        counts[d[i]]++;
    double e = 0;
    for (size_t c : counts)
        if (c) {
            const double p = double(c) / double(n);
            e -= p * std::log2(p);
        }
    return e;
}

struct RawStats {
    double entropy = 0, nearMid = 0, nearEdge = 0, oddNearZero = 0;
    std::string autoFormat, guess, first16;
};

RawStats rawStats(const std::vector<uint8_t> &d) {
    RawStats st;
    const double n = double(std::max<size_t>(d.size(), 1));
    size_t hist[256] = {};
    for (uint8_t b : d)
        hist[b]++;
    st.entropy = entropy(d);
    size_t mid = 0;
    size_t edge = 0;
    size_t oddEdge = 0;
    size_t odd = 0;
    for (int v = 0x70; v < 0x90; v++)
        mid += hist[v];
    for (int v = 0; v < 0x10; v++)
        edge += hist[v] + hist[0xF0 + v];
    for (size_t i = 1; i < d.size(); i += 2, odd++)
        if (d[i] < 0x08 || d[i] >= 0xF8)
            oddEdge++;
    st.nearMid = double(mid) / n;
    st.nearEdge = double(edge) / n;
    st.oddNearZero = double(oddEdge) / double(std::max<size_t>(odd, 1));
    if (st.entropy > 7.6 && st.oddNearZero < 0.5)
        st.guess = "high entropy: compressed/ADPCM or encrypted?";
    else if (st.oddNearZero > 0.6 && st.nearEdge < 0.6 && d.size() % 2 == 0)
        st.guess = "16-bit signed little-endian?";
    else if (st.nearMid >= st.nearEdge)
        st.guess = "8-bit unsigned";
    else
        st.guess = "8-bit signed";
    st.autoFormat = formatName(stharbinger::detectRawFormat(d));
    st.first16 = hexBytes(d.data(), d.size());
    return st;
}

// --- Backgrounds: which palette ----------------------------------------------------

// The palette a background or animation most likely uses, by name: its
// own, the nearest lower number of the same prefix, or the longest common
// prefix of 3 or more. (The game's scripts say which; this is for previews.)
std::string choosePalette(const std::string &stemIn, const std::map<std::string, Rgb> &names) {
    const std::string s = upper(stemIn);
    if (names.count(s))
        return s;
    auto split = [](const std::string &n, std::string &prefix, long &num) {
        size_t i = n.size();
        while (i > 0 && std::isdigit(static_cast<unsigned char>(n[i - 1])))
            i--;
        if (i == n.size())
            return false;
        prefix = n.substr(0, i);
        num = std::atol(n.c_str() + i);
        return true;
    };
    std::string prefix;
    long num = 0;
    if (split(s, prefix, num)) {
        long best = -1;
        std::string bestName;
        for (const auto &[name, pal] : names) {
            std::string p2;
            long n2 = 0;
            if (split(name, p2, n2) && p2 == prefix && n2 <= num && n2 > best) {
                best = n2;
                bestName = name;
            }
        }
        if (best >= 0)
            return bestName;
    }
    size_t bestLen = 0;
    std::string bestName;
    for (const auto &[name, pal] : names) {
        size_t l = 0;
        while (l < s.size() && l < name.size() && s[l] == name[l])
            l++;
        if (l > bestLen) {
            bestLen = l;
            bestName = name;
        }
    }
    return bestLen >= 3 ? bestName : std::string();
}

// --- Deck maps: their width ----------------------------------------------------------

// The width that lines rows up best: for a sample of the places where a
// byte differs from the next, how often the pair below is the same.
int guessWidth(const std::vector<uint8_t> &d, std::vector<std::pair<double, int>> *top = nullptr) {
    const size_t n = d.size();
    const int lo = 32;
    const int hi = int(std::min<size_t>(2048, n / 4));
    std::vector<size_t> edges;
    for (size_t i = 0; i + 1 < n; i++)
        if (d[i] != d[i + 1])
            edges.push_back(i);
    if (edges.empty() || hi <= lo)
        return 0;
    const size_t step = std::max<size_t>(1, edges.size() / 4000);
    std::vector<std::pair<double, int>> scores;
    for (int w = lo; w <= hi; w++) {
        const size_t lim = n - size_t(w) - 1;
        size_t t = 0;
        size_t m = 0;
        for (size_t k = 0; k < edges.size(); k += step) {
            const size_t i = edges[k];
            if (i < lim) {
                t++;
                if (d[i + size_t(w)] == d[i] && d[i + size_t(w) + 1] == d[i + 1])
                    m++;
            }
        }
        scores.push_back({t ? double(m) / double(t) : 0.0, w});
    }
    double best = 0;
    for (const auto &s : scores)
        best = std::max(best, s.first);
    int width = 0;
    for (const auto &s : scores)
        if (s.first >= 0.97 * best && (!width || s.second < width))
            width = s.second;
    if (top) {
        std::vector<std::pair<double, int>> sorted = scores;
        std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a > b; });
        sorted.resize(std::min<size_t>(5, sorted.size()));
        *top = sorted;
    }
    return width;
}

// Palettes picked by eye from the map-palettes contact sheets (the maps use
// colour slots shared by a family of palettes; any member works).
const std::map<std::string, std::string> kMapPalettes = {
    {"OPS", "OPS01"}, {"HABITAT", "CCRUN05"}, {"LOWER", "CCRUN05"}, {"UPPER", "CCRUN05"}};

// --- The extractor ------------------------------------------------------------------

struct Options {
    std::set<std::string> only;
    int limit = 0;
    std::string rawFormat = "auto";
    int rawRate = stharbinger::kSampleRate;
    std::map<std::string, std::string> mapPalettes = kMapPalettes;
    bool wants(const std::string &kind) const { return only.empty() || only.count(kind); }
};

struct Root {
    std::string label;
    stdfs::path root, out;
};

class Extractor {
public:
    Extractor(stdfs::path out, Options options) : _out(std::move(out)), _options(std::move(options)) {}

    void run(const std::vector<stharbinger::GamePart> &parts) {
        for (size_t i = 0; i < parts.size(); i++) {
            const auto &p = parts[i];
            const std::string label = "disc" + std::to_string(p.disc >= 0 ? p.disc + 1 : int(i) + 1);
            _roots.push_back({label, p.folder, _out / label});
        }
        if (_options.wants("archive")) {
            const std::vector<Root> discs = _roots;
            for (const Root &r : discs)
                unpackArchives(r.label, r.root, r.out, 0);
        }
        convert();
        writeReport();
    }

private:
    struct ArchiveInfo {
        std::string file;
        size_t entries = 0;
        std::vector<std::string> issues;
        size_t emptyClips = 0;
    };
    struct ExeInfo {
        std::string file, type;
        size_t size;
    };
    struct CmpInfo {
        std::string file, palette, dims, first16, error;
        size_t size = 0, outSize = 0;
        uint32_t header = 0;
        bool headerMatches = false;
    };
    struct RleFrameInfo {
        int frame, key, w, h, x, y;
        size_t bytes;
    };
    struct RleInfo {
        std::string file, palette, magic, error;
        size_t size = 0;
        int countField = 0, framesFound = 0;
        long trailing = 0;
        std::vector<std::string> issues;
        std::vector<RleFrameInfo> frames;
    };
    struct RawInfo {
        std::string file;
        size_t size = 0;
        RawStats stats;
    };
    struct MapInfo {
        std::string file, palette, values, topWidths;
        size_t size = 0;
        int width = 0, height = 0;
        long remainder = 0;
    };
    struct PalInfo {
        std::string file;
        bool sixBit = false;
    };

    void log(const std::string &s) { std::printf("%s\n", s.c_str()); }

    void unpackArchives(const std::string &label, const stdfs::path &root, const stdfs::path &outdir, int depth) {
        std::vector<stdfs::path> lsts;
        std::error_code ec;
        for (stdfs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file(ec) && upper(it->path().extension().string()) == ".LST" &&
                sibling(it->path(), ".ALL"))
                lsts.push_back(it->path());
        std::sort(lsts.begin(), lsts.end());
        for (const stdfs::path &lst : lsts) {
            const stdfs::path rel = lst.lexically_relative(root);
            ArchiveInfo info;
            info.file = label + ":" + rel.generic_string();
            std::vector<uint8_t> data;
            std::vector<Entry> entries;
            if (!readArchive(lst, data, entries, info.issues)) {
                info.issues.push_back("cannot be read");
                _archives.push_back(info);
                continue;
            }
            info.entries = entries.size();
            const stdfs::path dest = outdir / rel.parent_path() / rel.stem();
            auto bytesOf = [&](const Entry &e) {
                return std::vector<uint8_t>(data.begin() + e.start, data.begin() + e.end);
            };
            if (hasPart(rel, "DIALOGUE")) {
                for (size_t i = 0; i < entries.size(); i++) {
                    const std::vector<uint8_t> clip = bytesOf(entries[i]);
                    if (clip.empty()) {
                        info.emptyClips++;
                        continue;
                    }
                    const std::string fmt = _options.rawFormat == "auto"
                                                ? formatName(stharbinger::detectRawFormat(clip))
                                                : _options.rawFormat;
                    saveWav(dest / text::format("clip_%03zu.wav", i), clip, _options.rawRate, fmt);
                    if (i < 2)
                        _raws.push_back({text::format("%s#%zu", info.file.c_str(), i), clip.size(), rawStats(clip)});
                }
            } else if (hasPart(rel, "PHONEMES")) {
                std::string transcript;
                for (size_t i = 0; i < entries.size(); i++) {
                    const std::vector<uint8_t> clip = bytesOf(entries[i]);
                    if (!clip.empty())
                        save(dest / text::format("clip_%03zu.bin", i), clip);
                    transcript += text::format("%03zu  %s\n", i, clip.empty() ? "(empty)" : phonemeWords(clip).c_str());
                }
                saveText(dest / "transcript.txt", transcript);
            } else {
                for (const Entry &e : entries) {
                    const std::vector<uint8_t> d = bytesOf(e);
                    save(dest / e.name, d);
                    const std::string kind = identifyExe(d);
                    if (!kind.empty())
                        _exes.push_back(
                            {label + ":" + (rel.parent_path() / rel.stem() / e.name).generic_string(), kind, d.size()});
                }
                _roots.push_back({label, dest, dest});
                if (depth < 3)
                    unpackArchives(label, dest, dest, depth + 1);
            }
            _archives.push_back(info);
            std::string issues;
            for (const std::string &s : info.issues)
                issues += (issues.empty() ? "" : "; ") + s;
            log(text::format("archive %-45s %4zu entries%s", info.file.c_str(), info.entries,
                             issues.empty() ? "" : ("  [" + issues + "]").c_str()));
        }
    }

    void convert() {
        struct File {
            const Root *root;
            stdfs::path path;
        };
        std::vector<File> files;
        for (const Root &r : _roots) {
            std::vector<stdfs::path> found;
            std::error_code ec;
            for (stdfs::recursive_directory_iterator it(r.root, ec), end; !ec && it != end; it.increment(ec))
                if (it->is_regular_file(ec))
                    found.push_back(it->path());
            std::sort(found.begin(), found.end());
            for (const auto &p : found)
                files.push_back({&r, p});
        }
        std::map<std::string, Rgb> palettes;
        for (const File &f : files) {
            std::error_code ec;
            if (upper(f.path.extension().string()) == ".PAL" && stdfs::file_size(f.path, ec) == 768) {
                std::vector<uint8_t> d;
                readFile(f.path, d);
                palettes.emplace(upper(f.path.stem().string()), vgaPalette(d));
            }
        }
        std::set<std::string> seen;
        std::map<std::string, int> counts;
        const Rgb grey = greyPalette();
        size_t done = 0;
        for (const File &f : files) {
            const std::string ext = upper(f.path.extension().string());
            std::string kind = ext == ".CMP"   ? "cmp"
                               : ext == ".RLE" ? "rle"
                               : ext == ".RAW" ? "raw"
                               : ext == ".PAL" ? "pal"
                                               : "";
            if (ext == ".DAT" && upper(f.path.parent_path().filename().string()) == "MAPS")
                kind = "map";
            if (kind.empty() || !_options.wants(kind))
                continue;
            stdfs::path outbase = f.root->out / f.path.lexically_relative(f.root->root);
            outbase.replace_extension();
            if (!seen.insert(upper(outbase.string())).second)
                continue;
            if (_options.limit && counts[kind] >= _options.limit)
                continue;
            counts[kind]++;
            std::vector<uint8_t> d;
            readFile(f.path, d);
            const std::string file = f.path.string();
            if (kind == "cmp" || kind == "rle") {
                const std::string name = choosePalette(f.path.stem().string(), palettes);
                const Rgb &pal = name.empty() ? grey : palettes[name];
                if (kind == "cmp")
                    convertCmp(file, d, outbase, pal, name);
                else
                    convertRle(file, d, outbase, pal, name);
            } else if (kind == "raw") {
                RawInfo r{file, d.size(), rawStats(d)};
                saveWav(outbase.string() + ".wav", d, _options.rawRate,
                        _options.rawFormat == "auto" ? r.stats.autoFormat : _options.rawFormat);
                _raws.push_back(r);
            } else if (kind == "pal") {
                bool six = false;
                const Rgb pal = vgaPalette(d, &six);
                std::vector<uint8_t> img(256 * 256);
                for (int y = 0; y < 256; y++)
                    for (int x = 0; x < 256; x++)
                        img[size_t(y) * 256 + x] = uint8_t((y / 16) * 16 + x / 16);
                savePng(outbase.string() + ".png", 256, 256, img.data(), pal);
                _pals.push_back({file, six});
            } else {
                const auto it = _options.mapPalettes.find(upper(f.path.stem().string()));
                const bool known = it != _options.mapPalettes.end() && palettes.count(it->second);
                convertMap(file, d, outbase, known ? palettes[it->second] : falseColourPalette(),
                           known ? it->second : "false colour");
            }
            if (++done % 200 == 0)
                log(text::format("  %zu converted", done));
        }
        std::string summary;
        for (const auto &[k, n] : counts)
            summary += text::format("%s%d %s", summary.empty() ? "" : ", ", n, k.c_str());
        log("converted: " + summary);
    }

    void convertCmp(const std::string &file, const std::vector<uint8_t> &d, const stdfs::path &outbase, const Rgb &pal,
                    const std::string &palName) {
        CmpInfo c;
        c.file = file;
        c.size = d.size();
        c.palette = palName.empty() ? "grey" : palName;
        if (d.size() < 6) {
            c.error = "too short";
            _cmps.push_back(c);
            return;
        }
        c.header = readLE32(d.data());
        c.headerMatches = c.header == d.size() - 4;
        std::vector<uint8_t> pixels;
        if (!decompressDCL(d.data() + 4, uint32_t(d.size() - 4), pixels)) {
            c.error = "DCL: corrupt or truncated";
            _cmps.push_back(c);
            return;
        }
        c.outSize = pixels.size();
        c.first16 = hexBytes(pixels.data(), pixels.size());
        static const int kSizes[][2] = {{640, 480}, {640, 400}, {320, 200}, {640, 350}};
        int w = 0;
        int h = 0;
        size_t tail = 0;
        for (const auto &s : kSizes) {
            const size_t n = size_t(s[0]) * size_t(s[1]);
            if (pixels.size() == n || pixels.size() == n + 768) {
                w = s[0];
                h = s[1];
                tail = pixels.size() - n;
                break;
            }
        }
        if (!w && pixels.size() % 640 == 0) {
            w = 640;
            h = int(pixels.size() / 640);
        }
        if (!w) {
            save(outbase.string() + ".bin", pixels);
            c.error = "unknown dimensions; raw output written to .bin";
            _cmps.push_back(c);
            return;
        }
        Rgb use = pal;
        if (tail) {
            use = vgaPalette(std::vector<uint8_t>(pixels.begin() + std::ptrdiff_t(size_t(w) * h), pixels.end()));
            c.palette = "embedded";
        }
        savePng(outbase.string() + ".png", w, h, pixels.data(), use);
        c.dims = text::format("%dx%d", w, h);
        _cmps.push_back(c);
    }

    void convertRle(const std::string &file, const std::vector<uint8_t> &d, const stdfs::path &outbase, const Rgb &pal,
                    const std::string &palName) {
        RleInfo r;
        r.file = file;
        r.size = d.size();
        r.palette = palName.empty() ? "grey" : palName;
        if (d.size() < 10) {
            r.error = "too short";
            _rles.push_back(r);
            return;
        }
        r.magic = hexBytes(d.data(), 8);
        r.countField = readLE16(&d[8]);
        // The frames chained: a 25-byte header each, its data length at +21.
        size_t off = 10;
        while (off + 25 <= d.size()) {
            const uint32_t size = readLE32(&d[off + 21]);
            if (off + 25 + size > d.size()) {
                r.issues.push_back(text::format("frame at 0x%zX claims %u bytes, past end of file", off, size));
                break;
            }
            off += 25 + size;
            r.framesFound++;
        }
        r.trailing = long(d.size()) - long(off);

        MemoryReadStream stream(d);
        stharbinger::RleAnimation anim;
        uint8_t header[8];
        if (!anim.loadWithHeader(stream, header)) {
            r.error = "cannot be decoded";
            _rles.push_back(r);
            return;
        }
        for (int i = 0; i < anim.frameCount(); i++) {
            const auto &f = anim.getFrame(i);
            r.frames.push_back({i, f.key, f.w, f.h, f.x, f.y, f.data.size()});
            if (!f.w || !f.h || f.w > 640 || f.h > 480)
                continue;
            // Drawn twice, over 0 and over 255: a pixel the frame writes is the
            // same in both; the rest is transparent.
            Surface a;
            Surface b;
            draw::create(a, f.w, f.h, 0);
            draw::create(b, f.w, f.h, 255);
            anim.drawFrameAt(i, a, 0, 0);
            anim.drawFrameAt(i, b, 0, 0);
            image::Image img;
            img.w = f.w;
            img.h = f.h;
            img.rgba.assign(size_t(f.w) * f.h * 4, 0);
            for (size_t p = 0; p < size_t(f.w) * f.h; p++)
                if (a.pixels[p] == b.pixels[p]) {
                    const uint8_t *c = pal.data() + a.pixels[p] * 3;
                    std::memcpy(&img.rgba[p * 4], c, 3);
                    img.rgba[p * 4 + 3] = 255;
                }
            const std::string name =
                anim.frameCount() == 1 ? outbase.string() + ".png" : outbase.string() + text::format("_%03d.png", i);
            save(name, image::encodePng(img));
        }
        _rles.push_back(r);
    }

    void convertMap(const std::string &file, const std::vector<uint8_t> &d, const stdfs::path &outbase, const Rgb &pal,
                    const std::string &palName) {
        MapInfo m;
        m.file = file;
        m.size = d.size();
        std::vector<std::pair<double, int>> top;
        m.width = guessWidth(d, &top);
        for (const auto &[score, w] : top)
            m.topWidths += text::format("%s%d (%.2f)", m.topWidths.empty() ? "" : ", ", w, score);
        std::map<uint8_t, size_t> counts;
        for (uint8_t v : d)
            counts[v]++;
        std::vector<std::pair<size_t, uint8_t>> common;
        for (const auto &[v, n] : counts)
            common.push_back({n, v});
        std::sort(common.begin(), common.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        for (size_t i = 0; i < common.size() && i < 12; i++)
            m.values += text::format("%s%02X:%zu", i ? ", " : "", common[i].second, common[i].first);
        if (m.width) {
            m.height = int(d.size() / size_t(m.width));
            m.remainder = long(d.size()) - long(m.width) * m.height;
            m.palette = palName;
            savePng(outbase.string() + ".png", m.width, m.height, d.data(), pal);
        }
        _maps.push_back(m);
    }

    void writeReport() {
        std::string r;
        auto add = [&](const std::string &s) { r += s + "\n"; };
        add("== Archives");
        for (const ArchiveInfo &a : _archives) {
            std::string issues;
            for (const std::string &s : a.issues)
                issues += (issues.empty() ? "" : "; ") + s;
            add(text::format("  %-50s %zu entries %s%s", a.file.c_str(), a.entries,
                             issues.empty() ? "OK" : issues.c_str(),
                             a.emptyClips ? text::format(", %zu empty clips", a.emptyClips).c_str() : ""));
        }
        add("\n== Executables found inside archives");
        if (_exes.empty())
            add(text::format("  %-50s %8s  %s", "(none)", "", ""));
        for (const ExeInfo &e : _exes)
            add(text::format("  %-50s %8zu  %s", e.file.c_str(), e.size, e.type.c_str()));

        size_t ok = 0;
        size_t headerOk = 0;
        std::map<size_t, int> sizes;
        for (const CmpInfo &c : _cmps) {
            ok += !c.dims.empty();
            headerOk += c.headerMatches;
            sizes[c.outSize]++;
        }
        add(text::format("\n== CMP backgrounds: %zu converted, %zu failed", ok, _cmps.size() - ok));
        add(text::format("  header u32 == file size - 4: %zu of %zu", headerOk, _cmps.size()));
        std::vector<std::pair<int, size_t>> bySize;
        for (const auto &[s, n] : sizes)
            bySize.push_back({n, s});
        std::sort(bySize.begin(), bySize.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        std::string sizeList;
        for (size_t i = 0; i < bySize.size() && i < 8; i++)
            sizeList += text::format("%s%zu x%d", i ? ", " : "", bySize[i].second, bySize[i].first);
        add("  decompressed sizes: " + sizeList);
        int shown = 0;
        for (const CmpInfo &c : _cmps)
            if (!c.error.empty() && shown++ < 15)
                add("  FAIL " + c.file + ": " + c.error);

        size_t frames = 0;
        size_t chained = 0;
        size_t countOk = 0;
        std::map<std::string, int> magics;
        for (const RleInfo &x : _rles) {
            frames += x.frames.size();
            chained += x.trailing == 0;
            countOk += x.countField == x.framesFound;
            magics[x.magic]++;
        }
        add(text::format("\n== RLE animations: %zu files, %zu frames", _rles.size(), frames));
        add(text::format("  frames chain exactly to end of file: %zu of %zu", chained, _rles.size()));
        add(text::format("  frame-count field matches frames found: %zu of %zu", countOk, _rles.size()));
        std::vector<std::pair<int, std::string>> byMagic;
        for (const auto &[m, n] : magics)
            byMagic.push_back({n, m});
        std::sort(byMagic.begin(), byMagic.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        std::string magicList;
        for (size_t i = 0; i < byMagic.size() && i < 5; i++)
            magicList += text::format("%s%s x%d", i ? "; " : "", byMagic[i].second.c_str(), byMagic[i].first);
        add("  distinct 8-byte magics: " + magicList);
        shown = 0;
        for (const RleInfo &x : _rles) {
            std::string issues = x.error;
            for (const std::string &s : x.issues)
                issues += (issues.empty() ? "" : "; ") + s;
            if (!issues.empty() && shown++ < 15)
                add("  ISSUE " + x.file + ": " + issues);
        }

        add("\n== RAW audio (sound effects + first clips of each dialogue archive)");
        // Most common first (in the order first seen among equals).
        std::vector<std::pair<std::string, int>> formats;
        size_t odd16 = 0;
        for (const RawInfo &x : _raws) {
            auto it = std::find_if(formats.begin(), formats.end(),
                                   [&](const auto &f) { return f.first == x.stats.autoFormat; });
            if (it == formats.end())
                formats.push_back({x.stats.autoFormat, 1});
            else
                it->second++;
            odd16 += x.stats.autoFormat.rfind("s16", 0) == 0 && x.size % 2;
        }
        std::stable_sort(formats.begin(), formats.end(),
                         [](const auto &a, const auto &b) { return a.second > b.second; });
        std::string formatList;
        for (const auto &[f, n] : formats)
            formatList += text::format("%s%s x%d", formatList.empty() ? "" : ", ", f.c_str(), n);
        add("  auto-detected formats: " + formatList);
        add(text::format("  odd-sized files detected as 16-bit: %zu", odd16));
        for (size_t i = 0; i < _raws.size() && i < 12; i++) {
            const RawInfo &x = _raws[i];
            add(text::format("  %-45s ent=%-5.2f mid=%-6.3f edge=%-6.3f odd16=%-6.3f %s | %s",
                             stdfs::path(x.file).filename().string().c_str(), x.stats.entropy, x.stats.nearMid,
                             x.stats.nearEdge, x.stats.oddNearZero, x.stats.autoFormat.c_str(),
                             x.stats.first16.c_str()));
        }

        add("\n== Map grids");
        for (const MapInfo &m : _maps) {
            add(text::format("  %s: %zu bytes, width %d, height %d, remainder %ld; other candidates %s", m.file.c_str(),
                             m.size, m.width, m.height, m.remainder, m.topWidths.c_str()));
            add("     palette: " + m.palette);
            add("     most common values: " + m.values);
        }
        bool allSix = true;
        for (const PalInfo &p : _pals)
            allSix = allSix && p.sixBit;
        add(text::format("\n== Palettes: %zu, all 6-bit VGA: %s", _pals.size(), allSix ? "True" : "False"));

        std::vector<std::string> errors;
        for (const CmpInfo &c : _cmps)
            if (!c.error.empty())
                errors.push_back(c.file + ": " + c.error);
        for (const RleInfo &x : _rles)
            if (!x.error.empty())
                errors.push_back(x.file + ": " + x.error);
        add(text::format("\n== Errors: %zu", errors.size()));
        for (size_t i = 0; i < errors.size() && i < 40; i++)
            add("  " + errors[i]);
        saveText(_out / "report.txt", r);

        auto csv = [](std::string v) {
            if (v.find_first_of(",\"\n") == std::string::npos)
                return v;
            std::string out = "\"";
            for (char c : v)
                out += c == '"' ? std::string("\"\"") : std::string(1, c);
            return out + "\"";
        };
        std::string cmp = "file,size,header_u32,out_size,dims,palette,first16,error\n";
        for (const CmpInfo &c : _cmps)
            cmp += csv(c.file) + "," + std::to_string(c.size) + "," + std::to_string(c.header) + "," +
                   std::to_string(c.outSize) + "," + c.dims + "," + csv(c.palette) + "," + c.first16 + "," +
                   csv(c.error) + "\n";
        saveText(_out / "cmp.csv", cmp);
        std::string rle = "file,frame,key,w,h,x,y,data_bytes\n";
        for (const RleInfo &x : _rles)
            for (const RleFrameInfo &f : x.frames)
                rle +=
                    csv(x.file) + text::format(",%d,%d,%d,%d,%d,%d,%zu\n", f.frame, f.key, f.w, f.h, f.x, f.y, f.bytes);
        saveText(_out / "rle_frames.csv", rle);
        std::string raw = "file,size,auto,entropy,near_0x80,near_0x00_0xFF,odd_bytes_near_0,first16\n";
        for (const RawInfo &x : _raws)
            raw += csv(x.file) +
                   text::format(",%zu,%s,%.2f,%.3f,%.3f,%.3f,", x.size, x.stats.autoFormat.c_str(), x.stats.entropy,
                                x.stats.nearMid, x.stats.nearEdge, x.stats.oddNearZero) +
                   x.stats.first16 + "\n";
        saveText(_out / "raw.csv", raw);
        std::printf("\n%s\nFull report: %s\n", r.substr(0, 4000).c_str(), (_out / "report.txt").string().c_str());
    }

    stdfs::path _out;
    Options _options;
    std::vector<Root> _roots;
    std::vector<ArchiveInfo> _archives;
    std::vector<ExeInfo> _exes;
    std::vector<CmpInfo> _cmps;
    std::vector<RleInfo> _rles;
    std::vector<RawInfo> _raws;
    std::vector<MapInfo> _maps;
    std::vector<PalInfo> _pals;
};

std::vector<stharbinger::GamePart> partsOf(const char *folder) {
    auto parts = stharbinger::findGameParts(folder);
    if (parts.empty())
        std::fprintf(stderr, "%s: no part of Harbinger found (a CD, both CDs or an install)\n", folder);
    return parts;
}

int cmdExtract(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool stharbinger extract <out folder> <game folder> [--only KINDS] "
                             "[--limit N] [--raw-format auto|u8|s8|s16|s16be] [--raw-rate HZ] "
                             "[--map-palette MAP=PAL]...\n");
        return 2;
    }
    Options options;
    for (int i = 4; i < argc; i += 2) { // each option and its value
        const std::string a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : nullptr;
        if (!v) {
            std::fprintf(stderr, "%s needs a value\n", a.c_str());
            return 2;
        }
        if (a == "--only") {
            for (const std::string &k : text::split(v, ','))
                options.only.insert(k);
        } else if (a == "--limit") {
            options.limit = std::atoi(v);
        } else if (a == "--raw-format") {
            options.rawFormat = v;
            if (options.rawFormat != "auto" && options.rawFormat != "u8" && options.rawFormat != "s8" &&
                options.rawFormat != "s16" && options.rawFormat != "s16be") {
                std::fprintf(stderr, "--raw-format: auto, u8, s8, s16 or s16be\n");
                return 2;
            }
        } else if (a == "--raw-rate") {
            options.rawRate = std::max(1, std::atoi(v));
        } else if (a == "--map-palette") {
            const std::string s = upper(v);
            const size_t eq = s.find('=');
            if (eq == std::string::npos) {
                std::fprintf(stderr, "--map-palette MAP=PAL\n");
                return 2;
            }
            options.mapPalettes[s.substr(0, eq)] = s.substr(eq + 1);
        } else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return 2;
        }
    }
    const auto parts = partsOf(argv[3]);
    if (parts.empty())
        return 1;
    Extractor(argv[2], options).run(parts);
    return 0;
}

int cmdProbeRaw(int argc, char **argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: reluva-tool stharbinger probe-raw <out folder> <file.raw>\n");
        return 2;
    }
    std::vector<uint8_t> d;
    if (!readFile(argv[3], d)) {
        std::fprintf(stderr, "%s: cannot read\n", argv[3]);
        return 1;
    }
    const RawStats st = rawStats(d);
    std::printf("%s: %zu bytes\n", argv[3], d.size());
    std::printf("  %-18s %.2f\n  %-18s %.3f\n  %-18s %s\n  %-18s %.3f\n  %-18s %.3f\n  %-18s %s\n  %-18s %s\n",
                "entropy", st.entropy, "near_0x80", st.nearMid, "auto", st.autoFormat.c_str(), "near_0x00_0xFF",
                st.nearEdge, "odd_bytes_near_0", st.oddNearZero, "guess", st.guess.c_str(), "first16",
                st.first16.c_str());
    const std::string stem = stdfs::path(argv[3]).stem().string();
    for (const char *fmt : {"u8", "s8", "s16"})
        for (int rate : {11025, 22050}) {
            if (!std::strcmp(fmt, "s16") && d.size() % 2)
                continue;
            const stdfs::path p = stdfs::path(argv[2]) / text::format("%s_%s_%d.wav", stem.c_str(), fmt, rate);
            saveWav(p, d, rate, fmt);
            std::printf("  wrote %s\n", p.string().c_str());
        }
    return 0;
}

int cmdMapPalettes(int argc, char **argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: reluva-tool stharbinger map-palettes <out folder> <game folder>\n");
        return 2;
    }
    const auto parts = partsOf(argv[3]);
    if (parts.empty())
        return 1;
    std::map<std::string, Rgb> pals;
    std::map<std::string, std::vector<uint8_t>> maps;
    for (const auto &part : parts) {
        std::vector<stdfs::path> files;
        std::error_code ec;
        for (stdfs::recursive_directory_iterator it(part.folder, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file(ec))
                files.push_back(it->path());
        std::sort(files.begin(), files.end());
        for (const auto &p : files) {
            const std::string ext = upper(p.extension().string());
            std::vector<uint8_t> d;
            if (ext == ".PAL" && stdfs::file_size(p, ec) == 768 && readFile(p, d))
                pals.emplace(upper(p.stem().string()), vgaPalette(d));
            else if (ext == ".DAT" && upper(p.parent_path().filename().string()) == "MAPS" && readFile(p, d))
                maps.emplace(upper(p.stem().string()), d);
        }
    }
    const int tile = 200;
    const int cols = 8;
    for (const auto &[name, d] : maps) {
        const int w = guessWidth(d);
        if (!w || pals.empty())
            continue;
        const int h = int(d.size() / size_t(w));
        const int step = std::max(1, (std::max(w, h) + tile - 1) / tile);
        const int tw = (w + step - 1) / step;
        const int th = (h + step - 1) / step;
        const int rows = (int(pals.size()) + cols - 1) / cols;
        const int sw = cols * (tw + 4);
        const int sh = rows * (th + 4);
        image::Image sheet;
        sheet.w = sw;
        sheet.h = sh;
        sheet.rgba.assign(size_t(sw) * sh * 4, 0);
        for (size_t i = 3; i < sheet.rgba.size(); i += 4)
            sheet.rgba[i] = 255;
        std::string index;
        int i = 0;
        for (const auto &[pname, pal] : pals) {
            const int ox = (i % cols) * (tw + 4);
            const int oy = (i / cols) * (th + 4);
            for (int y = 0; y < th; y++)
                for (int x = 0; x < tw; x++) {
                    const uint8_t v = d[size_t(y * step) * size_t(w) + size_t(x * step)];
                    std::memcpy(sheet.pixel(ox + x, oy + y), pal.data() + v * 3, 3);
                }
            index += text::format("%3d  row %d col %d  %s\n", i + 1, i / cols + 1, i % cols + 1, pname.c_str());
            i++;
        }
        const stdfs::path png = stdfs::path(argv[2]) / (name + "_palettes.png");
        save(png, image::encodePng(sheet));
        saveText(stdfs::path(argv[2]) / (name + "_palettes.txt"), index);
        std::printf("%s: %dx%d, %zu palettes -> %s\n", name.c_str(), w, h, pals.size(), png.string().c_str());
    }
    return 0;
}

const Commands kCommands(
    "stharbinger",
    {
        {"extract", "<out folder> <game folder> [options]",
         "unpack every archive and convert the assets (backgrounds, animations, sounds, palettes, deck maps) to "
         ".png and .wav, with report.txt and CSV files; --only archive,cmp,rle,raw,pal,map, --limit N per kind, "
         "--raw-format auto|u8|s8|s16|s16be, --raw-rate HZ, --map-palette MAP=PAL",
         cmdExtract},
        {"probe-raw", "<out folder> <file.raw>",
         "a raw sound's statistics, and the sound written in several formats and rates to tell them apart by ear",
         cmdProbeRaw},
        {"map-palettes", "<out folder> <game folder>",
         "each deck map (MAPS/*.DAT) drawn with every palette on the discs, as contact sheets with an index",
         cmdMapPalettes},
    });

} // namespace
