// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/x86/com.h"

#include <cstdio>
#include <cstdlib>

namespace reluva::x86 {

void ComHost::define(ComInterface& iface) {
    if (iface.vtable)
        return;
    Cpu& c = process_.cpu();
    iface.vtable = process_.memory().alloc(uint32_t(iface.methods.size() * 4));
    for (size_t i = 0; i < iface.methods.size(); ++i) {
        const int index = int(i);
        const uint32_t pop = 4u * uint32_t(1 + iface.methods[i].args);
        const ComInterface* in = &iface;
        const uint32_t fn = c.addNative(iface.name + "::" + iface.methods[i].name, [this, in, index, pop](Cpu& cpu) {
            cpu.reg[Cpu::EAX] = dispatch(*in, index, cpu);
            return pop;
        });
        c.write32(iface.vtable + 4 * uint32_t(i), fn);
    }
}

uint32_t ComHost::pointer(const std::shared_ptr<ComObject>& object, const ComInterface& iface) {
    for (const auto& [p, e] : pointers_)
        if (e.object == object && e.iface == &iface)
            return p;
    const uint32_t p = process_.memory().alloc(8);
    process_.cpu().write32(p, iface.vtable);
    pointers_[p] = {object, &iface};
    return p;
}

ComObject* ComHost::object(uint32_t pointer) const {
    auto it = pointers_.find(pointer);
    return it == pointers_.end() ? nullptr : it->second.object.get();
}

void ComHost::release(ComObject* object) {
    for (auto it = pointers_.begin(); it != pointers_.end();) {
        if (it->second.object.get() == object) {
            process_.memory().free(it->first);
            it = pointers_.erase(it);
        } else {
            ++it;
        }
    }
}

uint32_t ComHost::dispatch(const ComInterface& iface, int index, Cpu& c) {
    auto it = pointers_.find(c.arg(0));
    if (it == pointers_.end()) {
        c.abort(iface.name + "::" + iface.methods[size_t(index)].name + " called on something that isn't one");
        return kEFail;
    }
    const std::shared_ptr<ComObject> object = it->second.object; // alive until this returns
    switch (index) {
    case 0: { // QueryInterface(iid, out)
        const uint32_t out = c.arg(2);
        const ComInterface* other = object->query(guid(c, c.arg(1)));
        if (!other || !other->vtable) {
            if (c.valid(out, 4))
                c.write32(out, 0);
            return kENoInterface;
        }
        ++object->refs;
        c.write32(out, pointer(object, *other));
        return kSOk;
    }
    case 1: return ++object->refs; // AddRef
    case 2:                        // Release
        if (object->refs > 0 && --object->refs == 0) {
            release(object.get());
            return 0;
        }
        return object->refs;
    default: {
        std::optional<uint32_t> r = object->invoke(iface, index, c);
        if (!r) {
            c.abort(iface.name + "::" + iface.methods[size_t(index)].name + " isn't stood in for");
            return kENotImpl;
        }
        return *r;
    }
    }
}

std::string ComHost::guid(Cpu& c, uint32_t p) {
    if (!c.valid(p, 16))
        return "{}";
    char buf[40];
    std::snprintf(buf, sizeof buf, "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", c.read32(p), c.read16(p + 4),
                  c.read16(p + 6), c.read8(p + 8), c.read8(p + 9), c.read8(p + 10), c.read8(p + 11), c.read8(p + 12),
                  c.read8(p + 13), c.read8(p + 14), c.read8(p + 15));
    return buf;
}

void ComHost::writeGuid(Cpu& c, uint32_t p, const std::string& g) {
    // {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}
    if (g.size() != 38 || !c.valid(p, 16))
        return;
    auto hex = [&](size_t at, size_t n) { return uint32_t(std::strtoul(g.substr(at, n).c_str(), nullptr, 16)); };
    c.write32(p, hex(1, 8));
    c.write16(p + 4, uint16_t(hex(10, 4)));
    c.write16(p + 6, uint16_t(hex(15, 4)));
    c.write8(p + 8, uint8_t(hex(20, 2)));
    c.write8(p + 9, uint8_t(hex(22, 2)));
    for (int i = 0; i < 6; ++i)
        c.write8(p + 10 + uint32_t(i), uint8_t(hex(25 + 2 * size_t(i), 2)));
}

} // namespace reluva::x86
