// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's commands for the AWE engine's games (Agatha Christie:
// Murder on the Orient Express): see docs/engines/awe.md.

#include "tools/awe_tool.h"

#include "engines/awe/ac2.h"
#include "engines/awe/camera.h"
#include "engines/awe/gr2.h"
#include "engines/awe/granny_dll.h"
#include "engines/awe/model.h"
#include "engines/awe/savegame.h"
#include "engines/awe/story.h"
#include "engines/awe/tables.h"
#include "engines/awe/viewers.h"
#include "common/archive.h"
#include "common/image.h"
#include "common/system.h"
#include "common/video/bink_file.h"
#include "common/video/bink_audio.h"
#include "common/video/bink_video.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

namespace awe = reluva::awe;

bool readFile(const std::filesystem::path& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "can't open %s\n", path.string().c_str());
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

// granny2.dll from beside the file, its folder's parents, or `-dll path`.
bool loadGranny(const std::filesystem::path& near, const std::string& given, awe::GrannyDll& dll) {
    std::filesystem::path path = given;
    if (path.empty())
        for (std::filesystem::path dir = std::filesystem::absolute(near).parent_path(); !dir.empty();
             dir = dir.parent_path()) {
            for (const char* name : {"granny2.dll", "GRANNY2.DLL", "Granny2.dll"})
                if (std::filesystem::exists(dir / name)) {
                    path = dir / name;
                    break;
                }
            if (!path.empty() || dir == dir.parent_path())
                break;
        }
    if (path.empty()) {
        std::fprintf(stderr, "granny2.dll not found near %s (give it with -dll)\n", near.string().c_str());
        return false;
    }
    std::vector<uint8_t> data;
    if (!readFile(path, data))
        return false;
    if (reluva::Error e = dll.load(data)) {
        std::fprintf(stderr, "%s: %s\n", path.string().c_str(), e.message.c_str());
        return false;
    }
    return true;
}

int cmdGr2Info(int argc, char** argv) {
    std::string file;
    unsigned maxElements = 4;
    bool tree = false;
    for (int i = 2; i < argc; i++) {
        if (std::strcmp(argv[i], "-t") == 0)
            tree = true;
        else if (std::strcmp(argv[i], "-n") == 0 && i + 1 < argc)
            maxElements = unsigned(std::strtoul(argv[++i], nullptr, 10)), tree = true;
        else
            file = argv[i];
    }
    if (file.empty()) {
        std::fprintf(stderr, "usage: reluva-tool gr2-info <file.gr2> [-t] [-n elements]\n");
        return 2;
    }
    std::vector<uint8_t> data;
    if (!readFile(file, data))
        return 1;
    awe::Gr2File gr2;
    const reluva::Error err = gr2.open(data);
    std::printf("format %u, type tag 0x%08x, %zu sections\n", gr2.version(), gr2.typeTag(), gr2.sections().size());
    static const char* const kCompression[] = {"none", "Oodle0", "Oodle1"};
    for (size_t i = 0; i < gr2.sections().size(); i++) {
        const awe::Gr2Section& s = gr2.sections()[i];
        std::printf("  %zu: %-6s %8u -> %8u bytes, align %2u, stops %u %u, %u fixups, %u marshalling\n", i,
                    s.compression < 3 ? kCompression[s.compression] : "?", s.compressedSize, s.size, s.alignment,
                    s.stop0, s.stop1, s.fixupCount, s.marshallingCount);
    }
    if (err) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    if (tree)
        std::fputs(gr2.dump(maxElements).c_str(), stdout);
    return 0;
}

// Opens a .gr2 file, and loads the granny2.dll found near it (for its
// textures and compressed curves).
bool openGr2(const std::string& file, awe::GrannyDll& dll, awe::Gr2File& gr2) {
    std::vector<uint8_t> data;
    if (!readFile(file, data))
        return false;
    if (!dll.loaded() && !loadGranny(file, "", dll))
        return false;
    const reluva::Error err = gr2.open(data);
    if (err) {
        std::fprintf(stderr, "%s: %s\n", file.c_str(), err.message.c_str());
        return false;
    }
    return true;
}

