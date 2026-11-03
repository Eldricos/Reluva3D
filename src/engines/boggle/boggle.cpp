// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/engine.h"
#include "engines/boggle/frontend/game.h"
#include "engines/boggle/classic/options.h"
#include "engines/boggle/resources.h"
#include "engines/boggle/import.h"
#include "common/fs.h"

namespace reluva::boggle {
namespace {
// Measured from the owner's install and matching expanded CD directory;
// docs/plans/boggle-1997/inspection.md and manifests record provenance.
// This multilingual copy's exact pressing/patch label is not established.
const std::vector<GameVariant> kVariants = {
    {"boggle1997", "Boggle (1997, multilingual)", "multilingual", "windows",
     {{"Boggle.exe", "72f212ea3c87e3c47d480164b8a0f981", 656384},
      {"Language/English.dll", "b5d3759c5103ff9c2ddeb8debc3dce2a", 124416},
      {"Dicts/English.dic", "fd7191cd64475101ad717be88ef9985a", 718084},
      {"Dicts/EngOrg.dst", "", 209}, {"Palettes/Title.pal", "", 2716},
      {"Scripts/Die.x", "", 4496}, {"Anims/INTRO.AVI", "", 4311730},
      {"Fonts/TARA.TTF", "", 54348}, {"Waves/Shake.wav", "", -1},
      {"Bitmaps/ClsM1CS.bmp", "", 61078}, {"Textures/Letters.bmp", "", -1}}},
};
class BoggleMetaEngine : public MetaEngine {
public:
    const char* id() const override { return "boggle"; }
    const char* name() const override { return "Boggle (1997)"; }
    std::vector<DetectedGame> detect(const std::filesystem::path& dir) const override {
        return detectFromTable(id(), kVariants, dir);
    }
    std::vector<ImportPlan> planImport(const std::filesystem::path& source) const override {
        std::vector<std::filesystem::path> roots{source};
        if (auto expanded = fs::findDirectory(source,"Boggle")) roots.push_back(*expanded);
        for (const auto& root : roots) {
            const auto found = detect(root);
            if (found.empty()) continue;
            ImportPlan plan;
            if (!classicImportPlan(root,found.front(),kVariants.front().files,plan)) return {std::move(plan)};
        }
        return {};
    }
    std::vector<GameOption> options(const std::string& gameId, const std::filesystem::path& dir) const override {
        if (gameId != "boggle1997") return {};
        std::vector<std::pair<std::string,std::string>> music = {{"none","No music"},{"random","Random track"}};
        Resources resources; Language language;
        if (!resources.open(dir)) (void)resources.language("Language/English.dll",language);
        for (size_t i=0;i<classicMusicFiles.size();++i) {
            std::string title = "Track " + std::to_string(i+1);
            for (const auto& text : language.strings) if (text.id == 0x32c9+i) { title = text.text; break; }
            music.emplace_back(std::to_string(i),std::move(title));
        }
        return {
            {"frontend_movies", "Intro and menu animations", "Play the installed intro with its soundtrack and animate available mode buttons. Escape skips the intro.", true, {}},
            {"classic_menu", "Game selection menu", "Choose Classic and its board before play; disable to open the existing round screen directly.", true, {}},
            {"classic_music", "Classic music", "Loop an installed WAV track independently of sound effects. Random chooses once per launch.", true, std::move(music)},
            {"classic_sound", "Classic sound effects", "Play the installed game's WAV sound effects.", true, {}},
            {"classic_animations", "Classic round animations", "Play the installed Classic lid/roll clips before and after each round.", true, {}},
            {"classic_variant", "Classic variant", "Original: 4 x 4, minimum 3 letters. Master: 5 x 5, minimum 4 letters.", true,
             {{"original", "Original"}, {"master", "Master"}}},
            {"classic_seconds", "Round duration", "Used for each Classic round; Not Timed ends with End Round (F1).", true,
             {{"180", "3 minutes (default)"}, {"60", "1 minute"}, {"90", "1 minute 30 seconds"},
              {"120", "2 minutes"}, {"150", "2 minutes 30 seconds"}, {"210", "3 minutes 30 seconds"},
              {"240", "4 minutes"}, {"270", "4 minutes 30 seconds"}, {"300", "5 minutes"}, {"0", "Not Timed"}}},
        };
    }
    std::unique_ptr<Engine> create(System& system, const GameTarget& target) const override {
        return std::make_unique<FrontendGame>(system, target);
    }
};
} // namespace
} // namespace reluva::boggle
namespace reluva {
const MetaEngine& boggleMetaEngine() {
    static const boggle::BoggleMetaEngine instance;
    return instance;
}
} // namespace reluva
