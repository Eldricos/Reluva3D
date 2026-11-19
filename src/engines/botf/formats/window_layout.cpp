// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/formats/window_layout.h"

#include "common/fs.h"
#include "common/text.h"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace reluva::botf {

namespace {

// Reads the file front to back; any read past its end sets `failed`.
struct Reader {
    const std::vector<uint8_t> &data;
    size_t pos = 0;
    bool failed = false;

    bool has(size_t n) {
        if (pos + n > data.size())
            failed = true;
        return !failed;
    }
    uint8_t u8() { return has(1) ? data[pos++] : 0; }
    int32_t i32() {
        if (!has(4))
            return 0;
        const uint8_t *p = data.data() + pos;
        pos += 4;
        return int32_t(uint32_t(p[0] | p[1] << 8 | p[2] << 16) | uint32_t(p[3]) << 24);
    }
    // A 13-byte file name, zero-terminated (what follows the zero is
    // leftover); "none" is no file.
    std::string name() {
        if (!has(13))
            return {};
        const char *p = reinterpret_cast<const char *>(data.data() + pos);
        pos += 13;
        std::string s = fs::toLower(text::windows1252ToUtf8(std::string_view(p, strnlen(p, 13))));
        while (!s.empty() && s.back() == ' ')
            s.pop_back();
        return s == "none" ? std::string() : s;
    }
};

void picture(Widget &w, const char *role, std::string file) {
    if (!file.empty())
        w.pictures.push_back({role, std::move(file)});
}

WidgetFont readFont(Reader &in) {
    WidgetFont f;
    f.color.r = in.u8();
    f.color.b = in.u8();
    f.color.g = in.u8();
    for (int32_t &v : f.other)
        v = in.i32();
    f.own = in.i32() != 0;
    f.file = in.name();
    return f;
}

Error readWidget(Reader &in, WidgetType type, Widget &w, int depth);

Error readWindowContents(Reader &in, Widget &w, int depth) {
    // The number of widgets, then how many of each type (types 1 to 15). The
    // two agree in the files; where they wouldn't, the larger is read, as
    // far as the file goes.
    const int32_t total = in.i32();
    int32_t byType = 0;
    for (int i = 0; i < 15; ++i)
        byType += in.i32();
    if (in.failed)
        return Error::fail("ends in a window's header");
    int32_t count = std::max(total, byType);
    for (; count > 0 && in.pos < in.data.size(); --count) {
        const uint32_t t = uint32_t(in.i32());
        Widget child;
        if (Error err = readWidget(in, WidgetType(t), child, depth + 1))
            return err;
        w.children.push_back(std::move(child));
    }
    return Error::success();
}

Error readWidget(Reader &in, WidgetType type, Widget &w, int depth) {
    if (depth > 16)
        return Error::fail("windows nested too deep");
    w.type = type;
    w.id = in.i32();
    w.x = in.i32();
    w.y = in.i32();
    w.w = in.i32();
    w.h = in.i32();
    w.background = in.name();
    picture(w, "background", w.background);
    switch (type) {
    case WidgetType::kWindow:
        if (Error err = readWindowContents(in, w, depth))
            return err;
        break;
    case WidgetType::kButton:
        w.unknown.push_back(in.i32());
        w.buttonKind = in.u8();
        w.repeats = in.i32() != 0;
        w.repeatsPerSecond = in.i32();
        picture(w, "normal", in.name());
        picture(w, "selected", in.name());
        break;
    case WidgetType::kMenu:
        w.textX = in.i32();
        w.textY = in.i32();
        w.textW = in.i32();
        w.textH = in.i32();
        w.fonts.push_back(readFont(in));
        w.fonts.push_back(readFont(in));
        w.menuFile = in.name();
        picture(w, "left", in.name());
        picture(w, "top", in.name());
        picture(w, "right", in.name());
        picture(w, "bottom", in.name());
        picture(w, "other", in.name());
        break;
    case WidgetType::kSlider: {
        const int lo = in.u8();
        w.unknown.push_back(lo | in.u8() << 8);
        w.maxValue = in.i32();
        w.units = in.i32();
        w.unknown.push_back(in.i32()); // the cells' width
        w.unknown.push_back(in.i32());
        picture(w, "cell off", in.name());
        picture(w, "cell on", in.name());
        picture(w, "marker off", in.name());
        picture(w, "marker on", in.name());
        for (int i = 0; i < 3; ++i) // the maximum, units and width again
            w.unknown.push_back(in.i32());
        break;
    }
    case WidgetType::kScrollBar:
        w.unknown.push_back(in.i32());
        picture(w, "handle on", in.name());
        picture(w, "handle off", in.name());
        w.minValue = in.i32();
        w.maxValue = in.i32();
        break;
    case WidgetType::kListBox:
        w.unknown.push_back(in.i32());
        w.fonts.push_back(readFont(in));
        w.fonts.push_back(readFont(in));
        for (int32_t &c : w.columnWidths)
            c = in.i32();
        w.columns = in.i32();
        break;
    case WidgetType::kPicture:
        w.unknown.push_back(in.i32());
        break;
    case WidgetType::kTextBox:
        w.textX = in.i32();
        w.textY = in.i32();
        w.textW = in.i32();
        w.textH = in.i32();
        w.fonts.push_back(readFont(in));
        break;
    default:
        return Error::fail("unknown widget type " + std::to_string(uint32_t(type)) + " at byte " +
                           std::to_string(in.pos - 4));
    }
    if (in.failed)
        return Error::fail(std::string("ends inside a ") + widgetTypeName(type));
    return Error::success();
}

void place(const Widget &w, int x, int y, int depth, std::vector<WindowLayout::Placed> &out) {
    out.push_back({&w, x + w.x, y + w.y, depth});
    for (const Widget &c : w.children)
        place(c, x + w.x, y + w.y, depth + 1, out);
}

} // namespace

const char *widgetTypeName(WidgetType type) {
    switch (type) {
    case WidgetType::kWindow:
        return "window";
    case WidgetType::kButton:
        return "button";
    case WidgetType::kMenu:
        return "menu";
    case WidgetType::kSlider:
        return "slider";
    case WidgetType::kScrollBar:
        return "scroll bar";
    case WidgetType::kListBox:
        return "list box";
    case WidgetType::kPicture:
        return "picture";
    case WidgetType::kTextBox:
        return "text box";
    }
    return "?";
}

Error WindowLayout::load(const std::vector<uint8_t> &data) {
    _root = Widget{};
    Reader in{data};
    _kind = uint32_t(in.i32());
    const uint32_t type = uint32_t(in.i32());
    if (in.failed)
        return Error::fail("too small");
    if (type != uint32_t(WidgetType::kWindow))
        return Error::fail("doesn't start with a window (type " + std::to_string(type) + ")");
    Error err = readWidget(in, WidgetType::kWindow, _root, 0);
    _used = in.pos;
    return err;
}

std::vector<WindowLayout::Placed> WindowLayout::placed() const {
    std::vector<Placed> out;
    place(_root, 0, 0, 0, out);
    return out;
}

} // namespace reluva::botf
