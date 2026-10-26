// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's commands for the AWE engine's games (Agatha Christie:
// Murder on the Orient Express): see docs/engines/awe.md.

#include "tools/awe_tool.h"

#include "engines/awe/gr2.h"
#include "engines/awe/granny_dll.h"
#include "engines/awe/model.h"
#include "common/image.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    std::string file, dllPath;
    unsigned maxElements = 4;
    bool tree = false;
    for (int i = 2; i < argc; i++) {
        if (std::strcmp(argv[i], "-dll") == 0 && i + 1 < argc)
            dllPath = argv[++i];
        else if (std::strcmp(argv[i], "-t") == 0)
            tree = true;
        else if (std::strcmp(argv[i], "-n") == 0 && i + 1 < argc)
            maxElements = unsigned(std::strtoul(argv[++i], nullptr, 10)), tree = true;
        else
            file = argv[i];
    }
    if (file.empty()) {
        std::fprintf(stderr, "usage: reluva-tool gr2-info <file.gr2> [-t] [-n elements] [-dll granny2.dll]\n");
        return 2;
    }
    std::vector<uint8_t> data;
    if (!readFile(file, data))
        return 1;
    awe::GrannyDll dll;
    awe::Gr2Decompressor decompress;
    if (loadGranny(file, dllPath, dll))
        decompress = [&dll](uint32_t format, const std::vector<uint8_t>& in, uint32_t stop0, uint32_t stop1,
                            std::vector<uint8_t>& out) { return dll.decompress(format, in, stop0, stop1, out); };
    awe::Gr2File gr2;
    const reluva::Error err = gr2.open(data, decompress);
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

// Opens a .gr2 file, decompressing it with the granny2.dll found near it.
bool openGr2(const std::string& file, awe::GrannyDll& dll, awe::Gr2File& gr2) {
    std::vector<uint8_t> data;
    if (!readFile(file, data))
        return false;
    if (!dll.loaded() && !loadGranny(file, "", dll))
        return false;
    const reluva::Error err = gr2.open(data, [&dll](uint32_t format, const std::vector<uint8_t>& in, uint32_t stop0,
                                                    uint32_t stop1, std::vector<uint8_t>& out) {
        return dll.decompress(format, in, stop0, stop1, out);
    });
    if (err) {
        std::fprintf(stderr, "%s: %s\n", file.c_str(), err.message.c_str());
        return false;
    }
    return true;
}

// Summarises a model; optionally poses it with an animation and writes
// it as Wavefront OBJ (with its textures as TGA files beside it).
int cmdGr2Model(int argc, char** argv) {
    std::string file, animFile, objFile;
    float time = 0;
    for (int i = 2; i < argc; i++) {
        if (std::strcmp(argv[i], "-anim") == 0 && i + 2 < argc) {
            animFile = argv[++i];
            time = std::strtof(argv[++i], nullptr);
        } else if (std::strcmp(argv[i], "-obj") == 0 && i + 1 < argc) {
            objFile = argv[++i];
        } else {
            file = argv[i];
        }
    }
    if (file.empty()) {
        std::fprintf(stderr, "usage: reluva-tool gr2-model <model.gr2> [-anim <anim.gr2> <seconds>] [-obj out.obj]\n");
        return 2;
    }
    awe::GrannyDll dll;
    awe::Gr2File gr2;
    if (!openGr2(file, dll, gr2))
        return 1;
    awe::Model model;
    if (reluva::Error e = awe::loadModel(gr2, &dll, model)) {
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

} // namespace

int aweToolCommand(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "gr2-info") == 0)
        return cmdGr2Info(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "gr2-model") == 0)
        return cmdGr2Model(argc, argv);
    return -1;
}

const char* aweToolUsage() {
    return "  reluva-tool gr2-info <file.gr2> [-t] [-n elements] [-dll granny2.dll]\n"
           "  reluva-tool gr2-model <model.gr2> [-anim <anim.gr2> <seconds>] [-obj out.obj]\n";
}
