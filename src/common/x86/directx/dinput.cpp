// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// DirectInput, as the DirectX 7 documentation describes it. Interface
// method orders, GUIDs and structure layouts are the SDK's dinput.h.

#include "common/x86/directx/dinput.h"

#include "common/x86/com.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <string_view>

namespace reluva::x86 {

namespace {

const char* const kIidDirectInput = "{89521360-aa8a-11cf-bfc7-444553540000}";
const char* const kIidDirectInput2 = "{5944e662-aa8a-11cf-bfc7-444553540000}";
const char* const kIidDirectInput7 = "{9a4cb684-236d-11d3-8e9d-00c04f6844ae}";
const char* const kIidDevice = "{5944e680-c92e-11cf-bfc7-444553540000}";
const char* const kIidDevice2 = "{5944e682-c92e-11cf-bfc7-444553540000}";
const char* const kIidDevice7 = "{57d7c6bc-2356-11d3-8e9d-00c04f6844ae}";
const char* const kSysMouse = "{6f1d2b60-d5a0-11cf-bfc7-444553540000}";
const char* const kSysKeyboard = "{6f1d2b61-d5a0-11cf-bfc7-444553540000}";
const char* const kXAxis = "{a36d02e0-c9f3-11cf-bfc7-444553540000}";
const char* const kYAxis = "{a36d02e1-c9f3-11cf-bfc7-444553540000}";
const char* const kZAxis = "{a36d02e2-c9f3-11cf-bfc7-444553540000}";
const char* const kButton = "{a36d02f0-c9f3-11cf-bfc7-444553540000}";
const char* const kKey = "{55728220-d33c-11cf-bfc7-444553540000}";

constexpr uint32_t kOk = 0, kNoEffect = 1, kBufferOverflow = 1;
constexpr uint32_t kErrInvalidParam = 0x80070057u, kErrNotAcquired = 0x8007000cu, kErrDeviceNotReg = 0x80040154u,
                   kErrNotFound = 0x80070002u, kErrUnsupported = 0x80004001u;
// Data format object types (DIDFT_).
constexpr uint32_t kTypeAxis = 0x3, kTypeButton = 0xc, kTypeAnyInstance = 0xffff, kTypeOptional = 0x80000000u;
constexpr uint32_t kRelativeAxes = 2; // DIDF_RELAXIS
constexpr uint32_t kDevTypeMouse = 2, kDevTypeKeyboard = 3;

const std::vector<ComInterface::Method> kUnknown = {{"QueryInterface", 2}, {"AddRef", 0}, {"Release", 0}};
std::vector<ComInterface::Method> with(std::vector<ComInterface::Method> m) {
    m.insert(m.begin(), kUnknown.begin(), kUnknown.end());
    return m;
}
std::vector<ComInterface::Method> first(const std::vector<ComInterface::Method>& m, size_t n) {
    return {m.begin(), m.begin() + long(n)};
}

const std::vector<ComInterface::Method> kDirectInput7 = with({
    {"CreateDevice", 3}, {"EnumDevices", 4}, {"GetDeviceStatus", 1}, {"RunControlPanel", 2}, {"Initialize", 2},
    {"FindDevice", 3},     // 2
    {"CreateDeviceEx", 4}, // 7
});
const std::vector<ComInterface::Method> kDevice7 = with({
    {"GetCapabilities", 1}, {"EnumObjects", 3}, {"GetProperty", 2}, {"SetProperty", 2}, {"Acquire", 0},
    {"Unacquire", 0}, {"GetDeviceState", 2}, {"GetDeviceData", 4}, {"SetDataFormat", 1},
    {"SetEventNotification", 1}, {"SetCooperativeLevel", 2}, {"GetObjectInfo", 3}, {"GetDeviceInfo", 1},
    {"RunControlPanel", 2}, {"Initialize", 3},
    {"CreateEffect", 4}, {"EnumEffects", 3}, {"GetEffectInfo", 2}, {"GetForceFeedbackState", 1},
    {"SendForceFeedbackCommand", 1}, {"EnumCreatedEffectObjects", 3}, {"Escape", 1}, {"Poll", 0},
    {"SendDeviceData", 4}, // 2
    {"EnumEffectsInFile", 4}, {"WriteEffectToFile", 4}, // 7
});

} // namespace

struct DirectInput::State {
    Win32Process& process;
    Cpu& cpu;
    ComHost com;
    ComInterface directInput{"IDirectInputA", kIidDirectInput, first(kDirectInput7, 8)};
    ComInterface directInput2{"IDirectInput2A", kIidDirectInput2, first(kDirectInput7, 9)};
    ComInterface directInput7{"IDirectInput7A", kIidDirectInput7, kDirectInput7};
    ComInterface device{"IDirectInputDeviceA", kIidDevice, first(kDevice7, 18)};
    ComInterface device2{"IDirectInputDevice2A", kIidDevice2, first(kDevice7, 27)};
    ComInterface device7{"IDirectInputDevice7A", kIidDevice7, kDevice7};
    explicit State(Win32Process& p) : process(p), cpu(p.cpu()), com(p) {}
};

namespace {

// The system mouse (3 axes, 4 buttons) or keyboard (256 keys by scan code).
class Device : public ComObject {
public:
    DirectInput::State* s = nullptr;
    bool mouse = false;
    bool acquired = false;
    // The data format: each object's place in the program's data.
    struct Object {
        enum class Kind { Axis, Button, Key } kind;
        int index;       // axis 0-2, button 0-7, key scan code
        uint32_t offset; // in the data
    };
    std::vector<Object> objects;
    uint32_t dataSize = 0;
    bool relative = true;
    // The device's state.
    int axes[3]{}, motion[3]{}; // position; movement since last read
    bool buttons[8]{};
    bool keys[256]{};
    // Buffered changes (DIDEVICEOBJECTDATA's offset, data, time, sequence).
    struct Event {
        uint32_t offset, data, time, sequence;
    };
    std::deque<Event> buffer;
    uint32_t bufferSize = 0, sequence = 0;
    bool overflowed = false;

