// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Star Trek: Generations commands for reading all of a
// game's data, for the game-files test (tests/engines/stgenerations/
// gamedata.cmake): the parts of the game a folder holds, and every
// archive, picture, level, script, sound and movie read, each with a
// checksum of what was decoded.

#include "common/fs.h"
#include "common/stream.h"
#include "common/text.h"
#include "video/avi.h"
#include "engines/stgenerations/audio.h"
#include "engines/stgenerations/detection.h"
#include "engines/stgenerations/formats/bitmap.h"
#include "engines/stgenerations/formats/font.h"
#include "engines/stgenerations/formats/level.h"
#include "engines/stgenerations/formats/locations.h"
#include "engines/stgenerations/formats/overlay.h"
#include "engines/stgenerations/formats/resources.h"
#include "engines/stgenerations/formats/script.h"
#include "engines/stgenerations/formats/sprite.h"
#include "devtools/tool.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using namespace reluva;
using namespace reluva::stgenerations;

// FNV-1a, 64 bits: the same on every machine.
struct Sum {
    uint64_t h = 0xcbf29ce484222325ull;
    Sum &bytes(const void *p, size_t n) {
        const uint8_t *b = static_cast<const uint8_t *>(p);
        for (size_t i = 0; i < n; i++)
            h = (h ^ b[i]) * 0x100000001b3ull;
        return *this;
    }
    Sum &canvas(const Canvas &c) {
        for (int y = 0; y < c.h; y++)
            bytes(c.getBasePtr(0, y), size_t(c.w) * c.bytesPerPixel());
        return *this;
    }
    std::string hex() const { return text::format("%016llx", static_cast<unsigned long long>(h)); }
};

const char *kindName(GamePart::Kind k) {
    return k == GamePart::kInstall ? "install" : k == GamePart::kDisc1 ? "cd1" : "cd2";
}

// The game's folder (and its CDs': --cd1 <dir> --cd2 <dir>, when they
// aren't in it), its resources opened.
bool openGame(int argc, char **argv, ResourceManager &resources, const char *usage) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool stgenerations %s <game folder> [--cd1 <dir>] [--cd2 <dir>]\n", usage);
        return false;
    }
    std::filesystem::path cd1, cd2;
    for (int i = 3; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--cd1"))
            cd1 = argv[i + 1];
        else if (!std::strcmp(argv[i], "--cd2"))
            cd2 = argv[i + 1];
    }
    resources.addSearchPaths(argv[2], cd1, cd2);
    resources.openArchives();
    resources.scan();
    return true;
}

// Each command lists what it can't read, with a summary line, for the
// game-files test's record; a copy of the game with damaged files is still
// read through. They fail only when the game's folder can't be opened.

// Every member of every archive, and every loose file, by name.
std::vector<ResourceRef> everyFile(const ResourceManager &resources) {
    std::vector<ResourceRef> out;
    for (const auto &[name, zip] : resources.getArchives())
        for (const ZipArchive::Entry &e : zip->entries())
            if (!e.name.empty() && e.name.back() != '/')
                out.push_back({name, e.name});
    for (const std::filesystem::path &dir : resources.getDataDirs()) {
        DirectoryArchive files;
        if (files.open(dir, 1))
            continue;
        std::vector<std::string> names = files.list();
        std::sort(names.begin(), names.end(), [](const std::string &a, const std::string &b) {
            return normalizeArchiveName(a) < normalizeArchiveName(b);
        });
        for (const std::string &n : names)
            out.push_back({"", n});
    }
    return out;
}

bool hasExtension(const ResourceRef &r, const char *ext) {
    return text::hasSuffixIgnoreCase(r.name, ext);
}

// The parts of the game in a folder: the install and each CD, as an import
// or the launcher finds them.
int cmdParts(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool stgenerations parts <folder>\n");
        return 2;
    }
    const std::filesystem::path root = argv[2];
    for (const GamePart &p : findGameParts(root))
        std::printf("%s: %s\n", kindName(p.kind), p.dir.lexically_relative(root).generic_string().c_str());
    return 0;
}

