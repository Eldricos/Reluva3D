// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Read as Star Trek.exe's reader reads them (0x40dc60): the format's
// version, the class's, then each property by its kind, then the class's
// own data. The addresses of each kind's writer and of the own data's are
// in docs/engines/stawayteam/research/binary-objects.md.

#include "engines/stawayteam/formats/binary_object.h"

#include "engines/stawayteam/formats/object_schema.h"

#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace reluva::stawayteam {

namespace {

constexpr float kFormatVersion = 26.0f;
// Bounds for counts and lengths, far over any in the game's files: a damaged
// file fails rather than asking for gigabytes.
constexpr int32_t kMaxCount = 1 << 20;
constexpr int kMaxDepth = 64;

class Reader {
public:
    Reader(const uint8_t *data, size_t size) : _data(data), _size(size) {}

    bool fail(const std::string &why) {
        if (_error.empty())
            _error = why + " at byte " + std::to_string(_at);
        return false;
    }
    const std::string &error() const { return _error; }
    size_t at() const { return _at; }

    bool bytes(void *out, size_t n) {
        if (_size - _at < n)
            return fail("the file ends");
        std::memcpy(out, _data + _at, n);
        _at += n;
        return true;
    }
    bool i32(int32_t &v) { return bytes(&v, 4); }
    bool f32(float &v) { return bytes(&v, 4); }
    bool u8(uint8_t &v) { return bytes(&v, 1); }
    bool count(int32_t &n) {
        if (!i32(n))
            return false;
        if (n < 0 || n > kMaxCount)
            return fail("a count of " + std::to_string(n));
        return true;
    }
    bool string(std::string &s) {
        int32_t n = 0;
        if (!count(n))
            return false;
        s.resize(size_t(n));
        return n == 0 || bytes(s.data(), size_t(n));
    }
    bool frame(std::optional<Frame> &out) {
        Frame f;
        size_t used = 0;
        if (Error err = readFrame(_data + _at, _size - _at, f, &used))
            return fail(err.message);
        _at += used;
        out = std::move(f);
        return true;
    }

    bool object(const ClassSchema *cls, BinaryObject &out, int depth, uint16_t size = 0);

private:
    bool value(const PropertySchema &p, Saved saved, TextField &field, int depth);
    bool ownData(const ClassSchema &cls, BinaryObject &out, int depth);
    // A class's properties, made once for the file: a waypoint map has tens
    // of thousands of objects of a few classes.
    const std::vector<const PropertySchema *> &properties(const ClassSchema &cls) {
        auto &props = _properties[&cls];
        if (props.empty())
            props = propertiesOf(cls);
        return props;
    }

