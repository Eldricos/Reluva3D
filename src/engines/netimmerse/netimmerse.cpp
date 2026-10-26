// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// NetImmerse engine, for games built on Numerical Design's NetImmerse,
// starting with LEGO Alpha Team (docs/engines/alphateam.md). Clean-room:
// everything comes from the games' data, observing them run, or study of
// owned executables; never from NetImmerse SDK source. See CONTRIBUTING.md.
//
// For now the engine detects the game and checks its data: it opens every
// .pac archive in the game folder and shows what each holds. Esc goes back
// to the launcher.

#include "engines/engine.h"
#include "engines/netimmerse/pac_archive.h"
#include "common/font.h"
#include "common/fs.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva {

namespace {

using netimmerse::PacArchive;

// Built with `reluva --hash`: each entry needs the size and the MD5 of the
// first kDetectionHashBytes bytes.
//
// Detection keys on the language's archive, which also tells the languages
// apart; the executable, which a patch would replace, is only required to
// exist. See docs/formats/alphateam/install.md.
const std::vector<GameVariant> kVariants = {
    // English Windows retail CD, volume LEGOATEN (October 2026).
    {"legoalphateam", "LEGO Alpha Team", "en", "windows",
     {{"Locale_en.pac", "333f2476ec15acf658af87f09dcbca43", 82323720}, {"LEGOATeam.exe", "", -1}}},
};

// The same game by file names alone, for releases whose hashes aren't in
// kVariants yet (other languages, patches). Used only when nothing there
// matches.
const std::vector<GameVariant> kFallback = {
    {"legoalphateam", "LEGO Alpha Team (unknown release)", "", "windows",
     {{"LEGOATeam.exe", "", -1}, {"Bhvr.pac", "", -1}}},
};

constexpr int kScreenW = 640;
constexpr int kScreenH = 480;

class NetImmerseEngine : public Engine {
public:
    NetImmerseEngine(System& system, GameTarget target) : sys_(system), target_(std::move(target)) {}

    Error run() override {
        if (Error err = sys_.initGraphics(kScreenW, kScreenH, PixelFormat::RGBA32))
            return err;
        sys_.setWindowTitle("Reluva - " + target_.description + " (Esc: back)");
        screen_.create(kScreenW, kScreenH, PixelFormat::RGBA32);
        drawSummary(checkArchives());

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
    struct ArchiveLine {
        std::string name;
        std::string text; // what it holds, or why it couldn't be read
        bool ok = false;
    };

    // Every .pac in the game folder, opened, with its files counted by
    // extension.
    std::vector<ArchiveLine> checkArchives() const {
        std::vector<std::filesystem::path> paths;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(target_.path, ec), end; !ec && it != end; it.increment(ec))
            if (fs::toLower(it->path().extension().string()) == ".pac")
                paths.push_back(it->path());
        std::sort(paths.begin(), paths.end());

        std::vector<ArchiveLine> lines;
        for (const auto& path : paths) {
            ArchiveLine line;
            line.name = path.filename().string();
            PacArchive pac;
            if (Error err = pac.open(path)) {
                line.text = err.message;
                lines.push_back(std::move(line));
                continue;
            }
            std::map<std::string, size_t> byExtension;
            for (const auto& e : pac.entries()) {
                if (e.size == 0)
                    continue; // the placeholders some archives list for themselves
                const size_t dot = e.name.rfind('.');
                ++byExtension[dot == std::string::npos ? std::string("(none)") : e.name.substr(dot + 1)];
            }
            for (const auto& [ext, count] : byExtension)
                line.text += (line.text.empty() ? "" : ", ") + std::to_string(count) + " " + ext;
            line.ok = true;
            lines.push_back(std::move(line));
        }
        return lines;
    }

    void drawSummary(const std::vector<ArchiveLine>& lines) {
        for (size_t i = 3; i < screen_.pixels.size(); i += 4)
            screen_.pixels[i] = 255;
        const Color white{255, 255, 255}, grey{170, 170, 170}, green{120, 220, 120}, red{240, 110, 110};
        const int line = font::kLineHeight * 2;
        int y = 24;
        font::drawText(screen_, 24, y, target_.description, white, 2);
        y += line + 8;
        font::drawText(screen_, 24, y, "Not playable yet: this page checks the game's archives.", grey);
        y += line;
        if (lines.empty())
            font::drawText(screen_, 24, y, "No .pac archives in " + target_.path.string(), red);
        for (const ArchiveLine& l : lines) {
            font::drawText(screen_, 24, y, l.name, l.ok ? green : red);
            // Long error messages wrap onto the lines below.
            const size_t perLine = size_t((kScreenW - 180) / font::kAdvance);
            for (size_t at = 0; at < l.text.size(); at += perLine) {
                font::drawText(screen_, 180, y, l.text.substr(at, perLine), white);
                y += font::kLineHeight + 3;
            }
            y += 6;
        }
        font::drawText(screen_, 24, kScreenH - 32, "Esc: back to the launcher", grey);
    }

    System& sys_;
    GameTarget target_;
    Surface screen_;
};

class NetImmerseMetaEngine : public MetaEngine {
public:
    const char* id() const override { return "netimmerse"; }
    const char* name() const override { return "NetImmerse"; }

    std::vector<DetectedGame> detect(const std::filesystem::path& dir) const override {
        auto found = detectFromTable(id(), kVariants, dir);
        return found.empty() ? detectFromTable(id(), kFallback, dir) : found;
    }

    std::unique_ptr<Engine> create(System& system, const GameTarget& target) const override {
        return std::make_unique<NetImmerseEngine>(system, target);
    }
};

} // namespace

const MetaEngine& netImmerseMetaEngine() {
    static const NetImmerseMetaEngine instance;
    return instance;
}

} // namespace reluva