// Summarises a model; optionally poses it with an animation and writes
// it as Wavefront OBJ (with its textures as TGA files beside it).
int cmdGr2Model(int argc, char** argv) {
    std::string file, animFile, objFile, trackBone;
    float time = 0;
    for (int i = 2; i < argc; i++) {
        if (std::strcmp(argv[i], "-anim") == 0 && i + 2 < argc) {
            animFile = argv[++i];
            time = std::strtof(argv[++i], nullptr);
        } else if (std::strcmp(argv[i], "-obj") == 0 && i + 1 < argc) {
            objFile = argv[++i];
        } else if (std::strcmp(argv[i], "-track") == 0 && i + 1 < argc) {
            trackBone = argv[++i];
        } else {
            file = argv[i];
        }
    }
    if (file.empty()) {
        std::fprintf(stderr, "usage: reluva-tool gr2-model <model.gr2> [-anim <anim.gr2> <seconds>] [-obj out.obj] "
                             "[-track bone]\n");
        return 2;
    }
    awe::GrannyDll dll;
    awe::Gr2File gr2;
    if (!openGr2(file, dll, gr2))
        return 1;
    awe::Model model;
    if (reluva::Error e = awe::loadModel(gr2, model)) {
        std::fprintf(stderr, "%s\n", e.message.c_str());
        return 1;
    }
    std::printf("model \"%s\": %zu bones, %zu meshes, %zu textures, %g units per metre\n", model.name.c_str(),
                model.bones.size(), model.meshes.size(), model.textures.size(), model.unitsPerMeter);
    for (const awe::ModelMesh& m : model.meshes) {
        std::printf("  mesh %-24s %6zu vertices %6zu triangles, textures", m.name.c_str(), m.vertices.size(),
                    m.indices.size() / 3);
        for (const auto& g : m.groups)
            std::printf(" %d", g.texture);
        std::printf("\n");
    }
    for (size_t i = 0; i < model.textures.size(); i++) {
        const awe::ModelTexture& t = model.textures[i];
        std::printf("  texture %zu: %s %dx%d%s\n", i, t.name.c_str(), t.width, t.height,
                    t.rgba.empty() ? " (not decoded)" : "");
    }
    awe::Animation anim;
    awe::Gr2File animGr2;
    if (!animFile.empty()) {
        if (!openGr2(animFile, dll, animGr2))
            return 1;
        if (reluva::Error e = awe::loadAnimation(animGr2, anim)) {
            std::fprintf(stderr, "%s\n", e.message.c_str());
            return 1;
        }
        size_t matched = 0;
        for (const awe::AnimationTrack& t : anim.tracks)
            for (const awe::Bone& b : model.bones)
                matched += b.name == t.bone;
        std::printf("animation \"%s\": %.3f s (step %.4f), %zu tracks, %zu on the model's bones\n", anim.name.c_str(),
                    anim.duration, anim.timeStep, anim.tracks.size(), matched);
        if (!anim.mask.empty()) {
            // Its track mask: the bones it weighs, by weight.
            std::map<float, std::vector<std::string>> byWeight;
            for (const auto& [bone, w] : anim.mask)
                byWeight[w].push_back(bone);
            std::printf("track mask:");
            for (const auto& [w, bones] : byWeight)
                std::printf(" %g on %zu bones (%s%s);", w, bones.size(), bones[0].c_str(), bones.size() > 1 ? "..." : "");
            std::printf("\n");
        }
    }
    if (!trackBone.empty()) {
        // The bone's place in the model (its own axes) through the
        // animation, a line per time step.
        int bone = -1;
        for (size_t b = 0; b < model.bones.size(); b++)
            if (model.bones[b].name == trackBone)
                bone = int(b);
        if (bone < 0) {
            std::fprintf(stderr, "no bone %s\n", trackBone.c_str());
            return 1;
        }
        const float step = anim.timeStep > 0 ? anim.timeStep : 1 / 60.0f;
        awe::Pose pose;
        for (float t = 0; t <= anim.duration + step / 2; t += step) {
            awe::computePose(model, animFile.empty() ? nullptr : &anim, t, pose);
            const awe::Mat4& m = pose.world[size_t(bone)];
            std::printf("%.4f %g %g %g\n", t, m[12], m[13], m[14]);
        }
    }
    if (objFile.empty())
        return 0;
    awe::Pose pose;
    awe::computePose(model, animFile.empty() ? nullptr : &anim, time, pose);
    const std::filesystem::path obj = objFile;
    std::ofstream out(obj);
    std::ofstream mtl(std::filesystem::path(obj).replace_extension(".mtl"));
    out << "mtllib " << std::filesystem::path(obj).replace_extension(".mtl").filename().string() << "\n";
    for (size_t i = 0; i < model.textures.size(); i++) {
        const awe::ModelTexture& t = model.textures[i];
        const std::string tga = obj.stem().string() + "_tex" + std::to_string(i) + ".tga";
        mtl << "newmtl tex" << i << "\nKd 1 1 1\n";
        if (!t.rgba.empty()) {
            reluva::image::Image img;
            img.w = t.width;
            img.h = t.height;
            img.rgba = t.rgba;
            const std::vector<uint8_t> bytes = reluva::image::encodeTga(img);
            std::ofstream(obj.parent_path() / tga, std::ios::binary)
                .write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
            mtl << "map_Kd " << tga << "\n";
        }
    }
    size_t base = 1;
    std::vector<std::array<float, 3>> positions, normals;
    for (const awe::ModelMesh& m : model.meshes) {
        awe::skin(m, pose, positions, normals);
        out << "o " << m.name << "\n";
        for (size_t v = 0; v < m.vertices.size(); v++) {
            out << "v " << positions[v][0] << ' ' << positions[v][1] << ' ' << positions[v][2] << "\n";
            out << "vn " << normals[v][0] << ' ' << normals[v][1] << ' ' << normals[v][2] << "\n";
            out << "vt " << m.vertices[v].uv[0] << ' ' << 1 - m.vertices[v].uv[1] << "\n";
        }
        for (const auto& g : m.groups) {
            out << "usemtl tex" << g.texture << "\n";
            for (uint32_t i = g.firstIndex; i + 2 < g.firstIndex + g.indexCount; i += 3) {
                out << 'f';
                for (int k = 0; k < 3; k++) {
                    const size_t idx = base + m.indices[i + uint32_t(k)];
                    out << ' ' << idx << '/' << idx << '/' << idx;
                }
                out << "\n";
            }
        }
        base += m.vertices.size();
    }
    std::printf("wrote %s\n", objFile.c_str());
    return 0;
}

