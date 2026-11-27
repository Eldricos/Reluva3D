// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/tpw/saves.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

namespace reluva::tpw {

namespace {

constexpr char kMagic[8] = {'R', 'L', 'V', 'S', 'A', 'V', 'E', 0x1a};
constexpr uint32_t kFileVersion = 1;
constexpr const char *kSavesFolder = "saves";
constexpr const char *kExtension = ".rsv";
// A name or a state longer than these isn't one of ours.
constexpr uint32_t kLongestText = 1024;

void put32(std::vector<uint8_t> &out, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.push_back(uint8_t(v >> (8 * i)));
}

void putBytes(std::vector<uint8_t> &out, const void *p, size_t n) {
    put32(out, uint32_t(n));
    const auto *b = static_cast<const uint8_t *>(p);
    out.insert(out.end(), b, b + n);
}

// Takes from the front of what's left; false past its end.
struct Taker {
    const std::vector<uint8_t> &data;
    size_t at = 0;
    bool get32(uint32_t &v) {
        if (data.size() - at < 4)
            return false;
        v = 0;
        for (int i = 0; i < 4; ++i)
            v |= uint32_t(data[at + size_t(i)]) << (8 * i);
        at += 4;
        return true;
    }
    bool getBytes(std::vector<uint8_t> &out, uint32_t most) {
        uint32_t n = 0;
        if (!get32(n) || n > most || data.size() - at < n)
            return false;
        out.assign(data.begin() + long(at), data.begin() + long(at + n));
        at += n;
        return true;
    }
    bool getText(std::string &out) {
        std::vector<uint8_t> b;
        if (!getBytes(b, kLongestText))
            return false;
        out.assign(b.begin(), b.end());
        return true;
    }
};

} // namespace

std::filesystem::path savesFolder(const std::filesystem::path &saveFolder) {
    return saveFolder / kSavesFolder;
}

Error writeSave(const std::filesystem::path &file, const SavedGame &save) {
    std::vector<uint8_t> out(std::begin(kMagic), std::end(kMagic));
    put32(out, kFileVersion);
    putBytes(out, save.name.data(), save.name.size());
    putBytes(out, save.player.data(), save.player.size());
    putBytes(out, save.world.data(), save.world.size());
    put32(out, uint32_t(save.mission));
    put32(out, save.day);
    putBytes(out, save.park.data(), save.park.size());
    putBytes(out, save.screen.data(), save.screen.size());
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    if (!f.write(reinterpret_cast<const char *>(out.data()), std::streamsize(out.size())))
        return Error::fail("can't write " + file.string());
    return Error::success();
}

Error readSave(const std::filesystem::path &file, SavedGame &out, bool headerOnly) {
    std::ifstream f(file, std::ios::binary);
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.size() < sizeof kMagic || !std::equal(std::begin(kMagic), std::end(kMagic), data.begin()))
        return Error::fail(file.string() + " isn't a saved game");
    Taker t{data, sizeof kMagic};
    uint32_t version = 0;
    uint32_t mission = 0;
    if (!t.get32(version) || version != kFileVersion)
        return Error::fail(file.string() + " is a saved game of another version");
    out = {};
    if (!t.getText(out.name) || !t.getText(out.player) || !t.getText(out.world) || !t.get32(mission) ||
        !t.get32(out.day))
        return Error::fail(file.string() + " is cut short");
    out.mission = int(mission);
    if (headerOnly)
        return Error::success();
    if (!t.getBytes(out.park, uint32_t(data.size())) || !t.getBytes(out.screen, uint32_t(data.size())))
        return Error::fail(file.string() + " is cut short");
    return Error::success();
}

std::vector<SaveSlot> listSaves(const std::filesystem::path &folder, const std::string &player) {
    std::vector<SaveSlot> slots;
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(folder, ec)) {
        if (entry.path().extension() != kExtension)
            continue;
        SavedGame save;
        if (!readSave(entry.path(), save, true) && save.player == player)
            slots.push_back({entry.path(), save.name, save.day});
    }
    std::sort(slots.begin(), slots.end(),
              [](const SaveSlot &a, const SaveSlot &b) { return a.day != b.day ? a.day > b.day : a.file < b.file; });
    return slots;
}

Error deleteSave(const std::filesystem::path &file) {
    std::error_code ec;
    if (!std::filesystem::remove(file, ec) || ec)
        return Error::fail("can't delete " + file.string());
    return Error::success();
}

std::filesystem::path newSaveFile(const std::filesystem::path &folder) {
    for (int n = 1;; ++n) {
        const std::filesystem::path file = folder / ("save" + std::to_string(n) + kExtension);
        std::error_code ec;
        if (!std::filesystem::exists(file, ec))
            return file;
    }
}

} // namespace reluva::tpw
