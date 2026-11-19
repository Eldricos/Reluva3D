// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// What the Need for Speed engine's reluva-tool commands share: reading and
// writing files, a file from a .viv.

#pragma once

#include "common/types.h"
#include "engines/nfs/formats/viv.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace reluva::nfs::tools {

using reluva::Error;
using reluva::nfs::VivArchive;
namespace stdfs = std::filesystem;

inline std::string lowerAscii(std::string s) {
    for (char &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

inline std::string safeObjName(const std::string &s) {
    std::string out;
    for (char c : s)
        out += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    return out;
}

inline int fail(const Error &err) {
    std::fprintf(stderr, "%s\n", err.message.c_str());
    return 1;
}

inline bool readFile(const stdfs::path &path, std::vector<uint8_t> &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

inline bool writeFile(const stdfs::path &path, const std::vector<uint8_t> &data) {
    std::error_code ec;
    stdfs::create_directories(path.parent_path(), ec);
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
    return static_cast<bool>(f);
}

// A name from an archive as a file name in an output folder, or empty if
// it could escape it.
inline stdfs::path safeName(const std::string &name) {
    const stdfs::path rel(name);
    if (rel.empty() || rel.is_absolute() || rel.has_root_name())
        return {};
    for (const auto &part : rel)
        if (part == "..")
            return {};
    return rel;
}

// A file's contents, or a file's in a .viv when `inner` is given.
inline Error load(const char *file, const char *inner, std::vector<uint8_t> &out) {
    if (!inner) {
        if (!readFile(file, out))
            return Error::fail(std::string("Can't read ") + file);
        return Error::success();
    }
    VivArchive viv;
    if (Error err = viv.open(file))
        return err;
    return viv.read(inner, out);
}

} // namespace reluva::nfs::tools