// Reads scene files and prints what they hold; with several files, only
// whether each reads to its end.
int cmdAc2Dump(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool ac2-dump <scene.ac2>...\n");
        return 2;
    }
    int failed = 0;
    for (int i = 2; i < argc; i++) {
        std::vector<uint8_t> data;
        if (!readFile(argv[i], data)) {
            failed++;
            continue;
        }
        awe::Ac2Scene scene;
        const reluva::Error err = awe::parseAc2(data, scene);
        if (argc == 3)
            std::fputs(awe::dumpAc2(scene).c_str(), stdout);
        else
            std::printf("%-60s %zu records%s%s\n", argv[i], scene.records.size(), err ? ": " : "",
                        err ? err.message.c_str() : "");
        if (err) {
            if (argc == 3)
                std::fprintf(stderr, "%s\n", err.message.c_str());
            failed++;
        }
    }
    return failed ? 1 : 0;
}

// Prints a text table (Text/*.txt) row by row, cells as the game's
// script commands where they parse as some.
int cmdAweTable(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: reluva-tool awe-table <file.txt>\n");
        return 2;
    }
    std::vector<uint8_t> data;
    if (!readFile(argv[2], data))
        return 1;
    const awe::Table t = awe::parseTable(awe::decodeText(data));
    std::printf("%zu columns, %zu rows\n", t.headings.size(), t.rows.size());
    for (size_t r = 0; r < t.rows.size(); r++) {
        std::printf("[%zu]\n", r);
        for (size_t c = 0; c < t.rows[r].size(); c++) {
            if (t.rows[r][c].empty())
                continue;
            std::printf("  %s: %s\n", c < t.headings.size() ? t.headings[c].c_str() : "?", t.rows[r][c].c_str());
            const std::vector<awe::Command> cmds = awe::parseCommands(t.rows[r][c]);
            if (cmds.size() > 1)
                for (const awe::Command& cmd : cmds) {
                    std::printf("    -");
                    for (const std::string& w : cmd.words)
                        std::printf(" [%s]", w.c_str());
                    std::printf("\n");
                }
        }
    }
    return 0;
}

// A System with no window: the last frame shown is kept, events come from
// a queue.
class HeadlessSystem : public reluva::System {
public:
    reluva::Surface last;
    std::deque<reluva::Event> events;
    reluva::Error initGraphics(int, int, reluva::PixelFormat) override { return reluva::Error::success(); }
    void setPalette(const reluva::Color*, int, int) override {}
    void updateScreen(const reluva::Surface& s) override { last = s; }
    void setWindowTitle(const std::string&) override {}
    bool pollEvent(reluva::Event& ev) override {
        if (events.empty())
            return false;
        ev = events.front();
        events.pop_front();
        return true;
    }
    uint64_t getMillis() const override { return now; }
    uint64_t now = 0;
    void delayMillis(uint32_t) override {}
    reluva::Error startAudio(AudioCallback) override { return reluva::Error::fail("no audio"); }
    void stopAudio() override {}
    int audioRate() const override { return 44100; }
};

