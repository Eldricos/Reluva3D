// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/detection.h"

#include "common/bytes.h"
#include "common/fs.h"
#include "common/stream.h"
#include "common/text.h"
#include "common/zip.h"
#include "engines/stgenerations/formats/resources.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <system_error>

namespace reluva::stgenerations {

namespace {

constexpr const char* kGameId = "stgenerations";
constexpr const char* kTitle = "Star Trek: Generations";

bool isDir(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::is_directory(p, ec);
}

// The CD in `dir` (its GENS/ folder holding MUSIC.1 or MUSIC.2), if it is one.
bool discIn(const std::filesystem::path& dir, GamePart::Kind& kind) {
    const auto gens = fs::findDirectory(dir, "GENS");
    if (!gens)
        return false;
    if (fs::findFile(*gens, "MUSIC.1"))
        kind = GamePart::kDisc1;
    else if (fs::findFile(*gens, "MUSIC.2"))
        kind = GamePart::kDisc2;
    else
        return false;
    return true;
}

// The installed game: DATA/COMMON.DAT and an executable (sgens.exe, or
// any for a release whose executable is named otherwise).
bool installIn(const std::filesystem::path& dir) {
    if (!fs::findFile(dir, "DATA/COMMON.DAT"))
        return false;
    if (fs::findFile(dir, "sgens.exe"))
        return true;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
        if (entry.is_regular_file(ec) && text::hasSuffixIgnoreCase(entry.path().filename().string(), ".exe"))
            return true;
    return false;
}

// Search a buffer for an ASCII string, or its UTF-16LE form, ignoring case.
bool containsString(const std::vector<uint8_t>& data, const char* needle) {
    const size_t len = std::strlen(needle);
    auto matchAt = [&](size_t pos, size_t stride) {
        for (size_t i = 0; i < len; i++) {
            const size_t p = pos + i * stride;
            if (std::tolower(data[p]) != std::tolower(static_cast<unsigned char>(needle[i])))
                return false;
            if (stride == 2 && data[p + 1] != 0)
                return false;
        }
        return true;
    };
    for (size_t pos = 0; pos + len <= data.size(); pos++) {
        if (matchAt(pos, 1))
            return true;
        if (pos + len * 2 <= data.size() && matchAt(pos, 2))
            return true;
    }
    return false;
}

// Every file under `dir`, relative to it with '/', in one order on every
// system.
std::vector<std::string> listTree(const std::filesystem::path& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_regular_file(ec))
            out.push_back(std::filesystem::relative(it->path(), dir, ec).generic_string());
    }
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        return fs::windowsNameLess(a, b);
    });
    return out;
}

uint64_t sizeOf(const std::filesystem::path& p) {
    std::error_code ec;
    const auto n = std::filesystem::file_size(p, ec);
    return ec ? 0 : uint64_t(n);
}

// How many of an archive's members can't be read as they should (damaged
// data, or a wrong CRC); -1 if it isn't an archive at all.
int damagedMembers(const std::filesystem::path& file) {
    ZipArchive zip;
    if (zip.open(file))
        return -1;
    int damaged = 0;
    for (const ZipArchive::Entry& e : zip.entries()) {
        std::vector<uint8_t> data;
        if (!e.name.empty() && e.name.back() != '/' && zip.read(e, data))
            ++damaged;
    }
    return damaged;
}

bool sameFile(const std::filesystem::path& a, const std::filesystem::path& b) {
    if (sizeOf(a) != sizeOf(b))
        return false;
    std::vector<uint8_t> x, y;
    return readFile(a, x) && readFile(b, y) && x == y;
}

bool planned(const ImportPlan& plan, const std::string& to) {
    return std::any_of(plan.files.begin(), plan.files.end(),
                       [&](const ImportPlan::File& f) { return text::equalsIgnoreCase(f.to, to); });
}

} // namespace

std::vector<GamePart> findGameParts(const std::filesystem::path& dir) {
    std::vector<GamePart> parts;
    auto look = [&](const std::filesystem::path& d) {
        GamePart::Kind kind;
        if (installIn(d))
            parts.push_back({GamePart::kInstall, d});
        if (discIn(d, kind))
            parts.push_back({kind, d});
    };
    if (!isDir(dir))
        return parts;
    look(dir);
    std::vector<std::filesystem::path> children;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
        if (entry.is_directory(ec))
            children.push_back(entry.path());
    std::sort(children.begin(), children.end(), [](const auto& a, const auto& b) {
        return fs::windowsNameLess(a.filename().string(), b.filename().string());
    });
    for (const auto& child : children)
        look(child);
    // One of each: the first found.
    std::vector<GamePart> unique;
    for (const GamePart& p : parts)
        if (std::none_of(unique.begin(), unique.end(), [&](const GamePart& u) { return u.kind == p.kind; }))
            unique.push_back(p);
    return unique;
}