// Every archive opened, and every file in it read (each checked against
// its CRC).
int cmdArchives(int argc, char **argv) {
    ResourceManager resources;
    if (!openGame(argc, argv, resources, "archives"))
        return 2;
    int read = 0, failed = 0, damaged = 0;
    for (const auto &[name, zip] : resources.getArchives()) {
        Sum sum;
        int n = 0;
        for (const ZipArchive::Entry &e : zip->entries()) {
            if (e.name.empty() || e.name.back() == '/')
                continue;
            std::vector<uint8_t> data;
            bool crcOk = true;
            if (zip->readUnchecked(e, data, crcOk)) {
                std::printf("%s:%s: FAILED\n", name.c_str(), e.name.c_str());
                ++failed;
                continue;
            }
            if (!crcOk) {
                // Read as the game reads it.
                std::printf("%s:%s: damaged (its CRC doesn't match)\n", name.c_str(), e.name.c_str());
                ++damaged;
            }
            sum.bytes(e.name.data(), e.name.size()).bytes(data.data(), data.size());
            ++n;
        }
        read += n;
        std::printf("%-12s %4d files %s\n", name.c_str(), n, sum.hex().c_str());
    }
    for (const std::string &missing : resources.missingDataFiles())
        std::printf("missing: %s\n", missing.c_str());
    std::printf("%d images, %d sounds found\n", (int)resources.getImages().size(), (int)resources.getSounds().size());
    std::printf("%zu archives, %d files read (%d of them damaged), %d unreadable\n", resources.getArchives().size(),
                read, damaged, failed);
    return 0;
}

// Every picture decoded: BMPs, overlays, sprite sets, fonts.
int cmdPictures(int argc, char **argv) {
    ResourceManager resources;
    if (!openGame(argc, argv, resources, "pictures"))
        return 2;
    int counts[4] = {}, failed = 0, unreachable = 0;
    for (const ResourceRef &ref : everyFile(resources)) {
        const int kind = hasExtension(ref, ".bmp")   ? 0
                         : hasExtension(ref, ".ovr") ? 1
                         : hasExtension(ref, ".gs2") ? 2
                         : hasExtension(ref, ".fnt") ? 3
                                                     : -1;
        if (kind < 0)
            continue;
        std::unique_ptr<SeekableReadStream> s = resources.open(ref);
        // Loose files in a data folder's own folders are listed (as the
        // resource viewer lists them) but never found by name, by the
        // game or the original: the game's data is in its archives.
        if (!s) {
            ++unreachable;
            continue;
        }
        std::string line = ref.toString() + ": ";
        bool ok = true;
        if (ok && kind == 0) {
            BitmapDecoder b;
            ok = b.loadStream(*s);
            if (ok)
                line += text::format("%dx%d %s %s", b.getSurface().w, b.getSurface().h,
                                     b.hasPalette() ? "8-bit" : "true colour",
                                     Sum().canvas(b.getSurface()).bytes(b.getPalette().data(), 768).hex().c_str());
        } else if (ok && kind == 1) {
            Overlay o;
            ok = o.load(*s);
            if (ok)
                line += text::format("%d wide, %d rows", o.getWidth(), o.getRowCount());
        } else if (ok && kind == 2) {
            SpriteSet set;
            ok = set.load(*s);
            if (ok)
                line +=
                    text::format("%d frames, up to %dx%d", set.getFrameCount(), set.getMaxWidth(), set.getMaxHeight());
        } else if (ok && kind == 3) {
            Font f;
            ok = f.load(*s);
            if (ok)
                line += text::format("%d glyphs, %d high", f.getGlyphCount(), f.getHeight());
        }
        if (!ok) {
            line += "FAILED";
            ++failed;
        } else {
            ++counts[kind];
        }
        std::printf("%s\n", line.c_str());
    }
    std::printf("%d bitmaps, %d overlays, %d sprite sets, %d fonts read, %d failed; %d loose in subfolders, not read\n",
                counts[0], counts[1], counts[2], counts[3], failed, unreachable);
    return 0;
}

