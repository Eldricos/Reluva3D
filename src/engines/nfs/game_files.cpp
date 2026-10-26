// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/game_files.h"

#include "common/archive.h"
#include "common/fs.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>

namespace reluva::nfs {

namespace {

namespace stdfs = std::filesystem;

bool endsWith(const std::string& lower, const char* ext) {
    const size_t n = std::char_traits<char>::length(ext);
    return lower.size() >= n && lower.compare(lower.size() - n, n, ext) == 0;
}

} // namespace

void GameFiles::scan(const GameFolders& folders) {
    files_.clear();
    cached_.reset();
    cachedPath_.clear();
    std::set<std::string> seen; // normalized relative paths
    std::vector<stdfs::path> roots = {folders.install};
    if (!folders.cd.empty() && folders.cd != folders.install)
        roots.push_back(folders.cd);

    for (const stdfs::path& root : roots) {
        std::vector<std::pair<std::string, stdfs::path>> found;
        std::error_code ec;
        for (auto it = stdfs::recursive_directory_iterator(root, ec); !ec && it != stdfs::recursive_directory_iterator();
             it.increment(ec))
            if (it->is_regular_file(ec))
                found.emplace_back(stdfs::relative(it->path(), root, ec).generic_string(), it->path());
        std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
            return normalizeArchiveName(a.first) < normalizeArchiveName(b.first);
        });

        for (const auto& [rel, path] : found) {
            if (!seen.insert(normalizeArchiveName(rel)).second)
                continue;
            files_.push_back({rel, path, {}});
            if (!endsWith(fs::toLower(rel), ".viv"))
                continue;
            VivArchive viv;
            if (viv.open(path))
                continue; // a broken archive just adds nothing
            for (const auto& e : viv.entries())
                files_.push_back({rel + "/" + e.name, path, e.name});
        }
    }
}

std::vector<FileRef> GameFiles::withExtensions(std::initializer_list<const char*> extensions) const {
    std::vector<FileRef> out;
    for (const FileRef& f : files_) {
        const std::string lower = fs::toLower(f.name);
        for (const char* ext : extensions)
            if (endsWith(lower, ext)) {
                out.push_back(f);
                break;
            }
    }
    return out;
}

std::vector<FileRef> GameFiles::textTables() const {
    std::vector<FileRef> out;
    for (const FileRef& f : files_) {
        const std::string lower = fs::toLower(f.name);
        if (f.inner.empty() && lower.rfind("fedata/text/text.", 0) == 0)
            out.push_back(f);
    }
    return out;
}

Error GameFiles::read(const FileRef& ref, std::vector<uint8_t>& out) const {
    if (ref.inner.empty()) {
        std::ifstream in(ref.file, std::ios::binary);
        if (!in)
            return Error::fail("Can't read " + ref.file.string());
        out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return Error::success();
    }
    if (!cached_ || cachedPath_ != ref.file) {
        auto viv = std::make_unique<VivArchive>();
        if (Error err = viv->open(ref.file))
            return err;
        cached_ = std::move(viv);
        cachedPath_ = ref.file;
    }
    return cached_->read(ref.inner, out);
}

} // namespace reluva::nfs
