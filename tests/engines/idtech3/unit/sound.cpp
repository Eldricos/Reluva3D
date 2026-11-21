// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// id Tech 3 unit tests: Sounds: the library, the sound system, ambient sound sets,
// dynamic music.

#include "common/fs.h"
#include "engines/idtech3/audio/dynamic_music.h"
#include "engines/idtech3/client/bindings.h"
#include "engines/idtech3/client/joystick_input.h"
#include "engines/idtech3/client/player_input.h"
#include "engines/idtech3/botlib/aas.h"
#include "engines/idtech3/botlib/bot_library.h"
#include "engines/idtech3/botlib/bot_script.h"
#include "engines/idtech3/botlib/character.h"
#include "engines/idtech3/formats/bsp.h"
#include "engines/idtech3/collision.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/eliteforce/cgame_host.h"
#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/eliteforce/game_info.h"
#include "engines/idtech3/eliteforce/net_client.h"
#include "engines/idtech3/raven/savegame.h"
#include "engines/idtech3/server/server_world.h"
#include "engines/idtech3/eliteforce/net_fields.h"
#include "engines/idtech3/eliteforce/net_server.h"
#include "engines/idtech3/eliteforce/sp_game_host.h"
#include "engines/idtech3/eliteforce/ui_host.h"
#include "engines/idtech3/formats/entities.h"
#include "engines/idtech3/filesystem.h"
#include "engines/idtech3/info_string.h"
#include "engines/idtech3/games.h"
#include "engines/idtech3/formats/lexer.h"
#include "engines/idtech3/formats/marks.h"
#include "engines/idtech3/render/light_grid.h"
#include "engines/idtech3/formats/md3.h"
#include "engines/idtech3/render/model_cache.h"
#include "engines/idtech3/wire/message.h"
#include "engines/idtech3/wire/netchan.h"
#include "engines/idtech3/render/patch.h"
#include "engines/idtech3/formats/player_animation.h"
#include "engines/idtech3/pure.h"
#include "engines/idtech3/vm/qvm.h"
#include "engines/idtech3/client/server_browser.h"
#include "backends/networking/sockets/socket_net.h"
#include "engines/idtech3/formats/shader.h"
#include "engines/idtech3/raven/ambient_sets.h"
#include "engines/idtech3/audio/sound_system.h"
#include "engines/idtech3/audio/sounds.h"
#include "engines/idtech3/client/page.h"
#include "audio/wav.h"
#include "common/md4.h"
#include "common/zip.h"
#include "engines/registry.h"
#include "tests/engines/idtech3/data/qvm/fake_cgame_qvm.h"
#include "tests/engines/idtech3/data/qvm/fake_game_qvm.h"
#include "tests/engines/idtech3/data/qvm/fake_ui_qvm.h"
#include "tests/engines/idtech3/data/qvm/test_vm_qvm.h"
#include "engines/idtech3/vm/vm_services.h"
#include "tests/engines/idtech3/fixtures.h"
#include "tests/support/test.h"
#include "tests/support/zip_writer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <memory>
#include <thread>
#include <chrono>
#include <set>
#include <string>

using namespace reluva;
using namespace reluva::idtech3;
namespace q3 = reluva::idtech3;
using namespace reluva::idtech3::fixtures;

