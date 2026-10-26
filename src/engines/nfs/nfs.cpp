// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Need for Speed engine, starting with Need for Speed III: Hot Pursuit
// (docs/engines/nfs.md).
//
// Everything in this engine must come from the game's data files, observed
// behaviour of the original, public format documentation, or documented
// reverse engineering of a legally owned copy. Never from leaked source
// code. See CONTRIBUTING.md.
//
// For now the engine shows what it found: the install, the CD's files
// (music and movies) and the tracks and cars. Esc goes back to the
// launcher.

#include "engines/engine.h"
#include "engines/nfs/folders.h"
#include "engines/nfs/games.h"

#include "common/font.h"

#include <string>
#include <vector>

namespace reluva {

namespace {

using nfs::GameFolders;
using nfs::GameInfo;

constexpr int kScreenW = 640;
constexpr int kScreenH = 480;
constexpr int kMargin = 16;
constexpr Color kBackground{16, 20, 32};
constexpr Color kTitleColor{255, 200, 64};
constexpr Color kLabelColor{150, 160, 180};
constexpr Color kTextColor{230, 230, 230};
constexpr Color kWarnColor{255, 120, 100};

class NfsEngine : public Engine {
public:
    NfsEngine(System& system, GameTarget target, const GameInfo& game)
        : sys_(system), target_(std::move(target)), game_(game) {}

    Error run() override {
        const GameFolders folders = nfs::findFolders(target_.path, target_.settings, game_.executable);
        const std::vector<std::string> tracks = nfs::trackFolders(folders.install);
        if (tracks.empty())
            return Error::fail("No tracks (GameData/Tracks/trkNNN) found in " + folders.install.string());

        if (Error err = sys_.initGraphics(kScreenW, kScreenH, PixelFormat::RGBA32))
            return err;
        sys_.setWindowTitle(std::string("Reluva - ") + game_.name);
        screen_.create(kScreenW, kScreenH, PixelFormat::RGBA32);
        drawInfo(folders, tracks, nfs::carFolders(folders.install));

        while (true) {
            Event ev;
            while (sys_.pollEvent(ev)) {
                if (ev.type == EventType::Quit || ev.type == EventType::ReturnToLauncher)
                    return Error::success();
                if (ev.type == EventType::KeyDown && ev.key == key::Escape)
                    return Error::success();
            }
            sys_.updateScreen(screen_);
            sys_.delayMillis(10);
        }
    }

private:
    void drawInfo(const GameFolders& folders, const std::vector<std::string>& tracks,
                  const std::vector<std::string>& cars) {
        for (int y = 0; y < screen_.h; ++y) {
            uint8_t* p = screen_.row(y);
            for (int x = 0; x < screen_.w; ++x, p += 4) {
                p[0] = kBackground.r;
                p[1] = kBackground.g;
                p[2] = kBackground.b;
                p[3] = 255;
            }
        }

        int y = kMargin;
        font::drawText(screen_, kMargin, y, game_.name, kTitleColor, 2);
        y += font::kLineHeight * 2 + 8;
        line(y, "Install", folders.install.string(), kTextColor);

        const std::vector<std::string> music = nfs::filesWithExtension(folders.cd, "GameData/Audio/pc", ".mus");
        const std::vector<std::string> movies = nfs::filesWithExtension(folders.cd, "FeData/movies", ".mad");
        if (folders.cd.empty()) {
            line(y, "CD files", "not found: no music or movies", kWarnColor);
            line(y, "", "Set cdpath=<folder> in this game's section of reluva.ini,", kLabelColor);
            line(y, "", "or put a copy of the CD beside the install.", kLabelColor);
        } else {
            line(y, "CD files", folders.cd.string(), kTextColor);
            line(y, "", "(" + folders.cdSource + ")", kLabelColor);
            line(y, "Music", std::to_string(music.size()) + " streams", kTextColor);
            line(y, "Movies", std::to_string(movies.size()) + " files", kTextColor);
        }
        y += font::kLineHeight;

        line(y, "Tracks", std::to_string(tracks.size()) + ": " + join(tracks), kTextColor);
        line(y, "Cars", std::to_string(cars.size()) + ": " + join(cars), kTextColor);
        y += font::kLineHeight;

        line(y, "", "The engine reads no game data yet (docs/engines/nfs.md).", kLabelColor);
        line(y, "", "Esc: back to the launcher", kLabelColor);
    }

    // A label and its value, the value wrapped to the screen's width.
    void line(int& y, const std::string& label, const std::string& value, Color color) {
        constexpr int kValueX = kMargin + 10 * font::kAdvance;
        constexpr size_t kWidth = (kScreenW - kValueX - kMargin) / font::kAdvance;
        font::drawText(screen_, kMargin, y, label, kLabelColor);
        for (size_t i = 0; i < value.size() || i == 0; i += kWidth) {
            font::drawText(screen_, kValueX, y, value.substr(i, kWidth), color);
            y += font::kLineHeight;
        }
    }

    static std::string join(const std::vector<std::string>& names) {
        std::string out;
        for (const std::string& n : names)
            out += (out.empty() ? "" : " ") + n;
        return out;
    }

    System& sys_;
    GameTarget target_;
    const GameInfo& game_;
    Surface screen_;
};

class NfsMetaEngine : public MetaEngine {
public:
    const char* id() const override { return "nfs"; }
    const char* name() const override { return "Need for Speed"; }

    std::vector<DetectedGame> detect(const std::filesystem::path& dir) const override {
        auto found = detectFromTable(id(), nfs::detectionTable(), dir);
        return found.empty() ? detectFromTable(id(), nfs::fallbackTable(), dir) : found;
    }

    std::unique_ptr<Engine> create(System& system, const GameTarget& target) const override {
        const GameInfo* game = nfs::findGame(target.gameId);
        if (!game)
            game = &nfs::games().front();
        return std::make_unique<NfsEngine>(system, target, *game);
    }
};

} // namespace

const MetaEngine& nfsMetaEngine() {
    static const NfsMetaEngine instance;
    return instance;
}

} // namespace reluva