    const ComInterface* query(const std::string& iid) override {
        for (ComInterface* f : {&s->device7, &s->device2, &s->device})
            if (iid == f->iid)
                return f;
        return nullptr;
    }

    void hear(const Win32Process::RawInput& r) {
        using Kind = Win32Process::RawInput::Kind;
        if (mouse) {
            if (r.kind == Kind::Motion) {
                change(Object::Kind::Axis, 0, r.a);
                change(Object::Kind::Axis, 1, r.b);
            } else if (r.kind == Kind::Wheel) {
                change(Object::Kind::Axis, 2, r.a);
            } else if (r.kind == Kind::Button && r.a >= 0 && r.a < 8) {
                buttons[r.a] = r.b != 0;
                record(Object::Kind::Button, r.a, r.b ? 0x80 : 0);
            }
        } else if (r.kind == Kind::Key && r.a >= 0 && r.a < 256) {
            keys[r.a] = r.b != 0;
            record(Object::Kind::Key, r.a, r.b ? 0x80 : 0);
        }
    }
    void change(Object::Kind kind, int axis, int by) {
        if (!by)
            return;
        axes[axis] += by;
        motion[axis] += by;
        record(kind, axis, uint32_t(relative ? by : axes[axis]));
    }
    void record(Object::Kind kind, int index, uint32_t data) {
        if (!acquired || !bufferSize)
            return;
        for (const Object& o : objects)
            if (o.kind == kind && o.index == index) {
                if (buffer.size() >= bufferSize) {
                    buffer.pop_front();
                    overflowed = true;
                }
                buffer.push_back({o.offset, data, s->process.milliseconds ? s->process.milliseconds() : 0, ++sequence});
            }
    }