// Shows a page of the engine's viewer without a window, presses keys on
// it, and saves the screen as a TGA file.
int cmdAweShot(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: reluva-tool awe-shot <game folder> <scenes|pictures|text|sounds|movies> <out.tga> "
                             "[key...]\n  keys: left right up down pgup pgdn home end esc enter f2 f3, or one character;\n"
                             "  savedir:<folder> for the game\'s saves; ms:N lets N milliseconds pass;\n"
                             "  move:x,y, click:x,y and release:x,y move, click and let go of the mouse\n");
        return 2;
    }
    reluva::DirectoryArchive files;
    if (reluva::Error e = files.open(argv[2])) {
        std::fprintf(stderr, "%s\n", e.message.c_str());
        return 1;
    }
    HeadlessSystem sys;
    reluva::Surface screen;
    screen.create(awe::kScreenW, awe::kScreenH, reluva::PixelFormat::RGBA32);
    awe::GameFonts fonts;
    const bool haveFonts = !fonts.load(files);
    // "savedir:<folder>" among the keys: where the game's saves go.
    std::filesystem::path saves;
    for (int i = 5; i < argc; i++)
        if (std::strncmp(argv[i], "savedir:", 8) == 0)
            saves = argv[i] + 8;
    awe::ViewerContext ctx{sys, files, nullptr, screen, saves, haveFonts ? &fonts : nullptr};
    std::unique_ptr<awe::Page> page;
    const std::string which = argv[3];
    if (which == "scenes")
        page = std::make_unique<awe::ScenePage>(ctx);
    else if (which == "pictures")
        page = std::make_unique<awe::PicturePage>(ctx);
    else if (which == "text")
        page = std::make_unique<awe::TextPage>(ctx);
    else if (which == "sounds")
        page = std::make_unique<awe::SoundPage>(ctx);
    else if (which == "movies")
        page = std::make_unique<awe::MoviePage>(ctx);
    else {
        std::fprintf(stderr, "unknown page %s\n", which.c_str());
        return 2;
    }
    std::printf("%zu items\n", page->count());
    page->activate();
    for (int i = 5; i < argc; i++) {
        const std::string k = argv[i];
        if (k.rfind("ms:", 0) == 0) {
            // Time passing, a frame each 10 ms.
            const uint64_t until = sys.now + std::strtoull(k.c_str() + 3, nullptr, 10);
            while (sys.now < until) {
                sys.now += 10;
                page->update(sys.now);
            }
            continue;
        }
        if (k.rfind("savedir:", 0) == 0)
            continue;
        reluva::Event ev;
        if (k.rfind("click:", 0) == 0 || k.rfind("move:", 0) == 0 || k.rfind("release:", 0) == 0) {
            // The mouse moved to, clicked at, or let go at x,y on the screen.
            const bool click = k[0] == 'c', release = k[0] == 'r';
            const char* at = k.c_str() + (click ? 6 : release ? 8 : 5);
            char* end = nullptr;
            ev.mouseX = int(std::strtol(at, &end, 10));
            ev.mouseY = end && *end == ',' ? int(std::strtol(end + 1, nullptr, 10)) : 0;
            ev.type = click     ? reluva::EventType::MouseDown
                      : release ? reluva::EventType::MouseUp
                                : reluva::EventType::MouseMove;
            ev.button = click || release ? reluva::MouseButton::Left : reluva::MouseButton::None;
            ev.clicks = click ? 1 : 0;
            page->handleEvent(ev);
            continue;
        }
        ev.type = reluva::EventType::KeyDown;
        namespace key = reluva::key;
        ev.key = k == "left" ? key::Left : k == "right" ? key::Right : k == "up" ? key::Up : k == "down" ? key::Down
               : k == "pgup" ? key::PageUp : k == "pgdn" ? key::PageDown : k == "home" ? key::Home
               : k == "end" ? key::End : k == "esc" ? key::Escape : k == "enter" ? key::Return
               : k == "f2" ? key::F1 + 1 : k == "f3" ? key::F1 + 2 : uint32_t(uint8_t(k[0]));
        page->handleEvent(ev);
    }
    page->update(sys.now);
    reluva::image::Image img;
    img.w = sys.last.w;
    img.h = sys.last.h;
    img.rgba = sys.last.pixels;
    const std::vector<uint8_t> tga = reluva::image::encodeTga(img);
    std::ofstream(argv[4], std::ios::binary).write(reinterpret_cast<const char*>(tga.data()), std::streamsize(tga.size()));
    return 0;
}

