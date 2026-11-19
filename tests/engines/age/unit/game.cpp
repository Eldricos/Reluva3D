// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Angel Game Engine's unit tests: the game: lessons, the player's
// record, the menus' layout, music, options, replays, drivers, cruising.
// Every file is built in the test.

#include "tests/engines/age/support.h"

using namespace reluva;
using namespace reluva::age;
using namespace reluva::age::test_support;

namespace {

void testLessons() {
    LessonSettings l;
    CHECK(!parseLessonSettings("Filename,Event,Checkpoints,TimeLimit,AmbDensity,extra,extra,extra,extra,etra,\r\n"
                               "corner0waypoints,4,1,0,0,50,0,0,0,0,0\r\n",
                               l));
    CHECK(l.file == "corner0waypoints" && l.event == LessonEvent::kMinimumSpeed && l.timeLimit == 0 &&
          l.extra[0] == 50);
    LessonSettings none;
    CHECK(parseLessonSettings("Filename,Event\r\n", none));
    // A lesson's checkpoints' fifth column is a radius whatever it's called.
    std::vector<Waypoint> w;
    CHECK(!parseWaypoints("x,y,z,a,poly count,frane rate\n0,0,0,0,7,0\n10,0,0,0,9,0\n", w, true) && w[1].radius == 9);
    CHECK(!parseWaypoints("x,y,z,a,poly count,frane rate\n0,0,0,0,7,0\n10,0,0,0,9,0\n", w) && w[1].radius == 0);

    using R = LessonJudge::Result;
    LessonJudge j;
    LessonJudge::Frame f;
    f.dt = 0.1f;
    // Minimum speed: too slow at a checkpoint fails.
    j.start(l);
    CHECK(j.message() == 232 && j.minimumSpeed() == 50);
    f.clock = 1;
    f.speedMph = 55;
    CHECK(j.step(f) == R::kGoing && j.message() == 237); // up to speed
    f.checkpoint = true;
    f.speedMph = 40;
    CHECK(j.step(f) == R::kFailed && j.message() == 238);

    // Destroy: rammed into submission, done; with police, then lose them
    // at the end; the car at its destination first fails.
    LessonSettings destroy;
    destroy.event = LessonEvent::kDestroy;
    destroy.timeLimit = 100;
    j.start(destroy);
    CHECK(j.message() == 619);
    f = {};
    f.dt = 0.1f;
    f.clock = 5;
    f.finished = true; // the checkpoint's no use yet
    CHECK(j.step(f) == R::kGoing);
    f.targetStopped = true;
    CHECK(j.step(f) == R::kPassed && j.message() == 620);
    j.start(destroy);
    f.police = 6;
    f.finished = false;
    CHECK(j.step(f) == R::kGoing && j.message() == 652); // "Get going!  Lose the tail"
    f.finished = true;
    f.chasing = 1;
    CHECK(j.step(f) == R::kGoing);
    f.chasing = 0;
    CHECK(j.step(f) == R::kPassed && j.message() == 196);
    j.start(destroy);
    f = {};
    f.clock = 5;
    f.targetDone = true;
    CHECK(j.step(f) == R::kFailed && j.message() == 621);

    // No touching: a car touched after the start fails.
    LessonSettings touch;
    touch.event = LessonEvent::kNoTouching;
    j.start(touch);
    f = {};
    f.carTouches = 2; // before the start
    f.clock = 1;
    CHECK(j.step(f) == R::kGoing);
    f.carTouches = 3;
    CHECK(j.step(f) == R::kFailed && j.message() == 230);

    // Cop chase: the end counts once the police are lost; busted fails.
    LessonSettings chase;
    chase.event = LessonEvent::kCopChase;
    chase.timeLimit = 100;
    j.start(chase);
    f = {};
    f.dt = 4; // the opening message gone
    f.clock = 10;
    f.finished = true;
    f.chasing = 2;
    CHECK(j.step(f) == R::kGoing && j.message() == 197);
    f.chasing = 0;
    CHECK(j.step(f) == R::kPassed && j.message() == 196);
    j.start(chase);
    f.busted = true;
    CHECK(j.step(f) == R::kFailed);

    // Follow: escaped fails; kept up with to its end passes, however long
    // (its TimeLimit isn't a clock).
    LessonSettings follow;
    follow.event = LessonEvent::kFollow;
    follow.timeLimit = 36;
    j.start(follow);
    f = {};
    f.clock = 5;
    f.targetDistance = 50;
    CHECK(j.step(f) == R::kGoing);
    f.targetDistance = LessonJudge::kEscape + 1;
    CHECK(j.step(f) == R::kFailed && j.message() == 222);
    j.start(follow);
    f.targetDistance = 30;
    f.clock = 88;
    CHECK(j.step(f) == R::kGoing && j.timeLeft(f.clock) < 0);
    f.targetDone = true;
    CHECK(j.step(f) == R::kPassed && j.message() == 221);

    // Gates: in time, or not.
    LessonSettings gates;
    gates.event = LessonEvent::kGates;
    gates.timeLimit = 22;
    j.start(gates);
    f = {};
    f.clock = 22.5f;
    CHECK(j.step(f) == R::kFailed && j.message() == 244);
}

void testRecord() {
    PlayerRecord r;
    CHECK(!r.parse("# a comment\nrace sf checkpoint 2 a 3\nrace sf checkpoint 2 a 1\nlesson london 3\nnonsense\n"));
    CHECK(r.bestPlace("sf", RaceKind::kCheckpoint, 2, false) == 1 &&
          r.bestPlace("sf", RaceKind::kCheckpoint, 2, true) == 0);
    r.raced("sf", RaceKind::kCheckpoint, 2, false, 4); // not better
    CHECK(r.bestPlace("sf", RaceKind::kCheckpoint, 2, false) == 1);
    CHECK(r.hasPassed("london", 3) && !r.hasPassed("sf", 3));
    PlayerRecord again;
    CHECK(!again.parse(r.text()) && again.bestPlace("sf", RaceKind::kCheckpoint, 2, false) == 1 &&
          again.hasPassed("london", 3));

    // Races open three at a time, each group once every race before is
    // placed 3rd or better; lessons one after another.
    PlayerRecord p;
    CHECK(raceOpen(p, "sf", RaceKind::kBlitz, 2, false, 3, 3) && !raceOpen(p, "sf", RaceKind::kBlitz, 3, false, 3, 3));
    for (int i = 0; i < 3; ++i)
        p.raced("sf", RaceKind::kBlitz, i, false, i == 2 ? 4 : 2);
    CHECK(!raceOpen(p, "sf", RaceKind::kBlitz, 3, false, 3, 3));
    p.raced("sf", RaceKind::kBlitz, 2, true, 3); // a professional's result counts for amateurs
    CHECK(raceOpen(p, "sf", RaceKind::kBlitz, 5, false, 3, 3) && !raceOpen(p, "sf", RaceKind::kBlitz, 6, false, 3, 3));
    CHECK(!raceOpen(p, "sf", RaceKind::kBlitz, 3, true, 3, 3));
    // Lessons in any order; the Final once the three Midterms are passed.
    CHECK(raceOpen(p, "sf", RaceKind::kCrashCourse, 0, false, 3, 3) &&
          raceOpen(p, "sf", RaceKind::kCrashCourse, 11, false, 3, 3) &&
          !raceOpen(p, "sf", RaceKind::kCrashCourse, 12, false, 3, 3));
    p.passed("sf", 3);
    p.passed("sf", 7);
    CHECK(!raceOpen(p, "sf", RaceKind::kCrashCourse, 12, false, 3, 3));
    p.passed("sf", 11);
    CHECK(raceOpen(p, "sf", RaceKind::kCrashCourse, 12, false, 3, 3));

    // Unlocking, as the pictures say.
    const RaceCount twelve = [](const std::string &, RaceKind) { return 12; };
    PlayerRecord u;
    CHECK(unlocked(u, "vpbug", -1, false, twelve)); // no rule: open
    CHECK(!unlocked(u, "vpauditt", -1, false, twelve));
    for (int i = 0; i < 6; ++i)
        u.raced("sf", RaceKind::kCheckpoint, i, false, 3);
    CHECK(unlocked(u, "vpauditt", -1, false, twelve) && !unlocked(u, "vpauditt", -1, true, twelve));
    CHECK(!unlocked(u, "vpauditt", 4, false, twelve)); // all twelve for the paint job
    CHECK(!unlocked(u, "vp4x4", -1, false, twelve));
    u.passed("sf", 12);
    CHECK(unlocked(u, "vp4x4", -1, false, twelve));
    for (const UnlockRule &rule : unlockRules())
        if (rule.car == "vpauditt" && rule.paintJob < 0) {
            CHECK(unlockText(rule, false) ==
                  "To unlock this vehicle, place 1st, 2nd, or 3rd in six San Francisco Checkpoint Races");
            CHECK(unlockText(rule, true) ==
                  "To unlock this vehicle, finish first in six San Francisco Checkpoint Races");
        } else if (rule.car == "vpcab") {
            CHECK(unlockText(rule, false) ==
                  "To unlock this custom paint job, pass London Crash Course Midterm Exam Two");
        }
    // race/<city>/<city>_rewards.csv: the results screen's words.
    const std::vector<Reward> rewards =
        parseRewards("RaceType,RaceNum,CarName,VariantNum (zero if it unlocks a car),\r\n"
                     "blitz,half,vpsemi,0,Good Job! You've unlocked the Freightliner Fire Truck!,\r\n"
                     "crash,3,vpvwcup,6,Congrats!  Mystery paint job for the New Beetle RSi!,\r\n"
                     "circuit,half,vpvwcup,0,Congratulations! You've unlocked the New Beetle RSi!\r\n");
    CHECK(rewards.size() == 3 && rewards[0].kind == "blitz" && rewards[0].which == "half" &&
          rewards[0].car == "vpsemi" && rewards[0].paintJob == 0 &&
          rewards[0].message == "Good Job! You've unlocked the Freightliner Fire Truck!");
    CHECK(rewards[1].which == "3" && rewards[1].paintJob == 6 &&
          rewards[2].message == "Congratulations! You've unlocked the New Beetle RSi!");
}

void testWidgets() {
    std::vector<Widget> w;
    CHECK(!parseWidgets("MENU NAME,ID,WIDGET NAME,ID,X,Y,W,H,DESC\r\n"
                        "Main Menu,1,dvrcc,0,439,242,230,90,4 State crash\r\n"
                        "Main Menu,1,DRIVER NAME,4,177,128,205,24,\r\n"
                        "short,line\r\n",
                        w));
    CHECK(w.size() == 2 && w[0].menu == "Main Menu" && w[0].name == "dvrcc" && w[0].x == 439 && w[0].h == 90 &&
          w[0].desc == "4 State crash" && w[1].desc.empty());
    CHECK(parseWidgets("MENU NAME,ID\r\n", w));
}

void testMusic(const std::filesystem::path &tmp) {
    // The tunes' segments by state (aud/dmusic/csv_files): the race file's
    // and the cruising file's columns differ in order.
    const auto race =
        parseMusicSets("Start Music,Return Music,Idle Race Music,Idle Cop Music,Cop chase music,"
                       "Pause Music,Race results Music,Big air Motif style,Big Air Motif name,Big Air Motif Band\r\n"
                       "EnemyStart,EnemyReturn,EnemyIdle,EnemyIdleCops,EnemyCops,Pause,Results1,GrooverStyle,BigAir,"
                       "BigAir\r\n",
                       true);
    CHECK(race.size() == 1 && race[0].idleCops == "EnemyIdleCops" && race[0].cops == "EnemyCops" &&
          race[0].results == "Results1" && race[0].airStyle == "GrooverStyle" && race[0].airMotif == "BigAir" &&
          race[0].airBand == "BigAir");
    const auto roam = parseMusicSets("Start Music,Return Music,Idle Music,Cop Chase Music,idle cop music,Pause Music\n"
                                     "SunroofStart,SunRoofReturn,SunRoofIdle,SunRoofCops,SunIdleCops,Pause\n",
                                     false);
    CHECK(roam.size() == 1 && roam[0].cops == "SunRoofCops" && roam[0].idleCops == "SunIdleCops");

    // Which segment it asks for as things happen (the segments themselves
    // aren't there: only the choice is checked).
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "aud" / "dmusic" / "csv_files" / "singleroam.csv",
                    toBytes("Start,Return,Idle,Cops,IdleCops,Pause\nS,R,I,C,IC,P\n"));
    SearchSet files;
    auto loose = std::make_unique<DirectoryArchive>();
    CHECK(!loose->open(tmp));
    files.add(std::move(loose), 1);
    audio::Mixer mixer(22050);
    GameMusic music;
    CHECK(!music.start(files, mixer, false, 0) && music.segment() == "S");
    GameMusic::State st;
    st.speed = 20;
    music.update(1, st);
    CHECK(music.segment() == "S"); // on the move from the start: the start plays on
    st.speed = 0;
    for (int i = 0; i < 5; ++i)
        music.update(1, st);
    CHECK(music.segment() == "I"); // stopped a while
    st.chased = true;
    music.update(1, st);
    CHECK(music.segment() == "IC");
    st.speed = 20;
    music.update(1, st);
    CHECK(music.segment() == "C"); // chased on the move
    st.chased = false;
    music.update(1, st);
    CHECK(music.segment() == "R"); // the chase over
    st.lastStretch = true;
    music.update(1, st);
    CHECK(music.segment() == "C"); // the final lap
    st.lastStretch = false;
    music.update(1, st);
    CHECK(music.segment() == "R");
    st.paused = true;
    music.update(1, st);
    CHECK(music.segment() == "P");
    // The race restarted: the tune plays on, not from its start.
    CHECK(!music.start(files, mixer, false, 0) && music.segment() == "P");
    music.stop();
    CHECK(music.segment().empty());
    CHECK(music.start(files, mixer, true, 0)); // no race list
}

