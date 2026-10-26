// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/savegame.h"

#include "common/text.h"

#include <cstring>

namespace reluva::awe {

namespace {

// A tag as the game writes it: the u32 whose value reads as the name, so
// "DESC" is the bytes "CSED".
constexpr uint32_t tag(const char (&s)[5]) {
    return uint32_t(uint8_t(s[0])) << 24 | uint32_t(uint8_t(s[1])) << 16 | uint32_t(uint8_t(s[2])) << 8 |
           uint32_t(uint8_t(s[3]));
}

class Reader {
public:
    explicit Reader(const std::vector<uint8_t>& d) : d_(d) {}
    bool fail() const { return fail_; }
    bool atEnd() const { return pos_ >= d_.size(); }
    size_t pos() const { return pos_; }
    uint32_t u32() {
        if (!need(4))
            return 0;
        uint32_t v;
        std::memcpy(&v, d_.data() + pos_, 4);
        pos_ += 4;
        return v;
    }
    int s16() {
        if (!need(2))
            return 0;
        int16_t v;
        std::memcpy(&v, d_.data() + pos_, 2);
        pos_ += 2;
        return v;
    }
    int s32() { return int32_t(u32()); }
    float f32() {
        const uint32_t u = u32();
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }
    // w[n]: up to the first 0; what follows it is old memory.
    std::string text(size_t n) {
        if (!need(2 * n))
            return {};
        std::vector<uint16_t> units(n);
        std::memcpy(units.data(), d_.data() + pos_, 2 * n);
        pos_ += 2 * n;
        size_t len = 0;
        while (len < n && units[len])
            len++;
        return text::utf16leToUtf8(units.data(), len);
    }

private:
    bool need(size_t n) {
        if (pos_ + n > d_.size())
            fail_ = true;
        return !fail_;
    }
    const std::vector<uint8_t>& d_;
    size_t pos_ = 0;
    bool fail_ = false;
};

class Writer {
public:
    std::vector<uint8_t> out;
    void u32(uint32_t v) {
        const size_t at = out.size();
        out.resize(at + 4);
        std::memcpy(out.data() + at, &v, 4);
    }
    void s16(int v) {
        const int16_t s = int16_t(v);
        const size_t at = out.size();
        out.resize(at + 2);
        std::memcpy(out.data() + at, &s, 2);
    }
    void f32(float f) {
        uint32_t u;
        std::memcpy(&u, &f, 4);
        u32(u);
    }
    // w[n], zeros after the text (cut to fit with its 0).
    void text(const std::string& s, size_t n) {
        std::vector<uint16_t> units;
        for (size_t i = 0; i < s.size();) {
            uint32_t cp = text::nextUtf8(s, i);
            if (cp > 0xffff) {
                cp -= 0x10000;
                units.push_back(uint16_t(0xd800 + (cp >> 10)));
                units.push_back(uint16_t(0xdc00 + (cp & 0x3ff)));
            } else {
                units.push_back(uint16_t(cp));
            }
        }
        units.resize(n, 0);
        units[n - 1] = 0;
        const size_t at = out.size();
        out.resize(at + 2 * n);
        std::memcpy(out.data() + at, units.data(), 2 * n);
    }
};

void readPlacement(Reader& r, SaveGame::Placement& p) {
    for (float& v : p.position)
        v = r.f32();
    for (float& v : p.rotation)
        v = r.f32();
}

void writePlacement(Writer& w, const SaveGame::Placement& p) {
    for (float v : p.position)
        w.f32(v);
    for (float v : p.rotation)
        w.f32(v);
}

std::string tagName(uint32_t t) {
    std::string s;
    for (int k = 3; k >= 0; k--) {
        const char c = char((t >> (8 * k)) & 0xff);
        s += c >= 32 && c < 127 ? c : '?';
    }
    return s;
}

} // namespace

Error readSaveHeader(const std::vector<uint8_t>& data, std::string& description, std::string& date, std::string& time) {
    // Positionally, as the game does: tag, w[32] three times (DESC, TIME,
    // DATE).
    Reader r(data);
    r.u32();
    description = r.text(32);
    r.u32();
    time = r.text(32);
    r.u32();
    date = r.text(32);
    return r.fail() ? Error::fail("The saved game is too short") : Error::success();
}

Error readSaveGame(const std::vector<uint8_t>& data, SaveGame& out) {
    out = SaveGame();
    Reader r(data);
    while (!r.atEnd() && !r.fail()) {
        const size_t at = r.pos();
        const uint32_t t = r.u32();
        if (t == tag("DESC")) {
            out.description = r.text(32);
        } else if (t == tag("TIME")) {
            out.time = r.text(32);
        } else if (t == tag("DATE")) {
            out.date = r.text(32);
        } else if (t == tag("GLBT")) {
            out.timeOfDay = r.s16();
        } else if (t == tag("GLBW")) {
            out.weather = r.s16();
        } else if (t == tag("GLBM")) {
            out.moving = r.s16();
        } else if (t == tag("TIMR")) {
            r.u32(); // always 0, unused
        } else if (t == tag("INFO")) {
            out.location = r.text(32);
            out.scene = r.text(256);
            out.actMajor = r.s16();
            out.actMinor = r.s16();
        } else if (t == tag("ACTI")) {
            SaveGame::Act& a = out.act;
            a.preLoad = r.text(256);
            a.location = r.text(32);
            a.postLoad = r.text(256);
            a.completion = r.text(256);
            a.event = r.text(256);
            a.music = r.text(256);
            a.weather = r.s16();
            a.timeOfDay = r.s16();
            a.moving = r.s16();
        } else if (t == tag("CHAR")) {
            readPlacement(r, out.player.emplace());
        } else if (t == tag("FLLW")) {
            readPlacement(r, out.follower.emplace());
        } else if (t == tag("TAGS") || t == tag("TIMS")) {
            auto& list = t == tag("TAGS") ? out.tags : out.timers;
            for (int n = int(uint16_t(r.s16())); n > 0 && !r.fail(); n--) {
                std::string name = r.text(64);
                list.emplace_back(std::move(name), r.s16());
            }
        } else if (t == tag("STAT")) {
            for (int n = int(uint16_t(r.s16())); n > 0 && !r.fail(); n--) {
                std::string name = r.text(64);
                out.texts.emplace_back(std::move(name), r.text(256));
            }
        } else if (t == tag("POSI")) {
            for (int n = int(uint16_t(r.s16())); n > 0 && !r.fail(); n--) {
                SaveGame::Position p;
                p.scene = r.text(64);
                p.character = r.text(64);
                readPlacement(r, p.at);
                out.positions.push_back(std::move(p));
            }
        } else if (t == tag("TALK")) {
            for (int n = int(uint16_t(r.s16())); n > 0 && !r.fail(); n--) {
                SaveGame::Talk k;
                k.name = r.text(32);
                k.act = r.text(32);
                k.part = r.s32();
                k.thread = r.s32();
                k.status = r.s16();
                k.nest = r.s32();
                out.talks.push_back(std::move(k));
            }
        } else if (t == tag("ITEM")) {
            out.itemPage = r.s16();
            for (int n = int(uint16_t(r.s16())); n > 0 && !r.fail(); n--) {
                SaveGame::Item i;
                i.name = r.text(32);
                i.state = r.s16();
                i.slot = r.s16();
                i.page = r.s16();
                out.items.push_back(std::move(i));
            }
        } else {
            char buf[96];
            std::snprintf(buf, sizeof buf, "Unknown record \"%s\" at offset 0x%zx", tagName(t).c_str(), at);
            return Error::fail(buf);
        }
    }
    return r.fail() ? Error::fail("The saved game ends inside a record") : Error::success();
}

std::vector<uint8_t> writeSaveGame(const SaveGame& s) {
    Writer w;
    w.u32(tag("DESC"));
    w.text(s.description, 32);
    w.u32(tag("TIME"));
    w.text(s.time, 32);
    w.u32(tag("DATE"));
    w.text(s.date, 32);
    w.u32(tag("GLBT"));
    w.s16(s.timeOfDay);
    w.u32(tag("GLBW"));
    w.s16(s.weather);
    w.u32(tag("GLBM"));
    w.s16(s.moving);
    w.u32(tag("TIMR"));
    w.u32(0);
    w.u32(tag("INFO"));
    w.text(s.location, 32);
    w.text(s.scene, 256);
    w.s16(s.actMajor);
    w.s16(s.actMinor);
    w.u32(tag("ACTI"));
    w.text(s.act.preLoad, 256);
    w.text(s.act.location, 32);
    w.text(s.act.postLoad, 256);
    w.text(s.act.completion, 256);
    w.text(s.act.event, 256);
    w.text(s.act.music, 256);
    w.s16(s.act.weather);
    w.s16(s.act.timeOfDay);
    w.s16(s.act.moving);
    if (s.player) {
        w.u32(tag("CHAR"));
        writePlacement(w, *s.player);
    }
    if (s.follower) {
        w.u32(tag("FLLW"));
        writePlacement(w, *s.follower);
    }
    for (const auto* list : {&s.tags, &s.timers}) {
        w.u32(list == &s.tags ? tag("TAGS") : tag("TIMS"));
        w.s16(int(list->size()));
        for (const auto& [name, value] : *list) {
            w.text(name, 64);
            w.s16(value);
        }
    }
    w.u32(tag("STAT"));
    w.s16(int(s.texts.size()));
    for (const auto& [name, value] : s.texts) {
        w.text(name, 64);
        w.text(value, 256);
    }
    w.u32(tag("POSI"));
    w.s16(int(s.positions.size()));
    for (const SaveGame::Position& p : s.positions) {
        w.text(p.scene, 64);
        w.text(p.character, 64);
        writePlacement(w, p.at);
    }
    w.u32(tag("TALK"));
    w.s16(int(s.talks.size()));
    for (const SaveGame::Talk& k : s.talks) {
        w.text(k.name, 32);
        w.text(k.act, 32);
        w.u32(uint32_t(k.part));
        w.u32(uint32_t(k.thread));
        w.s16(k.status);
        w.u32(uint32_t(k.nest));
    }
    w.u32(tag("ITEM"));
    w.s16(s.itemPage);
    w.s16(int(s.items.size()));
    for (const SaveGame::Item& i : s.items) {
        w.text(i.name, 32);
        w.s16(i.state);
        w.s16(i.slot);
        w.s16(i.page);
    }
    return std::move(w.out);
}

std::vector<uint8_t> encodeBmp32(int width, int height, const std::vector<uint8_t>& rgba) {
    // BITMAPFILEHEADER, BITMAPINFOHEADER, then BGRA rows bottom first.
    const uint32_t imageSize = uint32_t(width) * uint32_t(height) * 4;
    std::vector<uint8_t> out(54 + imageSize, 0);
    auto put32 = [&](size_t at, uint32_t v) { std::memcpy(out.data() + at, &v, 4); };
    auto put16 = [&](size_t at, uint16_t v) { std::memcpy(out.data() + at, &v, 2); };
    out[0] = 'B', out[1] = 'M';
    put32(2, uint32_t(out.size()));
    put32(10, 54);
    put32(14, 40);
    put32(18, uint32_t(width));
    put32(22, uint32_t(height));
    put16(26, 1);
    put16(28, 32);
    put32(34, imageSize);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++) {
            const uint8_t* p = rgba.data() + (size_t(y) * size_t(width) + size_t(x)) * 4;
            uint8_t* q = out.data() + 54 + (size_t(height - 1 - y) * size_t(width) + size_t(x)) * 4;
            q[0] = p[2], q[1] = p[1], q[2] = p[0], q[3] = 255;
        }
    return out;
}

} // namespace reluva::awe
