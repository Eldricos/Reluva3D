// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/ui/ui.h"

#include <algorithm>

namespace reluva::clue {

namespace {

// An image of the interface: an animation's frame (its first layer) or,
// for names of images ("menu:IprefsKnob"), the image itself.
struct Picture {
    const Image *image = nullptr;
    int dx = 0;
    int dy = 0;
    int frames = 0;
};

Picture picture(const GameData &data, const std::string &name, int frame) {
    ResourceName n;
    if (!splitResourceName(name, n))
        return {};
    if (n.name[0] == 'I' || n.name[0] == 'i') {
        const Image *img = data.image(name);
        return {img, img ? img->offsetX : 0, img ? img->offsetY : 0, img ? 1 : 0};
    }
    const Animation *a = data.animation(name);
    if (!a || a->layers.empty() || a->frameCount() == 0)
        return {};
    const int count = int(a->frameCount());
    const Animation::Part &part = a->layers[0][size_t(std::clamp(frame, 0, count - 1))];
    if (part.image >= a->images.size())
        return {nullptr, 0, 0, count};
    const Image *img = data.image(a->images[part.image]);
    if (!img)
        return {nullptr, 0, 0, count};
    return {img, part.x + img->offsetX, part.y + img->offsetY, count};
}

bool clickable(const ResiControl &c) {
    return c.type == ResiControl::kImageButton || c.type == ResiControl::kTextButton || c.type == ResiControl::kEdit ||
           c.type == ResiControl::kSlider || c.type == ResiControl::kScrollKnob;
}

} // namespace

BitmapFont::BitmapFont(const GameData &data, const ResiFont &font, bool packed) {
    const Animation *a = data.animation(font.animation);
    if (!a || a->layers.empty())
        return;
    _lineHeight = a->height + font.lineSpacing;
    // Characters without a glyph (the space) take a third of the cell.
    const int blank = std::max(3, a->width / 3);
    for (size_t c = 0; c < _glyphs.size() && c < a->frameCount(); ++c) {
        const Animation::Part &part = a->layers[0][c];
        Glyph &g = _glyphs[c];
        g.advance = blank + font.textSpacing;
        if (part.image >= a->images.size())
            continue;
        g.image = data.image(a->images[part.image]);
        if (!g.image)
            continue;
        g.advance = (g.image->version == 0x100 ? g.image->cellW : g.image->pixels.w) + font.textSpacing;
        g.dx = g.image->offsetX;
        if (!packed)
            continue;
        // Packed: each glyph's ink one pixel after the last's, as the
        // original draws its ticker (its sentence 168 pixels wide in a
        // 640x480 recording, 189 by the glyphs' cells; the same font's
        // labels in dialogs are drawn by their cells). A glyph without ink
        // (the space) keeps its cell and a pixel.
        const image::Image &px = g.image->pixels;
        int first = -1;
        int last = -1;
        for (int x = 0; x < px.w; ++x)
            for (int y = 0; y < px.h; ++y)
                if (px.pixel(x, y)[3]) {
                    if (first < 0)
                        first = x;
                    last = x;
                    break;
                }
        if (first < 0) {
            g.advance += 1;
            continue;
        }
        g.dx = -first;
        g.advance = last - first + 2 + font.textSpacing;
    }
}

int BitmapFont::width(std::string_view line) const {
    int w = 0;
    for (char ch : line)
        w += _glyphs[uint8_t(ch)].advance;
    return w;
}

void BitmapFont::draw(Surface &s, int x, int y, std::string_view line) const {
    for (char ch : line) {
        const Glyph &g = _glyphs[uint8_t(ch)];
        if (g.image)
            image::blit(s, g.image->pixels, x + g.dx, y + g.image->offsetY);
        x += g.advance;
    }
}

Fonts::Fonts(const GameData &data, const Resi &resi) {
    for (const ResiFont &f : resi.fonts)
        _fonts[int(f.id)] = std::make_unique<BitmapFont>(data, f);
}

const BitmapFont *Fonts::get(int id) const {
    auto it = _fonts.find(id);
    return it == _fonts.end() ? nullptr : it->second.get();
}

std::string typedText(std::string_view utf8) {
    // The fonts are Windows-1252: keep ASCII and Latin-1 from UTF-8.
    std::string out;
    for (size_t k = 0; k < utf8.size(); ++k) {
        const uint8_t b = uint8_t(utf8[k]);
        if (b < 0x80) {
            if (b >= 32)
                out += char(b);
        } else if ((b == 0xc2 || b == 0xc3) && k + 1 < utf8.size()) {
            out += char(((b & 0x03) << 6) | (uint8_t(utf8[k + 1]) & 0x3f));
            ++k;
        }
    }
    return out;
}

std::vector<std::string> splitLines(std::string_view text) {
    std::vector<std::string> lines(1);
    for (char ch : text) {
        if (ch == '|' || ch == '\n')
            lines.emplace_back();
        else if (ch != '\r')
            lines.back() += ch;
    }
    return lines;
}

void drawText(Surface &s, const BitmapFont &font, std::string_view text, int x, int y, int w, int h, bool centreX,
              bool centreY) {
    const std::vector<std::string> lines = splitLines(text);
    const int total = int(lines.size()) * font.lineHeight();
    int ly = centreY && h > 0 ? y + (h - total) / 2 : y;
    for (const std::string &line : lines) {
        const int lw = font.width(line);
        font.draw(s, centreX && w > 0 ? x + (w - lw) / 2 : x, ly, line);
        ly += font.lineHeight();
    }
}

DialogView::DialogView(const GameData &data, const Fonts &fonts, const ResiDialog &dialog, int x, int y)
    : _data(data), _fonts(fonts), _dialog(dialog), _x(x), _y(y), _state(dialog.controls.size()) {
    for (size_t i = 0; i < _state.size(); ++i)
        _state[i].visible = _dialog.controls[i].group == 0;
    // Its size: its background's; without one, its largest picture's (the
    // disproving window's backgrounds are pictures in it); else the screen's.
    const Picture bg = picture(_data, _dialog.background, 0);
    if (bg.image) {
        _width = bg.image->pixels.w + bg.dx;
        _height = bg.image->pixels.h + bg.dy;
    } else {
        for (const ResiControl &c : _dialog.controls)
            if (c.type == ResiControl::kImage)
                if (const Picture p = picture(_data, c.text, 0); p.image) {
                    _width = std::max(_width, c.left + p.image->pixels.w + p.dx);
                    _height = std::max(_height, c.top + p.image->pixels.h + p.dy);
                }
    }
    if (_width <= 0 || _height <= 0) {
        _width = kScreenW - _x;
        _height = kScreenH - _y;
    }
}

void DialogView::setVisible(int id, bool v) {
    for (size_t i = 0; i < _state.size(); ++i)
        if (_dialog.controls[i].id == id)
            _state[i].visible = v;
}

void DialogView::setTextAt(size_t index, std::string text) {
    if (index < _state.size()) {
        _state[index].text = std::move(text);
        _state[index].textSet = true;
    }
}

void DialogView::setVisibleAt(size_t index, bool v) {
    if (index < _state.size())
        _state[index].visible = v;
}

void DialogView::setGroupVisible(int group, bool v) {
    for (size_t i = 0; i < _state.size(); ++i)
        if (_dialog.controls[i].group == group)
            _state[i].visible = v;
}

bool DialogView::visible(int id) const {
    for (size_t i = 0; i < _state.size(); ++i)
        if (_dialog.controls[i].id == id)
            return _state[i].visible;
    return false;
}

void DialogView::setEnabled(int id, bool e) {
    for (size_t i = 0; i < _state.size(); ++i)
        if (_dialog.controls[i].id == id)
            _state[i].enabled = e;
}

void DialogView::setText(int id, const std::string &text) {
    for (size_t i = 0; i < _state.size(); ++i)
        if (_dialog.controls[i].id == id) {
            _state[i].text = text;
            _state[i].textSet = true;
            _state[i].scroll = 0;
        }
}

std::string DialogView::text(int id) const {
    for (size_t i = 0; i < _state.size(); ++i)
        if (_dialog.controls[i].id == id)
            return _state[i].textSet ? _state[i].text : _dialog.controls[i].text;
    return {};
}

void DialogView::setChosen(int id, bool c) {
    for (size_t i = 0; i < _state.size(); ++i)
        if (_dialog.controls[i].id == id)
            _state[i].chosen = c;
}

void DialogView::setValue(int id, int v) {
    for (size_t i = 0; i < _state.size(); ++i)
        if (_dialog.controls[i].id == id)
            _state[i].value = std::clamp(v, 0, 100);
}

int DialogView::value(int id) const {
    for (size_t i = 0; i < _state.size(); ++i)
        if (_dialog.controls[i].id == id)
            return _state[i].value;
    return 0;
}

void DialogView::setScroll(int id, int line) {
    for (size_t i = 0; i < _state.size(); ++i)
        if (_dialog.controls[i].id == id) {
            const int most = std::max(0, lineCount(id) - visibleLines(id));
            _state[i].scroll = std::clamp(line, 0, most);
        }
}

int DialogView::scroll(int id) const {
    for (size_t i = 0; i < _state.size(); ++i)
        if (_dialog.controls[i].id == id)
            return _state[i].scroll;
    return 0;
}

void DialogView::focus(int id) {
    _focus = id;
}

UiRect DialogView::rect(int id) const {
    const ResiControl *c = _dialog.control(id);
    return c ? rectOf(*c) : UiRect{};
}

UiRect DialogView::rectOf(const ResiControl &c) const {
    UiRect r{_x + c.left, _y + c.top, 0, 0};
    const std::string &name = c.type == ResiControl::kTextButton ? c.animation : c.text;
    if (c.type != ResiControl::kText && c.type != ResiControl::kEdit) {
        const Picture p = picture(_data, name, 0);
        if (p.image) {
            r.w = p.image->pixels.w + p.dx;
            r.h = p.image->pixels.h + p.dy;
        }
    }
    if (c.right > c.left)
        r.w = c.right - c.left;
    if (c.bottom > c.top)
        r.h = c.bottom - c.top;
    // -1: as far as the dialog goes.
    if (c.right == -1)
        r.w = _width - c.left;
    if (c.bottom == -1)
        r.h = _height - c.top;
    if (c.type == ResiControl::kEdit && r.h == 0)
        if (const BitmapFont *f = _fonts.get(c.font))
            r.h = f->lineHeight();
    return r;
}

std::vector<std::string> DialogView::wrapped(const ResiControl &c) const {
    const size_t i = size_t(&c - _dialog.controls.data());
    const std::string &text = _state[i].textSet ? _state[i].text : c.text;
    const BitmapFont *font = _fonts.get(c.font);
    const UiRect r = rectOf(c);
    std::vector<std::string> out;
    for (const std::string &line : splitLines(text)) {
        if (!font || r.w <= 0 || font->width(line) <= r.w) {
            out.push_back(line);
            continue;
        }
        // Word-wrap to the box.
        std::string cur;
        size_t p = 0;
        while (p <= line.size()) {
            size_t q = line.find(' ', p);
            if (q == std::string::npos)
                q = line.size();
            const std::string word = line.substr(p, q - p);
            const std::string tryLine = cur.empty() ? word : cur + " " + word;
            if (!cur.empty() && font->width(tryLine) > r.w) {
                out.push_back(cur);
                cur = word;
            } else {
                cur = tryLine;
            }
            p = q + 1;
        }
        out.push_back(cur);
    }
    return out;
}

int DialogView::lineCount(int id) const {
    const ResiControl *c = _dialog.control(id);
    return c ? int(wrapped(*c).size()) : 0;
}

int DialogView::visibleLines(int id) const {
    const ResiControl *c = _dialog.control(id);
    const BitmapFont *f = c ? _fonts.get(c->font) : nullptr;
    if (!c || !f || f->lineHeight() <= 0)
        return 0;
    return std::max(1, rectOf(*c).h / f->lineHeight());
}

void DialogView::drawBackground(Surface &s) const {
    if (const Picture bg = picture(_data, _dialog.background, 0); bg.image)
        image::blit(s, bg.image->pixels, _x + bg.dx, _y + bg.dy);
}

void DialogView::drawControls(Surface &s) const {
    for (size_t i = 0; i < _dialog.controls.size(); ++i) {
        const ResiControl &c = _dialog.controls[i];
        const State &st = _state[i];
        if (!st.visible)
            continue;
        const UiRect r = rectOf(c);
        const bool hot = int(i) == _hover;
        switch (c.type) {
        case ResiControl::kImage:
            if (const Picture p = picture(_data, c.text, 0); p.image)
                image::blit(s, p.image->pixels, r.x + p.dx, r.y + p.dy);
            break;
        case ResiControl::kImageButton:
        case ResiControl::kTextButton: {
            // Frames: 0 as it is, 1 under the mouse, 2 pressed (or chosen),
            // 3 disabled when the animation has a fourth.
            const std::string &anim = c.type == ResiControl::kTextButton ? c.animation : c.text;
            int frame = 0;
            if (st.chosen || (hot && int(i) == _pressed))
                frame = 2;
            else if (hot && st.enabled)
                frame = 1;
            Picture p = picture(_data, anim, frame);
            if (!st.enabled && p.frames >= 4)
                p = picture(_data, anim, 3);
            if (p.image)
                image::blit(s, p.image->pixels, r.x + p.dx, r.y + p.dy);
            if (c.type == ResiControl::kTextButton) {
                const BitmapFont *f = _fonts.get(hot && st.enabled ? c.otherFont : c.font);
                if (!f)
                    f = _fonts.get(c.font);
                // centreY 2 (the suggestion window's choices): the name along
                // the picture's foot, as recordings show it; else in its middle.
                if (f && c.centreY == 2)
                    drawText(s, *f, st.textSet ? st.text : c.text, r.x, r.y + r.h - f->lineHeight(), r.w,
                             f->lineHeight(), true, false);
                else if (f)
                    drawText(s, *f, st.textSet ? st.text : c.text, r.x, r.y, r.w, r.h, true, true);
            }
            break;
        }
        case ResiControl::kText: {
            const BitmapFont *f = _fonts.get(c.font);
            if (!f)
                break;
            const std::vector<std::string> lines = wrapped(c);
            const int fit = r.h > 0 ? std::max(1, r.h / std::max(1, f->lineHeight())) : int(lines.size());
            // Only a dialog with a scroll knob scrolls its text; elsewhere
            // text a little over its box is drawn whole, as the original's
            // ("Please wait for the Host|to start this game").
            const bool scrolls = std::any_of(_dialog.controls.begin(), _dialog.controls.end(),
                                             [](const ResiControl &k) { return k.type == ResiControl::kScrollKnob; });
            if (int(lines.size()) <= fit || !scrolls) {
                std::string joined;
                for (size_t k = 0; k < lines.size(); ++k)
                    joined += (k ? "|" : "") + lines[k];
                drawText(s, *f, joined, r.x, r.y, r.w, r.h, c.centreX != 0, c.centreY == 1);
            } else {
                // A box of text longer than it: from its scroll line on.
                for (int k = 0; k < fit && st.scroll + k < int(lines.size()); ++k)
                    f->draw(s, r.x, r.y + k * f->lineHeight(), lines[size_t(st.scroll + k)]);
            }
            break;
        }
        case ResiControl::kEdit: {
            const BitmapFont *f = _fonts.get(c.font);
            if (!f)
                break;
            std::string t = st.textSet ? st.text : c.text;
            if (_focus == c.id)
                t += '_';
            f->draw(s, r.x, r.y, t);
            break;
        }
        case ResiControl::kSlider:
        case ResiControl::kScrollKnob: {
            const Picture p = picture(_data, c.text, 0);
            if (!p.image)
                break;
            const int kw = p.image->pixels.w;
            const int kh = p.image->pixels.h;
            const int range = c.type == ResiControl::kSlider ? std::max(0, r.w - kw) : std::max(0, r.h - kh);
            const int at = range * st.value / 100;
            if (c.type == ResiControl::kSlider)
                image::blit(s, p.image->pixels, r.x + at, r.y);
            else
                image::blit(s, p.image->pixels, r.x, r.y + at);
            break;
        }
        default:
            break;
        }
    }
}

const ResiControl *DialogView::hit(int mx, int my) const {
    for (size_t i = _dialog.controls.size(); i-- > 0;) {
        const ResiControl &c = _dialog.controls[i];
        if (_state[i].visible && _state[i].enabled && clickable(c) && rectOf(c).contains(mx, my))
            return &c;
    }
    return nullptr;
}

int DialogView::handleEvent(const Event &ev) {
    const auto indexOf = [&](const ResiControl *c) { return c ? int(c - _dialog.controls.data()) : -1; };
    const auto dragTo = [&](int index, int mx, int my) {
        const ResiControl &c = _dialog.controls[size_t(index)];
        const UiRect r = rectOf(c);
        const Picture p = picture(_data, c.text, 0);
        const int k = p.image ? (c.type == ResiControl::kSlider ? p.image->pixels.w : p.image->pixels.h) : 0;
        const int range = std::max(1, (c.type == ResiControl::kSlider ? r.w : r.h) - k);
        const int pos = c.type == ResiControl::kSlider ? mx - r.x - k / 2 : my - r.y - k / 2;
        _state[size_t(index)].value = std::clamp(pos * 100 / range, 0, 100);
        return c.id;
    };
    switch (ev.type) {
    case EventType::kMouseMove:
        _hover = indexOf(hit(ev.mouseX, ev.mouseY));
        if (_dragging >= 0)
            return dragTo(_dragging, ev.mouseX, ev.mouseY);
        return -1;
    case EventType::kMouseDown: {
        if (ev.button != MouseButton::kLeft)
            return -1;
        const int i = indexOf(hit(ev.mouseX, ev.mouseY));
        _hover = _pressed = i;
        if (i < 0)
            return -1;
        const ResiControl &c = _dialog.controls[size_t(i)];
        if (c.type == ResiControl::kEdit)
            _focus = c.id;
        if (c.type == ResiControl::kSlider || c.type == ResiControl::kScrollKnob) {
            _dragging = i;
            return dragTo(i, ev.mouseX, ev.mouseY);
        }
        return -1;
    }
    case EventType::kMouseUp: {
        if (ev.button != MouseButton::kLeft)
            return -1;
        const int was = _pressed;
        _pressed = -1;
        _dragging = -1;
        const int i = indexOf(hit(ev.mouseX, ev.mouseY));
        if (was >= 0 && was == i) {
            const ResiControl &c = _dialog.controls[size_t(i)];
            if (c.type == ResiControl::kImageButton || c.type == ResiControl::kTextButton)
                return c.id;
        }
        return -1;
    }
    case EventType::kTextInput: {
        if (_focus < 0)
            return -1;
        for (size_t i = 0; i < _state.size(); ++i)
            if (_dialog.controls[i].id == _focus) {
                State &st = _state[i];
                if (!st.textSet) {
                    st.text = _dialog.controls[i].text;
                    st.textSet = true;
                }
                const size_t most = _dialog.controls[i].maxLength > 0 ? size_t(_dialog.controls[i].maxLength) : 20;
                st.text += typedText(ev.text);
                if (st.text.size() > most)
                    st.text.resize(most);
            }
        return -1;
    }
    case EventType::kKeyDown:
        if (ev.key == key::kBackspace && _focus >= 0)
            for (size_t i = 0; i < _state.size(); ++i)
                if (_dialog.controls[i].id == _focus && _state[i].textSet && !_state[i].text.empty())
                    _state[i].text.pop_back();
        return -1;
    default:
        return -1;
    }
}

void fillRect(Surface &s, int x, int y, int w, int h, Color c) {
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(s.w, x + w);
    const int y1 = std::min(s.h, y + h);
    for (int yy = y0; yy < y1; ++yy) {
        uint8_t *p = s.row(yy) + x0 * 4;
        for (int xx = x0; xx < x1; ++xx, p += 4) {
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = 255;
        }
    }
}

} // namespace reluva::clue