// Plays the acts without a player (the Scenes page's autoplay): each act
// started as "act <x>" starts it, or each developer's save loaded, then
// moved on till it settles (nothing running, nothing said) or sticks (an
// event that doesn't end). With -hotspots, every hotspot and character of
// the scene it settles in is then clicked, each from the act afresh. What
// the scripts ask that Reluva doesn't do, or can't find, is listed.
int cmdAweActs(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool awe-acts <game folder> [act|slotN...] [-hotspots] [-t seconds] [-v]\n");
        return 2;
    }
    reluva::DirectoryArchive files;
    if (reluva::Error e = files.open(argv[2])) {
        std::fprintf(stderr, "%s\n", e.message.c_str());
        return 1;
    }
    std::vector<std::string> targets;
    bool verbose = false, hotspots = false;
    double limit = 900;
    for (int i = 3; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "-v")
            verbose = true;
        else if (a == "-hotspots")
            hotspots = true;
        else if (a == "-t" && i + 1 < argc)
            limit = std::atof(argv[++i]);
        else
            targets.push_back(a);
    }
    if (targets.empty()) {
        std::vector<uint8_t> data;
        if (!files.read("text/acts.txt", data))
            for (const awe::ActRow& r : awe::readActs(awe::parseTable(awe::decodeText(data))))
                targets.push_back(r.act);
        for (int i = 0; i < 15; i++)
            targets.push_back("slot" + std::to_string(i));
    }
    HeadlessSystem sys;
    reluva::Surface screen;
    screen.create(awe::kScreenW, awe::kScreenH, reluva::PixelFormat::RGBA32);
    awe::ViewerContext ctx{sys, files, nullptr, screen, std::filesystem::temp_directory_path() / "reluva-awe-acts",
                           nullptr};
    int problems = 0;
    // Runs the game on till it settles; false if it sticks.
    auto settle = [&](awe::ScenePage& page, std::vector<std::string>& log, const std::string& what) {
        const uint64_t start = sys.now;
        uint64_t quietSince = 0, eventSince = sys.now;
        std::string lastEvent;
        while (sys.now - start < uint64_t(limit * 1000)) {
            sys.now += 40; // 25 frames a second
            page.update(sys.now);
            const std::string did = page.autoplay();
            if (!did.empty())
                log.push_back("  [player] " + did);
            else if (page.status().puzzle)
                return true; // a puzzle, a menu: for a player to do
            const awe::ScenePage::GameStatus st = page.status();
            if (st.event != lastEvent || st.idle)
                lastEvent = st.event, eventSince = sys.now;
            if (!st.idle && sys.now - eventSince > 120000) {
                log.push_back("  STUCK: " + st.event + " (in " + st.scene + ")");
                return false;
            }
            const bool quiet = st.idle && !st.talking && !st.movie && !st.loading && !st.fading;
            if (!quiet)
                quietSince = 0;
            else if (!quietSince)
                quietSince = sys.now;
            else if (sys.now - quietSince > 3000)
                return true;
        }
        log.push_back("  STUCK: still going after " + std::to_string(int(limit)) + " s (" + what + ")");
        return false;
    };
    auto isProblem = [](const std::string& l) {
        for (const char* p : {"not done yet", "(no scene", "(no saved game", "STUCK", "(no conversation", "(no event"})
            if (l.find(p) != std::string::npos)
                return true;
        return false;
    };
    auto report = [&](const std::vector<std::string>& log, const awe::ScenePage::GameStatus& st,
                      const std::string& what) {
        int n = 0;
        for (const std::string& l : log)
            if (verbose || isProblem(l)) {
                std::printf("%s\n", l.c_str());
                n += isProblem(l);
            }
        std::printf("%s: act %s, scene %s%s, %d problem(s)\n", what.c_str(), st.act.c_str(), st.scene.c_str(),
                    st.puzzle ? " (a puzzle or menu open)" : "", n);
        problems += n;
    };
    for (const std::string& target : targets) {
        awe::ScenePage page(ctx);
        std::vector<std::string> log;
        page.onLog = [&](const std::string& l) { log.push_back(l); };
        page.activate();
        page.startAt(target);
        settle(page, log, target);
        report(log, page.status(), target);
        if (!hotspots)
            continue;
        // Each a click, or an item it asks for used on it ("" a click).
        const std::vector<std::string> spots = page.clickables();
        std::vector<std::pair<size_t, std::string>> tries;
        for (size_t i = 0; i < spots.size(); i++) {
            tries.emplace_back(i, "");
            for (const std::string& item : page.itemsFor(i))
                tries.emplace_back(i, item);
        }
        for (const auto& [i, item] : tries) {
            awe::ScenePage again(ctx);
            std::vector<std::string> log2;
            again.onLog = [&](const std::string& l) { log2.push_back(l); };
            again.activate();
            again.startAt(target);
            settle(again, log2, target);
            log2.clear();
            if (item.empty() ? !again.click(i) : !again.useOn(item, i))
                continue;
            const std::string what = (item.empty() ? "click " : "use " + item + " on ") + spots[i];
            settle(again, log2, target + " " + what);
            report(log2, again.status(), "  " + target + " " + what);
        }
    }
    std::printf("%d problem(s)\n", problems);
    return problems ? 1 : 0;
}

