// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// ADVAPI32's registry and ole32's COM start-up, as Microsoft documents
// them. The registry lives in memory: what the engine puts there (the
// values an installer would have written) and what the program writes.
// COM objects come from the engine (createComObject); without it, no class
// is registered.

#include "common/x86/win32.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace reluva::x86 {

namespace {

constexpr uint32_t kErrorFileNotFound = 2, kErrorInvalidHandle = 6, kErrorMoreData = 234,
                   kErrorNoMoreItems = 259;
constexpr uint32_t kRegSz = 1;
constexpr uint32_t kClassNotRegistered = 0x80040154u; // REGDB_E_CLASSNOTREG

std::string lower(std::string s) {
    for (char& c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

const char* rootName(uint32_t key) {
    switch (key) {
    case 0x80000000u: return "HKEY_CLASSES_ROOT";
    case 0x80000001u: return "HKEY_CURRENT_USER";
    case 0x80000002u: return "HKEY_LOCAL_MACHINE";
    case 0x80000003u: return "HKEY_USERS";
    case 0x80000005u: return "HKEY_CURRENT_CONFIG";
    default: return nullptr;
    }
}

std::string guid(Cpu& c, uint32_t p) {
    if (!c.valid(p, 16))
        return "{?}";
    char buf[40];
    std::snprintf(buf, sizeof buf, "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", c.read32(p), c.read16(p + 4),
                  c.read16(p + 6), c.read8(p + 8), c.read8(p + 9), c.read8(p + 10), c.read8(p + 11), c.read8(p + 12),
                  c.read8(p + 13), c.read8(p + 14), c.read8(p + 15));
    return buf;
}

} // namespace

void Win32Process::setRegistryValue(const std::string& key, const std::string& name, uint32_t type,
                                    std::vector<uint8_t> data) {
    // The key and those above it come into being.
    for (size_t at = key.find('\\'); at != std::string::npos; at = key.find('\\', at + 1))
        registry_.emplace(lower(key.substr(0, at)), RegistryKey{key.substr(0, at), {}});
    RegistryKey& k = registry_.emplace(lower(key), RegistryKey{key, {}}).first->second;
    for (auto& [n, v] : k.values)
        if (lower(n) == lower(name)) {
            v = {type, std::move(data)};
            return;
        }
    k.values.push_back({name, {type, std::move(data)}});
}

void Win32Process::setRegistryString(const std::string& key, const std::string& name, const std::string& text) {
    std::vector<uint8_t> data(text.begin(), text.end());
    data.push_back(0);
    setRegistryValue(key, name, kRegSz, std::move(data));
}

std::optional<std::vector<uint8_t>> Win32Process::registryValue(const std::string& key, const std::string& name,
                                                                uint32_t* type) const {
    auto it = registry_.find(lower(key));
    if (it == registry_.end())
        return std::nullopt;
    for (const auto& [n, v] : it->second.values)
        if (lower(n) == lower(name)) {
            if (type)
                *type = v.first;
            return v.second;
        }
    return std::nullopt;
}

void Win32Process::addRegistryStandIns() {
    const char* a = "ADVAPI32.dll";
    const char* o = "ole32.dll";

    // What every Windows has: the installed locales, English (United
    // States) the system's.
    const std::string locales = "HKEY_LOCAL_MACHINE\\System\\CurrentControlSet\\Control\\Nls\\Locale";
    setRegistryString(locales, "", "00000409");
    setRegistryString(locales, "00000409", "1");

    // The full path of `sub` under an open key or root; empty if the
    // handle is neither.
    auto pathOf = [this](Cpu& c, uint32_t key, uint32_t sub) -> std::string {
        std::string base;
        if (const char* root = rootName(key))
            base = root;
        else if (auto it = openKeys_.find(key); it != openKeys_.end())
            base = registry_.at(it->second).path;
        else
            return {};
        std::string rest = sub ? c.string(sub) : std::string();
        while (!rest.empty() && rest.back() == '\\')
            rest.pop_back();
        while (!rest.empty() && rest.front() == '\\')
            rest.erase(0, 1);
        return rest.empty() ? base : base + "\\" + rest;
    };
    auto open = [this](const std::string& path) {
        const uint32_t handle = nextKey_;
        nextKey_ += 4;
        openKeys_[handle] = lower(path);
        return handle;
    };
    auto keyOf = [this](uint32_t handle) -> RegistryKey* {
        if (const char* root = rootName(handle)) {
            auto it = registry_.emplace(lower(root), RegistryKey{root, {}}).first;
            return &it->second;
        }
        auto it = openKeys_.find(handle);
        return it == openKeys_.end() ? nullptr : &registry_.at(it->second);
    };

    // RegOpenKeyExA(key, sub, options, access, result)
    stdcall(a, "RegOpenKeyExA", 5, [this, pathOf, open](Cpu& c) {
        const std::string path = pathOf(c, c.arg(0), c.arg(1));
        if (path.empty())
            return kErrorInvalidHandle;
        const char* root = rootName(c.arg(0));
        const bool isRoot = root && lower(path) == lower(root); // a root is always there
        if (!registry_.count(lower(path)) && !isRoot)
            return kErrorFileNotFound;
        registry_.emplace(lower(path), RegistryKey{path, {}});
        c.write32(c.arg(4), open(path));
        return 0u;
    });
    stdcall(a, "RegOpenKeyA", 3, [this, pathOf, open](Cpu& c) {
        const std::string path = pathOf(c, c.arg(0), c.arg(1));
        if (path.empty())
            return kErrorInvalidHandle;
        if (!registry_.count(lower(path)))
            return kErrorFileNotFound;
        c.write32(c.arg(2), open(path));
        return 0u;
    });
    // RegCreateKeyExA(key, sub, reserved, class, options, access, security,
    // result, disposition): 1 made, 2 there already.
    stdcall(a, "RegCreateKeyExA", 9, [this, pathOf, open](Cpu& c) {
        const std::string path = pathOf(c, c.arg(0), c.arg(1));
        if (path.empty())
            return kErrorInvalidHandle;
        const bool existed = registry_.count(lower(path)) != 0;
        if (!existed) {
            for (size_t at = path.find('\\'); at != std::string::npos; at = path.find('\\', at + 1))
                registry_.emplace(lower(path.substr(0, at)), RegistryKey{path.substr(0, at), {}});
            registry_.emplace(lower(path), RegistryKey{path, {}});
        }
        c.write32(c.arg(7), open(path));
        if (c.arg(8))
            c.write32(c.arg(8), existed ? 2u : 1u);
        return 0u;
    });
    stdcall(a, "RegCloseKey", 1, [this](Cpu& c) {
        if (rootName(c.arg(0)))
            return 0u;
        return openKeys_.erase(c.arg(0)) ? 0u : kErrorInvalidHandle;
    });
    // RegSetValueExA(key, name, reserved, type, data, size)
    stdcall(a, "RegSetValueExA", 6, [this, keyOf](Cpu& c) {
        RegistryKey* k = keyOf(c.arg(0));
        if (!k || !c.valid(c.arg(4), c.arg(5)))
            return kErrorInvalidHandle;
        const std::string name = c.arg(1) ? c.string(c.arg(1)) : std::string();
        const uint8_t* p = c.memory().data() + c.arg(4);
        const std::string path = k->path;
        setRegistryValue(path, name, c.arg(3), std::vector<uint8_t>(p, p + c.arg(5)));
        if (registryWritten)
            registryWritten(path, name);
        return 0u;
    });
    // RegQueryValueExA(key, name, reserved, type, data, size): with no
    // buffer, or too small a one, the size needed.
    stdcall(a, "RegQueryValueExA", 6, [this, keyOf](Cpu& c) {
        RegistryKey* k = keyOf(c.arg(0));
        if (!k)
            return kErrorInvalidHandle;
        const std::string name = c.arg(1) ? c.string(c.arg(1)) : std::string();
        uint32_t type = 0;
        auto value = registryValue(k->path, name, &type);
        if (!value)
            return kErrorFileNotFound;
        const uint32_t typeOut = c.arg(3), data = c.arg(4), size = c.arg(5);
        if (typeOut)
            c.write32(typeOut, type);
        const uint32_t n = uint32_t(value->size());
        const uint32_t room = size ? c.read32(size) : 0;
        if (size)
            c.write32(size, n);
        if (!data)
            return 0u;
        if (room < n || !c.valid(data, n))
            return kErrorMoreData;
        if (n)
            std::memcpy(c.memory().data() + data, value->data(), n);
        return 0u;
    });
    // RegEnumValueA(key, index, name, name size, reserved, type, data, data
    // size): the sizes are in and out; the name's leaves out its terminator.
    stdcall(a, "RegEnumValueA", 8, [keyOf](Cpu& c) {
        RegistryKey* k = keyOf(c.arg(0));
        if (!k)
            return kErrorInvalidHandle;
        const uint32_t index = c.arg(1);
        if (index >= k->values.size())
            return kErrorNoMoreItems;
        const auto& [name, value] = k->values[index];
        const uint32_t nameOut = c.arg(2), nameSize = c.arg(3), typeOut = c.arg(5), data = c.arg(6),
                       dataSize = c.arg(7);
        if (!c.valid(nameSize, 4) || c.read32(nameSize) < name.size() + 1 || !c.valid(nameOut, uint32_t(name.size() + 1)))
            return kErrorMoreData;
        c.writeString(nameOut, name, uint32_t(name.size() + 1));
        c.write32(nameSize, uint32_t(name.size()));
        if (typeOut)
            c.write32(typeOut, value.first);
        const uint32_t n = uint32_t(value.second.size());
        if (dataSize) {
            const uint32_t room = c.read32(dataSize);
            c.write32(dataSize, n);
            if (data) {
                if (room < n || !c.valid(data, n))
                    return kErrorMoreData;
                if (n)
                    std::memcpy(c.memory().data() + data, value.second.data(), n);
            }
        }
        return 0u;
    });

    // --- COM
    stdcall(o, "CoInitialize", 1, [](Cpu&) { return 0u; }); // S_OK
    stdcall(o, "CoInitializeEx", 2, [](Cpu&) { return 0u; });
    stdcall(o, "CoUninitialize", 0, [](Cpu&) { return 0u; });
    // CoCreateInstance(class, outer, context, interface, result)
    stdcall(o, "CoCreateInstance", 5, [this](Cpu& c) {
        const uint32_t out = c.arg(4);
        if (out && c.valid(out, 4))
            c.write32(out, 0);
        if (!createComObject)
            return kClassNotRegistered;
        return createComObject(guid(c, c.arg(0)), guid(c, c.arg(3)), out);
    });
}

} // namespace reluva::x86
