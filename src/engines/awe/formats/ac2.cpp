// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The record layouts come from reading how motoe.exe (1.0, 684,032 bytes)
// loads a scene: its scene loader, its object factory and each object
// class's load function, checked field by field against the game's
// files. docs/engines/awe/games/motoe/formats/ac2.md has the addresses.

#include "engines/awe/formats/ac2.h"

#include "common/text.h"

#include <cstdio>
#include <cstring>

namespace reluva::awe {

namespace {

using T = Ac2Value::Type;

// A field in a record's fixed order.
enum class F : uint8_t {
    kU16,
    kU32,
    kF32,
    kStr,      // UTF-16, 256 characters
    kStr32,    // UTF-16, 32 characters
    kStrPairs, // count (u16), then that many pairs of Str
    kU32Pairs, // count (u16), then that many pairs of u32
};
struct FieldSpec {
    const char *name;
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
    // `narrow`: text a byte a character, Windows-1252 (.ac1), not UTF-16.
    Reader(const std::vector<uint8_t> &d, bool narrow) : _d(d), _narrow(narrow) {}

    size_t pos() const { return _pos; }
    void seek(size_t p) { _pos = p; }
    bool atEnd() const { return _pos >= _d.size(); }
    bool fail() const { return _fail; }

    bool has(size_t n) const { return !_fail && _pos + n <= _d.size(); }
    uint16_t u16() {
        if (!has(2))
            return _fail = true, 0;
        const uint16_t v = uint16_t(_d[_pos] | _d[_pos + 1] << 8);
        _pos += 2;
        return v;
    }
    uint32_t u32() {
        if (!has(4))
            return _fail = true, 0;
        const uint32_t v = uint32_t(_d[_pos]) | uint32_t(_d[_pos + 1]) << 8 | uint32_t(_d[_pos + 2]) << 16 |
                           uint32_t(_d[_pos + 3]) << 24;
        _pos += 4;
        return v;
    }
    // A tag: the four characters of the number, most significant first.
    std::string tag() {
        const uint32_t v = u32();
        if (_fail)
            return "";
        std::string t(4, ' ');
        for (int i = 0; i < 4; i++)
            t[size_t(i)] = char(v >> (24 - 8 * i));
        return t;
    }
    // Text in a fixed buffer of `chars` characters (UTF-16 units, or bytes
    // when narrow), up to its first NUL (the rest of the buffer is left over
    // from whatever was there).
    std::string text(size_t chars) {
        if (_narrow) {
            if (!has(chars))
                return _fail = true, "";
            size_t n = 0;
            while (n < chars && _d[_pos + n] != 0)
                n++;
            std::string s =
                text::windows1252ToUtf8(std::string_view(reinterpret_cast<const char *>(_d.data() + _pos), n));
            _pos += chars;
            return s;
        }
        if (!has(chars * 2))
            return _fail = true, "";
        std::vector<uint16_t> units;
        for (size_t i = 0; i < chars; i++) {
            const uint16_t u = uint16_t(_d[_pos + 2 * i] | _d[_pos + 2 * i + 1] << 8);
            if (u == 0)
                break;
            units.push_back(u);
        }
        _pos += chars * 2;
        return text::utf16leToUtf8(units.data(), units.size());
    }
    std::vector<uint8_t> bytes(size_t n) {
        if (!has(n))
            return _fail = true, std::vector<uint8_t>();
        std::vector<uint8_t> b(_d.begin() + ptrdiff_t(_pos), _d.begin() + ptrdiff_t(_pos + n));
        _pos += n;
        return b;
    }