// A scene's camera as granny2.dll builds it, and where the scene's
// characters (and any points given) land on the screen.
int cmdAweCamera(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool awe-camera <game folder> <scene.ac2> [x y z]...\n");
        return 2;
    }
    std::vector<uint8_t> data;
    awe::Ac2Scene scene;
    if (!readFile(argv[3], data) || awe::parseAc2(data, scene).ok == false) {
        std::fprintf(stderr, "can't read %s\n", argv[3]);
        return 1;
    }
    const awe::Ac2Record* cam = scene.camera();
    if (!cam) {
        std::fprintf(stderr, "no camera in %s\n", argv[3]);
        return 1;
    }
    awe::GrannyDll dll;
    if (!loadGranny(std::filesystem::path(argv[2]) / "x", "", dll))
        return 1;
    awe::GrannyDll::Camera c;
    c.fovY = cam->f("fov"), c.nearPlane = cam->f("near"), c.farPlane = cam->f("far");
    const char* const pos[3] = {"x", "y", "z"};
    const char* const ear[3] = {"pitch", "yaw", "roll"};
    const char* const off[3] = {"offsetX", "offsetY", "offsetZ"};
    for (int k = 0; k < 3; k++)
        c.position[k] = cam->f(pos[k]), c.elevAzimRoll[k] = cam->f(ear[k]), c.offset[k] = cam->f(off[k]);
    std::vector<float> points;
    std::vector<std::string> names;
    for (const awe::Ac2Record* o : scene.objects())
        if (o->tag == "NPCX" || o->tag == "PCXX")
            for (float h : {0.0f, 170.0f}) {
                points.insert(points.end(), {o->f("x"), o->f("y") + h, o->f("z")});
                names.push_back(o->text("file") + (h > 0 ? " +170" : " feet"));
            }
    for (int i = 4; i + 2 < argc; i += 3) {
        points.insert(points.end(), {std::strtof(argv[i], nullptr), std::strtof(argv[i + 1], nullptr),
                                     std::strtof(argv[i + 2], nullptr)});
        names.push_back("point");
    }
    float view[16], proj[16];
    std::vector<float> window;
    if (reluva::Error e = dll.camera(c, 1024, 768, view, proj, &points, &window)) {
        std::fprintf(stderr, "%s\n", e.message.c_str());
        return 1;
    }
    auto print = [](const char* name, const float* m) {
        std::printf("%s (columns):\n", name);
        for (int col = 0; col < 4; col++)
            std::printf("  %10.4f %10.4f %10.4f %10.4f\n", m[col * 4], m[col * 4 + 1], m[col * 4 + 2], m[col * 4 + 3]);
    };
    print("view", view);
    print("projection", proj);
    // Reluva's own camera, to compare.
    const awe::SceneCamera native = awe::SceneCamera::fromRecord(*cam);
    const reluva::gfx::Mat4 nv = native.view();
    float worst = 0;
    for (int k = 0; k < 16; k++)
        worst = std::max(worst, std::abs(nv[size_t(k)] - view[k]) / (k >= 12 ? 100.0f : 1.0f));
    std::printf("Reluva's view matrix differs by at most %g (translations / 100)\n", worst);
    std::printf("screen y from the top; DLL then Reluva:\n");
    for (size_t i = 0; i < names.size(); i++) {
        float x = 0, y = 0;
        native.project(&points[i * 3], 1024, 768, x, y);
        std::printf("%-50s (%7.1f %7.1f %7.1f) -> (%7.1f, %7.1f)  (%7.1f, %7.1f)\n", names[i].c_str(), points[i * 3],
                    points[i * 3 + 1], points[i * 3 + 2], window[i * 3], 768 - window[i * 3 + 1], x, y);
    }
    return worst < 0.01f ? 0 : 1;
}

