// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/game.h"
#include "engines/boggle/classic/round.h"
#include "engines/boggle/resources.h"
#include "common/font.h"
#include <algorithm>
#include <cstdio>

namespace reluva::boggle {
namespace {
constexpr int width = 800, height = 600, boardX = 24, boardY = 100, cellSize = 92;
struct Ink { uint8_t r, g, b; };
constexpr Ink white{246, 249, 255}, muted{164, 184, 205}, dark{18, 32, 48};
void rect(Surface& screen, int x, int y, int w, int h, Ink color) {
    for (int row = std::max(0, y); row < std::min(screen.h, y + h); ++row)
        for (int col = std::max(0, x); col < std::min(screen.w, x + w); ++col) {
            auto* p = screen.row(row) + col * 4;
            p[0] = color.r; p[1] = color.g; p[2] = color.b; p[3] = 255;
        }
}
void label(Surface& screen, int x, int y, std::string_view text, Ink ink = white, int scale = 1) {
    font::drawText(screen, x, y, text, {ink.r, ink.g, ink.b}, scale);
}
bool hit(int x, int y, int left, int top, int w, int h) {
    return x >= left && y >= top && x < left + w && y < top + h;
}
void glyph(Surface& screen, int x, int y, const std::string& face,
           const image::Image& letters, const image::Image& qu) {
    if (face.size() != 1 && face != "QU") { label(screen, x + 14, y + 25, face, {0, 0, 220}, 2); return; }
    const unsigned index = unsigned(face[0] - 'A');
    const auto& atlas = face == "QU" ? qu : letters;
    // The observed atlas has 6x6 cells, five alphabet entries per row.
    const int sx = int(index % 5) * 512 / 6, sy = int(index / 5) * 512 / 6;
    const int sw = int(index % 5 + 1) * 512 / 6 - sx, sh = int(index / 5 + 1) * 512 / 6 - sy;
    for (int dy = 0; dy < cellSize - 12; ++dy)
        for (int dx = 0; dx < cellSize - 12; ++dx) {
            const auto* p = atlas.pixel(sx + dx * sw / (cellSize - 12), sy + dy * sh / (cellSize - 12));
            auto* out = screen.row(y + 6 + dy) + (x + 6 + dx) * 4;
            std::copy_n(p, 4, out);
        }
}
const char* feedback(Submission result) {
    switch (result) {
    case Submission::Accepted: return "Counted!";
    case Submission::NotPlaying: return "The round has ended.";
    case Submission::InvalidLetters: return "Enter a word using A-Z.";
    case Submission::TooShort: return "Use at least 3 letters.";
    case Submission::UnknownWord: return "Not in this dictionary.";
    case Submission::NotOnBoard: return "No matching path on this board.";
    case Submission::Duplicate: return "Already counted.";
    case Submission::SelectPath: return "Please select this word on the board.";
    }
    return "";
}
void draw(Surface& screen, const ClassicRound& round, uint64_t now,
          const image::Image& letters, const image::Image& qu, const Palette& palette,
          const std::string& input, const std::vector<size_t>& path, const std::string& message) {
    rect(screen, 0, 0, width, height, dark);
    rect(screen, 0, 0, width, 76, {29, 49, 70});
    const auto& accent = palette[1]; rect(screen, 24, 76, 752, 2, {accent.r, accent.g, accent.b});
    label(screen, 24, 18, "BOGGLE / CLASSIC ORIGINAL", white, 2);
    label(screen, 24, 50, "U.S. English  |  4 x 4  |  3-letter minimum", muted);
    const auto seconds = (round.remaining(now) + 999) / 1000;
    char timer[48]; std::snprintf(timer, sizeof(timer), "%llu:%02llu", (unsigned long long)(seconds / 60), (unsigned long long)(seconds % 60));
    label(screen, 670, 25, timer, white, 3);
    const bool ready = round.phase() == RoundPhase::Ready, playing = round.phase() == RoundPhase::Playing;
    for (size_t i = 0; i < round.board().faces().size(); ++i) {
        const int x = boardX + int(i % 4) * cellSize, y = boardY + int(i / 4) * cellSize;
        const bool selected = std::find(path.begin(), path.end(), i) != path.end();
        rect(screen, x, y, cellSize - 2, cellSize - 2, selected ? Ink{57, 180, 221} : Ink{86, 107, 130});
        rect(screen, x + 3, y + 3, cellSize - 8, cellSize - 8, white);
        if (!ready) glyph(screen, x, y, round.board().faces()[i], letters, qu);
        else label(screen, x + 30, y + 27, "?", {76, 95, 120}, 3);
    }
    label(screen, 430, 102, ready ? "READY" : playing ? "ROUND IN PROGRESS" : "ROUND COMPLETE", white, 2);
    label(screen, 430, 140, "SCORE  " + std::to_string(round.score()), {110, 220, 200}, 2);
    label(screen, 430, 179, "WORDS  " + std::to_string(round.accepted().size()), muted);
    rect(screen, 430, 204, 346, 260, {24, 42, 62});
    const auto& words = round.accepted(); const size_t begin = words.size() > 14 ? words.size() - 14 : 0;
    for (size_t i = begin; i < words.size(); ++i) {
        std::string shown = words[i].word; if (shown.size() > 23) shown = shown.substr(0, 20) + "...";
        label(screen, 442, 217 + int(i - begin) * 17, shown);
        label(screen, 739, 217 + int(i - begin) * 17, std::to_string(words[i].points), {110, 220, 200});
    }
    rect(screen, 24, 485, 752, 43, {35, 57, 81});
    label(screen, 35, 497, input.empty() ? (playing ? "Type or select a word..." : "Press Enter to start or replay") : input.substr(input.size() > 50 ? input.size() - 50 : 0), white, input.size() > 29 ? 1 : 2);
    label(screen, 24, 535, message.empty() ? (ready ? "Press Enter when ready. The timer starts with the board." : playing ? "Enter: submit   Backspace: undo   Right click: clear" : "Enter: replay this board   F2: new board") : message, muted);
    rect(screen, 24, 557, 180, 29, {48, 88, 120}); label(screen, 35, 566, playing ? "END ROUND (F1)" : "START / REPLAY (ENTER)");
    if (!playing) { rect(screen, 216, 557, 158, 29, {48, 88, 120}); label(screen, 227, 566, "NEW BOARD (F2)"); }
    label(screen, 610, 566, "ESC: LAUNCHER", muted);
}
}
Error ClassicGame::run() {
    Resources resources;
    if (auto err = resources.open(target_.path)) return err;
    Dictionary dictionary; DiceSet dice; Palette palette;
    image::Image letters, qu;
    if (auto err = resources.dictionary("Dicts/English.dic", dictionary)) return err;
    if (auto err = resources.diceSet("Dicts/EngOrg.dst", dice)) return err;
    if (auto err = resources.palette("Palettes/ClsO1Pal.pal", palette)) return err;
    if (auto err = resources.image("Textures/Letters.bmp", letters)) return err;
    if (auto err = resources.image("Textures/LettersQu.bmp", qu)) return err;
    if (letters.w != 512 || letters.h != 512 || qu.w != 512 || qu.h != 512 || dice.side != 4)
        return Error::fail("Unsupported Classic Original letter atlas or dice layout");
    ClassicRound round(dictionary); Board board; uint32_t seed = 0x1997;
    if (auto err = Board::generate(dice, seed, board)) return err;
    if (auto err = round.board(std::move(board))) return err;
    if (auto err = sys_.initGraphics(width, height, PixelFormat::RGBA32)) return err;
    sys_.setWindowTitle("Reluva - Boggle Classic Original");
    sys_.setMouseCursorVisible(true); sys_.setTextInput(true);
    struct TextInputCleanup { System& system; ~TextInputCleanup() { system.setTextInput(false); } } cleanup{sys_};
    Surface screen; screen.create(width, height, PixelFormat::RGBA32);
    std::string input, message; std::vector<size_t> selected;
    bool running = true, dragging = false, resultsVisible = false;
    auto clear = [&] { input.clear(); selected.clear(); };
    auto updateInput = [&] { input.clear(); for (size_t i : selected) input += round.board().faces()[i]; };
    auto startOrFinish = [&]() -> Error {
        if (round.phase() == RoundPhase::Results && !resultsVisible) return Error::success();
        resultsVisible = false; clear(); message.clear();
        if (round.phase() == RoundPhase::Playing) round.finish();
        else if (auto err = round.start(sys_.getMillis())) return err;
        return Error::success();
    };
    auto newBoard = [&]() -> Error {
        if (round.phase() == RoundPhase::Playing) return Error::success();
        if (auto err = Board::generate(dice, ++seed, board)) return err;
        if (auto err = round.board(std::move(board))) return err;
        resultsVisible = false; clear(); message.clear(); return Error::success();
    };
    auto select = [&](int x, int y) {
        if (round.phase() != RoundPhase::Playing || !hit(x, y, boardX, boardY, cellSize * 4, cellSize * 4)) return;
        const size_t cell = size_t((y - boardY) / cellSize) * 4 + size_t((x - boardX) / cellSize);
        if (!selected.empty() && cell == selected.back()) return;
        if (std::find(selected.begin(), selected.end(), cell) != selected.end()) { message = "That cube is already used."; return; }
        if (!selected.empty() && !round.board().adjacent(selected.back(), cell)) { message = "Choose a touching cube."; return; }
        if (selected.empty()) input.clear();
        selected.push_back(cell); updateInput(); message.clear();
    };
    auto advance = [&] {
        const bool playing = round.phase() == RoundPhase::Playing;
        round.tick(sys_.getMillis());
        if (playing && round.phase() == RoundPhase::Results) {
            clear(); dragging = false; resultsVisible = false; message = "Time is up.";
        }
    };
    while (running) {
        advance();
        Event event;
        while (sys_.pollEvent(event)) {
            advance();
            if (event.type == EventType::Quit || event.type == EventType::ReturnToLauncher ||
                (event.type == EventType::KeyDown && event.key == key::Escape)) { running = false; break; }
            if (event.type == EventType::KeyDown) {
                if ((event.key == key::Return || event.key == key::KpEnter) && !event.repeat) {
                    if (round.phase() != RoundPhase::Playing) { if (auto err = startOrFinish()) return err; }
                    else { const auto result = round.submit(input, sys_.getMillis(), selected); message = feedback(result); if (result != Submission::SelectPath) clear(); }
                } else if (event.key == key::Backspace && round.phase() == RoundPhase::Playing) {
                    if (!selected.empty()) { selected.pop_back(); updateInput(); }
                    else if (!input.empty()) input.pop_back();
                    message.clear();
                } else if (event.key == key::F1 && !event.repeat && round.phase() == RoundPhase::Playing) {
                    if (auto err = startOrFinish()) return err;
                } else if (event.key == key::F1 + 1 && !event.repeat) { if (auto err = newBoard()) return err; }
            } else if (event.type == EventType::TextInput && round.phase() == RoundPhase::Playing) {
                const auto end = std::find(std::begin(event.text), std::end(event.text), '\0');
                std::string normalized;
                if (!englishWord(std::string_view(event.text, size_t(end - event.text)), normalized)) { message = "Use A-Z for this English round."; continue; }
                if (input.size() + normalized.size() > 64) { message = "Word is too long."; continue; }
                selected.clear(); input += normalized; message.clear();
            } else if (event.type == EventType::MouseDown && event.button == MouseButton::Right) {
                clear(); message.clear(); dragging = false;
            } else if (event.type == EventType::MouseDown && event.button == MouseButton::Left) {
                if (hit(event.mouseX, event.mouseY, 24, 557, 180, 29)) { if (auto err = startOrFinish()) return err; }
                else if (hit(event.mouseX, event.mouseY, 216, 557, 158, 29)) { if (auto err = newBoard()) return err; }
                else { dragging = true; select(event.mouseX, event.mouseY); }
            } else if (event.type == EventType::MouseMove && dragging) select(event.mouseX, event.mouseY);
            else if (event.type == EventType::MouseUp && event.button == MouseButton::Left) dragging = false;
        }
        if (!running) break;
        draw(screen, round, sys_.getMillis(), letters, qu, palette, input, selected, message);
        sys_.updateScreen(screen);
        if (round.phase() == RoundPhase::Results) resultsVisible = true;
        sys_.delayMillis(16);
    }
    return Error::success();
}
} // namespace reluva::boggle
