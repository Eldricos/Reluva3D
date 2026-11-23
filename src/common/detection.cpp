// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/detection.h"
#include "common/fs.h"

#include "common/text.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <optional>

namespace reluva {

std::vector<DetectedGame> detectFromTable(const std::string &engineId, const std::vector<GameVariant> &table,
                                          const std::filesystem::path &dir) {
    // Many variants share files, so cache per-file results.
    std::map<std::string, std::optional<std::filesystem::path>> found;
    std::map<std::string, std::optional<std::string>> hashes;

    auto locate = [&](const std::string &name) -> const std::optional<std::filesystem::path> & {
        auto it = found.find(name);
        if (it == found.end())
            it = found.emplace(name, fs::findFile(dir, name)).first;
        return it->second;
    };

    std::vector<DetectedGame> results;
    for (const GameVariant &variant : table) {
        bool matches = !variant.files.empty();
        for (const FileSignature &sig : variant.files) {
            const auto &path = locate(sig.name);
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
            results.push_back({engineId, variant.gameId, variant.description, variant.language, variant.platform});
    }
    return results;
}

std::vector<InstallShieldGroup> installShieldGroups(const InstallShieldCabinet &cabinet,
                                                    const std::vector<std::string> &names) {
    std::vector<InstallShieldGroup> found;
    for (const std::string &name : names) {
        auto it = std::find_if(cabinet.groups.begin(), cabinet.groups.end(),
                               [&](const InstallShieldGroup &g) { return g.name == name; });
        if (it == cabinet.groups.end())
            return {};
        found.push_back(*it);
    }
    return found;
}

bool installShieldMatches(const GameVariant &variant, const InstallShieldCabinet &cabinet,
                          const std::vector<InstallShieldGroup> &groups) {
    return std::all_of(variant.files.begin(), variant.files.end(), [&](const FileSignature &sig) {
        return std::any_of(groups.begin(), groups.end(), [&](const InstallShieldGroup &g) {
            const long i = cabinet.find(g, sig.name);
            return i >= 0 && (sig.size < 0 || cabinet.files[size_t(i)].size == uint64_t(sig.size));
        });
    });
}

void planInstallShield(ImportPlan &plan, const std::shared_ptr<const InstallShieldCabinet> &cabinet,
                       const std::vector<InstallShieldGroup> &groups, const std::vector<std::string> &leaveOut) {
    plan.cabinet = cabinet;
    plan.files.clear();
    plan.notes.clear();
    std::vector<int> wanted;
    std::vector<std::string> loose; // not on the CDs given (on one still to be added, maybe)
    // The CDs given: the header's and each volume's folder.
    std::vector<std::filesystem::path> discs = {cabinet->header.parent_path()};
    for (const InstallShieldVolume &v : cabinet->volumes)
        if (!v.path.empty() && std::find(discs.begin(), discs.end(), v.path.parent_path()) == discs.end())
            discs.push_back(v.path.parent_path());
    for (const InstallShieldGroup &g : groups)
        for (size_t i = g.first; i <= g.last && i < cabinet->files.size(); ++i) {
            const InstallShieldFile &f = cabinet->files[i];
            if (f.path.empty() || std::any_of(leaveOut.begin(), leaveOut.end(), [&](const std::string &folder) {
                    return f.path.size() > folder.size() && f.path[folder.size()] == '/' &&
                           text::equalsIgnoreCase(f.path.substr(0, folder.size()), folder);
                }))
                continue;
            if (f.loose) {
                std::optional<std::filesystem::path> found;
                for (size_t d = 0; d < discs.size() && !found; ++d)
                    found = fs::findFile(discs[d], f.path);
                if (found)
                    plan.files.push_back({*found, f.path, f.size, std::nullopt, std::nullopt});
                else
                    loose.push_back(f.path);
                continue;
            }
            plan.files.push_back({cabinet->header, f.path, f.size, std::nullopt, i});
            if (cabinet->reachable(i))
                continue;
            for (size_t v = cabinet->files[f.data].volume; v < cabinet->volumes.size(); ++v)
                if (cabinet->volumes[v].path.empty() && std::find(wanted.begin(), wanted.end(), int(v)) == wanted.end())
                    wanted.push_back(int(v));
            // Version 5's volumes are known as they're found: the next one, at least.
            if (cabinet->files[f.data].volume >= cabinet->volumes.size())
                if (const int v = cabinet->files[f.data].volume;
                    std::find(wanted.begin(), wanted.end(), v) == wanted.end())
                    wanted.push_back(v);
        }
    if (wanted.empty()) {
        for (const std::string &path : loose)
            plan.notes.push_back(path + " isn't on the CDs: it's left out");
        return;
    }
    std::sort(wanted.begin(), wanted.end());
    std::string names;
    for (int v : wanted)
        names += (names.empty() ? "" : ", ") + cabinet->volumeName(v);
    plan.notes.push_back("The game is on two CDs: add the other (with " + names +
                         ") with \"Add other disc...\" to import it");
    plan.files.clear();
}

bool addInstallShieldVolumes(ImportPlan &plan, const std::filesystem::path &folder,
                             const std::vector<InstallShieldGroup> &groups, std::string &message,
                             const std::vector<std::string> &leaveOut) {
    if (!plan.cabinet) {
        message = "not a file Reluva can use for this game";
        return false;
    }
    auto cabinet = std::make_shared<InstallShieldCabinet>(*plan.cabinet);
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::is_directory(folder, ec) ? folder : folder.parent_path();
    if (findInstallShieldVolumes(*cabinet, dir) == 0) {
        std::string names;
        for (size_t v = 1; v < cabinet->volumes.size(); ++v)
            if (cabinet->volumes[v].path.empty())
                names += (names.empty() ? "" : ", ") + cabinet->volumeName(int(v));
        message = names.empty() ? "the game's CDs are both added already"
                                : "not the game's other CD: none of " + names + " is here";
        return false;
    }
    planInstallShield(plan, cabinet, groups, leaveOut);
    return true;
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
