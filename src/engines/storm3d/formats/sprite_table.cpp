// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/formats/sprite_table.h"

#include "common/text.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace reluva::storm3d {

namespace {

constexpr int kMaxIncludeDepth = 16;

std::vector<std::string> words(const std::string &line) {
    std::istringstream in(line);
    std::vector<std::string> out;
    for (std::string w; in >> w;)
        out.push_back(w);
    return out;
}

bool isNumber(const std::string &s, double &out) {
    const char *start = s.c_str();
    char *end = nullptr;
    out = std::strtod(start, &end);
    return end != start && *end == '\0';
}

// "@name=value" split; a bare "@name" has an empty value.
void directive(const std::string &word, std::string &name, std::string &value) {
    const size_t eq = word.find('=');
    name = word.substr(1, eq == std::string::npos ? std::string::npos : eq - 1);
    value = eq == std::string::npos ? "" : word.substr(eq + 1);
}

} // namespace

Error SpriteTable::load(std::string_view name, const Loader &loader) {
    _sprites.clear();
    _animations.clear();
    _emitters.clear();
    _spriteNodes.clear();
    _tables.clear();
    _problems.clear();
    return parse(name, loader, 0);
}

Error SpriteTable::parse(std::string_view name, const Loader &loader, int depth) {
    if (depth > kMaxIncludeDepth)
        return Error::fail(std::string(name) + ": includes nest more than " + std::to_string(kMaxIncludeDepth) +
                           " deep");
    std::string text;
    if (Error err = loader(name, text))
        return err;
    _tables.emplace_back(name);

    // What @reference, @skip and @tmaterial set holds for the sprites after
    // it in the same table.
    int referenceWidth = 0;
    int referenceHeight = 0;
    std::string skip;
    std::string material;
    Animation *animation = nullptr; // the one whose lines come next
    bool keyframes = false;         // and they're its keyframes
    Emitter *emitter = nullptr;
    size_t lineNumber = 0;
    for (std::string line : text::split(text, '\n')) {
        ++lineNumber;
        if (const size_t hash = line.find('#'); hash != std::string::npos)
            line.erase(hash);
        // A colour keyframe is a time and a colour, "0.5 (1,0.6,0.1)": its
        // numbers, in order.
        const bool rows = animation && (keyframes || !animation->kind.empty());
        if (rows && line.find('(') != std::string::npos)
            for (char &c : line)
                if (c == '(' || c == ')' || c == ',')
                    c = ' ';
        const std::vector<std::string> w = words(line);
        if (w.empty())
            continue;
        // Each table's first line names what it is.
        if (w.size() == 1 && w[0] == "sprite_table")
            continue;
        const std::string where = std::string(name) + ":" + std::to_string(lineNumber);

        if (emitter) {
            if (w[0] == "@end_emitter") {
                emitter = nullptr;
            } else if (const size_t eq = w[0].find('='); eq != std::string::npos) {
                const std::string rest = text::trim(line);
                const size_t at = rest.find('=');
                emitter->settings[text::trim(rest.substr(0, at))] = text::trim(rest.substr(at + 1));
            } else {
                _problems.push_back(where + ": " + text::trim(line));
            }
            continue;
        }

        double first = 0;
        if (isNumber(w[0], first)) {
            std::vector<double> row;
            for (const std::string &word : w) {
                double v = 0;
                if (!isNumber(word, v))
                    break;
                row.push_back(v);
            }
            // Rows straight after an animation's first line are its
            // keyframes too: animation.spr's holdbeam and assimbore have no
            // @keyframes.
            if (rows && row.size() == w.size())
                animation->keyframes.push_back(std::move(row));
            else
                _problems.push_back(where + ": " + text::trim(line));
            continue;
        }

        if (w[0][0] == '@') {
            std::string key;
            std::string value;
            directive(w[0], key, value);
            if (key == "include" && w.size() == 2) {
                if (Error err = parse(w[1], loader, depth + 1))
                    return Error::fail(where + ": " + err.message);
            } else if (key == "reference") {
                referenceWidth = referenceHeight = std::atoi(value.c_str());
            } else if (key == "referenceWidth") {
                referenceWidth = std::atoi(value.c_str());
            } else if (key == "referenceHeight") {
                referenceHeight = std::atoi(value.c_str());
            } else if (key == "skip") {
                skip = value;
            } else if (key == "tmaterial") {
                material = value;
            } else if (key == "animation" && w.size() == 2) {
                animation = &_animations[w[1]];
                *animation = Animation{};
                animation->name = w[1];
                keyframes = false;
            } else if (key == "auto" && animation) {
                animation->autoLayout = value;
            } else if (key == "keyframes" && animation) {
                keyframes = true;
            } else if (key == "emitter" && w.size() == 2) {
                _emitters.push_back({w[1], {}});
                emitter = &_emitters.back();
                animation = nullptr;
            } else if (key == "sprite_node") {
                SpriteNode node;
                const std::string trimmed = text::trim(line);
                if (parseSpriteNode(std::string_view(trimmed).substr(std::min<size_t>(trimmed.size(), 12)), node)) {
                    node.line = trimmed;
                    _spriteNodes.push_back(std::move(node));
                } else {
                    _problems.push_back(where + ": " + trimmed);
                }
            } else {
                _problems.push_back(where + ": " + text::trim(line));
            }
            continue;
        }

        // An animation's first line: what it changes, its keyframes'
        // count, its length and how it goes between them.
        double count = 0;
        double duration = 0;
        if (animation && !keyframes && animation->kind.empty() && w.size() == 4 && isNumber(w[1], count) &&
            isNumber(w[2], duration)) {
            animation->kind = w[0];
            animation->count = int(count);
            animation->duration = duration;
            animation->interpolation = w[3];
            continue;
        }

        // A sprite: name, texture, u, v, w, h, then @ parameters.
        double box[4] = {};
        if (w.size() >= 6 && isNumber(w[2], box[0]) && isNumber(w[3], box[1]) && isNumber(w[4], box[2]) &&
            isNumber(w[5], box[3])) {
            Sprite s;
            s.name = w[0];
            s.texture = w[1];
            s.u = box[0];
            s.v = box[1];
            s.w = box[2];
            s.h = box[3];
            s.referenceWidth = referenceWidth;
            s.referenceHeight = referenceHeight;
            s.skip = skip;
            s.material = material;
            s.file = std::string(name);
            for (size_t i = 6; i < w.size(); ++i) {
                if (w[i][0] != '@') {
                    _problems.push_back(where + ": '" + w[i] + "' after " + s.name);
                    continue;
                }
                std::string key;
                std::string value;
                directive(w[i], key, value);
                if (key == "anim")
                    s.animations.push_back(value);
                else
                    s.parameters[key] = value;
            }
            _sprites.push_back(std::move(s));
            animation = nullptr;
            continue;
        }
        _problems.push_back(where + ": " + text::trim(line));
    }
    return Error::success();
}