void testOptions(const std::filesystem::path &tmp) {
    // The defaults: the game's (its help's keyboard table), its names.
    GameOptions o;
    CHECK(o.controlOf(key::kUp) == Control::kThrottle && o.controlOf('w') == Control::kWideAngle &&
          o.controlOf(' ') == Control::kHandbrake && o.controlOf(key::kReturn) == Control::kHorn &&
          o.controlOf('q') == Control::kFullScreenMap && o.controlOf(key::kTab) == Control::kMapToggle &&
          o.controlOf('h') == Control::kHudToggle && o.controlOf(key::kBackspace) == Control::kRearViewMirror &&
          o.controlOf('s') == Control::kNextCheckpoint && o.controlOf('a') == Control::kShiftUp &&
          o.controlOf('v') == Control::kThrillCam && o.controlOf(key::kKp0 + 2) == Control::kLookBack &&
          o.controlOf('f') == Control::kRotatingMap && o.controlOf('5') == Control::kCdNext &&
          o.controlOf('i') == Control::kOpponentPosition && o.keys[size_t(Control::kCameraPan)][0] == 0);
    CHECK(controlStringId(Control::kThrottle) == 280 && controlStringId(Control::kRearViewMirror) == 305 &&
          keyName(key::kUp) == "Up" && keyName('w') == "W" && keyName(key::kF1 + 2) == "F3");
    CHECK(o.otherWith('z', Control::kThrottle) == Control::kShiftDown &&
          o.otherWith('z', Control::kShiftDown) == Control::kCount);

    // Changed, saved and read back.
    o.music = false;
    o.effectsVolume = 0.25f;
    o.balance = -0.5f;
    o.resolution = 2;
    o.objectDetail = 1;
    o.textureQuality = 2;
    o.cloudShadows = 1;
    o.startInCar = true;
    o.controller = GameOptions::kSteeringWheel;
    o.deadZone = 0.6f;
    o.keys[size_t(Control::kHorn)] = {'g', 'j'};
    CHECK(!o.save(tmp));
    GameOptions back;
    CHECK(!back.load(tmp));
    CHECK(!back.music && back.ambient && std::fabs(back.effectsVolume - 0.25f) < 1e-6f &&
          std::fabs(back.balance + 0.5f) < 1e-6f && back.resolution == 2 && back.objectDetail == 1 &&
          back.textureQuality == 2 && back.cloudShadows == 1 && back.startInCar &&
          back.controlOf('j') == Control::kHorn && back.controlOf(key::kReturn) == Control::kCount &&
          back.controller == GameOptions::kSteeringWheel && std::fabs(back.deadZone - 0.6f) < 1e-6f);
    back.resetControls();
    CHECK(back.controlOf(key::kReturn) == Control::kHorn && !back.startInCar && !back.music); // the audio stays
    GameOptions none;
    CHECK(!none.load(tmp / "nothing") && none.music); // no file yet: the defaults
    CHECK(GameOptions().farClip() == 4000 && o.steerRate() == 1);

    // Auto reverse: the throttle held against a wall backs the car away
    // once a second's passed; let go, it's in first again.
    CollisionWorld walled;
    walled.add({-300, 0, -300}, {-300, 0, 300}, {300, 0, 300});
    walled.add({-300, 0, -300}, {300, 0, 300}, {300, 0, -300});
    walled.add({-50, 0, -8}, {50, 0, -8}, {50, 10, -8});
    walled.add({-50, 0, -8}, {50, 10, -8}, {-50, 10, -8});
    walled.build(16);
    for (bool assist : {false, true}) {
        CarSim car;
        car.reset(testCar(), {0, 0, 0}, 0);
        car.autoReverse = assist;
        CarControls go;
        go.throttle = 1;
        for (int i = 0; i < 60; ++i)
            car.step(go, walled, 0.05f);
        const float atWall = car.position[2];
        for (int i = 0; i < 40; ++i)
            car.step(go, walled, 0.05f);
        if (assist)
            CHECK(car.gear < 0 && car.position[2] > atWall + 1);
        else
            CHECK(car.gear > 0 && car.position[2] < atWall + 0.5f);
        go.throttle = 0;
        car.step(go, walled, 0.05f);
        CHECK(car.gear > 0);
    }
}

