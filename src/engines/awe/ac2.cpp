// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The record layouts come from reading how motoe.exe (1.0, 684,032 bytes)
// loads a scene: its scene loader, its object factory and each object
// class's load function, checked field by field against the game's
// files. docs/formats/awe/ac2.md has the addresses.

#include "engines/awe/ac2.h"

#include "common/text.h"

#include <cstdio>
#include <cstring>

namespace reluva::awe {

namespace {

using T = Ac2Value::Type;

// A field in a record's fixed order.
enum class F : uint8_t {
    U16,
    U32,
    F32,
    Str,        // UTF-16, 256 characters
    Str32,      // UTF-16, 32 characters
    StrPairs,   // count (u16), then that many pairs of Str
    U32Pairs,   // count (u16), then that many pairs of u32
};
struct FieldSpec {
    const char* name;
    F type;
};

// Which of an object's optional groups of fields it has (the original's
// per-class flags).
enum : uint32_t { kPosition = 1, kRotation = 2, kAppearance = 4, kScript = 8 };

uint32_t classFlags(std::string_view tag) {
    if (tag == "NPCX" || tag == "PCXX" || tag == "FOOT")
        return kPosition | kRotation | kAppearance | kScript;
    if (tag == "PART" || tag == "RAIN" || tag == "SNOW")
        return kPosition | kRotation | kScript;
    return kPosition | kAppearance | kScript; // 2DOB, 2DAN, NING, BINK, SPOT, ITEM
}

class Reader {
public:
    explicit Reader(const std::vector<uint8_t>& d) : d_(d) {}

    size_t pos() const { return pos_; }
    void seek(size_t p) { pos_ = p; }
    bool atEnd() const { return pos_ >= d_.size(); }
    bool fail() const { return fail_; }

    bool has(size_t n) const { return !fail_ && pos_ + n <= d_.size(); }
    uint16_t u16() {
        if (!has(2))
            return fail_ = true, 0;
        const uint16_t v = uint16_t(d_[pos_] | d_[pos_ + 1] << 8);
        pos_ += 2;
        return v;
    }
    uint32_t u32() {
        if (!has(4))
            return fail_ = true, 0;
        const uint32_t v =
            uint32_t(d_[pos_]) | uint32_t(d_[pos_ + 1]) << 8 | uint32_t(d_[pos_ + 2]) << 16 | uint32_t(d_[pos_ + 3]) << 24;
        pos_ += 4;
        return v;
    }
    // A tag: the four characters of the number, most significant first.
    std::string tag() {
        const uint32_t v = u32();
        if (fail_)
            return "";
        std::string t(4, ' ');
        for (int i = 0; i < 4; i++)
            t[size_t(i)] = char(v >> (24 - 8 * i));
        return t;
    }
    // Text in a fixed buffer of `chars` UTF-16 units, up to its first NUL
    // (the rest of the buffer is left over from whatever was there).
    std::string text(size_t chars) {
        if (!has(chars * 2))
            return fail_ = true, "";
        std::vector<uint16_t> units;
        for (size_t i = 0; i < chars; i++) {
            const uint16_t u = uint16_t(d_[pos_ + 2 * i] | d_[pos_ + 2 * i + 1] << 8);
            if (u == 0)
                break;
            units.push_back(u);
        }
        pos_ += chars * 2;
        return text::utf16leToUtf8(units.data(), units.size());
    }
    std::vector<uint8_t> bytes(size_t n) {
        if (!has(n))
            return fail_ = true, std::vector<uint8_t>();
        std::vector<uint8_t> b(d_.begin() + ptrdiff_t(pos_), d_.begin() + ptrdiff_t(pos_ + n));
        pos_ += n;
        return b;
    }

