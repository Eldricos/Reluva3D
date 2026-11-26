// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Theme Park World: the engine's MetaEngine (its name, the releases it recognises,
// making an Engine to run one) and its Engine. So far it recognises
// SimCoaster, plays its opening and its front end, to a world chosen,
// then shows its loading screen.

#include "audio/mixer.h"
#include "common/settings.h"
#include "engines/engine.h"
#include "engines/tpw/formats/sam.h"
#include "engines/tpw/game_files.h"
#include "engines/tpw/games.h"
#include "engines/tpw/park/park.h"
#include "engines/tpw/players.h"
#include "engines/tpw/ui/briefing.h"
#include "engines/tpw/ui/fly_in.h"
#include "engines/tpw/ui/globe.h"
#include "engines/tpw/ui/loading_screen.h"
#include "engines/tpw/ui/park_screen.h"
#include "engines/tpw/ui/main_menu.h"
#include "engines/tpw/ui/opening.h"
#include "image/image.h"

#include <algorithm>
#include <cstring>

namespace reluva::tpw {

namespace {

/**
 * The game running: its opening, its front end to a world chosen, then
 * that world's park, and others' as the player moves between them.
 */
class TpwEngine : public Engine {
public:
    TpwEngine(System &system, const GameTarget &target) : _sys(system), _target(target) {}

    Error run() override {
        SearchSet files;
        std::vector<std::string> warnings;
        if (Error err = mountGame(files, _target.path, warnings))
            return err;
        if (Error err = _sys.initGraphics(kWidth, kHeight, PixelFormat::kRGBA32))
            return err;
        _sys.setWindowTitle(("Reluva - " + _target.description).c_str());

        audio::Mixer mixer(_sys.audioRate());
        mixer.setMasterVolume(settings::volume(_target.settings, settings::kMasterVolume));
        const bool audioOut = !audio::startMixerOutput(_sys, mixer);
        struct AudioOff { // stops the output before the mixer goes
            System &sys;
            bool on;
            ~AudioOff() {
                if (on)
                    sys.stopAudio();
            }
        } audioOff{_sys, audioOut};
        if (!playOpening(_sys, audioOut ? &mixer : nullptr, _target.path, files))
            return Error::success();
        // After the movie, which plays as music, at its own full level.
        setTypeVolumes(files, mixer);
        PlayerFile player;
        const int world = runFrontEnd(_sys, files, _target.savePath, audioOut ? &mixer : nullptr, &player);
        if (world < 0)
            return Error::success();

        // Each world's park in turn, as the Overview Map moves between them.
        _mixer = audioOut ? &mixer : nullptr;
        for (int next = world; next >= 0;)
            if (Error err = playWorld(files, next, player))
                return err;
        _mixer = nullptr;
        return Error::success();
    }

private:
    /**
     * The loading screen while a world's park loads (by Globe::worlds()'
     * order), then the park, set up as the player's missions done leave
     * it, with their objective if it's in this world; what it did kept in
     * the player's file.
     * @param world In, the world; out, the one the Overview Map chose
     *     next, −1 when the player quits or closes the window.
     */
    Error playWorld(const SearchSet &files, int &world, PlayerFile &player) {
        Surface screen;
        screen.create(kWidth, kHeight, PixelFormat::kRGBA32);
        image::Image loading;
        if (!composeLoadingScreen(files, languageOf(files), loading))
            for (int y = 0; y < std::min(loading.h, kHeight); ++y)
                std::memcpy(screen.row(y), loading.pixel(0, y), size_t(std::min(loading.w, kWidth)) * 4);
        _sys.updateScreen(screen);
        Park park;
        ParkScreen parkScreen;
        const std::string folder = Globe::worlds()[size_t(world)].folder;
        if (Error err = park.load(files, folder.substr(0, folder.size() - 1), std::max(0, player.objective)))
            return err;
        if (Error err = parkScreen.load(files, park))
            return err;
        // The player's shares are the company's they hold, as their file keeps them.
        parkScreen.setPlayer(player.name, player.rank);
        parkScreen.advisor().setMixer(_mixer);
        for (const PlayerFile::World &w : player.worlds)
            for (int theme = 0; theme < 3; ++theme)
                if (Park::themeName(theme) == w.theme)
                    parkScreen.setWorld(theme, w.open, w.challenges);
        if (player.sharesIssued > 0)
            park.setShares({player.sharesIssued, player.sharesOwned, player.sharePrice});
        park.setPlayer(player.goldenTickets, player.rank);
        park.setChallengeRecord(player.challengesWon, player.challengesFailed);
        park.setRidesActivated(player.ridesActivated);
        // The fly-in along the road to the gates, then the mission's
        // briefing over the park as it opens.
        FlyIn flyIn;
        if (!flyIn.load(files, folder.substr(0, folder.size() - 1)) && !playFlyIn(_sys, flyIn, parkScreen, park)) {
            world = -1;
            return Error::success();
        }
        Briefing briefing;
        soft3d::Rasterizer rasterizer;
        const ParkScreen::Camera start = parkScreen.startCamera(park);
        if (!briefing.load(files) && !showBriefing(_sys, briefing, park, player.name,
                                                   [&](Surface &s) { parkScreen.draw(s, rasterizer, park, start); })) {
            world = -1;
            return Error::success();
        }
        const bool left =
            playPark(_sys, parkScreen, park, [&](const std::function<void(Surface &)> &drawPark, bool challenges) {
                return showBriefing(_sys, briefing, park, player.name, drawPark, challenges);
            });
        world = -1;
        if (left && parkScreen.travel() >= 0)
            for (size_t i = 0; i < Globe::worlds().size(); ++i)
                if (Globe::worlds()[i].theme == Park::themeName(parkScreen.travel()))
                    world = int(i);
        // What the park did stays with the player, in their file too.
        keep(player, park);
        if (_target.savePath.empty() || !player.exists())
            return Error::success();
        PlayerSlots players = readPlayers(_target.savePath, {});
        for (PlayerFile &p : players)
            if (p.exists() && p.name == player.name)
                keep(p, park);
        return writePlayers(_target.savePath, players);
    }