void testReplays(const std::filesystem::path &tmp) {
    Replay r;
    r.name = "Mini: a race / of mine";
    r.city = "london";
    r.kind = int(RaceKind::kCircuit);
    r.race = 3;
    r.level = 1;
    r.car = "vpbug";
    r.paint = 2;
    r.autoReverse = true;
    r.steerRate = 1.25f;
    r.pedestrians = 0;
    for (int i = 0; i < 130; ++i)
        r.frames.push_back({0.05f, uint16_t(i % 7), int8_t(i % 3 - 1), uint8_t(i == 5 ? kReplayManual : 0)});
    r.marks = {{1, 2, 3}, {4, 5, 6}};
    CHECK(std::fabs(r.duration() - 6.5f) < 1e-3f);
    r.changed = true; // run in its conditions as changed on the race menu
    r.laps = 7;
    r.opponents = 2;
    r.timeOfDay = 3;
    r.weather = 2;
    r.cops = 4;
    r.traffic = 0.6f;
    r.peds = 0.2f;

    r.frames[7].padSteer = -90; // a game controller's stick and trigger
    r.frames[7].padThrottle = 200;
    // Written and read back as it was.
    Replay back;
    CHECK(!decodeReplay(encodeReplay(r), back));
    CHECK(back.name == r.name && back.city == "london" && back.kind == int(RaceKind::kCircuit) && back.race == 3 &&
          back.level == 1 && back.car == "vpbug" && back.paint == 2 && back.autoReverse &&
          std::fabs(back.steerRate - 1.25f) < 1e-6f && back.pedestrians == 0 && back.frames.size() == 130 &&
          back.frames[5].flags == kReplayManual && back.frames[4].shift == 0 && back.frames[6].keys == 6 &&
          back.marks.size() == 2 && back.marks[1][2] == 6 && back.frames[7].padSteer == -90 &&
          back.frames[7].padThrottle == 200 && back.frames[7].padBrake == 0);
    CHECK(back.changed && back.laps == 7 && back.opponents == 2 && back.timeOfDay == 3 && back.weather == 2 &&
          back.cops == 4 && std::fabs(back.traffic - 0.6f) < 1e-6f && std::fabs(back.peds - 0.2f) < 1e-6f);
    // A first-version replay (no conditions) still reads, unchanged.
    std::vector<uint8_t> v1 = encodeReplay(r);
    v1[7] = 1;
    // The magic and version (8), the name, city, kind, race, level, car,
    // paint, auto reverse, steering and pedestrians: then the conditions'
    // 14 bytes, which version 1 hadn't.
    const std::ptrdiff_t start =
        8 + std::ptrdiff_t(1 + r.name.size() + 1 + r.city.size() + 1 + 2 + 1 + 1 + r.car.size() + 1 + 1 + 4 + 4);
    v1.erase(v1.begin() + start, v1.begin() + start + 14);
    // Nor each frame's game controller bytes (version 3): 8 bytes a frame
    // after the count.
    for (size_t i = 0; i < r.frames.size(); ++i) {
        const std::ptrdiff_t at = start + 4 + std::ptrdiff_t(i * 8) + 8;
        v1.erase(v1.begin() + at, v1.begin() + at + 3);
    }
    Replay old;
    CHECK(!decodeReplay(v1, old) && !old.changed && old.pedestrians == 0 && old.frames.size() == 130 &&
          old.frames[6].keys == 6 && old.marks.size() == 2);
    std::vector<uint8_t> cut = encodeReplay(r);
    cut.resize(cut.size() / 2);
    CHECK(decodeReplay(cut, back) && decodeReplay({1, 2, 3}, back));

    // Saved under its name, listed, renamed, deleted.
    std::filesystem::remove_all(tmp);
    CHECK(replayFileName(r.name) == "Mini_ a race _ of mine.rpl" && replayFileName("") == "replay.rpl");
    CHECK(!saveReplay(tmp, r));
    r.name = "Another";
    CHECK(!saveReplay(tmp, r));
    auto list = listReplays(tmp);
    CHECK(list.size() == 2 && list[0].name == "Another" && list[1].name == "Mini: a race / of mine" &&
          list[1].race == 3 && std::fabs(list[1].duration - 6.5f) < 1e-3f);
    CHECK(!renameReplay(tmp, list[1], "Best lap"));
    list = listReplays(tmp);
    CHECK(list.size() == 2 && list[1].name == "Best lap" &&
          !std::filesystem::exists(replayFolder(tmp) / "Mini_ a race _ of mine.rpl"));
    CHECK(!deleteReplay(list[0]) && listReplays(tmp).size() == 1);
    CHECK(listReplays(tmp / "none").empty());
}

