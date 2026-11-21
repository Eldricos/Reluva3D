// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/pages/object_viewer.h"

#include "common/fs.h"
#include "engines/stawayteam/formats/binary_object.h"
#include "engines/stawayteam/formats/rds_script.h"
#include "engines/stawayteam/formats/text_object.h"
#include "engines/stawayteam/pages/draw.h"
#include "graphics/font.h"

#include <algorithm>

namespace reluva::stawayteam {

namespace {

constexpr int kLineHeight = 10;

bool isText(const std::string &lower) {
    return lower.ends_with(".txt") || lower.ends_with(".zax") || lower.ends_with(".mds") || lower.ends_with(".sty");
}

bool isBinary(const std::string &lower) {
    return lower.ends_with(".mdl16") || lower.ends_with(".seq16") || lower.ends_with(".way");
}

void addObject(const TextObject &object, int depth, std::vector<std::string> &lines) {
    for (const TextField &f : object.fields) {
        lines.push_back(std::string(size_t(depth) * 2, ' ') + (f.key.empty() ? "(no name)" : f.key) + " = " + f.value);
        if (f.object)
            addObject(*f.object, depth + 1, lines);
    }
}

void addSequence(const SequenceData &seq, int depth, std::vector<std::string> &lines) {
    size_t present = 0;
    for (const auto &f : seq.frames)
        present += f.has_value();
    lines.push_back(std::string(size_t(depth) * 2, ' ') + "(" + std::to_string(seq.frames.size()) + " frames, " +
                    std::to_string(present) + " with pictures)");
}

void addBinary(const BinaryObject &object, int depth, std::vector<std::string> &lines) {
    lines.push_back(std::string(size_t(depth) * 2, ' ') + object.object.className);
    addObject(object.object, depth + 1, lines);
    if (object.sequence)
        addSequence(*object.sequence, depth + 1, lines);
    for (const NamedSequence &s : object.sequences) {
        if (!s.sequence)
            continue;
        lines.push_back(std::string(size_t(depth + 1) * 2, ' ') + "sequence \"" + s.name + "\":");
        addBinary(*s.sequence, depth + 2, lines);
    }
    if (!object.wayPoints.empty() || !object.lines.empty())
        lines.push_back(std::string(size_t(depth + 1) * 2, ' ') + "(" + std::to_string(object.wayPoints.size()) +
                        " waypoints, " + std::to_string(object.lines.size()) + " lines)");
}

} // namespace

ObjectViewer::ObjectViewer(System &sys, Surface &screen, const Archive &data)
    : _sys(sys), _screen(screen), _data(data),
      _files(data, [](const std::string &name) { return isText(name) || isBinary(name) || name.ends_with(".rds"); }) {
}

void ObjectViewer::activate() {
    if (_lines.empty())
        load();
    draw();
}

void ObjectViewer::load() {
    _lines.clear();
    _scroll = 0;
    const std::string &name = _files.current();
    const std::string lower = fs::toLower(name);
    std::vector<uint8_t> bytes;
    if (Error err = _data.read(name, bytes)) {
        _lines.push_back(err.message);
        return;
    }
    const std::string text(bytes.begin(), bytes.end());
    if (lower.ends_with(".rds")) {
        for (const auto &entry : readRdsScript(text).entries) {
            _lines.push_back("[" + entry.label + "]");
            size_t start = 0;
            while (start <= entry.text.size()) {
                const size_t end = std::min(entry.text.find('\n', start), entry.text.size());
                _lines.push_back("  " + entry.text.substr(start, end - start));
                start = end + 1;
            }
        }
        if (_lines.empty())
            _lines.push_back("(no entries)");
    } else if (isText(lower)) {
        TextObject object;
        if (Error err = readTextObject(text, object)) {
            _lines.push_back("not an object: " + err.message);
            return;
        }
        _lines.push_back(object.className);
        addObject(object, 1, _lines);
    } else {
        const std::string cls = lower.ends_with(".mdl16") ? "CModel" : lower.ends_with(".way") ? "CWayPointMap" : "";
        BinaryObject object;
        if (Error err = readBinaryObject(bytes.data(), bytes.size(), cls, object)) {
            _lines.push_back(err.message);
            return;
        }
        addBinary(object, 0, _lines);
    }
}

void ObjectViewer::handleKey(uint32_t key) {
    const size_t page = size_t((_screen.h - kStatusHeight) / kLineHeight) - 1;
    if (key == key::kDown) {
        ++_scroll;
    } else if (key == key::kUp) {
        _scroll = _scroll ? _scroll - 1 : 0;
    } else if (key == '.') {
        _scroll += page;
    } else if (key == ',') {
        _scroll = _scroll > page ? _scroll - page : 0;
    } else if (_files.handleKey(key)) {
        load();
    } else {
        return;
    }
    draw();
}

void ObjectViewer::draw() {
    clearPage(_screen);
    const size_t page = size_t((_screen.h - kStatusHeight) / kLineHeight) - 1;
    _scroll = std::min(_scroll, _lines.size() > page ? _lines.size() - page : 0);
    for (size_t i = 0; i < page && _scroll + i < _lines.size(); ++i)
        font::drawText(_screen, 4, 4 + int(i) * kLineHeight, _lines[_scroll + i], {220, 220, 220});
    drawStatus(_screen, _files.position(),
               "lines " + std::to_string(_lines.empty() ? 0 : _scroll + 1) + "-" +
                   std::to_string(std::min(_lines.size(), _scroll + page)) + " of " + std::to_string(_lines.size()));
    _sys.setWindowTitle("Reluva - Star Trek: Away Team - objects (Left/Right, F: next folder, Up/Down: line, ,/.: "
                        "page, Tab: next page)");
}

} // namespace reluva::stawayteam