    Ac2Value read(F type) {
        Ac2Value v;
        switch (type) {
        case F::U16:
            v.type = T::U16;
            v.raw = u16();
            break;
        case F::U32:
            v.type = T::U32;
            v.raw = u32();
            break;
        case F::F32:
            v.type = T::F32;
            v.raw = u32();
            break;
        case F::Str:
            v.type = T::Text;
            v.text = text(256);
            break;
        case F::Str32:
            v.type = T::Text;
            v.text = text(32);
            break;
        case F::StrPairs: {
            v.type = T::TextPairs;
            const int n = int16_t(u16());
            for (int k = 0; k < n && !fail_; k++) {
                std::string a = text(256);
                std::string b = text(256);
                v.pairs.emplace_back(std::move(a), std::move(b));
            }
            break;
        }
        case F::U32Pairs: {
            v.type = T::NumberPairs;
            const int n = int16_t(u16());
            for (int k = 0; k < n && !fail_; k++) {
                const uint32_t a = u32();
                const uint32_t b = u32();
                v.numbers.emplace_back(a, b);
            }
            break;
        }
        }
        return v;
    }

    // Reads up to `count` fields of `spec` (fewer if the record has fewer).
    void fields(Ac2Record& r, const FieldSpec* spec, size_t specCount, size_t count) {
        for (size_t k = 0; k < specCount && k < count && !fail_; k++)
            r.fields.emplace_back(spec[k].name, read(spec[k].type));
    }
    // A counted record: its field count, then the fields.
    void counted(Ac2Record& r, const FieldSpec* spec, size_t specCount) {
        const uint16_t n = u16();
        fields(r, spec, specCount, n);
    }

private:
    const std::vector<uint8_t>& d_;
    size_t pos_ = 0;
    bool fail_ = false;
};

template <size_t N> void counted(Reader& r, Ac2Record& rec, const FieldSpec (&spec)[N]) { r.counted(rec, spec, N); }

// --- Objects -------------------------------------------------------------------

// Every object's common fields (motoe.exe 0x40fa20), the optional groups
// as the class has them; one count for all.
void readObjectBase(Reader& r, Ac2Record& rec) {
    static const FieldSpec kHead[] = {{"file", F::Str}, {"name", F::Str}, {"b08", F::U32}, {"b0c", F::U32}};
    static const FieldSpec kPositionFields[] = {{"x", F::F32}, {"y", F::F32}, {"z", F::F32}};
    static const FieldSpec kRotationFields[] = {{"rotX", F::F32}, {"rotY", F::F32}, {"rotZ", F::F32}};
    static const FieldSpec kAppearanceFields[] = {
        {"scaleX", F::F32}, {"scaleY", F::F32}, {"scaleZ", F::F32}, {"red", F::F32}, {"green", F::F32},
        {"blue", F::F32},   {"ac", F::F32},     {"b0", F::F32},     {"b4", F::F32},  {"bc", F::F32},
        {"c0", F::F32},     {"c4", F::F32},     {"cc", F::F32}};
    static const FieldSpec kMiddle[] = {
        {"b10", F::F32}, {"left", F::U32}, {"top", F::U32}, {"right", F::U32}, {"bottom", F::U32}};
    static const FieldSpec kScriptFields[] = {{"script", F::Str}, {"listA", F::StrPairs}, {"listB", F::StrPairs}};
    static const FieldSpec kTail[] = {
        {"alpha", F::F32}, {"b58", F::F32}, {"text", F::Str}, {"b4c", F::F32},       {"b50", F::F32},
        {"b54", F::U32},   {"flat", F::U32}, {"b68", F::U32}, {"b78", F::Str},       {"d78", F::U32},
        {"d7c", F::U16},   {"d0", F::F32},  {"b1e", F::U16}, {"listC", F::StrPairs}, {"listD", F::U32Pairs},
        {"b70", F::U32}};
    const uint32_t flags = classFlags(rec.tag);
    std::vector<FieldSpec> spec;
    auto add = [&spec](const auto& fields) {
        for (const FieldSpec& f : fields)
            spec.push_back(f);
    };
    add(kHead);
    if (flags & kPosition)
        add(kPositionFields);
    if (flags & kRotation)
        add(kRotationFields);
    if (flags & kAppearance)
        add(kAppearanceFields);
    add(kMiddle);
    if (flags & kScript)
        add(kScriptFields);
    add(kTail);
    r.counted(rec, spec.data(), spec.size());
}

// An optional part: its tag, else the stream is left where it was.
bool optionalPart(Reader& r, const char* tag, Ac2Record& rec) {
    const size_t at = r.pos();
    if (r.has(4) && r.tag() == tag) {
        rec.parts.push_back(Ac2Record{tag, {}, {}});
        return true;
    }
    r.seek(at);
    return false;
}

const FieldSpec kSprite[] = {{"da4", F::U16}, {"layer", F::F32},  {"db0", F::U16}, {"dc8", F::U16},
                             {"4", F::F32},   {"8", F::F32},      {"c", F::F32}};
const FieldSpec kHotspot[] = {{"name", F::Str}, {"hoverSound", F::Str}, {"clickSound", F::Str}, {"1410", F::U32},
                              {"cursor", F::Str32}};
const FieldSpec kAnimSprite[] = {{"dec", F::F32}, {"df0", F::F32}, {"df4", F::F32}, {"df8", F::U16},
                                 {"dfa", F::U16}, {"dfc", F::U16}, {"e02", F::U16}};
const FieldSpec kBink[] = {{"de4", F::F32}, {"4", F::F32}, {"8", F::F32}, {"c", F::F32}, {"dd8", F::F32}};
const FieldSpec kItem[] = {{"name", F::Str32}, {"e10", F::Str}, {"1610", F::U16}, {"1612", F::U16}};
const FieldSpec kModel[] = {{"da0", F::U32},   {"list", F::StrPairs}, {"e30", F::U32}, {"e34", F::U32},
                            {"anim", F::Str32}, {"anim2", F::Str32},  {"e38", F::U32}, {"text", F::Str}};
const FieldSpec kAnimation[] = {{"10cc", F::F32}, {"10d4", F::F32}, {"10d8", F::F32}, {"10dc", F::F32},
                                {"10e0", F::F32}, {"10e4", F::U16}, {"10e6", F::U16}, {"10e8", F::U16}};
const FieldSpec kSystem[] = {
    {"df0", F::F32}, {"df4", F::F32}, {"df8", F::F32}, {"dc8", F::F32}, {"dcc", F::F32}, {"dd0", F::F32},
    {"dc4", F::U16}, {"dd4", F::F32}, {"dec", F::U16}, {"dea", F::U16}, {"de4", F::F32}, {"de8", F::U16},
    {"ddc", F::F32}, {"e00", F::F32}, {"e04", F::U16}, {"e14", F::F32}, {"e18", F::F32}, {"e1c", F::F32},
    {"e28", F::F32}, {"e34", F::U32}, {"e0c", F::F32}, {"e10", F::F32}, {"e08", F::F32}, {"e40", F::F32},
    {"e44", F::F32}, {"e38", F::U16}, {"e3a", F::U16}, {"e3c", F::U16}};
const FieldSpec kLight[] = {
    {"d9c", F::U16}, {"dd0", F::F32}, {"dd4", F::F32}, {"dd8", F::F32}, {"ddc", F::F32}, {"de0", F::F32},
    {"de4", F::F32}, {"da0", F::F32}, {"da4", F::F32}, {"da8", F::F32}, {"dc0", F::F32}, {"dc4", F::F32},
    {"dc8", F::F32}, {"db0", F::F32}, {"db4", F::F32}, {"db8", F::F32}, {"de8", F::F32}, {"df0", F::F32},
    {"df4", F::F32}, {"df8", F::F32}, {"dfc", F::F32}, {"e00", F::F32}, {"e0c", F::F32}, {"e10", F::F32},
    {"b6c", F::U32}, {"e1a", F::U16}, {"name", F::Str}, {"text", F::Str}, {"e1c", F::U32}};
const FieldSpec kCamera[] = {
    {"180", F::F32},  {"184", F::F32},  {"188", F::F32}, {"x", F::F32},    {"y", F::F32},    {"z", F::F32},
    {"pitch", F::F32}, {"yaw", F::F32},  {"roll", F::F32}, {"74", F::F32},  {"78", F::F32},   {"7c", F::F32},
    {"fov", F::F32},  {"near", F::F32}, {"far", F::F32},  {"238", F::U16}, {"23a", F::U16},  {"23c", F::U16}};
const FieldSpec kCameraEnd[] = {{"1240", F::F32}, {"1244", F::F32}, {"1248", F::F32}};

// A 2D object (2DOB and the classes built on it): the common fields, then
// its sprite settings.
void readSprite(Reader& r, Ac2Record& rec) {
    readObjectBase(r, rec);
    if (optionalPart(r, "SPRT", rec))
        counted(r, rec.parts.back(), kSprite);
}

// A 3D model (the characters' base): the common fields, then MODL.
void readModel(Reader& r, Ac2Record& rec) {
    readObjectBase(r, rec);
    if (optionalPart(r, "MODL", rec))
        counted(r, rec.parts.back(), kModel);
}

// A character: the model, its animation settings, and where it walks.
void readCharacter(Reader& r, Ac2Record& rec) {
    readModel(r, rec);
    if (optionalPart(r, "ANIM", rec)) {
        Ac2Record& a = rec.parts.back();
        const uint16_t n = r.u16();
        r.fields(a, kAnimation, std::size(kAnimation), n);
        // Fields beyond the eighth: named animations, each a name and two
        // short ones.
        for (int k = 8; k < n && !r.fail(); k++) {
            Ac2Value v;
            v.type = T::TextPairs;
            std::string name = r.text(256);
            std::string first = r.text(64);
            std::string second = r.text(64);
            v.pairs.emplace_back(std::move(name), first + " / " + second);
            a.fields.emplace_back("named", std::move(v));
        }
    }
    if (optionalPart(r, "WALK", rec)) {
        Ac2Record& w = rec.parts.back();
        const int n = int16_t(r.u16());
        for (int k = 0; k < n && !r.fail(); k++) {
            Ac2Record point{"POINT", {}, {}};
            point.fields.emplace_back("id", r.read(F::U16));
            for (const char* name : {"x", "y", "z", "rotX", "rotY", "rotZ"})
                point.fields.emplace_back(name, r.read(F::F32));
            w.parts.push_back(std::move(point));
        }
    }
}

// A character other than the player: after its own parts, whom it
// follows, on what condition, and its idle animations.
void readNpc(Reader& r, Ac2Record& rec) {
    readCharacter(r, rec);
    if (!r.has(4))
        return;
    std::string tag = r.tag();
    if (tag == "FLLW") {
        rec.parts.push_back(Ac2Record{"FLLW", {}, {}});
        rec.parts.back().fields.emplace_back("follow", r.read(F::U32));
        tag = r.tag();
        if (tag == "COND") {
            rec.parts.push_back(Ac2Record{"COND", {}, {}});
            rec.parts.back().fields.emplace_back("condition", r.read(F::Str));
            tag = r.tag();
        }
    }
    // The next tag (IDLE in every file seen) isn't checked by the game.
    rec.parts.push_back(Ac2Record{tag, {}, {}});
    rec.parts.back().fields.emplace_back("idles", r.read(F::StrPairs));
}

void readParticles(Reader& r, Ac2Record& rec) {
    readObjectBase(r, rec);
    if (optionalPart(r, "SYST", rec))
        counted(r, rec.parts.back(), kSystem);
}

bool readObject(Reader& r, Ac2Record& rec) {
    const std::string& t = rec.tag;
    if (t == "2DOB") {
        readSprite(r, rec);
    } else if (t == "SPOT") {
        readSprite(r, rec);
        if (optionalPart(r, "HOTS", rec))
            counted(r, rec.parts.back(), kHotspot);
    } else if (t == "2DAN" || t == "NING") {
        readSprite(r, rec);
        if (optionalPart(r, "2DSP", rec))
            counted(r, rec.parts.back(), kAnimSprite);
    } else if (t == "BINK") {
        readSprite(r, rec);
        if (optionalPart(r, "BNKX", rec))
            counted(r, rec.parts.back(), kBink);
    } else if (t == "ITEM") {
        readObjectBase(r, rec);
        if (optionalPart(r, "ITEM", rec))
            counted(r, rec.parts.back(), kItem);
    } else if (t == "LITE") {
        counted(r, rec, kLight);
    } else if (t == "PART" || t == "RAIN" || t == "SNOW") {
        readParticles(r, rec);
    } else if (t == "NPCX") {
        readNpc(r, rec);
    } else if (t == "PCXX") {
        readCharacter(r, rec);
    } else if (t == "FOOT") {
        readModel(r, rec);
    } else {
        return false;
    }
    return true;
}

void readCamera(Reader& r, Ac2Record& rec) {
    const uint16_t n = r.u16();
    r.fields(rec, kCamera, std::size(kCamera), n);
    if (n <= std::size(kCamera))
        return;
    // A grid of size x size bytes (when it's at most 64 wide).
    const int size = int16_t(r.u16());
    Ac2Value grid;
    grid.type = T::Bytes;
    grid.raw = uint32_t(size);
    if (size > 0)
        grid.bytes = r.bytes(size_t(size) * size_t(size));
    rec.fields.emplace_back("grid", std::move(grid));
    for (size_t k = 0; k < std::size(kCameraEnd) && std::size(kCamera) + 1 + k < n; k++)
        rec.fields.emplace_back(kCameraEnd[k].name, r.read(kCameraEnd[k].type));
}

} // namespace

float Ac2Value::f() const {
    if (type == Type::U16)
        return float(int16_t(raw));
    if (type != Type::F32)
        return float(int32_t(raw));
    float v;
    std::memcpy(&v, &raw, 4);
    return v;
}

std::string Ac2Value::str() const {
    char buf[48];
    switch (type) {
    case Type::U16:
        return std::to_string(int16_t(raw));
    case Type::U32:
        return std::to_string(int32_t(raw));
    case Type::F32:
        std::snprintf(buf, sizeof buf, "%g", f());
        return buf;
    case Type::Text:
        return "\"" + text + "\"";
    case Type::TextPairs: {
        std::string s = "[";
        for (const auto& [a, b] : pairs)
            s += (s.size() > 1 ? ", \"" : "\"") + a + "\": \"" + b + "\"";
        return s + "]";
    }
    case Type::NumberPairs: {
        std::string s = "[";
        for (const auto& [a, b] : numbers)
            s += (s.size() > 1 ? ", " : "") + std::to_string(a) + ":" + std::to_string(b);
        return s + "]";
    }
    case Type::Bytes:
        return std::to_string(raw) + "x" + std::to_string(raw) + " bytes";
    }
    return "";
}

const Ac2Value* Ac2Record::field(std::string_view name) const {
    for (const auto& [n, v] : fields)
        if (n == name)
            return &v;
    return nullptr;
}

const Ac2Record* Ac2Record::part(std::string_view t) const {
    for (const Ac2Record& p : parts)
        if (p.tag == t)
            return &p;
    return nullptr;
}

float Ac2Record::f(std::string_view name) const {
    const Ac2Value* v = field(name);
    return v ? v->f() : 0.0f;
}

int32_t Ac2Record::i(std::string_view name) const {
    const Ac2Value* v = field(name);
    return v ? (v->type == Ac2Value::Type::U16 ? int16_t(v->raw) : v->i()) : 0;
}

std::string Ac2Record::text(std::string_view name) const {
    const Ac2Value* v = field(name);
    return v ? v->text : "";
}

const Ac2Record* Ac2Scene::camera() const {
    for (const Ac2Record& r : records)
        if (r.tag == "CAMR")
            return &r;
    return nullptr;
}

std::vector<const Ac2Record*> Ac2Scene::objects() const {
    std::vector<const Ac2Record*> out;
    for (const Ac2Record& r : records)
        if (isAc2Object(r.tag))
            out.push_back(&r);
    return out;
}

bool isAc2Object(std::string_view tag) {
    for (const char* t : {"2DOB", "2DAN", "NING", "BINK", "SPOT", "ITEM", "LITE", "PART", "RAIN", "SNOW", "NPCX",
                          "PCXX", "FOOT"})
        if (tag == t)
            return true;
    return false;
}

Error parseAc2(const std::vector<uint8_t>& data, Ac2Scene& out) {
    out = Ac2Scene();
    Reader r(data);
    while (!r.atEnd()) {
        const size_t at = r.pos();
        Ac2Record rec{r.tag(), {}, {}};
        if (r.fail())
            break;
        const std::string& t = rec.tag;
        // The scene's own settings (motoe.exe 0x435990).
        if (t == "DONE") {
            out.records.push_back(std::move(rec));
            break;
        } else if (t == "SCEN") {
            rec.fields.emplace_back("path", r.read(F::Str));
            out.path = rec.fields.back().second.text;
        } else if (t == "LOAD" || t == "FAKE" || t == "OUTD" || t == "NVNT" || t == "SHOW") {
            rec.fields.emplace_back("value", r.read(F::U16));
        } else if (t == "FOOT" || t == "PUZZ") {
            // Before the object factory: a scene's FOOT is its footstep sound set.
            rec.fields.emplace_back("value", r.read(F::Str32));
        } else if (t == "MUSC") {
            rec.fields.emplace_back("music", r.read(F::StrPairs));
        } else if (t == "EVNT") {
            rec.fields.emplace_back("events", r.read(F::StrPairs));
        } else if (t == "CAMR") {
            readCamera(r, rec);
        } else if (!readObject(r, rec)) {
            char buf[96];
            std::snprintf(buf, sizeof buf, "Unknown record \"%s\" at offset 0x%zx", t.c_str(), at);
            return Error::fail(buf);
        }
        if (r.fail()) {
            char buf[96];
            std::snprintf(buf, sizeof buf, "Record \"%s\" at offset 0x%zx runs past the end", t.c_str(), at);
            return Error::fail(buf);
        }
        out.records.push_back(std::move(rec));
    }
    return Error::success();
}

namespace {

void dumpRecord(const Ac2Record& rec, std::string& out, int depth) {
    const std::string pad(size_t(depth) * 2, ' ');
    out += pad + rec.tag + "\n";
    for (const auto& [name, v] : rec.fields) {
        // Leave out empty text and zeros, which most fields are.
        if ((v.type == Ac2Value::Type::Text && v.text.empty()) ||
            ((v.type == Ac2Value::Type::TextPairs || v.type == Ac2Value::Type::NumberPairs) && v.pairs.empty() &&
             v.numbers.empty()) ||
            (v.type != Ac2Value::Type::Text && v.type != Ac2Value::Type::TextPairs &&
             v.type != Ac2Value::Type::NumberPairs && v.type != Ac2Value::Type::Bytes && v.raw == 0))
            continue;
        out += pad + "  " + name + " = " + v.str() + "\n";
    }
    for (const Ac2Record& p : rec.parts)
        dumpRecord(p, out, depth + 1);
}

} // namespace

std::string dumpAc2(const Ac2Scene& scene) {
    std::string out;
    for (const Ac2Record& r : scene.records)
        dumpRecord(r, out, 0);
    return out;
}

} // namespace reluva::awe