void testDrivers(const std::filesystem::path &tmp) {
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);
    // Before there were drivers: the record that was is the first's.
    PlayerRecord old;
    CHECK(!old.load(tmp));
    old.raced("sf", RaceKind::kBlitz, 2, false, 1, 61.5f);
    old.raced("sf", RaceKind::kBlitz, 2, false, 3, 70.0f); // the best kept
    old.setLastRace("london", RaceKind::kCircuit, 4);
    CHECK(!old.save());
    DriverRoster roster;
    CHECK(!roster.load(tmp));
    CHECK(roster.drivers().size() == 1 && roster.driver().name == "Player" && roster.driver().file == "record.txt");
    PlayerRecord first;
    first.load(tmp, roster.driver().file);
    CHECK(first.bestPlace("sf", RaceKind::kBlitz, 2, false) == 1 &&
          std::fabs(first.bestTime("sf", RaceKind::kBlitz, 2, false) - 61.5f) < 0.01f &&
          first.bestTime("sf", RaceKind::kBlitz, 3, false) < 0);
    std::string city;
    RaceKind kind = RaceKind::kBlitz;
    int index = 0;
    CHECK(first.lastRace(city, kind, index) && city == "london" && kind == RaceKind::kCircuit && index == 4);

    // A new driver: theirs alone, a record of their own; no name twice.
    CHECK(!roster.add("Bob", true) && roster.current() == 1 && roster.driver().professional &&
          roster.driver().file == "record-1.txt");
    CHECK(roster.add("bob", false) && roster.has("BOB") && roster.drivers().size() == 2);
    PlayerRecord bob;
    bob.load(tmp, roster.driver().file);
    CHECK(bob.bestPlace("sf", RaceKind::kBlitz, 2, false) == 0 && !bob.lastRace(city, kind, index));
    bob.raced("sf", RaceKind::kBlitz, 2, true, 2, 55);
    CHECK(!bob.save() && !roster.save());
    DriverRoster again;
    again.load(tmp);
    CHECK(again.drivers().size() == 2 && again.current() == 1 && again.drivers()[1].name == "Bob" &&
          again.drivers()[1].professional);

    // Deleted, with the record; never the only one.
    CHECK(!again.remove(0) && again.drivers().size() == 1 && again.current() == 0 &&
          !std::filesystem::exists(tmp / "record.txt") && std::filesystem::exists(tmp / "record-1.txt"));
    CHECK(again.remove(0) && again.drivers().size() == 1);
}

