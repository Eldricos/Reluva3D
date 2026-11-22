// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/archive.h"
#include "engines/legoloco/pages/viewers.h"
#include "graphics/font.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>

namespace reluva::legoloco {

namespace {

constexpr int kScrollStep = 4 * kSquare;

bool readFile(const std::filesystem::path &path, std::vector<uint8_t> &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

} // namespace

BoardPage::BoardPage(System &sys, Surface &screen, const GameFiles &files) : _sys(sys), _screen(screen), _files(files) {
    for (const char *folder : {"SaveGame", "ScrSaver"}) {
        const auto dir = files.findDirectory(folder);
        if (!dir)
            continue;
        std::vector<std::filesystem::path> found;
        for (const auto &e : std::filesystem::directory_iterator(*dir))
            if (normalizeArchiveName(e.path().filename().string()).ends_with(".sav"))
                found.push_back(e.path());
        std::sort(found.begin(), found.end());
        for (const auto &path : found)
            _boards.push_back({std::string(folder) + "\\" + path.filename().string(), -1, path});
    }
    if (const auto current = files.findFile("~curr"))
        _boards.push_back({"~curr", -1, *current});
    const auto &entries = files.resources().entries();
    for (size_t i = 0; i < entries.size(); ++i)
        if (normalizeArchiveName(entries[i].name).ends_with(".sav"))
            _boards.push_back({entries[i].name, int(i), {}});
}

const Picture *BoardPage::piecePicture(uint16_t resource) {
    auto it = _pictures.find(resource);
    if (it == _pictures.end()) {
        std::unique_ptr<Picture> picture;
        const int entry = _files.findResource(resource, "bmp");
        if (entry >= 0) {
            picture = std::make_unique<Picture>();
            if (loadPicture(_files, size_t(entry), *picture))
                picture.reset();
        }
        it = _pictures.emplace(resource, std::move(picture)).first;
    }
    return it->second.get();
}

void BoardPage::load() {
    _error.clear();
    _board = Board();
    _backdrop.reset();
    _scrollX = 0;
    _scrollY = 0;
    if (_boards.empty())
        return;
    const Source &source = _boards[_current];
    std::vector<uint8_t> data;
    if (source.entry >= 0) {
        if (Error err = _files.resources().readEntry(size_t(source.entry), data))
            _error = err.message;
    } else if (!readFile(source.path, data)) {
        _error = "can't read " + source.path.string();
    }
    if (_error.empty())
        if (Error err = parseBoard(data, _board))
            _error = err.message;
    // The backdrop is a picture on disk; a board naming none gets the
    // default one, backdrop.bmp, by guess.
    const std::string backdrop = _board.backdrop.empty() ? "backdrop" : _board.backdrop;
    if (const auto path = _files.findFile("backdrop/" + backdrop + ".bmp"); path && readFile(*path, data)) {
        _backdrop = std::make_unique<image::Bitmap>();
        if (image::decodeBmp(data, *_backdrop))
            _backdrop.reset();
    }
    draw();
}

void BoardPage::handleKey(uint32_t key) {
    const size_t n = _boards.size();
    const size_t old = _current;
    const int maxX = std::max(0, _board.columns * kSquare - kScreenW);
    const int maxY = std::max(0, _board.rows * kSquare - kScreenH + 40);
    if (key == key::kRight)
        _current = (_current + 1) % n;
    else if (key == key::kLeft)
        _current = (_current + n - 1) % n;
    else if (key == key::kHome)
        _current = 0;
    else if (key == key::kEnd)
        _current = n - 1;
    else if (key == key::kDown)
        _scrollY = std::min(maxY, _scrollY + kScrollStep);
    else if (key == key::kUp)
        _scrollY = std::max(0, _scrollY - kScrollStep);
    else if (key == ']')
        _scrollX = std::min(maxX, _scrollX + kScrollStep);
    else if (key == '[')
        _scrollX = std::max(0, _scrollX - kScrollStep);
    else if (key == 's')
        _squares = !_squares;
    else if (key == 'p')
        _pieces = !_pieces;
    else if (key == 'b')
        _showBackdrop = !_showBackdrop;
    else
        return;
    if (_current != old)
        load();
    else
        draw();
}

void BoardPage::draw() {
    fillRect(_screen, 0, 0, _screen.w, _screen.h, {60, 110, 60});
    if (_boards.empty())
        return;
    const std::string position = "[" + std::to_string(_current + 1) + "/" + std::to_string(_boards.size()) + "] ";
    if (!_error.empty()) {
        drawStatus(_screen, position + _boards[_current].name, _error);
        return;
    }
    if (_showBackdrop && _backdrop && _backdrop->surface.w > 0 && _backdrop->surface.h > 0) {
        Picture tile;
        tile.bitmap = *_backdrop;
        tile.frameW = tile.bitmap.surface.w;
        for (int y = -(_scrollY % tile.bitmap.surface.h); y < kScreenH; y += tile.bitmap.surface.h)
            for (int x = -(_scrollX % tile.bitmap.surface.w); x < kScreenW; x += tile.bitmap.surface.w)
                drawFrame(_screen, tile, 0, x, y, 1, false);
    }
    // Pieces nearer the bottom are drawn over those above them; each
    // picture's foot is on the bottom of the squares it fills.
    _missing = 0;
    if (_pieces) {
        std::vector<std::pair<int, const BoardPiece *>> order;
        for (const auto &piece : _board.pieces) {
            const Picture *picture = piecePicture(piece.resource);
            order.push_back({piece.row + (picture ? picture->footprintRows() : 1), &piece});
        }
        std::stable_sort(order.begin(), order.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        for (const auto &[foot, piece] : order) {
            const Picture *picture = piecePicture(piece->resource);
            const int x = piece->column * kSquare - _scrollX;
            if (!picture) {
                ++_missing;
                fillRect(_screen, x, piece->row * kSquare - _scrollY, kSquare, kSquare, {255, 0, 255});
                continue;
            }
            drawFrame(_screen, *picture, 0, x, foot * kSquare - picture->bitmap.surface.h - _scrollY, 1, true);
        }
    }
    if (_squares) {
        static const Color kColors[] = {{0, 0, 0},     {255, 255, 255}, {255, 200, 0},  {0, 200, 255},
                                        {255, 80, 80}, {80, 255, 80},   {200, 80, 255}, {255, 140, 0}};
        for (int row = 0; row < _board.rows; ++row)
            for (int column = 0; column < _board.columns; ++column)
                if (const uint8_t v = _board.square(column, row))
                    fillRect(_screen, column * kSquare - _scrollX + 5, row * kSquare - _scrollY + 5, 6, 6,
                             kColors[v % 8]);
    }
    std::string line1 = position + _boards[_current].name + "  " + std::to_string(_board.columns) + "x" +
                        std::to_string(_board.rows) + "  " + std::to_string(_board.pieces.size()) + " pieces";
    if (_board.pieces.size() < _board.declaredPieces)
        line1 += " (of " + std::to_string(_board.declaredPieces) + " counted)";
    line1 += ", " + std::to_string(_board.trains.size()) + " trains, backdrop \"" + _board.backdrop + "\"";
    if (_missing)
        line1 += ", " + std::to_string(_missing) + " without a picture";
    // The squares' kinds as the boards show them, not yet traced in the code.
    const std::string legend = _squares ? "squares: yellow 2 scenery, cyan 3 building, green 5 track, purple 6 road, "
                                          "orange 7 path"
                                        : "";
    drawStatus(_screen, line1, "Left/Right board  Up/Down [/] scroll  S squares  P pieces  B backdrop  Tab page",
               legend);
}

} // namespace reluva::legoloco