// Every away mission's level, its sky and textures, and its script.
int cmdLevels(int argc, char **argv) {
    ResourceManager resources;
    if (!openGame(argc, argv, resources, "levels"))
        return 2;
    int levels = 0, failed = 0;
    for (const Location &location : getLocations()) {
        ResourceRef ref;
        if (!findLocationLevel(resources, location, ref)) {
            std::printf("%s: not installed\n", location.folder);
            continue;
        }
        std::unique_ptr<SeekableReadStream> s = resources.open(ref);
        LevelFile level;
        if (!s || !level.load(*s)) {
            std::printf("%s: FAILED\n", ref.toString().c_str());
            ++failed;
            continue;
        }
        Sum sum;
        int textures = 0, badTextures = 0;
        for (uint16_t id : level.getTextureIds()) {
            LevelFile::Texture t;
            if (level.loadTexture(id, t)) {
                sum.canvas(t.surface);
                ++textures;
            } else {
                ++badTextures;
            }
        }
        Canvas sky;
        const bool hasSky = level.loadSky(sky);
        if (hasSky)
            sum.canvas(sky);
        // Its script, beside it.
        const std::string folder = ref.name.substr(0, ref.name.find_last_of("/\\") + 1);
        std::unique_ptr<SeekableReadStream> bin = resources.open({ref.archive, folder + "GAME.BIN"});
        ScriptFile script;
        const bool scriptOk = bin && script.load(*bin);
        std::printf("%s: %d sectors, %d actors, %d textures (%d unread), %s, script %s: %d objects, %d words; %s\n",
                    ref.toString().c_str(), (int)level.getSectors().size(), (int)level.getActors().size(), textures,
                    badTextures, hasSky ? "a sky" : "no sky", scriptOk ? "read" : "FAILED",
                    scriptOk ? (int)script.getObjects().size() : 0, scriptOk ? (int)script.getCode().size() : 0,
                    sum.hex().c_str());
        if (!scriptOk || badTextures)
            ++failed;
        else
            ++levels;
    }
    std::printf("%d levels read, %d failed\n", levels, failed);
    return 0;
}

// Every sound decoded.
int cmdSounds(int argc, char **argv) {
    ResourceManager resources;
    if (!openGame(argc, argv, resources, "sounds"))
        return 2;
    int read = 0, failed = 0;
    uint64_t ms = 0;
    Sum all;
    for (const ResourceRef &ref : resources.getSounds()) {
        std::unique_ptr<SoundStream> s = resources.loadSound(ref);
        if (!s) {
            std::printf("%s: FAILED\n", ref.toString().c_str());
            ++failed;
            continue;
        }
        const audio::Sound &sound = s->sound();
        all.bytes(sound.samples.data(), sound.samples.size() * 2);
        ms += s->lengthMs();
        ++read;
    }
    std::printf("%d sounds read (%llu s), %d failed: %s\n", read, static_cast<unsigned long long>(ms / 1000), failed,
                all.hex().c_str());
    return 0;
}

// Every movie: each frame decoded, and its sound.
int cmdMovies(int argc, char **argv) {
    ResourceManager resources;
    if (!openGame(argc, argv, resources, "movies"))
        return 2;
    int read = 0, failed = 0;
    for (const DataFileLocation *f = dataFiles(); f->name; f++) {
        if (!text::hasSuffixIgnoreCase(f->name, ".avi"))
            continue;
        std::unique_ptr<SeekableReadStream> s = resources.open({"", f->name});
        avi::Decoder movie;
        if (!s) {
            std::printf("%s: not found\n", f->name);
            continue;
        }
        if (!movie.load(std::move(s)) || !movie.canDecode()) {
            std::printf("%s: FAILED\n", f->name);
            ++failed;
            continue;
        }
        Sum sum;
        int frames = 0;
        while (movie.decodeNextFrame()) {
            sum.bytes(movie.surface().pixels.data(), movie.surface().pixels.size());
            ++frames;
        }
        std::printf("%s: %dx%d, %d of %d frames, %d Hz %d-bit %s, %zu bytes of sound; %s\n", f->name, movie.width(),
                    movie.height(), frames, movie.frameCount(), movie.audioRate(), movie.audioBits(),
                    movie.audioChannels() == 2 ? "stereo" : "mono", movie.audio().size(), sum.hex().c_str());
        if (frames != movie.frameCount())
            ++failed;
        else
            ++read;
    }
    std::printf("%d movies read, %d failed\n", read, failed);
    return 0;
}

const tool::Commands commands(
    "stgenerations",
    {
        {"parts", "<folder>", "the parts of the game a folder holds: the installed game and each CD", cmdParts},
        {"archives", "<game folder> [--cd1 <dir>] [--cd2 <dir>]",
         "every data archive opened and every file in it read, with a checksum of each archive", cmdArchives},
        {"pictures", "<game folder> [--cd1 <dir>] [--cd2 <dir>]",
         "every bitmap, overlay, sprite set and font decoded, each with what it holds", cmdPictures},
        {"levels", "<game folder> [--cd1 <dir>] [--cd2 <dir>]",
         "every away mission's level, sky, textures and script read", cmdLevels},
        {"sounds", "<game folder> [--cd1 <dir>] [--cd2 <dir>]", "every sound decoded, with a checksum of them all",
         cmdSounds},
        {"movies", "<game folder> [--cd1 <dir>] [--cd2 <dir>]", "every movie's frames and sound decoded", cmdMovies},
    });

} // namespace