    // SetDataFormat(DIDATAFORMAT*): size, object size, flags, data size,
    // object count, objects (GUID*, offset, type, flags).
    uint32_t setFormat(Cpu& c, uint32_t p) {
        if (!c.valid(p, 24))
            return kErrInvalidParam;
        const uint32_t objSize = c.read32(p + 4), flags = c.read32(p + 8), count = c.read32(p + 16), list = c.read32(p + 20);
        if (!c.valid(list, objSize * count))
            return kErrInvalidParam;
        std::vector<Object> found;
        bool usedAxes[3]{}, usedButtons[8]{}, usedKeys[256]{};
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t o = list + i * objSize;
            const uint32_t guidPtr = c.read32(o), offset = c.read32(o + 4), type = c.read32(o + 8);
            const std::string guid = guidPtr ? ComHost::guid(c, guidPtr) : std::string();
            const uint32_t instance = (type >> 8) & 0xffff;
            const bool any = instance == kTypeAnyInstance;
            int index = -1;
            Object::Kind kind;
            if (mouse && (guid == kXAxis || guid == kYAxis || guid == kZAxis || (guid.empty() && (type & kTypeAxis)))) {
                kind = Object::Kind::Axis;
                index = guid == kXAxis ? 0 : guid == kYAxis ? 1 : guid == kZAxis ? 2 : any ? -1 : int(instance);
                if (index < 0)
                    for (int a = 0; a < 3 && index < 0; ++a)
                        if (!usedAxes[a])
                            index = a;
                if (index >= 3)
                    index = -1;
                if (index >= 0)
                    usedAxes[index] = true;
            } else if (mouse && (guid == kButton || (guid.empty() && (type & kTypeButton)))) {
                kind = Object::Kind::Button;
                index = any ? -1 : int(instance);
                if (index < 0)
                    for (int b = 0; b < 8 && index < 0; ++b)
                        if (!usedButtons[b])
                            index = b;
                if (index >= 8)
                    index = -1;
                if (index >= 0)
                    usedButtons[index] = true;
            } else if (!mouse && (guid == kKey || (guid.empty() && (type & kTypeButton)))) {
                kind = Object::Kind::Key;
                index = any ? -1 : int(instance);
                if (index < 0)
                    for (int k = 0; k < 256 && index < 0; ++k)
                        if (!usedKeys[k])
                            index = k;
                if (index >= 256)
                    index = -1;
                if (index >= 0)
                    usedKeys[index] = true;
            } else {
                kind = Object::Kind::Key;
            }
            if (index < 0) {
                if (type & kTypeOptional)
                    continue;
                return kErrInvalidParam;
            }
            found.push_back({kind, index, offset});
        }
        objects = std::move(found);
        dataSize = c.read32(p + 12);
        relative = mouse && (flags & kRelativeAxes);
        return kOk;
    }

    // DIDEVICEINSTANCEA: size, instance and product GUIDs, type, names.
    void writeInstance(Cpu& c, uint32_t p) const {
        const uint32_t size = c.read32(p) >= 580 ? 580 : 560;
        std::memset(c.memory().data() + p + 4, 0, size - 4);
        c.write32(p, size);
        ComHost::writeGuid(c, p + 4, mouse ? kSysMouse : kSysKeyboard);
        ComHost::writeGuid(c, p + 20, mouse ? kSysMouse : kSysKeyboard);
        c.write32(p + 36, mouse ? kDevTypeMouse : kDevTypeKeyboard);
        c.writeString(p + 40, mouse ? "Mouse" : "Keyboard", 260);
        c.writeString(p + 300, mouse ? "Mouse" : "Keyboard", 260);
    }

