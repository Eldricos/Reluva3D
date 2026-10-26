// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/x86/cpu.h"
#include "common/x86/win32.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace reluva::x86 {

// A COM interface as a program sees it: its methods in vtable order, each
// with the number of 32-bit argument words it takes after `this` (what the
// stdcall method pops). The first three are always IUnknown's.
struct ComInterface {
    std::string name; // "IDirectDraw7"
    std::string iid;  // "{15e65ec0-3b9c-11d2-b92f-00609797ea5b}", lower-case
    struct Method {
        const char* name;
        int args;
    };
    std::vector<Method> methods;
    uint32_t vtable = 0; // in the process's memory, once ComHost has made it
};

// An object behind one or more interface pointers. invoke() runs method
// `index` of `iface` (`this` is c.arg(0), its arguments c.arg(1) on) and
// returns its result; nothing means the method isn't stood in for, which
// ends the run naming it. IUnknown's three are ComHost's.
class ComObject : public std::enable_shared_from_this<ComObject> {
public:
    virtual ~ComObject() = default;
    virtual std::optional<uint32_t> invoke(const ComInterface& iface, int index, Cpu& c) = 0;
    // The interface QueryInterface finds for `iid`, if the object has it.
    virtual const ComInterface* query(const std::string& iid) = 0;
    uint32_t refs = 1;
};

// Makes the interface pointers programs call through: each a block of
// memory holding a vtable pointer, whose entries are native functions that
// find the object and call it.
class ComHost {
public:
    explicit ComHost(Win32Process& process) : process_(process) {}

    // Builds `iface`'s vtable (once; it must outlive the host).
    void define(ComInterface& iface);
    // `object`'s pointer as `iface` (defined), made the first time.
    uint32_t pointer(const std::shared_ptr<ComObject>& object, const ComInterface& iface);
    // The object behind an interface pointer; nullptr if it isn't one.
    ComObject* object(uint32_t pointer) const;
    template <class T>
    T* as(uint32_t pointer) const {
        return dynamic_cast<T*>(object(pointer));
    }
    // The object behind a pointer, shared; nullptr if it isn't a T.
    template <class T>
    std::shared_ptr<T> shared(uint32_t pointer) const {
        ComObject* o = object(pointer);
        return o ? std::dynamic_pointer_cast<T>(o->shared_from_this()) : nullptr;
    }
    // A GUID in memory as "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}".
    static std::string guid(Cpu& c, uint32_t address);
    // Writes a GUID given that way.
    static void writeGuid(Cpu& c, uint32_t address, const std::string& guid);

    Win32Process& process() { return process_; }

private:
    struct Entry {
        std::shared_ptr<ComObject> object;
        const ComInterface* iface;
    };
    Win32Process& process_;
    std::map<uint32_t, Entry> pointers_; // interface pointer -> object
    uint32_t dispatch(const ComInterface& iface, int index, Cpu& c);
    void release(ComObject* object);
};

constexpr uint32_t kSOk = 0, kSFalse = 1, kENoInterface = 0x80004002u, kEInvalidArg = 0x80070057u,
                   kEOutOfMemory = 0x8007000eu, kENotImpl = 0x80004001u, kEFail = 0x80004005u;

} // namespace reluva::x86
