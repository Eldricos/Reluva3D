// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/ui.h"

#include <algorithm>

namespace reluva::clue {

namespace {

constexpr int kScreenWidth = 640, kScreenHeight = 480;

// An image of the interface: an animation's frame (its first layer) or,
// for names of images ("menu:IprefsKnob"), the image itself.
struct Picture {
    const Image* image = nullptr;
    int dx = 0, dy = 0;
    int frames = 0;
};

Picture picture(const GameData& data, const std::string& name, int frame) {
    ResourceName n;
    if (!splitResourceName(name, n))
        return {};
    if (n.name[0] == 'I' || n.name[0] == 'i') {
        const Image* img = data.image(name);
        return {img, img ? img->offsetX : 0, img ? img->offsetY : 0, img ? 1 : 0};
    }
    const Animation* a = data.animation(name);
    if (!a || a->layers.empty() || a->frameCount() == 0)
        return {};
    const int count = int(a->frameCount());
    const Animation::Part& part = a->layers[0][size_t(std::clamp(frame, 0, count - 1))];
    if (part.image >= a->images.size())
        return {nullptr, 0, 0, count};
    const Image* img = data.image(a->images[part.image]);
    if (!img)
        return {nullptr, 0, 0, count};
    return {img, part.x + img->offsetX, part.y + img->offsetY, count};
}

bool clickable(const ResiControl& c) {
    return c.type == ResiControl::ImageButton || c.type == ResiControl::TextButton || c.type == ResiControl::Edit ||
           c.type == ResiControl::Slider || c.type == ResiControl::ScrollKnob;
}

} // namespace

BitmapFont::BitmapFont(const GameData& data, const ResiFont& font) {
    const Animation* a = data.animation(font.animation);
    if (!a || a->layers.empty())
        return;
    lineHeight_ = a->height + font.lineSpacing;
    // Characters without a glyph (the space) take a third of the cell.
    const int blank = std::max(3, a->width / 3);
    for (size_t c = 0; c < glyphs_.size() && c < a->frameCount(); ++c) {
        const Animation::Part& part = a->layers[0][c];
        Glyph& g = glyphs_[c];
        g.advance = blank + font.textSpacing;
        if (part.image >= a->images.size())
            continue;
        g.image = data.image(a->images[part.image]);
        if (g.image)
            g.advance = (g.image->version == 0x100 ? g.image->cellW : g.image->pixels.w) + font.textSpacing;
    }
}

int BitmapFont::width(std::string_view line) const {
    int w = 0;
    for (char ch : line)
        w += glyphs_[uint8_t(ch)].advance;
    return w;
}

void BitmapFont::draw(Surface& s, int x, int y, std::string_view line) const {
    for (char ch : line) {
        const Glyph& g = glyphs_[uint8_t(ch)];
        if (g.image)
            image::blit(s, g.image->pixels, x + g.image->offsetX, y + g.image->offsetY);
        x += g.advance;
    }
}

Fonts::Fonts(const GameData& data, const Resi& resi) {
    for (const ResiFont& f : resi.fonts)
        fonts_[int(f.id)] = std::make_unique<BitmapFont>(data, f);
}

const BitmapFont* Fonts::get(int id) const {
    auto it = fonts_.find(id);
    return it == fonts_.end() ? nullptr : it->second.get();
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

void drawText(Surface& s, const BitmapFont& font, std::string_view text, int x, int y, int w, int h, bool centreX,
              bool centreY) {
    const std::vector<std::string> lines = splitLines(text);
    const int total = int(lines.size()) * font.lineHeight();
    int ly = centreY && h > 0 ? y + (h - total) / 2 : y;
    for (const std::string& line : lines) {
        const int lw = font.width(line);
        font.draw(s, centreX && w > 0 ? x + (w - lw) / 2 : x, ly, line);
        ly += font.lineHeight();
    }
}

DialogView::DialogView(const GameData& data, const Fonts& fonts, const ResiDialog& dialog, int x, int y)
    : data_(data), fonts_(fonts), dialog_(dialog), x_(x), y_(y), state_(dialog.controls.size()) {
    for (size_t i = 0; i < state_.size(); ++i)
        state_[i].visible = dialog_.controls[i].group == 0;
    // Its size: its background's; without one, its largest picture's (the
    // disproving window's backgrounds are pictures in it); else the screen's.
    const Picture bg = picture(data_, dialog_.background, 0);
    if (bg.image) {
        width_ = bg.image->pixels.w + bg.dx;
        height_ = bg.image->pixels.h + bg.dy;
    } else {
        for (const ResiControl& c : dialog_.controls)
            if (c.type == ResiControl::Image)
                if (const Picture p = picture(data_, c.text, 0); p.image) {
                    width_ = std::max(width_, c.left + p.image->pixels.w + p.dx);
                    height_ = std::max(height_, c.top + p.image->pixels.h + p.dy);
                }
    }
    if (width_ <= 0 || height_ <= 0) {
        width_ = kScreenWidth - x_;
        height_ = kScreenHeight - y_;
    }
}

void DialogView::setVisible(int id, bool v) {
    for (size_t i = 0; i < state_.size(); ++i)
        if (dialog_.controls[i].id == id)
            state_[i].visible = v;
}

void DialogView::setGroupVisible(int group, bool v) {
    for (size_t i = 0; i < state_.size(); ++i)
        if (dialog_.controls[i].group == group)
            state_[i].visible = v;
}

bool DialogView::visible(int id) const {
    for (size_t i = 0; i < state_.size(); ++i)
        if (dialog_.controls[i].id == id)
            return state_[i].visible;
    return false;
}

void DialogView::setEnabled(int id, bool e) {
    for (size_t i = 0; i < state_.size(); ++i)
        if (dialog_.controls[i].id == id)
            state_[i].enabled = e;
}

void DialogView::setText(int id, std::string text) {
    for (size_t i = 0; i < state_.size(); ++i)
        if (dialog_.controls[i].id == id) {
            state_[i].text = text;
            state_[i].textSet = true;
            state_[i].scroll = 0;
        }
}

std::string DialogView::text(int id) const {
    for (size_t i = 0; i < state_.size(); ++i)
        if (dialog_.controls[i].id == id)
            return state_[i].textSet ? state_[i].text : dialog_.controls[i].text;
    return {};
}

void DialogView::setChosen(int id, bool c) {
    for (size_t i = 0; i < state_.size(); ++i)
        if (dialog_.controls[i].id == id)
            state_[i].chosen = c;
}

void DialogView::setValue(int id, int v) {
    for (size_t i = 0; i < state_.size(); ++i)
        if (dialog_.controls[i].id == id)
            state_[i].value = std::clamp(v, 0, 100);
}

int DialogView::value(int id) const {
    for (size_t i = 0; i < state_.size(); ++i)
        if (dialog_.controls[i].id == id)
            return state_[i].value;
    return 0;
}

void DialogView::setScroll(int id, int line) {
    for (size_t i = 0; i < state_.size(); ++i)
        if (dialog_.controls[i].id == id) {
            const int most = std::max(0, lineCount(id) - visibleLines(id));
            state_[i].scroll = std::clamp(line, 0, most);
        }
}

int DialogView::scroll(int id) const {
    for (size_t i = 0; i < state_.size(); ++i)
        if (dialog_.controls[i].id == id)
            return state_[i].scroll;
    return 0;
}

void DialogView::focus(int id) { focus_ = id; }

UiRect DialogView::rectOf(const ResiControl& c) const {
    UiRect r{x_ + c.left, y_ + c.top, 0, 0};
    const std::string& name = c.type == ResiControl::TextButton ? c.animation : c.text;
    if (c.type != ResiControl::Text && c.type != ResiControl::Edit) {
        const Picture p = picture(data_, name, 0);
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
        r.w = width_ - c.left;
    if (c.bottom == -1)
        r.h = height_ - c.top;
    if (c.type == ResiControl::Edit && r.h == 0)
        if (const BitmapFont* f = fonts_.get(c.font))
            r.h = f->lineHeight();
    return r;
}

std::vector<std::string> DialogView::wrapped(const ResiControl& c) const {
    const size_t i = size_t(&c - dialog_.controls.data());
    const std::string& text = state_[i].textSet ? state_[i].text : c.text;
    const BitmapFont* font = fonts_.get(c.font);
    const UiRect r = rectOf(c);
    std::vector<std::string> out;
    for (const std::string& line : splitLines(text)) {
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
    const ResiControl* c = dialog_.control(id);
    return c ? int(wrapped(*c).size()) : 0;
}

int DialogView::visibleLines(int id) const {
    const ResiControl* c = dialog_.control(id);
    const BitmapFont* f = c ? fonts_.get(c->font) : nullptr;
    if (!c || !f || f->lineHeight() <= 0)
        return 0;
    return std::max(1, rectOf(*c).h / f->lineHeight());
}

void DialogView::drawBackground(Surface& s) const {
    if (const Picture bg = picture(data_, dialog_.background, 0); bg.image)
        image::blit(s, bg.image->pixels, x_ + bg.dx, y_ + bg.dy);
}

void DialogView::drawControls(Surface& s) const {
    for (size_t i = 0; i < dialog_.controls.size(); ++i) {
        const ResiControl& c = dialog_.controls[i];
        const State& st = state_[i];
        if (!st.visible)
            continue;
        const UiRect r = rectOf(c);
        const bool hot = int(i) == hover_;
        switch (c.type) {
        case ResiControl::Image:
            if (const Picture p = picture(data_, c.text, 0); p.image)
                image::blit(s, p.image->pixels, r.x + p.dx, r.y + p.dy);
            break;
        case ResiControl::ImageButton:
        case ResiControl::TextButton: {
            // Frames: 0 as it is, 1 under the mouse, 2 pressed (or chosen),
            // 3 disabled when the animation has a fourth.
            const std::string& anim = c.type == ResiControl::TextButton ? c.animation : c.text;
            int frame = 0;
            if (st.chosen || (hot && int(i) == pressed_))
                frame = 2;
            else if (hot && st.enabled)
                frame = 1;
            Picture p = picture(data_, anim, frame);
            if (!st.enabled && p.frames >= 4)
                p = picture(data_, anim, 3);
            if (p.image)
                image::blit(s, p.image->pixels, r.x + p.dx, r.y + p.dy);
            if (c.type == ResiControl::TextButton) {
                const BitmapFont* f = fonts_.get(hot && st.enabled ? c.otherFont : c.font);
                if (!f)
                    f = fonts_.get(c.font);
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
        case ResiControl::Text: {
            const BitmapFont* f = fonts_.get(c.font);
            if (!f)
                break;
            const std::vector<std::string> lines = wrapped(c);
            const int fit = r.h > 0 ? std::max(1, r.h / std::max(1, f->lineHeight())) : int(lines.size());
            if (int(lines.size()) <= fit) {
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
        case ResiControl::Edit: {
            const BitmapFont* f = fonts_.get(c.font);
            if (!f)
                break;
            std::string t = st.textSet ? st.text : c.text;
            if (focus_ == c.id)
                t += '_';
            f->draw(s, r.x, r.y, t);
            break;
        }
        case ResiControl::Slider:
        case ResiControl::ScrollKnob: {
            const Picture p = picture(data_, c.text, 0);
            if (!p.image)
                break;
            const int kw = p.image->pixels.w, kh = p.image->pixels.h;
            const int range = c.type == ResiControl::Slider ? std::max(0, r.w - kw) : std::max(0, r.h - kh);
            const int at = range * st.value / 100;
            if (c.type == ResiControl::Slider)
                image::blit(s, p.image->pixels, r.x + at, r.y);
            else
                image::blit(s, p.image->pixels, r.x, r.y + at);
            break;
        }
        default: break;
        }
    }
}

const ResiControl* DialogView::hit(int mx, int my) const {
    for (size_t i = dialog_.controls.size(); i-- > 0;) {
        const ResiControl& c = dialog_.controls[i];
        if (state_[i].visible && state_[i].enabled && clickable(c) && rectOf(c).contains(mx, my))
            return &c;
    }
    return nullptr;
}

int DialogView::handleEvent(const Event& ev) {
    const auto indexOf = [&](const ResiControl* c) { return c ? int(c - dialog_.controls.data()) : -1; };
    const auto dragTo = [&](int index, int mx, int my) {
        const ResiControl& c = dialog_.controls[size_t(index)];
        const UiRect r = rectOf(c);
        const Picture p = picture(data_, c.text, 0);
        const int k = p.image ? (c.type == ResiControl::Slider ? p.image->pixels.w : p.image->pixels.h) : 0;
        const int range = std::max(1, (c.type == ResiControl::Slider ? r.w : r.h) - k);
        const int pos = c.type == ResiControl::Slider ? mx - r.x - k / 2 : my - r.y - k / 2;
        state_[size_t(index)].value = std::clamp(pos * 100 / range, 0, 100);
        return c.id;
    };
    switch (ev.type) {
    case EventType::MouseMove:
        hover_ = indexOf(hit(ev.mouseX, ev.mouseY));
        if (dragging_ >= 0)
            return dragTo(dragging_, ev.mouseX, ev.mouseY);
        return -1;
    case EventType::MouseDown: {
        if (ev.button != MouseButton::Left)
            return -1;
        const int i = indexOf(hit(ev.mouseX, ev.mouseY));
        hover_ = pressed_ = i;
        if (i < 0)
            return -1;
        const ResiControl& c = dialog_.controls[size_t(i)];
        if (c.type == ResiControl::Edit)
            focus_ = c.id;
        if (c.type == ResiControl::Slider || c.type == ResiControl::ScrollKnob) {
            dragging_ = i;
            return dragTo(i, ev.mouseX, ev.mouseY);
        }
        return -1;
    }
    case EventType::MouseUp: {
        if (ev.button != MouseButton::Left)
            return -1;
        const int was = pressed_;
        pressed_ = -1;
        dragging_ = -1;
        const int i = indexOf(hit(ev.mouseX, ev.mouseY));
        if (was >= 0 && was == i) {
            const ResiControl& c = dialog_.controls[size_t(i)];
            if (c.type == ResiControl::ImageButton || c.type == ResiControl::TextButton)
                return c.id;
        }
        return -1;
    }
    case EventType::TextInput: {
        if (focus_ < 0)
            return -1;
        for (size_t i = 0; i < state_.size(); ++i)
            if (dialog_.controls[i].id == focus_) {
                State& st = state_[i];
                if (!st.textSet) {
                    st.text = dialog_.controls[i].text;
                    st.textSet = true;
                }
                const size_t most = dialog_.controls[i].maxLength > 0 ? size_t(dialog_.controls[i].maxLength) : 20;
                // The fonts are Windows-1252: keep ASCII and Latin-1 from UTF-8.
                for (size_t k = 0; ev.text[k] && st.text.size() < most; ++k) {
                    const uint8_t b = uint8_t(ev.text[k]);
                    if (b < 0x80) {
                        if (b >= 32)
                            st.text += char(b);
                    } else if ((b == 0xc2 || b == 0xc3) && ev.text[k + 1]) {
                        st.text += char(((b & 0x03) << 6) | (uint8_t(ev.text[k + 1]) & 0x3f));
                        ++k;
                    }
                }
            }
        return -1;
    }
    case EventType::KeyDown:
        if (ev.key == key::Backspace && focus_ >= 0)
            for (size_t i = 0; i < state_.size(); ++i)
                if (dialog_.controls[i].id == focus_ && state_[i].textSet && !state_[i].text.empty())
                    state_[i].text.pop_back();
        return -1;
    default: return -1;
    }
}

} // namespace reluva::clue