    std::optional<uint32_t> invoke(const ComInterface& iface, int index, Cpu& c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        if (m == "SetDataFormat")
            return setFormat(c, c.arg(1));
        if (m == "SetCooperativeLevel" || m == "SetEventNotification" || m == "Initialize")
            return kOk;
        if (m == "Acquire") {
            acquired = true;
            return kOk;
        }
        if (m == "Unacquire") {
            acquired = false;
            return kOk;
        }
        if (m == "Poll")
            return kNoEffect; // these devices aren't polled
        if (m == "GetCapabilities") { // DIDEVCAPS: size, flags, type, axes, buttons, POVs
            const uint32_t p = c.arg(1);
            const uint32_t size = std::min<uint32_t>(c.read32(p), 44);
            std::memset(c.memory().data() + p + 4, 0, size - 4);
            c.write32(p + 4, 1); // DIDC_ATTACHED
            c.write32(p + 8, mouse ? kDevTypeMouse : kDevTypeKeyboard);
            c.write32(p + 12, mouse ? 3 : 0);
            c.write32(p + 16, mouse ? 4 : 256);
            return kOk;
        }
        if (m == "GetDeviceInfo") {
            writeInstance(c, c.arg(1));
            return kOk;
        }
        // GetDeviceState(size, data): the objects' state at their offsets;
        // relative axes give the movement since the last read.
        if (m == "GetDeviceState") {
            const uint32_t size = c.arg(1), p = c.arg(2);
            if (!acquired)
                return kErrNotAcquired;
            if (!c.valid(p, size) || size < dataSize)
                return kErrInvalidParam;
            std::memset(c.memory().data() + p, 0, size);
            for (const Object& o : objects) {
                switch (o.kind) {
                case Object::Kind::Axis:
                    c.write32(p + o.offset, uint32_t(relative ? motion[o.index] : axes[o.index]));
                    break;
                case Object::Kind::Button: c.write8(p + o.offset, buttons[o.index] ? 0x80 : 0); break;
                case Object::Kind::Key: c.write8(p + o.offset, keys[o.index] ? 0x80 : 0); break;
                }
            }
            std::fill(std::begin(motion), std::end(motion), 0);
            return kOk;
        }
        // GetDeviceData(object data size, data, count in/out, flags): the
        // oldest changes, removed unless DIGDD_PEEK (1); with no data, as
        // many as the count says are dropped.
        if (m == "GetDeviceData") {
            const uint32_t objSize = c.arg(1), p = c.arg(2), countAt = c.arg(3), flags = c.arg(4);
            if (!acquired)
                return kErrNotAcquired;
            if (!c.valid(countAt, 4) || (p && objSize < 16))
                return kErrInvalidParam;
            const uint32_t want = c.read32(countAt);
            uint32_t n = 0;
            for (; n < want && n < buffer.size(); ++n) {
                if (!p)
                    continue;
                const Event& e = buffer[n];
                const uint32_t at = p + n * objSize;
                if (!c.valid(at, 16))
                    return kErrInvalidParam;
                c.write32(at, e.offset);
                c.write32(at + 4, e.data);
                c.write32(at + 8, e.time);
                c.write32(at + 12, e.sequence);
            }
            c.write32(countAt, n);
            if (!(flags & 1))
                buffer.erase(buffer.begin(), buffer.begin() + long(n));
            const bool over = overflowed;
            overflowed = false;
            return over ? kBufferOverflow : kOk;
        }
        // Properties: the buffer's size (1) and the axes' mode (2).
        if (m == "SetProperty" || m == "GetProperty") {
            const uint32_t which = c.arg(1), p = c.arg(2);
            if (!c.valid(p, 20))
                return kErrInvalidParam;
            const bool set = m == "SetProperty";
            if (which == 1) {
                if (set) {
                    bufferSize = c.read32(p + 16);
                    while (buffer.size() > bufferSize)
                        buffer.pop_front();
                } else {
                    c.write32(p + 16, bufferSize);
                }
                return kOk;
            }
            if (which == 2) {
                if (set)
                    relative = c.read32(p + 16) == 1; // DIPROPAXISMODE_REL
                else
                    c.write32(p + 16, relative ? 1 : 0);
                return kOk;
            }
            return set ? kOk : kErrUnsupported;
        }
        return std::nullopt;
    }
};

// The DirectInput object.
class Input : public ComObject {
public:
    DirectInput::State* s = nullptr;
    const ComInterface* query(const std::string& iid) override {
        for (ComInterface* f : {&s->directInput7, &s->directInput2, &s->directInput})
            if (iid == f->iid)
                return f;
        return nullptr;
    }
    std::shared_ptr<Device> make(const std::string& guid) {
        if (guid != kSysMouse && guid != kSysKeyboard)
            return nullptr;
        auto d = std::make_shared<Device>();
        d->s = s;
        d->mouse = guid == kSysMouse;
        std::weak_ptr<Device> weak = d;
        s->process.rawInput.push_back([weak](const Win32Process::RawInput& r) {
            if (auto device = weak.lock())
                device->hear(r);
        });
        return d;
    }
    std::optional<uint32_t> invoke(const ComInterface& iface, int index, Cpu& c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        // CreateDevice(guid, out, outer); CreateDeviceEx(guid, iid, out, outer)
        if (m == "CreateDevice" || m == "CreateDeviceEx") {
            const bool ex = m == "CreateDeviceEx";
            const uint32_t out = c.arg(ex ? 3 : 2);
            auto d = make(ComHost::guid(c, c.arg(1)));
            if (!c.valid(out, 4))
                return kErrInvalidParam;
            if (!d) {
                c.write32(out, 0);
                return kErrDeviceNotReg;
            }
            const ComInterface* f = ex ? d->query(ComHost::guid(c, c.arg(2)))
                                    : &iface == &s->directInput7 ? &s->device7
                                    : &iface == &s->directInput2 ? &s->device2
                                                                 : &s->device;
            if (!f)
                return kErrInvalidParam;
            c.write32(out, s->com.pointer(d, *f));
            return kOk;
        }
        // EnumDevices(type, callback(DIDEVICEINSTANCEA*, context), context, flags)
        if (m == "EnumDevices") {
            const uint32_t type = c.arg(1) & 0xff, callback = c.arg(2), context = c.arg(3);
            const uint32_t desc = s->process.memory().alloc(580);
            for (bool isMouse : {true, false}) {
                const uint32_t devType = isMouse ? kDevTypeMouse : kDevTypeKeyboard;
                if (type && type != devType) // 0: every kind of device
                    continue;
                Device d;
                d.mouse = isMouse;
                c.write32(desc, 580);
                d.writeInstance(c, desc);
                uint32_t keepGoing = 0;
                if (Error e = c.call(callback, {desc, context}, keepGoing))
                    break;
                if (!keepGoing) // DIENUM_STOP
                    break;
            }
            s->process.memory().free(desc);
            return kOk;
        }
        if (m == "GetDeviceStatus") {
            const std::string guid = ComHost::guid(c, c.arg(1));
            return guid == kSysMouse || guid == kSysKeyboard ? kOk : kErrNotFound;
        }
        if (m == "Initialize" || m == "RunControlPanel")
            return kOk;
        return std::nullopt;
    }
};

} // namespace

