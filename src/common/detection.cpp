// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/detection.h"
#include "common/fs.h"

#include <fstream>
#include <map>
#include <optional>

namespace reluva {

std::vector<DetectedGame> detectFromTable(const std::string& engineId,
                                          const std::vector<GameVariant>& table,
                                          const std::filesystem::path& dir) {
    // Many variants share files, so cache per-file results.
    std::map<std::string, std::optional<std::filesystem::path>> found;
    std::map<std::string, std::optional<std::string>> hashes;

    auto locate = [&](const std::string& name) -> const std::optional<std::filesystem::path>& {
        auto it = found.find(name);
        if (it == found.end())
            it = found.emplace(name, fs::findFile(dir, name)).first;
        return it->second;
    };

    std::vector<DetectedGame> results;
    for (const GameVariant& variant : table) {
        bool matches = !variant.files.empty();
        for (const FileSignature& sig : variant.files) {
            const auto& path = locate(sig.name);
            if (!path) {
                matches = false;
                break;
            }
            if (sig.size >= 0) {
                std::error_code ec;
                auto size = std::filesystem::file_size(*path, ec);
                if (ec || static_cast<int64_t>(size) != sig.size) {
                    matches = false;
                    break;
                }
            }
            if (!sig.md5.empty()) {
                auto key = path->string();
                auto it = hashes.find(key);
                if (it == hashes.end())
                    it = hashes.emplace(key, fs::md5File(*path, kDetectionHashBytes)).first;
                if (!it->second || *it->second != sig.md5) {
                    matches = false;
                    break;
                }
            }
        }
        if (matches)
            results.push_back({engineId, variant.gameId, variant.description, variant.language,
                               variant.platform});
    }
    return results;
}

Error runImport(const ImportPlan& plan, const std::filesystem::path& dest,
                const std::function<bool(uint64_t done, uint64_t total)>& progress) {
    const uint64_t total = plan.totalBytes();
    uint64_t done = 0;
    std::vector<char> buffer(1 << 20);
    for (const ImportPlan::File& f : plan.files) {
        const std::filesystem::path to = dest / std::filesystem::path(f.to);
        std::error_code ec;
        std::filesystem::create_directories(to.parent_path(), ec);
        if (ec)
            return Error::fail("Couldn't make " + to.parent_path().string() + ": " + ec.message());
        std::filesystem::path part = to;
        part += ".part";
        {
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