// A Bink file's header, audio tracks and frames.
int cmdBinkInfo(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool bink-info <file.bik> [-frames] [-yuv out.yuv [frames]] [-tga first] [-wav out.wav]\n");
        return 2;
    }
    std::vector<uint8_t> data;
    if (!readFile(argv[2], data))
        return 1;
    reluva::video::BinkFile bink;
    if (reluva::Error e = bink.open(std::move(data))) {
        std::fprintf(stderr, "%s\n", e.message.c_str());
        return 1;
    }
    std::printf("Bink revision '%c': %dx%d, %zu frames at %u/%u (%.3f fps), flags 0x%08x%s%s, largest frame %u bytes\n",
                bink.revision(), bink.width(), bink.height(), bink.frameCount(), bink.fpsNumerator(),
                bink.fpsDenominator(), bink.fps(), bink.flags(), bink.hasAlpha() ? " (alpha)" : "",
                bink.grayscale() ? " (grey)" : "", bink.largestFrame());
    for (const reluva::video::BinkAudioTrack& t : bink.audioTracks())
        std::printf("  audio track %u: %d Hz, %d channel%s, %s, flags 0x%04x\n", t.id, t.sampleRate, t.channels,
                    t.channels == 1 ? "" : "s", t.dct ? "DCT" : "FFT", t.flags);
    bool list = false;
    std::string yuvFile, tgaPrefix, wavFile;
    size_t yuvFrames = bink.frameCount(), tgaFrame = 0;
    for (int i = 3; i < argc; i++) {
        if (std::strcmp(argv[i], "-frames") == 0)
            list = true;
        else if (std::strcmp(argv[i], "-yuv") == 0 && i + 1 < argc) {
            yuvFile = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-')
                yuvFrames = std::strtoul(argv[++i], nullptr, 10);
        } else if (std::strcmp(argv[i], "-wav") == 0 && i + 1 < argc) {
            wavFile = argv[++i];
        } else if (std::strcmp(argv[i], "-tga") == 0 && i + 1 < argc) {
            tgaFrame = std::strtoul(argv[++i], nullptr, 10);
            tgaPrefix = std::string(argv[2]) + "_frame";
        }
    }
    if (!wavFile.empty() && !bink.audioTracks().empty()) {
        // The first audio track, as a 16-bit WAV file.
        const reluva::video::BinkAudioTrack& t = bink.audioTracks()[0];
        reluva::video::BinkAudioDecoder dec;
        if (reluva::Error e = dec.init(t.sampleRate, t.channels, t.dct)) {
            std::fprintf(stderr, "%s\n", e.message.c_str());
            return 1;
        }
        std::vector<int16_t> pcm;
        for (size_t i = 0; i < bink.frameCount(); i++) {
            reluva::video::BinkFile::Frame f;
            bink.frame(i, f);
            if (!f.audio.empty() && f.audio[0].size)
                if (reluva::Error e = dec.decode(f.audio[0].data, f.audio[0].size, pcm, f.audioBytes[0]))
                    std::fprintf(stderr, "frame %zu: %s\n", i, e.message.c_str());
        }
        std::ofstream wav(wavFile, std::ios::binary);
        auto u32 = [&](uint32_t v) { wav.write(reinterpret_cast<const char*>(&v), 4); };
        auto u16 = [&](uint16_t v) { wav.write(reinterpret_cast<const char*>(&v), 2); };
        const uint32_t bytes = uint32_t(pcm.size() * 2);
        wav.write("RIFF", 4), u32(36 + bytes), wav.write("WAVEfmt ", 8), u32(16), u16(1), u16(uint16_t(t.channels));
        u32(uint32_t(t.sampleRate)), u32(uint32_t(t.sampleRate * t.channels * 2)), u16(uint16_t(t.channels * 2)), u16(16);
        wav.write("data", 4), u32(bytes);
        wav.write(reinterpret_cast<const char*>(pcm.data()), std::streamsize(bytes));
        std::printf("wrote %s: %zu samples a channel (%.2f s)\n", wavFile.c_str(), pcm.size() / size_t(t.channels),
                    double(pcm.size()) / t.channels / t.sampleRate);
    }
    if (!yuvFile.empty() || !tgaPrefix.empty()) {
        // Decodes the video: as raw YUV 4:2:0 (as ffmpeg -pix_fmt yuv420p
        // writes it, to compare), and one frame as a TGA picture.
        reluva::video::BinkVideoDecoder dec;
        if (reluva::Error e = dec.init(bink.width(), bink.height(), bink.revision(), bink.flags())) {
            std::fprintf(stderr, "%s\n", e.message.c_str());
            return 1;
        }
        std::ofstream yuv;
        if (!yuvFile.empty())
            yuv.open(yuvFile, std::ios::binary);
        const size_t last = std::max(yuvFile.empty() ? 0 : yuvFrames, tgaPrefix.empty() ? 0 : tgaFrame + 1);
        const auto start = std::chrono::steady_clock::now();
        for (size_t i = 0; i < last && i < bink.frameCount(); i++) {
            reluva::video::BinkFile::Frame f;
            bink.frame(i, f);
            if (reluva::Error e = dec.decode(f.video.data, f.video.size))
                std::fprintf(stderr, "frame %zu: %s\n", i, e.message.c_str());
            if (yuv && i < yuvFrames)
                for (int p = 0; p < 3; p++) {
                    const int w = p ? (bink.width() + 1) / 2 : bink.width(), h = p ? (bink.height() + 1) / 2 : bink.height();
                    for (int y = 0; y < h; y++)
                        yuv.write(reinterpret_cast<const char*>(dec.plane(p) + y * dec.stride(p)), w);
                }
            if (!tgaPrefix.empty() && i == tgaFrame) {
                reluva::image::Image img;
                img.w = bink.width();
                img.h = bink.height();
                dec.toRgba(img.rgba);
                const std::vector<uint8_t> tga = reluva::image::encodeTga(img);
                const std::string name = tgaPrefix + std::to_string(i) + ".tga";
                std::ofstream(name, std::ios::binary).write(reinterpret_cast<const char*>(tga.data()), std::streamsize(tga.size()));
                std::printf("wrote %s\n", name.c_str());
            }
        }
        std::printf("decoded %zu frames in %.2f s\n", last,
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    }
    size_t keyframes = 0, videoBytes = 0, audioBytes = 0, decoded = 0;
    for (size_t i = 0; i < bink.frameCount(); i++) {
        reluva::video::BinkFile::Frame f;
        if (reluva::Error e = bink.frame(i, f)) {
            std::fprintf(stderr, "%s\n", e.message.c_str());
            return 1;
        }
        keyframes += f.keyframe;
        videoBytes += f.video.size;
        for (size_t t = 0; t < f.audio.size(); t++)
            audioBytes += f.audio[t].size, decoded += f.audioBytes[t];
        if (list)
            std::printf("  frame %4zu%s: video %6zu bytes, audio %6zu bytes (%u bytes decoded)\n", i, f.keyframe ? " key" : "    ",
                        f.video.size, f.audio.empty() ? 0 : f.audio[0].size, f.audioBytes.empty() ? 0 : f.audioBytes[0]);
    }
    std::printf("%zu keyframes; video %zu bytes, audio %zu bytes (%zu bytes decoded)\n", keyframes, videoBytes, audioBytes,
                decoded);
    return 0;
}