    // The share price, golden tickets, rank and challenges as the park
    // leaves them; the special rides activated, the objective and the
    // worlds opened.
    static void keep(PlayerFile &p, const Park &park) {
        p.sharesIssued = park.shares().issued;
        p.sharesOwned = park.shares().owned;
        p.sharePrice = park.shares().price;
        p.goldenTickets = park.goldenTickets();
        p.rank = park.rank();
        p.challengesWon = park.challengesWon();
        p.challengesFailed = park.challengesFailed();
        // The special rides activated in this world, with the others' kept.
        for (int id : park.ridesActivated())
            if (std::find(p.ridesActivated.begin(), p.ridesActivated.end(), id) == p.ridesActivated.end())
                p.ridesActivated.push_back(id);
        // Their objective now, where it's played, and the worlds opened.
        if (park.progress() < int(park.missions().size())) {
            p.objective = park.progress();
            p.objectiveTheme = Park::themeName(park.missions()[size_t(park.progress())].theme);
        }
        for (const int theme : park.themesOpened())
            for (PlayerFile::World &w : p.worlds)
                if (w.theme == Park::themeName(theme))
                    w.open = true;
        // Each world's challenges won, as the Personnel File counts them.
        for (int theme = 0; theme < 3; ++theme)
            for (PlayerFile::World &w : p.worlds)
                if (w.theme == Park::themeName(theme))
                    w.won = park.challengesWonIn(theme);
    }

    // The game's own levels for its sounds, music and speech, percent, as
    // its settings give them (sound.sam's DefaultVolume); full without them.
    static void setTypeVolumes(const SearchSet &files, audio::Mixer &mixer) {
        std::vector<uint8_t> data;
        SamFile sound;
        if (files.read("sound.sam", data) || sound.parse(data))
            return;
        for (auto [type, name] : {std::pair{audio::SoundType::kEffects, "DefaultVolume.SFX"},
                                  std::pair{audio::SoundType::kMusic, "DefaultVolume.MUSIC"},
                                  std::pair{audio::SoundType::kSpeech, "DefaultVolume.SPEECH"}})
            mixer.setTypeVolume(type, int(sound.number(name, 100) * audio::kMaxVolume / 100));
    }

    static constexpr int kWidth = kOpeningWidth;
    static constexpr int kHeight = kOpeningHeight;
    System &_sys;
    GameTarget _target;
    audio::Mixer *_mixer = nullptr; // while the parks play, if the sound's on
};

/**
 * The engine as the launcher sees it: its name, the releases it
 * recognises, and making a TpwEngine to run one.
 */
class TpwMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "tpw"; }
    const char *name() const override { return "Theme Park World"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override {
        auto found = detectFromTable(id(), detectionTable(), dir);
        return found.empty() ? detectFromTable(id(), fallbackTable(), dir) : found;
    }

    std::vector<ImportPlan> planImport(const std::filesystem::path &source) const override {
        return tpw::planImport(source);
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<TpwEngine>(system, target);
    }
};

} // namespace

} // namespace reluva::tpw

namespace reluva {

const MetaEngine &tpwMetaEngine() {
    static const tpw::TpwMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