// Ambient sound sets: sound/sound.txt read for the sets asked for, then
// the general set's loop and occasional sounds, cross-fading, and local
// and brush model sets.
static void testAmbientSets() {
    const char *text = "amsdir ambience\r\n"
                       "type ambientSet\r\n"
                       "\r\n"
                       "generalSet hum\r\n"
                       "timeBetweenWaves 5 2\r\n"
                       "subWaves borg clank1 clank2\r\n"
                       "loopedWave ambience/borg/hum\r\n"
                       "volRange 100 50\r\n"
                       "; a comment ends a set\r\n"
                       "generalSet quiet\r\n"
                       "loopedWave ambience/quiet\r\n"
                       "generalSet unused\r\n"
                       "loopedWave ambience/unused\r\n"
                       "localSet drip\r\n"
                       "radius 200\r\n"
                       "subWaves water drip1\r\n"
                       "bogus 1\r\n"
                       "bmodelSet door\r\n"
                       "subWaves doors start move stop\r\n";
    raven::AmbientSets sets;
    for (const char *name : {"hum", "Quiet", "drip", "door", "missing"})
        sets.addPrecacheEntry(name);
    std::vector<std::string> warnings;
    CHECK(sets.parse(text, warnings));
    CHECK(sets.size() == 4 && !sets.find("unused"));
    CHECK(warnings.size() == 2); // "bogus", and "missing" not found
    const auto *hum = sets.find("HUM");
    CHECK(hum && hum->timeStart == 2 && hum->timeEnd == 5 && hum->volumeMin == 50 && hum->volumeMax == 100);
    CHECK(hum->subWaves.size() == 2 && hum->subWaves[1] == "sound/borg/clank2.wav");
    CHECK(hum->loopedWave == "sound/ambience/borg/hum.wav");
    const auto *drip = sets.find("drip");
    CHECK(drip && drip->kind == raven::AmbientSets::Kind::kLocal && drip->radius == 200 && drip->timeStart == 10);
    CHECK(sets.bmodelSound("door", 2) == "sound/doors/stop.wav" && sets.bmodelSound("door", 3).empty());

    // The general set: its loop fades in over a second; a sound at once,
    // then not again for at least 2 seconds.
    const Vec3f at{1, 2, 3};
    auto first = sets.updateGeneral("hum", at, 1000);
    CHECK(first.size() == 2 && first[0].loop && first[0].volume == 0 && !first[1].loop);
    auto half = sets.updateGeneral("hum", at, 1500);
    CHECK(half.size() == 1 && half[0].loop && half[0].volume > 0.45f && half[0].volume < 0.55f);
    CHECK(sets.updateGeneral("hum", at, 2900).size() == 1);
    // Changing set: the new one fades in while the old fades out.
    auto change = sets.updateGeneral("quiet", at, 10000);
    CHECK(change.size() >= 2 && change[0].name == "sound/ambience/quiet.wav" && change[0].volume == 0);
    CHECK(change[1].name == "sound/ambience/borg/hum.wav" && change[1].volume == 1);
    auto after = sets.updateGeneral("quiet", at, 11500);
    CHECK(after.size() == 1 && after[0].volume == 1);

    // A local set: full within half its radius, silent beyond it.
    int64_t last = 0;
    auto near = sets.addLocal("drip", {0, 0, 0}, {50, 0, 0}, 7, last, 20000);
    CHECK(near.size() == 1 && near[0].entity == 7 && near[0].volume == 1 && last == 20000);
    last = 0;
    auto far = sets.addLocal("drip", {0, 0, 0}, {300, 0, 0}, 7, last, 20000);
    CHECK(far.size() == 1 && far[0].volume == 0);
    last = 5;
    CHECK(sets.addLocal("nothing", {}, {}, 7, last, 20000).empty() && last == 20000);
}

// Sounds by name: decoded once, missing and broken ones remembered.
static void testSoundLibrary(const std::filesystem::path &tmp) {
    using test::toBytes;
    audio::Sound tone;
    tone.rate = 22050;
    tone.channels = 1;
    tone.format = 1;
    for (int i = 0; i < 2205; ++i)
        tone.samples.push_back(int16_t(i % 50 < 25 ? 8000 : -8000));
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "BaseEF" / "pak0.pk3",
                    test::buildZip({{"sound/interface/Button1.wav", audio::encodePcmWav(tone), {}},
                                    {"sound/broken.wav", toBytes("not a sound"), {}}}));
    SearchSet set;
    std::vector<std::string> warnings;
    CHECK(!mountGameDir(set, tmp, "BaseEF", 0, warnings));
    SoundLibrary sounds(set);
    const auto a = sounds.get("sound/interface/button1.wav");
    CHECK(a && a->rate == 22050 && a->channels == 1 && a->frames() == 2205 && a->samples[30] == -8000);
    CHECK(sounds.get("SOUND\\interface\\BUTTON1.WAV") == a); // the same, decoded once
    CHECK(!sounds.get("sound/missing.wav") && !sounds.get("sound/broken.wav") && !sounds.get(""));
    CHECK(sounds.problems().size() == 2);
    CHECK(!sounds.get("sound/missing.wav") && sounds.problems().size() == 2); // not tried again
    std::filesystem::remove_all(tmp);
}

