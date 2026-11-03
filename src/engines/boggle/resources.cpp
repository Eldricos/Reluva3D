// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/resources.h"
#include "common/audio/wav.h"
#include "common/md5.h"

#include <algorithm>
#include <bit>

namespace reluva::boggle {
namespace {
std::string_view text(const std::vector<uint8_t>& data) {
    return data.empty() ? std::string_view{} : std::string_view(reinterpret_cast<const char*>(data.data()), data.size());
}
class Digest {
public:
    void integer(uint64_t n) {
        uint8_t bytes[8];
        for (size_t i = 0; i < 8; ++i) bytes[i] = uint8_t(n >> (i * 8));
        md5_.update(bytes, 8);
    }
    void real(double n) { integer(std::bit_cast<uint64_t>(n)); }
    void string(std::string_view s) { integer(s.size()); md5_.update(s.data(), s.size()); }
    void bytes(const std::vector<uint8_t>& b) { integer(b.size()); md5_.update(b.data(), b.size()); }
    std::string finish() { return md5_.hexDigest(); }
private:
    Md5 md5_;
};
bool fileIs(const std::string& path, std::string_view dir, std::string_view extension) {
    return path.starts_with(dir) && path.ends_with(extension);
}
}
Error Resources::palette(std::string_view name, Palette& out) const {
    out = {};
    std::vector<uint8_t> bytes;
    if (auto err = archive_.read(name, bytes)) return err;
    return readPalette(text(bytes), out);
}
Error Resources::diceSet(std::string_view name, DiceSet& out) const {
    out = {};
    std::vector<uint8_t> bytes;
    if (auto err = archive_.read(name, bytes)) return err;
    return readDiceSet(text(bytes), out);
}
Error Resources::mesh(std::string_view name, Mesh& out) const {
    out = {};
    std::vector<uint8_t> bytes;
    if (auto err = archive_.read(name, bytes)) return err;
    return readMesh(text(bytes), out);
}
Error Resources::language(std::string_view name, Language& out) const {
    out = {};
    std::vector<uint8_t> bytes;
    if (auto err = archive_.read(name, bytes)) return err;
    Language result;
    if (auto err = readPeResources(bytes, result.resources)) return err;
    if (result.resources.empty()) return Error::fail("Language DLL has no resources");
    if (auto err = readPeStrings(result.resources, result.strings)) return err;
    if (result.strings.empty()) return Error::fail("Language DLL has no string table");
    out = std::move(result);
    return Error::success();
}
Error Resources::image(std::string_view name, image::Image& out) const {
    out = {};
    std::vector<uint8_t> bytes;
    if (auto err = archive_.read(name, bytes)) return err;
    return image::decode(bytes, out);
}
Error Resources::sound(std::string_view name, audio::Sound& out) const {
    out = {};
    std::vector<uint8_t> bytes;
    if (auto err = archive_.read(name, bytes)) return err;
    return audio::decodeWav(bytes, out);
}
Error Resources::check(ResourceSummary& out) const {
    out = {};
    ResourceSummary result;
    Digest digest;
    auto names = archive_.list();
    std::sort(names.begin(), names.end(), [](const auto& a, const auto& b) {
        return normalizeArchiveName(a) < normalizeArchiveName(b);
    });
    for (const auto& name : names) {
        const auto path = normalizeArchiveName(name);
        Error err;
        if (fileIs(path, "palettes/", ".pal")) {
            Palette colors;
            err = palette(name, colors);
            if (!err) {
                digest.string(path);
                for (const auto& color : colors) { digest.integer(color.r); digest.integer(color.g); digest.integer(color.b); }
                ++result.palettes;
            }
        } else if (fileIs(path, "dicts/", ".dst")) {
            DiceSet set;
            err = diceSet(name, set);
            if (!err) {
                digest.string(path); digest.integer(set.side);
                for (const auto& die : set.dice) for (const auto& face : die) digest.string(face);
                ++result.diceSets; result.dice += set.dice.size();
            }
        } else if (fileIs(path, "scripts/", ".x")) {
            Mesh model;
            err = mesh(name, model);
            if (!err) {
                digest.string(path);
                auto vectors = [&]<size_t N>(const std::vector<std::array<double, N>>& list) {
                    digest.integer(list.size()); for (const auto& v : list) for (auto n : v) digest.real(n);
                };
                auto faces = [&](const std::vector<Mesh::Face>& list) {
                    digest.integer(list.size());
                    for (const auto& face : list) { digest.integer(face.size()); for (auto n : face) digest.integer(n); }
                };
                vectors(model.positions); vectors(model.normals); vectors(model.texcoords);
                faces(model.faces); faces(model.normalFaces);
                digest.integer(model.materials.size());
                for (const auto& m : model.materials) {
                    for (auto n : m.diffuse) digest.real(n);
                    digest.real(m.power);
                    for (auto n : m.specular) digest.real(n);
                    for (auto n : m.emissive) digest.real(n);
                }
                digest.integer(model.faceMaterials.size());
                for (auto n : model.faceMaterials) digest.integer(n);
                ++result.meshes; result.vertices += model.positions.size(); result.faces += model.faces.size();
            }
        } else if (fileIs(path, "language/", ".dll")) {
            Language data;
            err = language(name, data);
            if (!err) {
                digest.string(path);
                digest.integer(data.resources.size());
                for (const auto& r : data.resources) {
                    for (const auto& key : {r.type, r.name}) {
                        digest.integer(key.named); digest.integer(key.id); digest.integer(key.text.size());
                        for (char16_t c : key.text) digest.integer(c);
                    }
                    digest.integer(r.language); digest.integer(r.codePage); digest.bytes(r.data);
                }
                digest.integer(data.strings.size());
                for (const auto& s : data.strings) { digest.integer(s.id); digest.integer(s.language); digest.string(s.text); }
                ++result.languages; result.strings += data.strings.size();
            }
        } else if (fileIs(path, "bitmaps/", ".bmp") || fileIs(path, "textures/", ".bmp")) {
            image::Image picture;
            err = image(name, picture);
            if (!err) {
                digest.string(path); digest.integer(picture.w); digest.integer(picture.h); digest.bytes(picture.rgba);
                ++result.images;
            }
        } else if (fileIs(path, "waves/", ".wav")) {
            audio::Sound wave;
            err = sound(name, wave);
            if (!err) {
                digest.string(path); digest.integer(wave.rate); digest.integer(wave.channels); digest.integer(wave.samples.size());
                for (int16_t sample : wave.samples) digest.integer(uint16_t(sample));
                ++result.sounds;
            }
        }
        if (err) return Error::fail(name + ": " + err.message);
    }
    if (!result.images || !result.sounds || !result.palettes || !result.diceSets || !result.meshes || !result.languages)
        return Error::fail("Missing supported resources: need BMPs, WAVs, palettes, dice sets, meshes and language DLLs");
    result.digest = digest.finish();
    out = std::move(result);
    return Error::success();
}
} // namespace reluva::boggle
