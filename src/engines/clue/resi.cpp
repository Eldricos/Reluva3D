// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/resi.h"

#include <cstring>

namespace reluva::clue {

namespace {

class Reader {
public:
    explicit Reader(const std::vector<uint8_t>& d) : d_(d) {}
    bool fail() const { return fail_; }
    bool need(size_t n) {
        if (fail_ || p_ > d_.size() || d_.size() - p_ < n)
            fail_ = true;
        return !fail_;
    }
    uint8_t u8() { return need(1) ? d_[p_++] : 0; }
    uint16_t u16() {
        if (!need(2))
            return 0;
        const uint16_t v = uint16_t(d_[p_] | d_[p_ + 1] << 8);
        p_ += 2;
        return v;
    }
    int32_t i32() {
        if (!need(4))
            return 0;
        const uint32_t v = uint32_t(d_[p_]) | uint32_t(d_[p_ + 1]) << 8 | uint32_t(d_[p_ + 2]) << 16 |
                           uint32_t(d_[p_ + 3]) << 24;
        p_ += 4;
        return int32_t(v);
    }
    // A string: its length, then that many characters and a NUL (absent
    // when the length is 0).
    std::string text() {
        const size_t n = u8();
        if (n == 0 || !need(n + 1))
            return {};
        std::string s(reinterpret_cast<const char*>(d_.data() + p_), n);
        p_ += n + 1;
        return s;
    }
    size_t pos() const { return p_; }

private:
    const std::vector<uint8_t>& d_;
    size_t p_ = 0;
    bool fail_ = false;
};

} // namespace

const ResiControl* ResiDialog::control(int32_t want) const {
    for (const ResiControl& c : controls)
        if (c.id == want)
            return &c;
    return nullptr;
}

const ResiDialog* Resi::dialog(int32_t id) const {
    for (const ResiDialog& d : dialogs)
        if (d.id == id)
            return &d;
    return nullptr;
}

const ResiFont* Resi::font(uint32_t id) const {
    for (const ResiFont& f : fonts)
        if (f.id == id)
            return &f;
    return nullptr;
}

Error parseResi(const std::vector<uint8_t>& data, Resi& out) {
    out = {};
    if (data.size() < 6 || std::memcmp(data.data(), "RESI", 4) != 0)
        return Error::fail("not a RESI file");
    Reader r(data);
    for (int i = 0; i < 4; ++i)
        r.u8();
    const int dialogs = r.u8(), fonts = r.u8();
    for (int i = 0; i < fonts && !r.fail(); ++i) {
        ResiFont f;
        f.id = uint32_t(r.i32());
        f.animation = r.text();
        f.textSpacing = r.i32();
        f.lineSpacing = r.i32();
        out.fonts.push_back(std::move(f));
    }
    for (int i = 0; i < dialogs && !r.fail(); ++i) {
        ResiDialog d;
        for (int32_t& v : d.raw)
            v = r.i32();
        d.id = d.raw[0];
        d.background = r.text();
        const int controls = r.u8();
        for (int c = 0; c < controls && !r.fail(); ++c) {
            ResiControl k;
            for (int32_t& v : k.raw)
                v = r.i32();
            k.type = k.raw[0];
            k.id = k.raw[1];
            k.left = k.raw[2];
            k.top = k.raw[3];
            k.right = k.raw[4];
            k.bottom = k.raw[5];
            k.centreX = k.raw[9];
            k.centreY = k.raw[10];
            k.font = k.raw[14];
            k.otherFont = k.raw[15];
            k.group = k.raw[16];
            k.maxLength = k.raw[20];
            k.style = k.raw[21];
            k.text = r.text();
            k.animation = r.text();
            const size_t items = r.u16();
            for (size_t n = 0; n < items && !r.fail(); ++n)
                k.items.push_back(r.text());
            d.controls.push_back(std::move(k));
        }
        out.dialogs.push_back(std::move(d));
    }
    if (r.fail())
        return Error::fail("RESI file cut short");
    if (r.pos() != data.size())
        return Error::fail("RESI file has " + std::to_string(data.size() - r.pos()) + " bytes left over");
    return Error::success();
}

} // namespace reluva::clue