    Ac2Value read(F type) {
        Ac2Value v;
        switch (type) {
        case F::kU16:
            v.type = T::kU16;
            v.raw = u16();
            break;
        case F::kU32:
            v.type = T::kU32;
            v.raw = u32();
            break;
        case F::kF32:
            v.type = T::kF32;
            v.raw = u32();
            break;
        case F::kStr:
            v.type = T::kText;
            v.text = text(256);
            break;
        case F::kStr32:
            v.type = T::kText;
            v.text = text(32);
            break;
        case F::kStrPairs: {
            v.type = T::kTextPairs;
            const int n = int16_t(u16());
            for (int k = 0; k < n && !_fail; k++) {
                std::string a = text(256);
                std::string b = text(256);
                v.pairs.emplace_back(std::move(a), std::move(b));
            }
            break;
        }
        case F::kU32Pairs: {
            v.type = T::kNumberPairs;
            const int n = int16_t(u16());
            for (int k = 0; k < n && !_fail; k++) {
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
    void fields(Ac2Record &r, const FieldSpec *spec, size_t specCount, size_t count) {
        for (size_t k = 0; k < specCount && k < count && !_fail; k++)
            r.fields.emplace_back(spec[k].name, read(spec[k].type));
    }
    // A counted record: its field count, then the fields.
    void counted(Ac2Record &r, const FieldSpec *spec, size_t specCount) {
        const uint16_t n = u16();
        fields(r, spec, specCount, n);
    }

private:
    const std::vector<uint8_t> &_d;
    bool _narrow;
    size_t _pos = 0;
    bool _fail = false;
};

template<size_t N>
void counted(Reader &r, Ac2Record &rec, const FieldSpec (&spec)[N]) {
    r.counted(rec, spec, N);
}

// --- Objects -------------------------------------------------------------------

// Every object's common fields (motoe.exe 0x40fa20), the optional groups
// as the class has them; one count for all.
void readObjectBase(Reader &r, Ac2Record &rec) {
    static const FieldSpec kHead[] = {{"file", F::kStr}, {"name", F::kStr}, {"alpha", F::kU32}, {"lighting", F::kU32}};
    static const FieldSpec kPositionFields[] = {{"x", F::kF32}, {"y", F::kF32}, {"z", F::kF32}};
    static const FieldSpec kRotationFields[] = {{"rotX", F::kF32}, {"rotY", F::kF32}, {"rotZ", F::kF32}};
    // The object's Direct3D material (D3DMATERIAL8 at 0x8c): its diffuse,
    // ambient, specular and emissive colours, then the specular power.
    static const FieldSpec kAppearanceFields[] = {
        {"diffuseR", F::kF32},  {"diffuseG", F::kF32},  {"diffuseB", F::kF32},  {"ambientR", F::kF32},
        {"ambientG", F::kF32},  {"ambientB", F::kF32},  {"specularR", F::kF32}, {"specularG", F::kF32},
        {"specularB", F::kF32}, {"emissiveR", F::kF32}, {"emissiveG", F::kF32}, {"emissiveB", F::kF32},
        {"power", F::kF32}};
    static const FieldSpec kMiddle[] = {
        {"player", F::kU32}, {"left", F::kU32}, {"top", F::kU32}, {"right", F::kU32}, {"bottom", F::kU32}};
    static const FieldSpec kScriptFields[] = {{"script", F::kStr}, {"listA", F::kStrPairs}, {"listB", F::kStrPairs}};
    static const FieldSpec kTail[] = {{"spriteScaleX", F::kF32}, {"b58", F::kF32},
                                      {"condition", F::kStr},    {"spriteScaleY", F::kF32},
                                      {"spriteScaleZ", F::kF32}, {"b54", F::kU32},
                                      {"flat", F::kU32},         {"b68", F::kU32},
                                      {"sound", F::kStr},        {"soundLoops", F::kU32},
                                      {"soundFrame", F::kU16},   {"depth", F::kF32},
                                      {"cursor", F::kU16},       {"conditionalCursors", F::kStrPairs},
                                      {"listD", F::kU32Pairs},   {"pickFirst", F::kU32}};
    const uint32_t flags = classFlags(rec.tag);
    std::vector<FieldSpec> spec;
    auto add = [&spec](const auto &fields) {
        for (const FieldSpec &f : fields)
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
bool optionalPart(Reader &r, const char *tag, Ac2Record &rec) {
    const size_t at = r.pos();
    if (r.has(4) && r.tag() == tag) {
        rec.parts.push_back(Ac2Record{tag, {}, {}});
        return true;
    }
    r.seek(at);
    return false;
}

const FieldSpec kSprite[] = {{"scroll", F::kU16}, {"scrollSpeed", F::kF32}, {"filter", F::kU16}, {"dc8", F::kU16},
                             {"yaw", F::kF32},    {"pitch", F::kF32},       {"roll", F::kF32}};
const FieldSpec kHotspot[] = {{"name", F::kStr},
                              {"hoverSound", F::kStr},
                              {"clickSound", F::kStr},
                              {"hoverSoundLoops", F::kU32},
                              {"key", F::kStr32}};
const FieldSpec kAnimSprite[] = {{"flags", F::kU32},         {"fps", F::kF32},    {"delay", F::kF32},
                                 {"loops", F::kU16},         {"frames", F::kU16}, {"startFrame", F::kU16},
                                 {"soundDirection", F::kU16}};
const FieldSpec kBink[] = {
    {"effect", F::kU32}, {"yaw", F::kF32}, {"pitch", F::kF32}, {"roll", F::kF32}, {"loop", F::kU32}};
const FieldSpec kItem[] = {{"name", F::kStr32}, {"description", F::kStr}, {"1610", F::kU16}, {"1612", F::kU16}};
// MODL: the shadow flag (+0xda0), models attached at load, breath
// (+0xe30) and footprints outdoors (+0xe34), the track mask of a pose
// (+0xe40, "sit": TrackMasksit) and the prefix tried on every animation's
// name (+0xe80, "sit": sitidle1), whether a click on the floor walks the
// player there (+0xe38), and when the pose and prefix apply (+0xec0).
const FieldSpec kModel[] = {{"shadow", F::kU32},      {"list", F::kStrPairs}, {"breath", F::kU32},
                            {"footprints", F::kU32},  {"anim", F::kStr32},    {"anim2", F::kStr32},
                            {"clickToWalk", F::kU32}, {"condition", F::kStr}};
// ANIM: the talk ease (the base ease is twice it), the blink interval
// (min, max), the idle variations' interval (min, max) and the chances
// of variations 2, 3 and 4 on (percent).
const FieldSpec kAnimation[] = {{"talkEase", F::kF32}, {"blinkMin", F::kF32}, {"blinkMax", F::kF32},
                                {"idleMin", F::kF32},  {"idleMax", F::kF32},  {"idle2", F::kU16},
                                {"idle3", F::kU16},    {"idle4", F::kU16}};
const FieldSpec kSystem[] = {
    {"red", F::kF32},       {"green", F::kF32},     {"blue", F::kF32},        {"velocityX", F::kF32},
    {"velocityY", F::kF32}, {"velocityZ", F::kF32}, {"burst", F::kU16},       {"interval", F::kF32},
    {"copies", F::kU16},    {"spread", F::kU16},    {"bounce", F::kF32},      {"bounces", F::kU16},
    {"size", F::kF32},      {"life", F::kF32},      {"mode", F::kU16},        {"blink", F::kF32},
    {"onTime", F::kF32},    {"offTime", F::kF32},   {"screenSpace", F::kF32}, {"warmUp", F::kU32},
    {"sway", F::kF32},      {"swaySpeed", F::kF32}, {"gravity", F::kF32},     {"fadeIn", F::kF32},
    {"fadeOut", F::kF32},   {"signX", F::kU16},     {"signY", F::kU16},       {"signZ", F::kU16}};
// LITE (0x421ab0): a Direct3D light (D3DLIGHT8 at 0xd9c): its type,
// position, direction, diffuse, ambient and specular colours, range,
// attenuation and spot cone. Then whether it's on, a flicker interval
// (seconds), its light index when saved, its name, the condition it's on
// under, and whether it only places the shadows.
const FieldSpec kLight[] = {
    {"type", F::kU16},      {"x", F::kF32},         {"y", F::kF32},         {"z", F::kF32},
    {"dirX", F::kF32},      {"dirY", F::kF32},      {"dirZ", F::kF32},      {"diffuseR", F::kF32},
    {"diffuseG", F::kF32},  {"diffuseB", F::kF32},  {"ambientR", F::kF32},  {"ambientG", F::kF32},
    {"ambientB", F::kF32},  {"specularR", F::kF32}, {"specularG", F::kF32}, {"specularB", F::kF32},
    {"range", F::kF32},     {"att0", F::kF32},      {"att1", F::kF32},      {"att2", F::kF32},
    {"theta", F::kF32},     {"phi", F::kF32},       {"on", F::kU32},        {"flicker", F::kF32},
    {"b6c", F::kU32},       {"index", F::kU16},     {"name", F::kStr},      {"condition", F::kStr},
    {"shadowOnly", F::kU32}};
// CAMR: the scene's ambient light (0x180), then the Granny camera's fields.
const FieldSpec kCamera[] = {{"ambientR", F::kF32}, {"ambientG", F::kF32}, {"ambientB", F::kF32}, {"x", F::kF32},
                             {"y", F::kF32},        {"z", F::kF32},        {"pitch", F::kF32},    {"yaw", F::kF32},
                             {"roll", F::kF32},     {"offsetX", F::kF32},  {"offsetY", F::kF32},  {"offsetZ", F::kF32},
                             {"fov", F::kF32},      {"near", F::kF32},     {"far", F::kF32},      {"238", F::kU16},
                             {"23a", F::kU16},      {"cellSize", F::kU16}};
const FieldSpec kCameraEnd[] = {{"1240", F::kU32}, {"whiteoutStart", F::kF32}, {"whiteoutEnd", F::kF32}};

// A 2D object (2DOB and the classes built on it): the common fields, then
// its sprite settings.
void readSprite(Reader &r, Ac2Record &rec) {
    readObjectBase(r, rec);
    if (optionalPart(r, "SPRT", rec))
        counted(r, rec.parts.back(), kSprite);
}

// A 3D model (the characters' base): the common fields, then MODL.
void readModel(Reader &r, Ac2Record &rec) {
    readObjectBase(r, rec);
    if (optionalPart(r, "MODL", rec))
        counted(r, rec.parts.back(), kModel);
}

// A character: the model, its animation settings, and where it walks.
void readCharacter(Reader &r, Ac2Record &rec) {
    readModel(r, rec);
    if (optionalPart(r, "ANIM", rec)) {
        Ac2Record &a = rec.parts.back();
        const uint16_t n = r.u16();
        r.fields(a, kAnimation, std::size(kAnimation), n);
        // Fields beyond the eighth: renamed animations, each a condition,
        // the name scripts use and the file played for it ("alias /
        // file").
        for (int k = 8; k < n && !r.fail(); k++) {
            Ac2Value v;
            v.type = T::kTextPairs;
            std::string name = r.text(256);
            std::string first = r.text(64);
            std::string second = r.text(64);
            v.pairs.emplace_back(std::move(name), first + " / " + second);
            a.fields.emplace_back("named", std::move(v));
        }
    }
    if (optionalPart(r, "WALK", rec)) {
        Ac2Record &w = rec.parts.back();
        const int n = int16_t(r.u16());
        for (int k = 0; k < n && !r.fail(); k++) {
            Ac2Record point{"POINT", {}, {}};
            point.fields.emplace_back("id", r.read(F::kU16));
            for (const char *name : {"x", "y", "z", "rotX", "rotY", "rotZ"})
                point.fields.emplace_back(name, r.read(F::kF32));
            w.parts.push_back(std::move(point));
        }
    }
}

// A character other than the player: after its own parts, whom it
// follows, on what condition, and its idle animations.
void readNpc(Reader &r, Ac2Record &rec) {
    readCharacter(r, rec);
    if (!r.has(4))
        return;
    std::string tag = r.tag();
    if (tag == "FLLW") {
        rec.parts.push_back(Ac2Record{"FLLW", {}, {}});
        rec.parts.back().fields.emplace_back("follow", r.read(F::kU32));
        tag = r.tag();
        if (tag == "COND") {
            rec.parts.push_back(Ac2Record{"COND", {}, {}});
            rec.parts.back().fields.emplace_back("condition", r.read(F::kStr));
            tag = r.tag();
        }
    }
    // The next tag (IDLE in every file seen) isn't checked by the game.
    rec.parts.push_back(Ac2Record{tag, {}, {}});
    rec.parts.back().fields.emplace_back("idles", r.read(F::kStrPairs));
}

void readParticles(Reader &r, Ac2Record &rec) {
    readObjectBase(r, rec);
    if (optionalPart(r, "SYST", rec))
        counted(r, rec.parts.back(), kSystem);
}

bool readObject(Reader &r, Ac2Record &rec) {
    const std::string &t = rec.tag;
    if (t == "2DOB" || t == "OLDS") {
        // OLDS: .ac1's, a picture whose SPRT has only its first four fields
        // (ac.exe 0x430910).
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

void readCamera(Reader &r, Ac2Record &rec) {
    const uint16_t n = r.u16();
    r.fields(rec, kCamera, std::size(kCamera), n);
    if (n <= std::size(kCamera))
        return;
    // A grid of size x size bytes (when it's at most 64 wide). Its size and
    // its bytes are two of the record's fields (ac.exe 0x408817, 0x40883c).
    const int size = int16_t(r.u16());
    Ac2Value grid;
    grid.type = T::kBytes;
    grid.raw = uint32_t(size);
    if (size > 0 && n > std::size(kCamera) + 1)
        grid.bytes = r.bytes(size_t(size) * size_t(size));
    rec.fields.emplace_back("grid", std::move(grid));
    for (size_t k = 0; k < std::size(kCameraEnd) && std::size(kCamera) + 2 + k < n; k++)
        rec.fields.emplace_back(kCameraEnd[k].name, r.read(kCameraEnd[k].type));
}

} // namespace

float Ac2Value::f() const {
    if (type == Type::kU16)
        return float(int16_t(raw));
    if (type != Type::kF32)
        return float(int32_t(raw));
    float v;
    std::memcpy(&v, &raw, 4);
    return v;
}

std::string Ac2Value::str() const {
    char buf[48];
    switch (type) {
    case Type::kU16:
        return std::to_string(int16_t(raw));
    case Type::kU32:
        return std::to_string(int32_t(raw));
    case Type::kF32:
        std::snprintf(buf, sizeof buf, "%g", f());
        return buf;
    case Type::kText:
        return "\"" + text + "\"";
    case Type::kTextPairs: {
        std::string s = "[";
        for (const auto &[a, b] : pairs)
            s += (s.size() > 1 ? ", \"" : "\"") + a + "\": \"" + b + "\"";
        return s + "]";
    }
    case Type::kNumberPairs: {
        std::string s = "[";
        for (const auto &[a, b] : numbers)
            s += (s.size() > 1 ? ", " : "") + std::to_string(a) + ":" + std::to_string(b);
        return s + "]";
    }
    case Type::kBytes:
        return std::to_string(raw) + "x" + std::to_string(raw) + " bytes";
    }
    return "";
}

const Ac2Value *Ac2Record::field(std::string_view name) const {
    for (const auto &[n, v] : fields)
        if (n == name)
            return &v;
    return nullptr;
}

const Ac2Record *Ac2Record::part(std::string_view t) const {
    for (const Ac2Record &p : parts)
        if (p.tag == t)
            return &p;
    return nullptr;
}

float Ac2Record::f(std::string_view name) const {
    const Ac2Value *v = field(name);
    return v ? v->f() : 0.0f;
}

int32_t Ac2Record::i(std::string_view name) const {
    const Ac2Value *v = field(name);
    return v ? (v->type == Ac2Value::Type::kU16 ? int16_t(v->raw) : v->i()) : 0;
}

std::string Ac2Record::text(std::string_view name) const {
    const Ac2Value *v = field(name);
    return v ? v->text : "";
}

const Ac2Record *Ac2Scene::camera() const {
    for (const Ac2Record &r : records)
        if (r.tag == "CAMR")
            return &r;
    return nullptr;
}

std::vector<const Ac2Record *> Ac2Scene::objects() const {
    std::vector<const Ac2Record *> out;
    for (const Ac2Record &r : records)
        if (isAc2Object(r.tag))
            out.push_back(&r);
    return out;
}

bool isAc2Object(std::string_view tag) {
    // (The object factory also makes FOOT objects, but a scene's FOOT
    // record is its footstep setting, read before the factory is asked.)
    for (const char *t :
         {"2DOB", "2DAN", "NING", "BINK", "SPOT", "ITEM", "LITE", "PART", "RAIN", "SNOW", "NPCX", "PCXX", "OLDS"})
        if (tag == t)
            return true;
    return false;
}

namespace {

Error parseScene(const std::vector<uint8_t> &data, Ac2Scene &out, bool narrow) {
    out = Ac2Scene();
    Reader r(data, narrow);
    while (!r.atEnd()) {
        const size_t at = r.pos();
        Ac2Record rec{r.tag(), {}, {}};
        if (r.fail())
            break;
        const std::string &t = rec.tag;
        // The scene's own settings (motoe.exe 0x435990).
        if (t == "DONE") {
            out.records.push_back(std::move(rec));
            out.end = r.pos();
            break;
        } else if (t == "SCEN") {
            rec.fields.emplace_back("path", r.read(F::kStr));
            out.path = rec.fields.back().second.text;
        } else if (t == "BASE") {
            // .ac1: the scene this one is built on (ac.exe 0x42c79e).
            rec.fields.emplace_back("scene", r.read(F::kStr));
        } else if (t == "LOAD" || t == "FAKE" || t == "OUTD" || t == "NVNT" || t == "SHOW") {
            rec.fields.emplace_back("value", r.read(F::kU16));
        } else if (t == "FOOT" || t == "PUZZ") {
            // Before the object factory: a scene's FOOT is its footstep sound set.
            rec.fields.emplace_back("value", r.read(F::kStr32));
        } else if (t == "MUSC") {
            rec.fields.emplace_back("music", r.read(F::kStrPairs));
        } else if (t == "EVNT") {
            rec.fields.emplace_back("events", r.read(F::kStrPairs));
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

} // namespace

Error parseAc2(const std::vector<uint8_t> &data, Ac2Scene &out) {
    return parseScene(data, out, false);
}

Error parseAc1(const std::vector<uint8_t> &data, Ac2Scene &out) {
    return parseScene(data, out, true);
}

namespace {

void dumpRecord(const Ac2Record &rec, std::string &out, int depth) {
    const std::string pad(size_t(depth) * 2, ' ');
    out += pad + rec.tag + "\n";
    for (const auto &[name, v] : rec.fields) {
        // Leave out empty text and zeros, which most fields are.
        if ((v.type == Ac2Value::Type::kText && v.text.empty()) ||
            ((v.type == Ac2Value::Type::kTextPairs || v.type == Ac2Value::Type::kNumberPairs) && v.pairs.empty() &&
             v.numbers.empty()) ||
            (v.type != Ac2Value::Type::kText && v.type != Ac2Value::Type::kTextPairs &&
             v.type != Ac2Value::Type::kNumberPairs && v.type != Ac2Value::Type::kBytes && v.raw == 0))
            continue;
        out += pad + "  " + name + " = " + v.str() + "\n";
    }
    for (const Ac2Record &p : rec.parts)
        dumpRecord(p, out, depth + 1);
}

} // namespace

std::string dumpAc2(const Ac2Scene &scene) {
    std::string out;
    for (const Ac2Record &r : scene.records)
        dumpRecord(r, out, 0);
    return out;
}

} // namespace reluva::awe
