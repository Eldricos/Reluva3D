// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool awe-check: reads every Granny file, scene and text file of an
// install, the way the engine does, and reports what fails or looks
// unusual. The regression test for anyone with the game (no game data is
// committed).

#include "tools/awe_tool.h"

#include "common/archive.h"
#include "common/fs.h"
#include "common/video/bink_audio.h"
#include "common/video/bink_file.h"
#include "common/video/bink_video.h"
#include "engines/awe/ac2.h"
#include "engines/awe/camera.h"
#include "engines/awe/gr2.h"
#include "engines/awe/granny_dll.h"
#include "engines/awe/model.h"
#include "engines/awe/tables.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace awe = reluva::awe;

bool endsWith(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// What one Granny file turned out to be.
struct Gr2Result {
    std::string error;           // empty if it read
    std::vector<std::string> warnings;
    bool model = false, animation = false;
    size_t bones = 0, meshes = 0, textures = 0, tracks = 0;
    std::string vertexLayout;    // the first mesh's vertex members
    std::set<int> degrees;       // curve degrees used
    std::set<std::string> compressions;
    uint32_t vectorTracks = 0;
    bool compressedCurves = false; // expanded by the DLL
};

std::string layoutOf(const awe::Gr2Object& vertexData) {
    const awe::Gr2Object::Array a = vertexData.array("Vertices");
    if (!a.type)
        return "";
    const awe::Gr2Object first = vertexData.element("Vertices", 0);
    std::string s;
    for (const awe::Gr2Member& m : first.members())
        s += (s.empty() ? "" : " ") + m.name + ":" + awe::gr2TypeName(m.type) +
             (m.arrayWidth > 1 ? "[" + std::to_string(m.arrayWidth) + "]" : "");
    return s;
}

// Checks one curve as loaded (plain, or expanded by the DLL from a
// compressed format), as Curve::evaluate will read it.
void checkCurve(const awe::Curve& c, const char* name, int dimension, Gr2Result& r, const std::string& trackName) {
    if (c.knots.empty())
        return;
    r.degrees.insert(c.degree);
    if (c.degree < 0 || c.degree > 3)
        r.warnings.push_back(trackName + " " + name + ": degree " + std::to_string(c.degree));
    if (c.controls.size() != c.knots.size() * size_t(dimension))
        r.warnings.push_back(trackName + " " + name + ": " + std::to_string(c.knots.size()) + " knots but " +
                             std::to_string(c.controls.size()) + " controls (expected " +
                             std::to_string(c.knots.size() * size_t(dimension)) + ")");
    for (size_t i = 1; i < c.knots.size(); i++)
        if (c.knots[i] < c.knots[i - 1]) {
            r.warnings.push_back(trackName + " " + name + ": knots go backwards at " + std::to_string(i));
            break;
        }
    for (float v : c.controls)
        if (!std::isfinite(v)) {
            r.warnings.push_back(trackName + " " + name + ": a control isn't a number");
            break;
        }
}

Gr2Result checkGr2(const reluva::Archive& files, const std::string& name, awe::GrannyDll& dll, bool textures) {
    Gr2Result r;
    std::vector<uint8_t> data;
    if (reluva::Error e = files.read(name, data)) {
        r.error = e.message;
        return r;
    }
    awe::Gr2File file;
    if (reluva::Error e = file.open(data, [&dll](uint32_t format, const std::vector<uint8_t>& in, uint32_t stop0,
                                                 uint32_t stop1, std::vector<uint8_t>& out) {
            return dll.decompress(format, in, stop0, stop1, out);
        })) {
        r.error = e.message;
        return r;
    }
    static const char* const kCompression[] = {"none", "Oodle0", "Oodle1"};
    for (const awe::Gr2Section& s : file.sections())
        if (s.size)
            r.compressions.insert(s.compression < 3 ? kCompression[s.compression]
                                                    : "format " + std::to_string(s.compression));
    const awe::Gr2Object root = file.root();
    if (!root.valid() || root.members().empty()) {
        r.error = "no root object";
        return r;
    }

    if (root.count("Models") > 0) {
        awe::Model model;
        if (reluva::Error e = awe::loadModel(file, textures ? &dll : nullptr, model)) {
            r.error = "model: " + e.message;
            return r;
        }
        r.model = !model.meshes.empty();
        r.bones = model.bones.size();
        r.meshes = model.meshes.size();
        r.textures = model.textures.size();
        for (size_t b = 0; b < model.bones.size(); b++)
            if (model.bones[b].parent >= int(b))
                r.warnings.push_back("bone " + model.bones[b].name + " has a later parent");
        for (const awe::ModelMesh& m : model.meshes) {
            if (m.indices.size() % 3)
                r.warnings.push_back("mesh " + m.name + ": " + std::to_string(m.indices.size()) + " indices");
            for (const awe::ModelVertex& v : m.vertices) {
                float total = 0;
                for (int k = 0; k < 4; k++) {
                    total += v.weights[k];
                    if (v.weights[k] > 0 && v.bones[k] >= model.bones.size()) {
                        r.warnings.push_back("mesh " + m.name + ": a vertex's bone out of range");
                        break;
                    }
                }
                if (std::abs(total - 1) > 0.02f) {
                    r.warnings.push_back("mesh " + m.name + ": weights add up to " + std::to_string(total));
                    break;
                }
            }
        }
        for (const awe::ModelTexture& t : model.textures)
            if (textures && t.rgba.empty())
                r.warnings.push_back("texture " + t.name + " not decoded");
        // The raw data, for what loadModel might have glossed over.
        const awe::Gr2Object first = root.element("Models", 0);
        for (uint32_t i = 0; i < first.count("MeshBindings"); i++) {
            const awe::Gr2Object mesh = first.element("MeshBindings", i).object("Mesh");
            const awe::Gr2Object vd = mesh.object("PrimaryVertexData");
            if (r.vertexLayout.empty())
                r.vertexLayout = layoutOf(vd);
            const uint32_t n = vd.count("Vertices");
            const awe::Gr2Object topo = mesh.object("PrimaryTopology");
            for (const char* list : {"Indices", "Indices16"})
                for (uint32_t k = 0; k < topo.count(list); k++) {
                    const awe::Gr2Object e = topo.element(list, k);
                    const double index = e.number(std::strcmp(list, "Indices") == 0 ? "Int32" : "UInt16");
                    if (index < 0 || index >= n) {
                        r.warnings.push_back("mesh " + mesh.string("Name") + ": index " + std::to_string(index) +
                                             " past its " + std::to_string(n) + " vertices");
                        break;
                    }
                }
            if (mesh.count("MorphTargets") > 0)
                r.warnings.push_back("mesh " + mesh.string("Name") + ": " + std::to_string(mesh.count("MorphTargets")) +
                                     " morph targets (not drawn)");
        }
    }

    if (root.count("Animations") > 0) {
        awe::Animation anim;
        if (reluva::Error e = awe::loadAnimation(file, anim, &dll)) {
            r.error = "animation: " + e.message;
            return r;
        }
        r.animation = true;
        r.tracks = anim.tracks.size();
        if (anim.duration <= 0)
            r.warnings.push_back("duration " + std::to_string(anim.duration));
        if (root.count("Animations") > 1)
            r.warnings.push_back(std::to_string(root.count("Animations")) + " animations (only the first is read)");
        // Vector tracks are the exporter's leftovers ("Displacement Max
        // Triangles", "value", "TrackMasktrackmask"): counted, not read.
        const awe::Gr2Object a = root.element("Animations", 0);
        for (uint32_t g = 0; g < a.count("TrackGroups"); g++)
            r.vectorTracks += a.element("TrackGroups", g).count("VectorTracks");
        for (const awe::Gr2Object& track : {a.element("TrackGroups", 0)})
            for (uint32_t t = 0; t < track.count("TransformTracks"); t++)
                if (!track.element("TransformTracks", t).object("PositionCurve").member("Knots"))
                    r.compressedCurves = true;
        for (const awe::AnimationTrack& t : anim.tracks) {
            checkCurve(t.position, "PositionCurve", 3, r, t.bone);
            checkCurve(t.orientation, "OrientationCurve", 4, r, t.bone);
            checkCurve(t.scaleShear, "ScaleShearCurve", 9, r, t.bone);
        }
    }
    if (!r.model && !r.animation && r.error.empty())
        r.warnings.push_back("neither a model with meshes nor an animation");
    return r;
}

std::vector<std::string> filesEndingWith(const reluva::Archive& files, const char* ext) {
    std::vector<std::string> out;
    for (const std::string& n : files.list())
        if (endsWith(reluva::fs::toLower(n), ext))
            out.push_back(n);
    std::sort(out.begin(), out.end());
    return out;
}

int checkGr2Files(const reluva::Archive& files, const std::filesystem::path& folder, const std::string& filter,
                  bool textures, unsigned jobs, bool verbose) {
    std::vector<std::string> names = filesEndingWith(files, ".gr2");
    if (!filter.empty())
        names.erase(std::remove_if(names.begin(), names.end(),
                                   [&](const std::string& n) {
                                       return reluva::fs::toLower(n).find(reluva::fs::toLower(filter)) ==
                                              std::string::npos;
                                   }),
                    names.end());
    std::printf("== %zu Granny files\n", names.size());
    if (names.empty())
        return 0;
    std::vector<uint8_t> dllData;
    const auto dllPath = reluva::fs::findFile(folder, "granny2.dll");
    if (!dllPath || files.read("granny2.dll", dllData)) {
        std::printf("FAIL granny2.dll not found in %s\n", folder.string().c_str());
        return 1;
    }

    std::vector<Gr2Result> results(names.size());
    std::atomic<size_t> next{0}, done{0};
    std::mutex mutex;
    std::string startError;
    const auto start = std::chrono::steady_clock::now();
    auto worker = [&] {
        awe::GrannyDll dll;
        if (reluva::Error e = dll.load(dllData)) {
            std::lock_guard<std::mutex> lock(mutex);
            startError = e.message;
            return;
        }
        for (size_t i; (i = next++) < names.size();) {
            results[i] = checkGr2(files, names[i], dll, textures);
            const size_t d = ++done;
            if (d % 100 == 0) {
                std::lock_guard<std::mutex> lock(mutex);
                std::fprintf(stderr, "  %zu/%zu...\n", d, names.size());
            }
        }
    };
    std::vector<std::thread> threads;
    for (unsigned j = 0; j < std::max(1u, jobs); j++)
        threads.emplace_back(worker);
    for (std::thread& t : threads)
        t.join();
    if (!startError.empty()) {
        std::printf("FAIL granny2.dll: %s\n", startError.c_str());
        return 1;
    }

    size_t failed = 0, warned = 0, models = 0, animations = 0, compressed = 0, withVectors = 0;
    std::map<std::string, size_t> layouts, compressions;
    std::map<int, size_t> degrees;
    for (size_t i = 0; i < names.size(); i++) {
        const Gr2Result& r = results[i];
        if (!r.error.empty()) {
            failed++;
            std::printf("FAIL %s: %s\n", names[i].c_str(), r.error.c_str());
            continue;
        }
        models += r.model;
        animations += r.animation;
        compressed += r.compressedCurves;
        withVectors += r.vectorTracks > 0;
        if (!r.vertexLayout.empty())
            layouts[r.vertexLayout]++;
        for (int d : r.degrees)
            degrees[d]++;
        for (const std::string& c : r.compressions)
            compressions[c]++;
        if (!r.warnings.empty()) {
            warned++;
            // The first few of a file's warnings (an animation can repeat one
            // for every track).
            for (size_t w = 0; w < r.warnings.size() && (verbose || w < 3); w++)
                std::printf("WARN %s: %s\n", names[i].c_str(), r.warnings[w].c_str());
            if (!verbose && r.warnings.size() > 3)
                std::printf("WARN %s: ... %zu more (-v shows them)\n", names[i].c_str(), r.warnings.size() - 3);
        } else if (verbose) {
            std::printf("ok   %s: %s%s, %zu bones, %zu meshes, %zu textures, %zu tracks\n", names[i].c_str(),
                        r.model ? "model" : "", r.animation ? (r.model ? " + animation" : "animation") : "", r.bones,
                        r.meshes, r.textures, r.tracks);
        }
    }
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("Granny: %zu files, %zu failed, %zu with warnings; %zu models, %zu animations (%.1f s)\n",
                names.size(), failed, warned, models, animations, seconds);
    std::printf("  %zu animations with compressed curves (expanded by granny2.dll), %zu with vector tracks (not used)\n",
                compressed, withVectors);
    std::printf("  section compression:");
    for (const auto& [c, n] : compressions)
        std::printf(" %s in %zu files", c.c_str(), n);
    std::printf("\n  curve degrees:");
    for (const auto& [d, n] : degrees)
        std::printf(" %d in %zu files", d, n);
    std::printf("\n  vertex layouts:\n");
    for (const auto& [l, n] : layouts)
        std::printf("    %5zu  %s\n", n, l.c_str());
    return failed ? 1 : 0;
}

// The scene's camera as Reluva builds it, against granny2.dll's: the
// largest difference in their view matrices (translations / 100).
float cameraDifference(const awe::Ac2Record& camr, awe::GrannyDll& dll, std::string& error) {
    const awe::SceneCamera native = awe::SceneCamera::fromRecord(camr);
    awe::GrannyDll::Camera c;
    c.fovY = native.fovY, c.nearPlane = native.nearPlane, c.farPlane = native.farPlane;
    for (int k = 0; k < 3; k++)
        c.position[k] = native.position[k], c.elevAzimRoll[k] = native.elevAzimRoll[k], c.offset[k] = native.offset[k];
    float view[16], projection[16];
    if (reluva::Error e = dll.camera(c, 1024, 768, view, projection)) {
        error = e.message;
        return 0;
    }
    const reluva::gfx::Mat4 v = native.view();
    float worst = 0;
    for (int k = 0; k < 16; k++)
        worst = std::max(worst, std::abs(v[size_t(k)] - view[k]) / (k >= 12 ? 100.0f : 1.0f));
    return worst;
}

int checkScenes(const reluva::Archive& files, bool verbose) {
    const std::vector<std::string> names = filesEndingWith(files, ".ac2");
    size_t failed = 0, scenes = 0, saves = 0, cameras = 0, cameraMismatches = 0;
    awe::GrannyDll dll;
    std::vector<uint8_t> dllData;
    const bool haveDll = !files.read("granny2.dll", dllData) && !dll.load(dllData);
    std::map<std::string, size_t> tags, parts;
    for (const std::string& n : names) {
        // Text/Scripts' slots are saves, with records of their own.
        if (reluva::fs::toLower(n).rfind("text/scripts/", 0) == 0) {
            saves++;
            continue;
        }
        scenes++;
        std::vector<uint8_t> data;
        awe::Ac2Scene scene;
        reluva::Error e = files.read(n, data);
        if (!e)
            e = awe::parseAc2(data, scene);
        if (!e && (scene.records.empty() || scene.records.back().tag != "DONE"))
            e = reluva::Error::fail("ends without DONE");
        if (e) {
            failed++;
            std::printf("FAIL %s: %s\n", n.c_str(), e.message.c_str());
        } else if (verbose) {
            std::printf("ok   %s: %zu records\n", n.c_str(), scene.records.size());
        }
        if (const awe::Ac2Record* camr = scene.camera(); camr && haveDll) {
            cameras++;
            std::string error;
            const float d = cameraDifference(*camr, dll, error);
            if (!error.empty() || d > 0.01f) {
                cameraMismatches++;
                std::printf("WARN %s: camera differs from granny2.dll's by %g%s%s\n", n.c_str(), d,
                            error.empty() ? "" : ": ", error.c_str());
            }
        }
        for (const awe::Ac2Record& r : scene.records) {
            tags[r.tag]++;
            for (const awe::Ac2Record& p : r.parts)
                parts[r.tag + "/" + p.tag]++;
        }
    }
    std::printf("Scenes: %zu files, %zu failed (%zu saves in Text/Scripts not checked)\n", scenes, failed, saves);
    if (haveDll)
        std::printf("  cameras: %zu checked against granny2.dll, %zu differ\n", cameras, cameraMismatches);
    std::printf("  records:");
    for (const auto& [t, n] : tags)
        std::printf(" %s %zu", t.c_str(), n);
    std::printf("\n  parts:");
    for (const auto& [t, n] : parts)
        std::printf(" %s %zu", t.c_str(), n);
    std::printf("\n");
    return failed ? 1 : 0;
}

int checkText(const reluva::Archive& files, bool verbose) {
    size_t count = 0, failed = 0;
    for (const std::string& n : filesEndingWith(files, ".txt")) {
        if (reluva::fs::toLower(n).rfind("text/", 0) != 0)
            continue;
        count++;
        std::vector<uint8_t> data;
        if (reluva::Error e = files.read(n, data)) {
            failed++;
            std::printf("FAIL %s: %s\n", n.c_str(), e.message.c_str());
            continue;
        }
        const std::string text = awe::decodeText(data);
        // U+FFFD: a broken surrogate pair, or not UTF-16 after all.
        if (text.find("\xef\xbf\xbd") != std::string::npos) {
            failed++;
            std::printf("FAIL %s: undecodable characters\n", n.c_str());
        } else if (verbose) {
            const awe::Table t = awe::parseTable(text);
            std::printf("ok   %s: %zu columns, %zu rows\n", n.c_str(), t.headings.size(), t.rows.size());
        }
    }
    std::printf("Text: %zu files, %zu failed\n", count, failed);
    return failed ? 1 : 0;
}

// Decodes every frame and the sound of every Bink file.
int checkBink(const reluva::Archive& files, unsigned jobs, bool verbose) {
    const std::vector<std::string> names = filesEndingWith(files, ".bik");
    std::vector<std::string> results(names.size());
    std::atomic<size_t> next{0};
    std::atomic<uint64_t> frames{0};
    const auto start = std::chrono::steady_clock::now();
    auto worker = [&] {
        for (size_t i; (i = next++) < names.size();) {
            std::vector<uint8_t> data;
            reluva::video::BinkFile bink;
            reluva::video::BinkVideoDecoder video;
            reluva::Error e = files.read(names[i], data);
            if (!e)
                e = bink.open(std::move(data));
            if (!e)
                e = video.init(bink.width(), bink.height(), bink.revision(), bink.flags());
            std::vector<reluva::video::BinkAudioDecoder> audio(bink.audioTracks().size());
            for (size_t t = 0; t < audio.size() && !e; t++)
                if (reluva::Error ae = audio[t].init(bink.audioTracks()[t].sampleRate, bink.audioTracks()[t].channels,
                                                     bink.audioTracks()[t].dct))
                    e = reluva::Error::fail("audio track " + std::to_string(t) + ": " + ae.message);
            std::vector<int16_t> pcm;
            for (size_t f = 0; f < bink.frameCount() && !e; f++) {
                reluva::video::BinkFile::Frame fr;
                e = bink.frame(f, fr);
                if (!e)
                    e = video.decode(fr.video.data, fr.video.size);
                for (size_t t = 0; t < audio.size() && !e; t++)
                    if (fr.audio[t].size) {
                        pcm.clear();
                        e = audio[t].decode(fr.audio[t].data, fr.audio[t].size, pcm);
                        // At least what the frame says (the last packet's
                        // block runs past the end, and is cut there).
                        if (!e && pcm.size() * 2 < fr.audioBytes[t])
                            e = reluva::Error::fail("audio decoded to " + std::to_string(pcm.size() * 2) +
                                                    " bytes, not " + std::to_string(fr.audioBytes[t]));
                    }
                if (e)
                    e = reluva::Error::fail("frame " + std::to_string(f) + ": " + e.message);
                else
                    frames++;
            }
            char buf[200];
            std::snprintf(buf, sizeof buf, "%dx%d, %zu frames, revision %c", bink.width(), bink.height(),
                          bink.frameCount(), bink.revision() ? bink.revision() : '?');
            results[i] = e ? "FAIL " + names[i] + ": " + e.message : (verbose ? "ok   " + names[i] + ": " + buf : "");
        }
    };
    std::vector<std::thread> threads;
    for (unsigned j = 0; j < std::max(1u, jobs); j++)
        threads.emplace_back(worker);
    for (std::thread& t : threads)
        t.join();
    size_t failed = 0;
    for (const std::string& r : results) {
        failed += r.rfind("FAIL", 0) == 0;
        if (!r.empty())
            std::printf("%s\n", r.c_str());
    }
    std::printf("Bink: %zu files, %zu failed, %llu frames decoded (%.1f s)\n", names.size(), failed,
                (unsigned long long)frames.load(),
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    return failed ? 1 : 0;
}

} // namespace

int aweCheckCommand(int argc, char** argv) {
    std::string folder, filter;
    bool gr2 = true, scenes = true, text = true, bink = true, textures = false, verbose = false;
    unsigned jobs = std::max(1u, std::thread::hardware_concurrency());
    for (int i = 2; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "-gr2" || a == "-ac2" || a == "-text" || a == "-bik") {
            gr2 = a == "-gr2", scenes = a == "-ac2", text = a == "-text", bink = a == "-bik";
        } else if (a == "-textures") {
            textures = true;
        } else if (a == "-v") {
            verbose = true;
        } else if (a == "-j" && i + 1 < argc) {
            jobs = unsigned(std::strtoul(argv[++i], nullptr, 10));
        } else if (a == "-only" && i + 1 < argc) {
            filter = argv[++i];
        } else {
            folder = a;
        }
    }
    if (folder.empty()) {
        std::fprintf(stderr, "usage: reluva-tool awe-check <game folder> [-gr2|-ac2|-text|-bik] [-only text] [-textures] "
                             "[-j threads] [-v]\n");
        return 2;
    }
    reluva::DirectoryArchive files;
    if (reluva::Error e = files.open(folder)) {
        std::fprintf(stderr, "%s\n", e.message.c_str());
        return 1;
    }
    int result = 0;
    if (text)
        result |= checkText(files, verbose);
    if (scenes)
        result |= checkScenes(files, verbose);
    if (gr2)
        result |= checkGr2Files(files, folder, filter, textures, jobs, verbose);
    if (bink)
        result |= checkBink(files, jobs, verbose);
    std::printf(result ? "Some files failed.\n" : "Everything read.\n");
    return result;
}