DirectInput::~DirectInput() = default;

DirectInput::DirectInput(Win32Process& process) : s_(std::make_unique<State>(process)) {
    State* s = s_.get();
    for (ComInterface* f : {&s->directInput, &s->directInput2, &s->directInput7, &s->device, &s->device2, &s->device7})
        s->com.define(*f);
    auto make = [s] {
        auto input = std::make_shared<Input>();
        input->s = s;
        return input;
    };
    // DirectInputCreateA(instance, version, out, outer): an IDirectInputA.
    process.provideStdcall("DINPUT.dll", "DirectInputCreateA", 4, [s, make](Cpu& c) {
        if (!c.valid(c.arg(2), 4))
            return kErrInvalidParam;
        c.write32(c.arg(2), s->com.pointer(make(), s->directInput));
        return kOk;
    });
    // DirectInputCreateEx(instance, version, iid, out, outer)
    process.provideStdcall("DINPUT.dll", "DirectInputCreateEx", 5, [s, make](Cpu& c) {
        if (!c.valid(c.arg(3), 4))
            return kErrInvalidParam;
        auto input = make();
        const ComInterface* f = input->query(ComHost::guid(c, c.arg(2)));
        if (!f) {
            c.write32(c.arg(3), 0);
            return 0x80004002u; // E_NOINTERFACE
        }
        c.write32(c.arg(3), s->com.pointer(input, *f));
        return kOk;
    });
}

} // namespace reluva::x86