    const uint8_t *_data;
    size_t _size;
    size_t _at = 0;
    std::string _error;
    std::unordered_map<const ClassSchema *, std::vector<const PropertySchema *>> _properties;
};

std::string number(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.9g", double(v));
    return buf;
}

// The class of an object whose class the file doesn't name: by its
// version, and its size where versions are shared.
const ClassSchema *classOf(float version, uint16_t size) {
    std::vector<const ClassSchema *> found = classesOfVersion(version);
    if (found.size() > 1 && size != 0) {
        std::vector<const ClassSchema *> sized;
        for (const ClassSchema *c : found)
            if (c->size == size)
                sized.push_back(c);
        found = sized;
    }
    return found.size() == 1 ? found[0] : nullptr;
}

bool Reader::object(const ClassSchema *cls, BinaryObject &out, int depth, uint16_t size) {
    if (depth > kMaxDepth)
        return fail("objects nested too deep");
    float format = 0;
    float version = 0;
    if (!f32(format) || !f32(version))
        return false;
    if (format != kFormatVersion)
        return fail("not one of the game's objects (format " + number(format) + ")");
    if (!cls) {
        cls = classOf(version, size);
        if (!cls)
            return fail("no one class of version " + number(version));
    }
    // The game reads no object of another version of its class (0x40dd05).
    if (version != cls->version)
        return fail(std::string(cls->name) + " of version " + number(version) + ", not its class's " +
                    number(cls->version));
    out.object.className = cls->name;
    for (const PropertySchema *p : properties(*cls)) {
        TextField field;
        field.key = p->name;
        if (!value(*p, p->saved, field, depth))
            return false;
        // A key with its group ("Damage/Hit Points") is written in text as
        // its last part.
        const size_t slash = field.key.rfind('/');
        if (slash != std::string::npos)
            field.key = field.key.substr(slash + 1);
        out.object.fields.push_back(std::move(field));
    }
    return ownData(*cls, out, depth);
}

bool Reader::value(const PropertySchema &p, Saved saved, TextField &field, int depth) {
    switch (saved) {
    case Saved::kInt:
    case Saved::kRef: {
        int32_t v = 0;
        if (!i32(v))
            return false;
        field.value = std::to_string(v);
        return true;
    }
    case Saved::kFloat: {
        float v = 0;
        if (!f32(v))
            return false;
        field.value = number(v);
        return true;
    }
    case Saved::kBool:
    case Saved::kFlag: {
        uint8_t v = 0;
        if (!u8(v))
            return false;
        field.value = v ? "1" : "0";
        return true;
    }
    case Saved::kPoint: {
        int32_t xy[2];
        if (!bytes(xy, 8))
            return false;
        field.value = std::to_string(xy[0]) + "," + std::to_string(xy[1]);
        return true;
    }
    case Saved::kDouble: {
        double v = 0;
        if (!bytes(&v, 8))
            return false;
        char buf[40];
        std::snprintf(buf, sizeof buf, "%.17g", v);
        field.value = buf;
        return true;
    }
    case Saved::kColour: {
        uint8_t rgb[3];
        if (!bytes(rgb, 3))
            return false;
        field.value = std::to_string(rgb[0]) + "," + std::to_string(rgb[1]) + "," + std::to_string(rgb[2]);
        return true;
    }
    case Saved::kString:
        return string(field.value);
    case Saved::kObject: {
        if (!string(field.value))
            return false;
        if (field.value.empty())
            return true; // none
        const ClassSchema *cls = findClass(field.value);
        if (!cls)
            return fail("an object of class " + field.value + ", which the game doesn't have");
        BinaryObject sub;
        if (!object(cls, sub, depth + 1))
            return false;
        field.object = std::make_unique<TextObject>(std::move(sub.object));
        return true;
    }
    case Saved::kInline: {
        const ClassSchema *cls = p.cls >= 0 ? &allClasses()[size_t(p.cls)] : nullptr;
        BinaryObject sub;
        if (!object(cls, sub, depth + 1, p.size))
            return false;
        field.value = sub.object.className;
        field.object = std::make_unique<TextObject>(std::move(sub.object));
        return true;
    }
    case Saved::kObjects:
    case Saved::kArray: {
        int32_t n = 0;
        if (!count(n))
            return false;
        auto array = std::make_unique<TextObject>();
        array->className = "Array";
        array->fields.push_back({"Item Count", std::to_string(n), nullptr});
        const std::string itemKey = field.key.substr(field.key.rfind('/') + 1);
        for (int32_t i = 0; i < n; ++i) {
            TextField item;
            item.key = itemKey;
            if (saved == Saved::kObjects) {
                if (p.cls < 0)
                    return fail("objects of a class not worked out");
                BinaryObject sub;
                if (!object(&allClasses()[size_t(p.cls)], sub, depth + 1))
                    return false;
                item.value = sub.object.className;
                item.object = std::make_unique<TextObject>(std::move(sub.object));
            } else {
                if (p.named && !string(item.key))
                    return false;
                if (!value(p, p.element, item, depth + 1))
                    return false;
            }
            array->fields.push_back(std::move(item));
        }
        field.value = "Array";
        field.object = std::move(array);
        return true;
    }
    case Saved::kPolygon: {
        uint8_t head[12];
        if (!bytes(head, 12))
            return false;
        if (std::memcmp(head, "POLY2D", 7) != 0)
            return fail("a polygon without its POLY2D");
        uint32_t n = 0;
        std::memcpy(&n, head + 8, 4);
        if (n > uint32_t(kMaxCount))
            return fail("a polygon of " + std::to_string(n) + " points");
        for (uint32_t i = 0; i < n; ++i) {
            int32_t xy[2];
            if (!bytes(xy, 8))
                return false;
            field.value += (i ? " " : "") + std::to_string(xy[0]) + "," + std::to_string(xy[1]);
        }
        return true;
    }
    case Saved::kUnknown:
        break;
    }
    return fail(std::string("property \"") + p.name + "\", of a kind not worked out");
}

bool Reader::ownData(const ClassSchema &cls, BinaryObject &out, int depth) {
    switch (ownDataOf(cls)) {
    case OwnData::kNone:
        return true;
    case OwnData::kSequence:
    case OwnData::kRotationSequence: {
        // The frame count is the class's first property (unnamed, +0x24).
        const long frames = out.object.fields.empty() ? -1 : out.object.integer(out.object.fields[0].key).value_or(-1);
        if (frames < 0 || frames > kMaxCount)
            return fail("a sequence without its frame count");
        SequenceData seq;
        if (!bytes(seq.box.data(), 16))
            return false;
        seq.frames.resize(size_t(frames));
        for (auto &frame : seq.frames) {
            uint8_t there = 0;
            if (!u8(there) || (there && !this->frame(frame)))
                return false;
        }
        if (ownDataOf(cls) == OwnData::kRotationSequence) {
            seq.frameExtra.resize(size_t(frames));
            for (auto &extra : seq.frameExtra)
                if (!bytes(extra.data(), 8))
                    return false;
        }
        out.sequence = std::move(seq);
        return true;
    }
    case OwnData::kModel: {
        int32_t n = 0;
        if (!count(n) || !string(out.modelLabel))
            return false;
        const ClassSchema *rotation = findClass("CRotationSequence");
        for (int32_t i = 0; i < n; ++i) {
            NamedSequence named;
            uint8_t there = 0;
            if (!u8(there))
                return false;
            if (there) {
                uint8_t zero = 0;
                named.sequence = std::make_unique<BinaryObject>();
                if (!string(named.name) || !u8(zero) || !object(rotation, *named.sequence, depth + 1))
                    return false;
            }
            out.sequences.push_back(std::move(named));
        }
        return true;
    }
    case OwnData::kWayPointMap: {
        const ClassSchema *point = findClass("CPathWayPoint");
        const ClassSchema *line = findClass("CPathLineSegment");
        int32_t n = 0;
        if (!count(n))
            return false;
        out.wayPoints.resize(size_t(n));
        for (WayPoint &w : out.wayPoints) {
            BinaryObject sub;
            if (!object(point, sub, depth + 1))
                return false;
            w.flags = int32_t(sub.object.integer("Flags").value_or(0));
            w.x = int32_t(sub.object.integer("Position X").value_or(0));
            w.y = int32_t(sub.object.integer("Position Y").value_or(0));
            if (!sub.wayPoints.empty())
                w.lines = std::move(sub.wayPoints[0].lines);
        }
        if (!count(n))
            return false;
        out.lines.resize(size_t(n));
        for (PathLine &l : out.lines) {
            BinaryObject sub;
            if (!object(line, sub, depth + 1))
                return false;
            l.flags = int32_t(sub.object.integer("Flags").value_or(0));
            l.maxPassWidth = float(sub.object.number("MaxPassWidth").value_or(0));
            l.length = float(sub.object.number("Length").value_or(0));
            l.angleAToB = float(sub.object.number("AngleAToB").value_or(0));
            l.a = int32_t(sub.object.integer("AIndex").value_or(0));
            l.b = int32_t(sub.object.integer("BIndex").value_or(0));
        }
        return true;
    }
    case OwnData::kWayPoint: {
        // Kept as one waypoint for the map that holds it to take.
        WayPoint w;
        int32_t n = 0;
        if (!count(n))
            return false;
        w.lines.resize(size_t(n));
        for (int32_t &line : w.lines)
            if (!i32(line))
                return false;
        out.wayPoints.push_back(std::move(w));
        return true;
    }
    case OwnData::kOther:
        break;
    }
    return fail(std::string(cls.name) + " saves data of its own, not worked out");
}

} // namespace

Error readBinaryObject(const uint8_t *data, size_t size, const std::string &className, BinaryObject &out) {
    out = BinaryObject{};
    const ClassSchema *cls = nullptr;
    if (!className.empty()) {
        cls = findClass(className);
        if (!cls)
            return Error::fail("no class " + className);
    }
    Reader reader(data, size);
    if (!reader.object(cls, out, 0))
        return Error::fail(reader.error());
    if (reader.at() != size)
        return Error::fail(std::to_string(size - reader.at()) + " bytes after the object");
    return Error::success();
}

} // namespace reluva::stawayteam