const SpriteTable::Sprite *SpriteTable::sprite(std::string_view name) const {
    // A name given twice is the later's: gui_global.spr's b_cancel, whose
    // second the original draws.
    for (auto s = _sprites.rbegin(); s != _sprites.rend(); ++s)
        if (s->name == name)
            return &*s;
    return nullptr;
}

bool SpriteTable::parseSpriteNode(std::string_view fields, SpriteNode &out) {
    // The words, then the bracketed groups' numbers in order: a size, then
    // a colour. A bracket left open or closed twice is taken as one.
    std::vector<std::string> words;
    std::vector<std::vector<double>> groups;
    size_t i = 0;
    while (i < fields.size()) {
        const char c = fields[i];
        if (c == ' ' || c == '\t' || c == ')') {
            ++i;
            continue;
        }
        if (c == '(') {
            const size_t close = fields.find_first_of(")", i);
            const size_t end = close == std::string_view::npos ? fields.size() : close;
            std::vector<double> numbers;
            for (const std::string &n : text::split(std::string(fields.substr(i + 1, end - i - 1)), ','))
                numbers.push_back(std::atof(n.c_str()));
            groups.push_back(numbers);
            i = end;
            continue;
        }
        size_t end = fields.find_first_of(" \t(", i);
        if (end == std::string_view::npos)
            end = fields.size();
        words.emplace_back(fields.substr(i, end - i));
        i = end;
    }
    if (words.size() < 3)
        return false;
    out.name = words[0];
    out.sprite = words[1];
    out.animation = words[2];
    // `//billboard` comments the flag out, as one table does.
    out.billboard = std::find(words.begin() + 3, words.end(), "billboard") != words.end();
    if (!groups.empty() && groups[0].size() >= 2) {
        out.width = groups[0][0];
        out.height = groups[0][1];
    }
    if (groups.size() >= 2)
        for (size_t k = 0; k < 3 && k < groups[1].size(); ++k)
            out.colour[k] = groups[1][k];
    return true;
}

const SpriteTable::SpriteNode *SpriteTable::spriteNodeFor(std::string_view nodeName) const {
    std::string name = text::toLower(nodeName);
    while (true) {
        for (const SpriteNode &n : _spriteNodes)
            if (text::toLower(n.name) == name)
                return &n;
        // Off with a trailing "_<number>", and try again.
        const size_t under = name.rfind('_');
        if (under == std::string::npos || under + 1 == name.size() ||
            name.find_first_not_of("0123456789", under + 1) != std::string::npos)
            return nullptr;
        name.resize(under);
    }
}

const SpriteTable::Animation *SpriteTable::animation(std::string_view name) const {
    const auto it = _animations.find(std::string(name));
    return it == _animations.end() ? nullptr : &it->second;
}

} // namespace reluva::storm3d