static void testSoundSystem(const std::filesystem::path &tmp) {
    audio::Sound tone;
    tone.rate = 22050;
    tone.channels = 1;
    tone.format = 1;
    tone.samples.assign(2205, 4000); // a tenth of a second
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "BaseEF" / "pak0.pk3", test::buildZip({{"sound/a.wav", audio::encodePcmWav(tone), {}},
                                                                 {"sound/b.wav", audio::encodePcmWav(tone), {}},
                                                                 {"music/intro.wav", audio::encodePcmWav(tone), {}},
                                                                 {"music/loop.wav", audio::encodePcmWav(tone), {}}}));
    SearchSet set;
    std::vector<std::string> warnings;
    CHECK(!mountGameDir(set, tmp, "BaseEF", 0, warnings));
    SoundLibrary library(set);
    audio::Mixer mixer(22050);
    SoundSystem sounds(mixer, library);
    // Listening from the origin, facing +x (left +y, up +z).
    sounds.respatialize(0, {0, 0, 0}, {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}});
    auto [near, nearPan] = sounds.spatialize({50, 0, 0});
    CHECK(near == 1 && std::fabs(nearPan) < 1e-4f); // within 80 units, ahead
    auto [mid, right] = sounds.spatialize({0, -580, 0});
    CHECK(std::fabs(mid - 0.6f) < 1e-3f && right > 0.99f); // 500 past 80: 0.4 lost; to the right
    auto [far, left] = sounds.spatialize({0, 2000, 0});
    CHECK(far == 0 && left < -0.99f);

    // No sound cuts another, on any channel; the same sound on the same
    // entity within 50 ms is a repeat, skipped.
    std::vector<int16_t> out(2 * 64);
    sounds.setTime(1000);
    sounds.startSound(Vec3f{0, 0, 0}, 3, 0, "sound/a.wav");
    sounds.startSound(Vec3f{0, 0, 0}, 3, 0, "sound/b.wav");
    sounds.startSound(std::nullopt, 3, 1, "sound/a.wav"); // following it, not at that spot: not a repeat
    sounds.startSound(std::nullopt, 3, 2, "sound/a.wav"); // but this is
    sounds.startSound(std::nullopt, 4, 1, "sound/a.wav");
    sounds.startSound(std::nullopt, 3, 1, "sound/missing.wav"); // nothing, and nothing stopped
    sounds.startSound(Vec3f{0, 0, 0}, 3, 0, "sound/a.wav");     // a repeat at the same spot
    sounds.startSound(Vec3f{64, 0, 0}, 3, 0, "sound/a.wav");    // elsewhere: not one
    sounds.setTime(1100);
    sounds.startSound(std::nullopt, 3, 1, "sound/a.wav"); // no longer a repeat
    mixer.mix(out.data(), 64);
    int playing = 0;
    for (audio::SoundHandle h = 1; h < 10; ++h)
        playing += mixer.isPlaying(h);
    CHECK(playing == 6);
    sounds.stopAll();

    // A looping sound plays while it's added each frame.
    sounds.clearLoopingSounds();
    sounds.addLoopingSound(5, {100, 0, 0}, "sound/a.wav");
    sounds.endFrame();
    auto effects = [&] {
        int n = 0;
        for (audio::SoundHandle h = 1; h < 20; ++h)
            n += mixer.isPlaying(h);
        return n;
    };
    CHECK(effects() == 1);
    sounds.clearLoopingSounds();
    sounds.addLoopingSound(5, {100, 0, 0}, "sound/a.wav");
    sounds.endFrame();
    CHECK(effects() == 1); // the same one
    sounds.clearLoopingSounds();
    sounds.endFrame();
    CHECK(effects() == 0);
    // Jedi Outcast's: an entity's loop stopped at once, though added
    // this frame; the first sound on an entity's channel silenced, and
    // nothing for channel 0 or another entity's.
    sounds.clearLoopingSounds();
    sounds.addLoopingSound(5, {100, 0, 0}, "sound/a.wav");
    sounds.addLoopingSound(6, {100, 0, 0}, "sound/b.wav");
    sounds.endFrame();
    CHECK(effects() == 2);
    sounds.stopLoopingSound(5);
    CHECK(effects() == 1);
    sounds.endFrame();
    CHECK(effects() == 1);
    sounds.clearLoopingSounds();
    sounds.endFrame();
    sounds.setTime(2000);
    sounds.startSound(std::nullopt, 7, 1, "sound/a.wav");
    sounds.startSound(std::nullopt, 7, 2, "sound/b.wav");
    CHECK(effects() == 2);
    sounds.muteSound(7, 0);
    sounds.muteSound(8, 1);
    CHECK(effects() == 2);
    sounds.muteSound(7, 1);
    CHECK(effects() == 1);
    sounds.stopAll();

    // Music: the intro, then the loop (names without an extension found).
    sounds.startBackgroundTrack("music/intro", "music/loop");
    CHECK(mixer.isTypePlaying(audio::SoundType::kMusic));
    std::vector<int16_t> lots(2 * 4000);
    mixer.mix(lots.data(), 4000); // past the intro's end
    sounds.endFrame();
    CHECK(mixer.isTypePlaying(audio::SoundType::kMusic));
    mixer.mix(lots.data(), 4000);
    CHECK(mixer.isTypePlaying(audio::SoundType::kMusic)); // looping
    sounds.stopAll();
    CHECK(!mixer.isTypePlaying(audio::SoundType::kMusic));
    std::filesystem::remove_all(tmp);
}

