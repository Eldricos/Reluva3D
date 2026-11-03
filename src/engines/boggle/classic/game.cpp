// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/game.h"
#include "engines/boggle/classic/layout.h"
#include "engines/boggle/classic/round.h"
#include "engines/boggle/classic/options.h"
#include "engines/boggle/classic/presentation.h"
#include "engines/boggle/classic/user_dictionary.h"
#include "engines/boggle/resources.h"
#include "common/font.h"
#include <algorithm>
#include <cstdio>

namespace reluva::boggle {
namespace {
constexpr int width = 800, height = 600, boardX = 24, boardY = 100, boardSize = 368;
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
           const image::Image& letters, const image::Image& qu, int cellSize) {
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
std::string feedback(Submission result, size_t minimum) {
    switch (result) {
    case Submission::Accepted: return "Counted!";
    case Submission::NotPlaying: return "The round has ended.";
    case Submission::InvalidLetters: return "Enter a word using A-Z.";
    case Submission::TooShort: return "Use at least " + std::to_string(minimum) + " letters.";
    case Submission::UnknownWord: return "Not in dictionary. Review after this round.";
    case Submission::NotOnBoard: return "No matching path on this board.";
    case Submission::Duplicate: return "";
    case Submission::SelectPath: return "Please select this word on the board.";
    }
    return "";
}
void draw(Surface& screen, const ClassicRound& round, uint64_t now,
          const image::Image& letters, const image::Image& qu, const Palette& palette,
          const std::string& input, const std::vector<size_t>& path, const std::string& message, size_t scroll, bool fromMenu,
          const ClassicArtwork& artwork,uint64_t duration) {
    const bool reviewing = round.candidate() != nullptr;
    const bool ready = round.phase() == RoundPhase::Ready, playing = round.phase() == RoundPhase::Playing;
    if(artwork.active()) artwork.draw(screen,round,now,duration,letters,qu,path,scroll);
    else {
    rect(screen, 0, 0, width, height, dark);
    rect(screen, 0, 0, width, 76, {29, 49, 70});
    const auto& accent = palette[1]; rect(screen, 24, 76, 752, 2, {accent.r, accent.g, accent.b});
    const size_t side = round.board().side();
    const int cellSize = boardSize / int(side);
    const bool master = side == 5;
    label(screen, 24, 18, master ? "BOGGLE / CLASSIC MASTER" : "BOGGLE / CLASSIC ORIGINAL", white, 2);
    label(screen, 24, 50, master ? "U.S. English  |  5 x 5  |  4-letter minimum" : "U.S. English  |  4 x 4  |  3-letter minimum", muted);
    const auto seconds = (round.remaining(now) + 999) / 1000;
    char timer[48]; std::snprintf(timer, sizeof(timer), "%llu:%02llu", (unsigned long long)(seconds / 60), (unsigned long long)(seconds % 60));
    if (round.timed()) label(screen, 670, 25, timer, white, 3);
    else label(screen, 652, 32, "NOT TIMED", white, 2);

    for (size_t i = 0; i < round.board().faces().size(); ++i) {
        const int x = boardX + int(i % side) * cellSize, y = boardY + int(i / side) * cellSize;
        const bool selected = std::find(path.begin(), path.end(), i) != path.end();
        rect(screen, x, y, cellSize - 2, cellSize - 2, selected ? Ink{57, 180, 221} : Ink{86, 107, 130});
        rect(screen, x + 3, y + 3, cellSize - 8, cellSize - 8, white);
        if (!ready) glyph(screen, x, y, round.board().faces()[i], letters, qu, cellSize);
        else label(screen, x + (cellSize - 32) / 2, y + (cellSize - 38) / 2, "?", {76, 95, 120}, 3);
    }
    label(screen, 430, 102, ready ? "READY" : playing ? "ROUND IN PROGRESS" : "ROUND COMPLETE", white, 2);
    label(screen, 430, 140, "SCORE  " + std::to_string(round.score()), {110, 220, 200}, 2);
    std::vector<const RoundWord*> words;
    for (const auto& word : round.words()) if (word.state != WordState::Declined) words.push_back(&word);
    label(screen, 430, 179, "WORDS  " + std::to_string(words.size()), muted);
    rect(screen, 430, 204, 346, 260, {24, 42, 62});
    const size_t maximum = words.size() > 14 ? words.size() - 14 : 0;
    const size_t begin = maximum - std::min(scroll, maximum);
    for (size_t i = begin; i < std::min(words.size(), begin + 14); ++i) {
        const auto& entry = words[i]->entry;
        const bool rejected = words[i]->state == WordState::Pending || words[i]->state == WordState::Declined;
        const Ink ink = rejected ? Ink{255, 116, 116} : white;
        std::string shown = entry.word; if (shown.size() > 23) shown = shown.substr(0, 20) + "...";
        label(screen, 442, 217 + int(i - begin) * 17, shown, ink);
        label(screen, 739, 217 + int(i - begin) * 17, words[i]->state == WordState::Declined ? "-" : std::to_string(entry.points), rejected ? ink : Ink{110, 220, 200});
    }
    if (words.size() > 14) label(screen, 430, 470, "PgUp/PgDn: word history", muted);
    }
    rect(screen, 24, 485, artwork.active()?580:752, 43, {35, 57, 81});
    label(screen, 35, 497, input.empty() ? (reviewing ? "Review pending words before the next round." : playing ? "Type or select a word..." : "Press Enter to start or replay") : input.substr(input.size() > 50 ? input.size() - 50 : 0), white, input.size() > 29 ? 1 : 2);
    label(screen, 24, 535, reviewing ? "Choose one of the four dictionary options." : message.empty() ? (ready ? "Press Enter when ready. The timer starts with the board." : playing ? "Enter: submit   Backspace: undo   Right click: clear" : "Enter: replay this board   F2: new board") : message, muted);
    if (!reviewing) { rect(screen, 24, 557, 180, 29, {48, 88, 120}); label(screen, 35, 566, playing ? "END ROUND (F1)" : "START / REPLAY (ENTER)"); }
    if (!playing && !reviewing) { rect(screen, 216, 557, 158, 29, {48, 88, 120}); label(screen, 227, 566, "NEW BOARD (F2)"); }
    label(screen, artwork.active()?450:610, 566, fromMenu ? "MENU (F3)" : "ESC: LAUNCHER", muted);
    if (const auto* word = round.candidate()) {
        rect(screen, 24, 182, 752, 270, {50, 78, 108});
        rect(screen, 27, 185, 746, 264, {22, 40, 60});
        label(screen, 48, 206, "ADD TO YOUR DICTIONARY?", white, 2);
        label(screen, 48, 244, word->entry.word, {255, 116, 116}, word->entry.word.size() > 40 ? 1 : 2);
        label(screen, 48, 282, "Yes keeps this word's points. No removes them.", muted);
        label(screen, 48, 306, "All applies to the remaining words in this round.", muted);
        for (int i = 0; i < 4; ++i) rect(screen, 40 + i * 184, 352, 168, 40, {48, 88, 120});
        label(screen, 54, 368, "YES (1)"); label(screen, 238, 368, "NO (2)");
        label(screen, 422, 368, "YES TO ALL (3)"); label(screen, 606, 368, "NO TO ALL (4)");
        if (!message.empty()) label(screen, 48, 416, message.substr(0, 88), muted);
    }

}
}
Error ClassicGame::run() {
    returnToMenu_ = false;
    ClassicOptions options;
    if (auto err = readClassicOptions(target_.settings, options)) return err;
    Resources resources;
    if (auto err = resources.open(target_.path)) return err;
    Dictionary dictionary; DiceSet dice; Palette palette;
    image::Image letters, qu;
    if (auto err = resources.dictionary("Dicts/English.dic", dictionary)) return err;
    if (auto err = resources.diceSet(options.dice(), dice)) return err;
    if (auto err = resources.palette(options.palette(), palette)) return err;
    if (auto err = resources.image("Textures/Letters.bmp", letters)) return err;
    if (auto err = resources.image("Textures/LettersQu.bmp", qu)) return err;
    if (letters.w != 512 || letters.h != 512 || qu.w != 512 || qu.h != 512 || dice.side != options.side())
        return Error::fail("Unsupported Classic letter atlas or dice layout");
    video::AviVideo1 clip;
    if (options.animations) {
        const char* path = options.side() == 5 ? "Anims/ClsM1All.avi" : "Anims/ClsO1All.avi";
        if (auto err = resources.animation(path,clip)) return err;
        if (clip.frames.size() != 55) return Error::fail("Unsupported Classic round animation length");
    }
    ClassicArtwork artwork;
    if(auto err=artwork.open(resources,target_.path,options,clip))return err;
    ClassicAudio sound(sys_);
    ClassicTransition transition;
    UserDictionary user;
    if (auto err = user.load(target_.savePath, target_.path)) return err;
    ClassicRound round(dictionary, &user); Board board; uint32_t seed = 0x1997;
    if (auto err = Board::generate(dice, seed, board)) return err;
    if (auto err = round.board(std::move(board), options.duration)) return err;
    if (auto err = sys_.initGraphics(width, height, PixelFormat::RGBA32)) return err;
    sys_.setWindowTitle(options.variant == ClassicVariant::Master ? "Reluva - Boggle Classic Master" : "Reluva - Boggle Classic Original");
    // Start output after the window and framebuffer are ready.
    if (auto err = sound.open(resources,options.sound,options.music,uint32_t(sys_.getMillis()))) return err;
    sys_.setMouseCursorVisible(true); sys_.setTextInput(true);
    struct TextInputCleanup { System& system; ~TextInputCleanup() { system.setTextInput(false); } } cleanup{sys_};
    Surface screen; screen.create(width, height, PixelFormat::RGBA32);
    std::string input, message; std::vector<size_t> selected; size_t scroll = 0;
    bool running = true, dragging = false, resultsVisible = false, promptVisible = false;
    auto clear = [&] { input.clear(); selected.clear(); scroll = 0; dragging = false; };
    auto updateInput = [&] { input.clear(); for (size_t i : selected) input += round.board().faces()[i]; };
    auto startOrFinish = [&]() -> Error {
        if (round.phase() == RoundPhase::Results && !resultsVisible) return Error::success();
        resultsVisible = false; clear(); message.clear();
        if (round.phase() == RoundPhase::Playing) {
            round.finish(); sound.stopShake();
            if (options.animations) transition.begin(Transition::Closing,sys_.getMillis());
        } else {
            sound.beginRound(options.animations);
            if (options.animations) transition.begin(Transition::Opening,sys_.getMillis());
            else if (auto err = round.start(sys_.getMillis(), options.duration)) return err;
        }
        return Error::success();
    };
    auto newBoard = [&]() -> Error {
        if (round.phase() == RoundPhase::Results && !resultsVisible) return Error::success();
        if (round.phase() == RoundPhase::Playing) return Error::success();
        if (auto err = Board::generate(dice, ++seed, board)) return err;
        if (auto err = round.board(std::move(board), options.duration)) return err;
        resultsVisible = false; clear(); message.clear(); return Error::success();
    };
    auto select = [&](int x, int y) {
        const size_t side = round.board().side(); const int cellSize = boardSize / int(side);
        if(round.phase()!=RoundPhase::Playing)return;
        size_t cell=0;
        if(artwork.active()) {
            const auto found=artwork.cellAt(x,y,side);if(!found)return;cell=*found;
        } else {
            if(!hit(x,y,boardX,boardY,cellSize*int(side),cellSize*int(side)))return;
            cell=size_t((y-boardY)/cellSize)*side+size_t((x-boardX)/cellSize);
        }
        if (!selected.empty() && cell == selected.back()) return;
        if (std::find(selected.begin(), selected.end(), cell) != selected.end()) { message = "That cube is already used."; return; }
        if (!selected.empty() && !round.board().adjacent(selected.back(), cell)) { message = "Choose a touching cube."; return; }
        if (selected.empty()) input.clear();
        selected.push_back(cell); updateInput(); message.clear(); sound.play(Cue::ClickDown);
    };
    auto advance = [&]() -> Error {
        const uint64_t now = sys_.getMillis();
        const auto type = transition.type();
        if (type == Transition::Opening && transition.frame(now) >= 31) sound.stopShake();
        if (transition.finish(now) && type == Transition::Opening) {
            sound.stopShake();
            if (auto err = round.start(now,options.duration)) return err;
        }
        if (transition.type() != Transition::None) return Error::success();
        const bool playing = round.phase() == RoundPhase::Playing;
        round.tick(now);
        if (playing && round.phase() == RoundPhase::Results) {
            clear(); dragging = false; resultsVisible = false; message = "Time is up.";
            sound.stopShake(); sound.countdown(0);
            if (options.animations) transition.begin(Transition::Closing,now);
        } else if (playing && round.timed()) sound.countdown(round.remaining(now));
        return Error::success();
    };
    if (fromMenu_) { if (auto err = startOrFinish()) return err; }
    while (running) {
        if (auto err = advance()) return err;
        Event event;
        while (sys_.pollEvent(event)) {
            if (auto err = advance()) return err;
            if (event.type == EventType::Quit || event.type == EventType::ReturnToLauncher ||
                (event.type == EventType::KeyDown && event.key == key::Escape && !event.repeat && !fromMenu_)) {
                running = false; break;
            }
            // Escape is inactive in the original game. Provide an explicit
            // native menu action without changing that observed key behavior.
            if (fromMenu_ && ((event.type == EventType::KeyDown && !event.repeat && event.key == key::F1+2) ||
                (transition.type() == Transition::None && event.type == EventType::MouseDown && event.button == MouseButton::Left &&
                 hit(event.mouseX,event.mouseY,artwork.active()?432:610,557,artwork.active()?172:166,29)))) {
                returnToMenu_ = true; running = false; break;
            }
            if (transition.type() != Transition::None) continue;
            if (round.candidate()) {
                int choice = -1;
                if (promptVisible && event.type == EventType::KeyDown && !event.repeat && event.key >= '1' && event.key <= '4')
                    choice = int(event.key - '1');
                if (promptVisible && event.type == EventType::MouseDown && event.button == MouseButton::Left)
                    for (int i = 0; i < 4; ++i) if (hit(event.mouseX, event.mouseY, 40 + i * 184, 352, 168, 40)) choice = i;
                if (choice >= 0) {
                    const DictionaryChoice choices[] = {DictionaryChoice::Yes, DictionaryChoice::No, DictionaryChoice::YesAll, DictionaryChoice::NoAll};
                    if (auto err = round.decide(choices[choice])) message = err.message;
                    else { message.clear(); promptVisible = false; resultsVisible = false; }
                }
                continue;
            }
            if (event.type == EventType::KeyDown) {
                if ((event.key == key::Return || event.key == key::KpEnter) && !event.repeat) {
                    if (round.phase() != RoundPhase::Playing) { if (auto err = startOrFinish()) return err; }
                    else { const auto result = round.submit(input, sys_.getMillis(), selected); message = feedback(result, round.minimum()); if (result == Submission::Accepted || result == Submission::UnknownWord) sound.recorded(round.words().back().entry.word.size()); if (result != Submission::SelectPath) clear(); }
                } else if (event.key == key::PageUp || event.key == key::PageDown) {
                    const size_t count = size_t(std::count_if(round.words().begin(), round.words().end(), [](const auto& word) { return word.state != WordState::Declined; }));
                    const size_t visible=artwork.active()?ClassicArtwork::visibleWords:14;
                    const size_t maximum = count > visible ? count - visible : 0;
                    scroll = event.key == key::PageUp ? std::min(maximum, scroll + 7) : scroll - std::min(scroll, size_t(7));
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
            else if (event.type == EventType::MouseUp && event.button == MouseButton::Left) {
                if (dragging && !selected.empty()) sound.play(Cue::ClickUp);
                dragging = false;
            }
        }
        if (!running) break;
        if (transition.type() != Transition::None) {
            const auto& frame = clip.frames[transition.frame(sys_.getMillis())];
            for (int y=0;y<height;++y) for (int x=0;x<width;++x)
                std::copy_n(frame.pixel(x*frame.w/width,y*frame.h/height),4,screen.row(y)+x*4);
            rect(screen,0,height-40,width,40,dark);
            label(screen,24,height-25,transition.type() == Transition::Opening ? "Rolling dice and opening the lid..." : "Closing the lid...");
        } else draw(screen, round, sys_.getMillis(), letters, qu, palette, input, selected, message, scroll, fromMenu_,artwork,options.duration);
        sys_.updateScreen(screen);
        if (transition.type() == Transition::None && round.phase() == RoundPhase::Results) resultsVisible = true;
        promptVisible = transition.type() == Transition::None && round.candidate() != nullptr;
        sys_.delayMillis(16);
    }
    return Error::success();
}
} // namespace reluva::boggle