std::vector<DetectedGame> detectInstall(const std::string& engineId, const std::vector<GameVariant>& variants,
                                        const std::filesystem::path& dir, bool* unknown) {
    if (unknown)
        *unknown = false;
    if (!fs::findFile(dir, "DATA/COMMON.DAT"))
        return {};
    std::vector<DetectedGame> found = detectFromTable(engineId, variants, dir);
    if (!found.empty())
        return found;
    // A release with no signatures listed: an executable mentioning the
    // game's name.
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec) || !text::hasSuffixIgnoreCase(entry.path().filename().string(), ".exe"))
            continue;
        const uint64_t size = sizeOf(entry.path());
        if (size < 2 || size > 16 * 1024 * 1024)
            continue;
        std::vector<uint8_t> data;
        if (!readFile(entry.path(), data) || data[0] != 'M' || data[1] != 'Z' || !containsString(data, "Generations"))
            continue;
        bool pe = false;
        if (data.size() >= 0x40) {
            const uint32_t at = readLE32(&data[0x3C]);
            pe = at + 4 <= data.size() && readBE32(&data[at]) == tagBE('P', 'E', 0, 0);
        }
        if (unknown)
            *unknown = true;
        return {DetectedGame{engineId, kGameId, std::string(kTitle) + " (a release Reluva doesn't know)", "",
                             pe ? "windows" : "dos"}};
    }
    return {};
}

void addPartToImport(ImportPlan& plan, const GamePart& part) {
    if (part.kind == GamePart::kInstall) {
        for (const std::string& rel : listTree(part.dir)) {
            if (planned(plan, rel))
                continue;
            plan.files.push_back({part.dir / rel, rel, sizeOf(part.dir / rel), std::nullopt});
        }
        return;
    }
    // From a CD, what the setup leaves on it, once.
    const std::filesystem::path gens = *fs::findDirectory(part.dir, "GENS");
    const std::string folder = part.kind == GamePart::kDisc1 ? "disc1/GENS/" : "disc2/GENS/";
    for (const DataFileLocation* f = dataFiles(); f->name; f++) {
        if (f->installed || (part.kind == GamePart::kDisc1 ? !f->onDisc1 : !f->onDisc2))
            continue;
        const auto file = fs::findFile(gens, f->name);
        if (!file)
            continue;
        // Files on both CDs are the same: one copy, from the first CD added.
        // Unless the copies differ (a CD read badly when it was copied) and
        // the other CD's archive is the whole one: then that one.
        auto have = std::find_if(plan.files.begin(), plan.files.end(), [&](const ImportPlan::File& p) {
            return text::equalsIgnoreCase(p.to, std::string("disc1/GENS/") + f->name) ||
                   text::equalsIgnoreCase(p.to, std::string("disc2/GENS/") + f->name);
        });
        if (have != plan.files.end()) {
            if (text::hasSuffixIgnoreCase(f->name, ".dat") && !sameFile(have->from, *file)) {
                const int planned = damagedMembers(have->from), here = damagedMembers(*file);
                if (here >= 0 && (planned < 0 || here < planned)) {
                    have->from = *file;
                    have->to = folder + f->name;
                    have->size = sizeOf(*file);
                }
            }
            continue;
        }
        plan.files.push_back({*file, folder + f->name, sizeOf(*file), std::nullopt});
    }
}

void noteImport(ImportPlan& plan) {
    bool install = false;
    for (const ImportPlan::File& f : plan.files)
        install = install || text::equalsIgnoreCase(f.to, "DATA/COMMON.DAT");
    plan.notes.clear();
    if (!install)
        plan.notes.push_back("The installed game isn't here: install it with its own setup, then add its folder "
                             "(the one with sgens.exe). Reluva can't install it from the CDs.");
    std::vector<std::string> missing;
    for (const DataFileLocation* f = dataFiles(); f->name; f++) {
        if (f->installed)
            continue;
        bool have = false;
        for (const char* folder : {"disc1/GENS/", "disc2/GENS/"})
            have = have || planned(plan, std::string(folder) + f->name);
        if (!have)
            missing.push_back(f->name);
    }
    if (!missing.empty()) {
        std::string list;
        for (size_t i = 0; i < missing.size() && i < 4; i++)
            list += (i ? ", " : "") + missing[i];
        if (missing.size() > 4)
            list += ", ...";
        plan.notes.push_back("From the CDs, still missing: " + list +
                             ". Add the CDs (or copies of them) for the story, space combat, the music and the movies.");
    }
    if (plan.notes.empty())
        plan.notes.push_back("The installed game and both CDs: everything the game needs.");
}

} // namespace reluva::stgenerations