static void testDynamicMusic() {
    // A set as dms.dat describes one: explore and action pieces, each with
    // an exit into a transition piece, and a boss piece; a level that uses
    // it.
    const char *dms = R"(musicfiles
{
	ex_explore
	{
		entry
		{
			marker0	0
			marker1	1.0
		}
		exit
		{
			nextfile	ex_etr00
			time00	1.0
		}
	}
	ex_action
	{
		entry
		{
			marker0	0
			marker1	2.0
		}
		exit
		{
			nextfile	ex_atr00
			nextmark	marker1
			time00	0.5
		}
	}
	ex_boss
	{
	}
}
levelmusic
{
	lvl
	{
		explore	ex_explore
		action	ex_action
		boss	ex_boss
	}
	other
	{
		uses	lvl
	}
}
)";
    std::set<std::string> files = {"music/lvl/ex_explore.mp3", "music/lvl/ex_action.mp3", "music/lvl/ex_boss.mp3",
                                   "music/lvl/ex_etr00.mp3",   "music/lvl/ex_atr00.mp3",  "music/death_music.mp3"};
    auto exists = [&](const std::string &p) { return files.count(p) != 0; };
    MusicSet set;
    CHECK(!findMusicSet(dms, "music/Other", exists, set));
    CHECK(set.name == "other" && set.folder == "lvl" && set.bossFolder == "lvl");
    CHECK(set.path(DynamicMusic::kAction) == "music/lvl/ex_action.mp3");
    CHECK(set.path(DynamicMusic::kExploreTransition) == "music/lvl/ex_etr00.mp3");
    CHECK(set.path(DynamicMusic::kActionTransition) == "music/lvl/ex_atr00.mp3");
    CHECK(set.path(DynamicMusic::kActionTransition + 1).empty());
    CHECK(set.path(DynamicMusic::kDeath) == "music/death_music.mp3");
    CHECK(set.action && set.action->times.size() == 1 && set.explore->marker("MARKER1") == 1.0f);
    MusicSet none;
    // A name with no set only a warning in s_debugdynamic's prints; other
    // errors under dms.dat's heading.
    Error err = findMusicSet(dms, "nowhere", exists, none);
    CHECK(err && musicSetReport(err) == "^3Unable to find entry for \"nowhere\" in \"ext_data/dms.dat\"\n");
    files.erase("music/lvl/ex_atr00.mp3");
    err = findMusicSet(dms, "lvl", exists, none); // a transition's file missing
    CHECK(err &&
          musicSetReport(err).rfind("Error parsing music data ( in \"ext_data/dms.dat\" ):\nTransition", 0) == 0);
    CHECK(!musicSetIncomplete(err));
    files.insert("music/lvl/ex_atr00.mp3");
    CHECK(set.notes.empty());
    // A boss piece with exits: allowed, but reported.
    std::string bossExits = dms;
    bossExits.replace(bossExits.find("ex_boss\n\t{\n"), 11,
                      "ex_boss\n\t{\n\t\texit\n\t\t{\n\t\t\tnextfile\tex_etr00\n\t\t}\n");
    MusicSet bossy;
    CHECK(!findMusicSet(bossExits, "lvl", exists, bossy));
    CHECK(bossy.notes.size() == 1 &&
          bossy.notes[0] == "\"ex_boss\" has boss transitions defined, this is not allowed!");

    // Playing it, a tenth of a second a frame: explore 5 s, action 5 s,
    // the transitions 3 s.
    audio::Mixer mixer(1000);
    auto tone = [](int ms) {
        auto s = std::make_shared<audio::Sound>();
        s->rate = 1000;
        s->channels = 1;
        s->format = 1;
        s->samples.assign(size_t(ms), 1000);
        return std::shared_ptr<const audio::Sound>(s);
    };
    std::array<std::shared_ptr<const audio::Sound>, DynamicMusic::kTracks> sounds{};
    sounds[DynamicMusic::kExplore] = tone(5000);
    sounds[DynamicMusic::kAction] = tone(5000);
    sounds[DynamicMusic::kBoss] = tone(5000);
    sounds[DynamicMusic::kDeath] = tone(3000);
    sounds[DynamicMusic::kActionTransition] = tone(3000);
    sounds[DynamicMusic::kExploreTransition] = tone(3000);
    DynamicMusic music(mixer);
    uint64_t now = 0;
    std::vector<int16_t> out(2 * 100);
    auto frame = [&] {
        mixer.mix(out.data(), 100);
        now += 100;
        music.update(now);
    };
    CHECK(music.status() == "( Dynamic music OFF )\n");
    CHECK(music.start(set, sounds, now));
    CHECK(music.current() == DynamicMusic::kExplore);
    // Action cuts in at once; s_debugdynamic prints the request (once)
    // and the switch.
    std::string printed;
    music.debug = 1;
    music.print = [&](const std::string &text) { printed += text; };
    music.request("action");
    music.request("action");
    CHECK(music.status() == "( Dynamic music ON, request state: 'action'(1), actual: 'explore' (0) )\n");
    frame();
    CHECK(music.current() == DynamicMusic::kAction);
    CHECK(printed == "^6S_SetDynamicMusicState( Request: \"action\" )\n^6S_SwitchDynamicTracks( \"action\" )\n");
    music.debug = 0;
    // Back to explore only at action's exit time, through its transition,
    // which hands over two seconds before its end.
    music.request("explore");
    std::vector<int> seen;
    for (int i = 0; i < 120 && music.current() != DynamicMusic::kExplore; ++i) {
        frame();
        if (seen.empty() || seen.back() != music.current())
            seen.push_back(music.current());
    }
    CHECK((seen == std::vector<int>{DynamicMusic::kAction, DynamicMusic::kActionTransition, DynamicMusic::kExplore}) ||
          (seen == std::vector<int>{DynamicMusic::kActionTransition, DynamicMusic::kExplore}));
    // Silence waits for explore's exit, through explore_tr0.
    music.request("Silence");
    seen.clear();
    for (int i = 0; i < 120 && music.current() != DynamicMusic::kSilence; ++i) {
        frame();
        if (seen.empty() || seen.back() != music.current())
            seen.push_back(music.current());
    }
    CHECK(!seen.empty() && seen.back() == DynamicMusic::kSilence);
    CHECK(std::find(seen.begin(), seen.end(), DynamicMusic::kExploreTransition) != seen.end());
    // Boss and death cut in; nothing cuts death.
    music.request("boss");
    frame();
    CHECK(music.current() == DynamicMusic::kBoss);
    music.request("death");
    frame();
    CHECK(music.current() == DynamicMusic::kDeath);
    music.request("action");
    frame();
    CHECK(music.current() == DynamicMusic::kDeath);
    music.stop();
    CHECK(!music.on() && !mixer.isTypePlaying(audio::SoundType::kMusic));

    // Measured from the decoding, ahead of what's heard: 4 s ahead, a
    // tenth of a second into explore (5 s) is under 2 s from its end, so
    // it starts again at once.
    DynamicMusic ahead(mixer);
    std::string switches;
    ahead.debug = 1;
    ahead.print = [&](const std::string &text) { switches += text; };
    ahead.decodeLead = 4;
    CHECK(ahead.start(set, sounds, now));
    mixer.mix(out.data(), 100);
    now += 100;
    ahead.update(now);
    CHECK(switches == "^6S_SwitchDynamicTracks( \"explore\" )\n");
}

static const reluva::test::Suite
    kSuite("idtech3.unit.sound",
           {
               {"sound library", [](const std::filesystem::path &tmp) { testSoundLibrary(tmp / "sounds"); }},
               {"sound system", [](const std::filesystem::path &tmp) { testSoundSystem(tmp / "soundsystem"); }},
               {"ambient sets", [](const std::filesystem::path &) { testAmbientSets(); }},
               {"dynamic music", [](const std::filesystem::path &) { testDynamicMusic(); }},
           });
