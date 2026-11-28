// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/detection.h"
#include "common/fs.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <optional>

namespace reluva {

FileLookup folderLookup(const std::filesystem::path &dir) {
    return [dir](const std::string &name) -> std::optional<FoundFile> {
        const auto path = fs::findFile(dir, name);
        if (!path)
            return std::nullopt;
        std::error_code ec;
        const auto size = std::filesystem::file_size(*path, ec);
        if (ec)
            return std::nullopt;
        return FoundFile{size, [p = *path] { return fs::md5File(p, kDetectionHashBytes); }};
    };
}

std::vector<DetectedGame> detectFromTable(const std::string &engineId, const std::vector<GameVariant> &table,
                                          const std::filesystem::path &dir) {
    return detectFromTable(engineId, table, folderLookup(dir));
}

std::vector<DetectedGame> detectFromTable(const std::string &engineId, const std::vector<GameVariant> &table,
                                          const FileLookup &files) {
    // Many variants share files, so cache per-file results.
    std::map<std::string, std::optional<FoundFile>> found;
    std::map<std::string, std::optional<std::string>> hashes;
    auto locate = [&](const std::string &name) -> const std::optional<FoundFile> & {
        auto it = found.find(name);
        if (it == found.end())
            it = found.emplace(name, files(name)).first;
        return it->second;
    };

    std::vector<DetectedGame> results;
    for (const GameVariant &variant : table) {
        bool matches = !variant.files.empty();
        for (const FileSignature &sig : variant.files) {
            const auto &file = locate(sig.name);
            if (!file || (sig.size >= 0 && static_cast<int64_t>(file->size) != sig.size)) {
                matches = false;
                break;
            }
            if (!sig.md5.empty()) {
                auto it = hashes.find(sig.name);
                if (it == hashes.end())
                    it = hashes.emplace(sig.name, file->md5()).first;
                if (!it->second || *it->second != sig.md5) {
                    matches = false;
                    break;
                }
            }
        }
        if (matches)
            results.push_back({engineId, variant.gameId, variant.description, variant.language, variant.platform});
    }
    return results;
}

Error runImport(const ImportPlan &plan, const std::filesystem::path &dest,
                const std::function<bool(uint64_t done, uint64_t total)> &progress) {
    const uint64_t total = plan.totalBytes();
    uint64_t done = 0;
    std::vector<char> buffer(1 << 20);
    for (const ImportPlan::File &f : plan.files) {
        const std::filesystem::path to = dest / std::filesystem::path(f.to);
        std::error_code ec;
        std::filesystem::create_directories(to.parent_path(), ec);
        if (ec)
            return Error::fail("Couldn't make " + to.parent_path().string() + ": " + ec.message());
        std::filesystem::path part = to;
        part += ".part";
        if (f.packed || f.cabinetFile) {
            // Out of an installer or a cabinet: unpacked whole, then written.
            std::vector<uint8_t> bytes;
            if (Error err = f.packed       ? extractWiseStream(f.from, *f.packed, bytes)
                            : plan.cabinet ? extractInstallShieldFile(*plan.cabinet, *f.cabinetFile, bytes)
                                           : Error::fail("No cabinet for " + f.to))
                return err;
            std::ofstream out(part, std::ios::binary | std::ios::trunc);
            if (!out.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size())))
                return Error::fail("Couldn't write " + part.string() + " (is the disk full?)");
            out.close();
            done += f.size;
            if (progress && !progress(done, total)) {
                std::filesystem::remove(part, ec);
                return Error::fail("Import cancelled");
            }
        } else if (f.disc) {
            IsoTrackReader in;
            if (Error err = in.open(*f.disc))
                return err;
            std::ofstream out(part, std::ios::binary | std::ios::trunc);
            if (!out)
                return Error::fail("Couldn't write " + part.string());
            for (uint64_t at = 0; at < f.size;) {
                const size_t n = size_t(std::min<uint64_t>(buffer.size(), f.size - at));
                if (Error err = in.read(f.discSector, at, buffer.data(), n))
                    return err;
                if (!out.write(buffer.data(), std::streamsize(n)))
                    return Error::fail("Couldn't write " + part.string() + " (is the disk full?)");
                at += n;
                done += n;
                if (progress && !progress(done, total)) {
                    out.close();
                    std::filesystem::remove(part, ec);
                    return Error::fail("Import cancelled");
                }
            }
        } else {
            std::ifstream in(f.from, std::ios::binary);
            if (!in)
                return Error::fail("Couldn't read " + f.from.string());
            std::ofstream out(part, std::ios::binary | std::ios::trunc);
            if (!out)
                return Error::fail("Couldn't write " + part.string());
            while (in) {
                in.read(buffer.data(), std::streamsize(buffer.size()));
                const std::streamsize n = in.gcount();
                if (n <= 0)
                    break;
                out.write(buffer.data(), n);
                if (!out)
                    return Error::fail("Couldn't write " + part.string() + " (is the disk full?)");
                done += uint64_t(n);
                if (progress && !progress(done, total)) {
                    out.close();
                    std::filesystem::remove(part, ec);
                    return Error::fail("Import cancelled");
                }
            }
            if (in.bad())
                return Error::fail("Couldn't read all of " + f.from.string());
        }
        std::filesystem::rename(part, to, ec);
        if (ec)
            return Error::fail("Couldn't write " + to.string() + ": " + ec.message());
    }
    if (progress)
        progress(total, total);
    return Error::success();
}

} // namespace reluva