// The race menu's Cruise: the cruise, and the stunt where the city has
// accel0's points; their aimaps; always open; Quick Race remembers it.
void testCruise(const std::filesystem::path &tmp) {
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "race" / "sf" / "accel0waypoints.csv",
                    toBytes("x,y,z,a,poly count,frane rate,state changes,texture changes,msg\n"
                            "-1283.6,40.0,854.1,39.6,15,0,0,0,\n-1.2,-0.8,-3.5,47.6,15,0,0,0,\n"));
    SearchSet files;
    auto loose = std::make_unique<DirectoryArchive>();
    CHECK(!loose->open(tmp));
    files.add(std::move(loose), 1);
    const auto sf = raceNames(files, "sf", RaceKind::kCruise);
    const auto london = raceNames(files, "london", RaceKind::kCruise);
    CHECK(sf.size() == 2 && sf[0] == "Cruise" && sf[1] == "Stunt Mode: Acceleration");
    CHECK(london.size() == 1);
    RaceDefinition cruise;
    RaceDefinition stunt;
    CHECK(!loadRace(files, "london", RaceKind::kCruise, kCruiseRace, cruise));
    CHECK(cruise.waypoints.empty() && cruise.settings[0].cops == 4 && cruise.opponents[0].empty());
    CHECK(!loadRace(files, "sf", RaceKind::kCruise, kStuntRace, stunt));
    CHECK(stunt.waypoints.size() == 2 && std::fabs(stunt.waypoints[1].radius - 15) < 0.01f &&
          stunt.settings[1].cops == 0);
    CHECK(raceAiMapName("sf", RaceKind::kCruise, kCruiseRace, true) == "race/sf/roam.aimap_p");
    PlayerRecord record;
    CHECK(raceOpen(record, "sf", RaceKind::kCruise, kStuntRace, false, 3, 3));
    record.setLastRace("sf", RaceKind::kCruise, kStuntRace);
    std::string city;
    RaceKind kind = RaceKind::kBlitz;
    int index = 0;
    CHECK(record.lastRace(city, kind, index) && kind == RaceKind::kCruise && index == kStuntRace);
}

} // namespace

static const reluva::test::Suite kSuite("age.unit.game",
                                        {
                                            {"lessons", [](const std::filesystem::path &) { testLessons(); }},
                                            {"record", [](const std::filesystem::path &) { testRecord(); }},
                                            {"widgets", [](const std::filesystem::path &) { testWidgets(); }},
                                            {"music", [](const std::filesystem::path &tmp) { testMusic(tmp); }},
                                            {"options", [](const std::filesystem::path &tmp) { testOptions(tmp); }},
                                            {"replays", [](const std::filesystem::path &tmp) { testReplays(tmp); }},
                                            {"drivers", [](const std::filesystem::path &tmp) { testDrivers(tmp); }},
                                            {"cruise", [](const std::filesystem::path &tmp) { testCruise(tmp); }},
                                        });
