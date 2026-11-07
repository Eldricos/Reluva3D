// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/vm_services.h"

#include "engines/idtech3/filesystem.h"

#include "engines/idtech3/qvm.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace reluva::idtech3 {

int CallCounts::count(int number) const {
    if (number >= 0 && number < kDirect)
        return direct_[size_t(number)];
    const auto it = others_.find(number);
    return it == others_.end() ? 0 : it->second;
}

std::map<int, int> CallCounts::map() const {
    std::map<int, int> out = others_;
    for (int i = 0; i < kDirect; ++i)
        if (direct_[size_t(i)])
            out[i] = direct_[size_t(i)];
    return out;
}

int32_t GameFiles::open(const std::string& path, int32_t mode, bool wantHandle, bool writable, uint32_t& handle) {
    handle = 0;
    OpenFile file;
    if (mode != 0) {
        if (!writable || path.empty())
            return -1;
    } else if (path.empty() || files_.read(path, file.data)) {
        return -1;
    } else if (onRead) {
        onRead(path);
    }
    const auto length = int32_t(file.data.size());
    if (wantHandle) {
        handle = next_++;
        open_[handle] = std::move(file);
    }
    return length;
}

size_t GameFiles::readable(uint32_t handle, size_t length) const {
    const auto it = open_.find(handle);
    if (it == open_.end())
        return 0;
    const OpenFile& f = it->second;
    return std::min(length, f.data.size() - std::min(f.position, f.data.size()));
}

size_t GameFiles::read(uint32_t handle, uint8_t* out, size_t length) {
    const size_t n = readable(handle, length);
    if (n) {
        OpenFile& f = open_.at(handle);
        std::memcpy(out, f.data.data() + f.position, n);
        f.position += n;
    }
    return n;
}

size_t GameFiles::write(uint32_t handle, const uint8_t* in, size_t length) {
    const auto it = open_.find(handle);
    if (it == open_.end())
        return 0;
    it->second.data.insert(it->second.data.end(), in, in + length);
    return length;
}

int32_t VmFiles::open(Qvm& vm, int32_t pathAddress, int32_t handleAddress, int32_t mode) {
    if (handleAddress)
        vm.writeInt(handleAddress, 0);
    uint32_t handle = 0;
    const int32_t length = open_.open(vm.string(pathAddress, 256), mode,
                                      handleAddress && vm.valid(handleAddress, 4), false, handle);
    if (handle)
        vm.writeInt(handleAddress, int32_t(handle));
    return length;
}

int32_t VmFiles::read(Qvm& vm, int32_t buffer, int32_t length, int32_t handle) {
    if (length <= 0)
        return 0;
    const size_t n = open_.readable(uint32_t(handle), size_t(length));
    if (!vm.checkWrite(buffer, int64_t(n)))
        return 0;
    return int32_t(open_.read(uint32_t(handle), n ? &vm.memory()[size_t(buffer)] : nullptr, n));
}

std::vector<std::string> VmFiles::listNames(const SearchSet& files, std::string_view folder,
                                            std::string_view extension) {
    std::string prefix = normalizeArchiveName(folder);
    while (!prefix.empty() && prefix.back() == '/')
        prefix.pop_back();
    if (!prefix.empty())
        prefix += '/';
    const bool folders = extension == "/";
    const std::string ext = normalizeArchiveName(extension);

    std::vector<std::string> out;
    std::set<std::string> seen; // folders, by normalized name
    for (const std::string& name : files.list()) {
        const std::string key = normalizeArchiveName(name);
        if (key.compare(0, prefix.size(), prefix) != 0)
            continue;
        const std::string rest = name.substr(prefix.size());
        const size_t slash = rest.find('/');
        if (folders) {
            if (slash != std::string::npos && slash > 0 && seen.insert(key.substr(prefix.size(), slash)).second)
                out.push_back(rest.substr(0, slash));
            continue;
        }
        if (slash != std::string::npos || rest.empty())
            continue;
        if (key.size() >= ext.size() && key.compare(key.size() - ext.size(), ext.size(), ext) == 0)
            out.push_back(rest);
    }
    return out;
}

int32_t VmFiles::list(Qvm& vm, int32_t folderAddress, int32_t extensionAddress, int32_t buffer, int32_t size) const {
    const std::string folder = vm.string(folderAddress, 256);
    if (size <= 0 || !vm.valid(buffer, size))
        return 0;
    if (folder == "$modlist") {
        // The mods' folders and descriptions, as the engine lists them.
        std::vector<uint8_t> list;
        const uint32_t count =
            packModList(modInstall_.empty() ? std::vector<ModFolder>{} : listMods(modInstall_, modBaseDir_), uint32_t(size), list);
        std::memcpy(&vm.memory()[size_t(buffer)], list.data(), list.size());
        return int32_t(count);
    }
    const auto names = listNames(files_, folder, vm.string(extensionAddress, 64));
    int32_t count = 0, used = 0;
    for (const std::string& n : names) {
        const auto need = int32_t(n.size()) + 1;
        if (used + need > size)
            break;
        vm.writeString(buffer + used, n, need);
        used += need;
        ++count;
    }
    return count;
}

bool commonSystemCall(Qvm& vm, int number, const int32_t* a, int32_t& result) {
    auto f = [](int32_t w) { return Qvm::asFloat(w); };
    result = 0;
    switch (number) {
    case 100: // memset(dest, value, count)
        if (a[2] > 0 && vm.checkWrite(a[0], a[2]))
            std::memset(&vm.memory()[size_t(a[0])], a[1], size_t(a[2]));
        result = a[0];
        return true;
    case 101: // memcpy(dest, src, count)
        if (!vm.valid(a[1], a[2]))
            vm.abort("memcpy from outside the program's memory");
        else if (a[2] > 0 && vm.checkWrite(a[0], a[2]))
            std::memmove(&vm.memory()[size_t(a[0])], &vm.memory()[size_t(a[1])], size_t(a[2]));
        result = a[0];
        return true;
    case 102: { // strncpy(dest, src, count): copies up to count bytes, pads with NULs
        if (a[2] <= 0 || !vm.checkWrite(a[0], a[2]))
            return true;
        const std::string src = vm.string(a[1], size_t(std::max(a[2], 0)));
        for (int32_t i = 0; i < a[2]; ++i)
            vm.memory()[size_t(a[0] + i)] = size_t(i) < src.size() ? uint8_t(src[size_t(i)]) : 0;
        result = a[0];
        return true;
    }
    case 103: result = Qvm::fromFloat(std::sin(f(a[0]))); return true;
    case 104: result = Qvm::fromFloat(std::cos(f(a[0]))); return true;
    case 105: result = Qvm::fromFloat(std::atan2(f(a[0]), f(a[1]))); return true;
    case 106: result = Qvm::fromFloat(std::sqrt(f(a[0]))); return true;
    default: return false;
    }
}

int32_t HandleTable::add(std::string_view name) {
    if (name.empty())
        return 0;
    const std::string key = normalizeArchiveName(name);
    if (const auto it = byName_.find(key); it != byName_.end())
        return it->second;
    names_.emplace_back(name);
    const auto handle = int32_t(names_.size());
    byName_.emplace(key, handle);
    return handle;
}

const std::string& HandleTable::name(int32_t handle) const {
    static const std::string none;
    return handle >= 1 && size_t(handle) <= names_.size() ? names_[size_t(handle) - 1] : none;
}

} // namespace reluva::idtech3