// A saved game's records, and whether writing it back gives a file of the
// same size (the buffers' old memory aside, the same layout).
int cmdAweSave(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool awe-save <slot.ac2>... [-v]\n");
        return 2;
    }
    bool verbose = false;
    for (int i = 2; i < argc; i++)
        verbose |= std::strcmp(argv[i], "-v") == 0;
    int failed = 0;
    for (int i = 2; i < argc; i++) {
        if (std::strcmp(argv[i], "-v") == 0)
            continue;
        std::vector<uint8_t> data;
        awe::SaveGame save;
        if (!readFile(argv[i], data)) {
            std::printf("%s: can't read\n", argv[i]);
            failed++;
            continue;
        }
        if (reluva::Error e = awe::readSaveGame(data, save)) {
            std::printf("%s: %s\n", argv[i], e.message.c_str());
            failed++;
            continue;
        }
        const size_t again = awe::writeSaveGame(save).size();
        std::printf("%s: \"%s\" %s %s, act %02d%c, %s, %s; time %d weather %d moving %d; %zu tags, %zu timers, "
                    "%zu text tags, %zu positions, %zu talks, %zu items%s%s; written back %zu bytes of %zu\n",
                    argv[i], save.description.c_str(), save.date.c_str(), save.time.c_str(), save.actMajor,
                    char('a' + save.actMinor - 1), save.location.c_str(), save.scene.c_str(), save.timeOfDay,
                    save.weather, save.moving, save.tags.size(), save.timers.size(), save.texts.size(),
                    save.positions.size(), save.talks.size(), save.items.size(), save.player ? ", player" : "",
                    save.follower ? ", follower" : "", again, data.size());
        if (again != data.size())
            failed++;
        if (verbose) {
            for (const auto& [n, v] : save.tags)
                std::printf("  tag %s = %d\n", n.c_str(), v);
            for (const auto& [n, v] : save.timers)
                std::printf("  timer %s = %d\n", n.c_str(), v);
            for (const auto& [n, v] : save.texts)
                std::printf("  text %s = %s\n", n.c_str(), v.c_str());
            for (const auto& p : save.positions)
                std::printf("  position %s %s (%g, %g, %g) turned %g\n", p.scene.c_str(), p.character.c_str(),
                            p.at.position[0], p.at.position[1], p.at.position[2], p.at.rotation[1]);
            for (const auto& k : save.talks)
                std::printf("  talk %s %s part %d thread %d status %d nest %d\n", k.name.c_str(), k.act.c_str(),
                            k.part, k.thread, k.status, k.nest);
            for (const auto& it : save.items)
                std::printf("  item %s state %d slot %d page %d\n", it.name.c_str(), it.state, it.slot, it.page);
        }
    }
    return failed ? 1 : 0;
}

} // namespace

int aweToolCommand(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "awe-save") == 0)
        return cmdAweSave(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "gr2-info") == 0)
        return cmdGr2Info(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "bink-info") == 0)
        return cmdBinkInfo(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "awe-camera") == 0)
        return cmdAweCamera(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "awe-check") == 0)
        return aweCheckCommand(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "awe-shot") == 0)
        return cmdAweShot(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "awe-acts") == 0)
        return cmdAweActs(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "awe-table") == 0)
        return cmdAweTable(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "ac2-dump") == 0)
        return cmdAc2Dump(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "gr2-model") == 0)
        return cmdGr2Model(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "awe-path") == 0)
        return reluva::awe::aweCommandPath(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "awe-scripts") == 0)
        return reluva::awe::aweCommandScripts(argc, argv);
    return -1;
}

const char* aweToolUsage() {
    return "  reluva-tool awe-check <game folder> [-gr2|-ac2|-text|-bik|-oodle] [-only text] [-textures] [-j threads] [-v]\n"
           "  reluva-tool awe-camera <game folder> <scene.ac2> [x y z]...\n"
           "  reluva-tool bink-info <file.bik> [-frames]\n"
           "  reluva-tool gr2-info <file.gr2> [-t] [-n elements]\n"
           "  reluva-tool awe-table <file.txt>\n"
           "  reluva-tool awe-shot <game folder> <scenes|pictures|text|sounds|movies> <out.tga> [key...]\n"
           "  reluva-tool awe-acts <game folder> [act|slotN...] [-hotspots] [-t seconds] [-v]\n"
           "  reluva-tool ac2-dump <scene.ac2>...\n"
           "  reluva-tool awe-path <game folder> <scene.ac2> <a0> <b0> <a1> <b1> | -all [pairs]\n"
           "  reluva-tool awe-scripts <game folder> [-v]\n"
           "  reluva-tool awe-save <slot.ac2>... [-v]\n"
           "  reluva-tool gr2-model <model.gr2> [-anim <anim.gr2> <seconds>] [-obj out.obj] [-track bone]\n";
}
