// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Read as Star Trek.exe reads it (0x40d9f0): leading tabs, spaces, CR and
// LF skipped (0x5dfc60, its set at 0x64ab20), a line starting '}' ending
// the object, one without '=' skipped, key and value split at the first
// '='. The game knows from each class's properties which fields hold
// objects; Reluva, knowing no classes yet, takes a field for an object when
// a '{' line follows, which is the same on all of the game's files.

#include "engines/stawayteam/formats/text_object.h"

#include <charconv>
#include <cstdlib>

namespace reluva::stawayteam {

namespace {

// The text's lines, one at a time, with their numbers for errors.
class Lines {
public:
    explicit Lines(std::string_view text) : _text(text) {}

    // The next line without its line end and leading blanks, or nothing at
    // the text's end.
    std::optional<std::string_view> next() {
        if (_at >= _text.size())
            return std::nullopt;
        size_t end = _text.find('\n', _at);
        if (end == std::string_view::npos)
            end = _text.size();
        std::string_view line = _text.substr(_at, end - _at);
        _at = end + 1;
        ++_number;
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        const size_t start = line.find_first_not_of("\t \r\n");
        return start == std::string_view::npos ? std::string_view() : line.substr(start);
    }

    // The next line that isn't blank, without taking it.
    std::optional<std::string_view> peek() {
        const size_t at = _at;
        const int number = _number;
        std::optional<std::string_view> line;
        while ((line = next()) && line->empty()) {}
        _at = at;
        _number = number;
        return line;
    }

    int number() const { return _number; }

private:
    std::string_view _text;
    size_t _at = 0;
    int _number = 0;
};

Error readBlock(Lines &lines, TextObject &out, int depth) {
    // Deeper than any of the game's files go (49) by far: a guard against a
    // damaged file, not a limit of the format.
    constexpr int kMaxDepth = 256;
    if (depth > kMaxDepth)
        return Error::fail("line " + std::to_string(lines.number()) + ": objects nested too deep");
    std::optional<std::string_view> line;
    while ((line = lines.next()) && line->empty()) {}
    if (!line || *line != "{")
        return Error::fail("line " + std::to_string(lines.number()) + ": no '{' after " + out.className);
    for (;;) {
        line = lines.next();
        if (!line)
            return Error::fail("the text ends inside " + out.className);
        if (!line->empty() && line->front() == '}')
            return Error::success();
        const size_t equals = line->find('=');
        if (equals == std::string_view::npos)
            continue;
        TextField field;
        field.key = std::string(line->substr(0, equals));
        field.value = std::string(line->substr(equals + 1));
        if (const auto after = lines.peek(); after && *after == "{") {
            field.object = std::make_unique<TextObject>();
            field.object->className = field.value;
            if (Error err = readBlock(lines, *field.object, depth + 1))
                return err;
        }
        out.fields.push_back(std::move(field));
    }
}

void writeBlock(const TextObject &object, int depth, std::string &out) {
    const std::string indent(size_t(depth), '\t');
    out += indent + "{\r\n";
    for (const TextField &field : object.fields) {
        out += indent + '\t' + field.key + '=' + field.value + "\r\n";
        if (field.object)
            writeBlock(*field.object, depth + 1, out);
    }
    out += indent + "}\r\n";
}

} // namespace

const TextField *TextObject::field(std::string_view key) const {
    for (const TextField &f : fields)
        if (f.key == key)
            return &f;
    return nullptr;
}

const std::string *TextObject::value(std::string_view key) const {
    const TextField *f = field(key);
    return f ? &f->value : nullptr;
}

const TextObject *TextObject::object(std::string_view key) const {
    const TextField *f = field(key);
    return f ? f->object.get() : nullptr;
}

std::optional<long> TextObject::integer(std::string_view key) const {
    const std::string *v = value(key);
    if (!v || v->empty())
        return std::nullopt;
    long n = 0;
    const auto [end, ec] = std::from_chars(v->data(), v->data() + v->size(), n);
    if (ec != std::errc() || end != v->data() + v->size())
        return std::nullopt;
    return n;
}

std::optional<double> TextObject::number(std::string_view key) const {
    const std::string *v = value(key);
    if (!v || v->empty())
        return std::nullopt;
    // The game writes '.' for the decimal point, as strtod reads it in the
    // "C" locale every program starts in (nothing in Reluva changes it).
    char *end = nullptr;
    const double n = std::strtod(v->c_str(), &end);
    if (end != v->c_str() + v->size())
        return std::nullopt;
    return n;
}

std::vector<const TextField *> TextObject::items() const {
    std::vector<const TextField *> out;
    for (size_t i = 0; i < fields.size(); ++i) {
        const TextField &f = fields[i];
        if (i == 0 && !f.object && (f.key == "Item Count" || f.key == "Array Count"))
            continue;
        out.push_back(&f);
    }
    return out;
}

Error readTextObject(std::string_view text, TextObject &out) {
    out = TextObject{};
    Lines lines(text);
    std::optional<std::string_view> line;
    while ((line = lines.next()) && line->empty()) {}
    if (!line)
        return Error::fail("no object: the text is empty");
    if (line->find('=') != std::string_view::npos || *line == "{")
        return Error::fail("line " + std::to_string(lines.number()) + ": no class name before the object");
    out.className = std::string(*line);
    return readBlock(lines, out, 0);
}

std::string writeTextObject(const TextObject &object) {
    std::string out = object.className + "\r\n";
    writeBlock(object, 0, out);
    return out;
}

} // namespace reluva::stawayteam
