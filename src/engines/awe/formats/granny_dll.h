// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "emulation/x86/cpu.h"
#include "emulation/x86/win32.h"

#include <cstdint>
#include <array>
#include <memory>
#include <string_view>
#include <vector>

namespace reluva::awe {

/**
 * The game's own granny2.dll, run in the x86 interpreter, for what its
 * files need that Reluva doesn't do natively yet: their Bink-compressed
 * textures and compressed curves. Its section decompression is kept to
 * check Reluva's own (oodle1.h). Only a few of its exports are called,
 * with the arguments its public header documents.
 */
class GrannyDll {
public:
    /** Loads and starts the DLL (its bytes). */
    Error load(const std::vector<uint8_t> &dll);
    /** Whether load() succeeded. */
    bool loaded() const { return _module != nullptr; }

    /**
     * Decompresses a section's `in` into `out` (sized to the decompressed
     * size) with GrannyDecompressData, to check the native decoder. The
     * data is in three parts, ending at `stop0`, `stop1` and out.size()
     * (Gr2Section).
     */
    Error decompress(uint32_t format, const std::vector<uint8_t> &in, uint32_t stop0, uint32_t stop1,
                     std::vector<uint8_t> &out);
    /**
     * Decodes a texture stored with Bink encoding (Encoding 3) into RGBA
     * pixels, rows top to bottom. `flags` is the texture's SubFormat, plus
     * 1 if its layout has alpha. Reluva decodes these natively
     * (bink_texture.h); this is kept to check that against.
     */
    Error binkTexture(int width, int height, uint32_t flags, const std::vector<uint8_t> &in,
                      std::vector<uint8_t> &rgba);

    /**
     * A granny_camera as the game sets one up from a scene's CAMR record:
     * vertical field of view (radians), near and far planes, the point it
     * looks at, its elevation, azimuth and roll (radians), and its offset
     * from that point.
     */
    struct Camera {
        float fovY = 0.785f;
        float nearPlane = 4;
        float farPlane = 1000;
        float position[3] = {0, 0, 0};
        float elevAzimRoll[3] = {0, 0, 0};
        float offset[3] = {0, 0, 0};
    };
    /**
     * A camera's view and projection matrices (Granny's, column-major, as
     * gfx::Mat4) for a width x height window, and optionally where
     * `points` (x, y, z each) land in the window (`windowPoints`: x, y in
     * pixels, z depth).
     */
    Error camera(const Camera &in, int width, int height, float view[16], float projection[16],
                 const std::vector<float> *points = nullptr, std::vector<float> *windowPoints = nullptr);

    /**
     * A file loaded by the DLL itself (GrannyReadEntireFileFromMemory),
     * to check Reluva's own expansion of its compressed curves against:
     * addresses in the DLL's memory.
     */
    struct LoadedFile {
        uint32_t file = 0;  // the granny_file
        uint32_t info = 0;  // its granny_file_info, laid out as the file's own types describe
        uint32_t bytes = 0; // the file's bytes, which it reads from
    };
    /** Loads a .gr2 file's bytes into the DLL; on failure nothing is left loaded. */
    Error loadFile(const std::vector<uint8_t> &gr2, LoadedFile &out);
    /** Frees a loaded file, and empties `file`. */
    void freeFile(LoadedFile &file);
    /** 32 bits of the DLL's memory; 0 before load(). */
    uint32_t read32(uint32_t address) const { return _cpu ? _cpu->read32(address) : 0; }
    /**
     * A loaded file's structures are the DLL's own types (it converts the
     * older exporters' files): type() gives one it exports by name
     * ("GrannyFileInfoType": a pointer to its member definitions), 0 if
     * none. memberOffset() finds a member in it (false if there's none),
     * typeSize() its size.
     */
    ///@{
    uint32_t type(const char *name) const;
    uint32_t typeSize(uint32_t type) const;
    bool memberOffset(uint32_t type, std::string_view name, uint32_t &offset) const;
    ///@}

    /** A curve's knots and its controls (knots x dimension values). */
    struct PlainCurve {
        int degree = 0;
        int dimension = 0;
        std::vector<float> knots;
        std::vector<float> controls;
    };
    /**
     * Expands the curve (a granny_curve2) at `address` in a loaded file to
     * its knots and controls, with GrannyCurveExtractKnotValues; an
     * identity or constant curve gives `identity` (or its constant) at one
     * knot.
     */
    Error expandCurve(uint32_t address, const std::vector<float> &identity, PlainCurve &out);
    /**
     * Evaluates the curve at `address` in a loaded file at time `t`
     * (GrannyEvaluateCurveAtT) into `out` (`identity.size()` values, 16 at
     * most): with `loopBack` or `loopForward`, as a looping animation
     * `duration` long, wrapping round at its start or its end; `normalize`
     * makes a quaternion unit length. The reference Curve::evaluate is
     * checked against (reluva-tool awe files-check -curves).
     */
    Error evaluateCurve(uint32_t address, const std::vector<float> &identity, bool normalize, bool loopBack,
                        bool loopForward, float duration, float t, std::vector<float> &out);

    /**
     * The world pose of a loaded model file's first model playing a
     * loaded animation file's first animation, `t` seconds in, as Granny
     * makes it (GrannyInstantiateModel, GrannyPlayControlledAnimation,
     * GrannySetModelClock, GrannySampleModelAnimations, GrannyBuildWorldPose):
     * each bone's 4x4 world matrix, column-major, no offset.
     */
    Error worldPose(const LoadedFile &model, const LoadedFile &animation, float t,
                    std::vector<std::array<float, 16>> &out);

private:
    std::unique_ptr<x86::Cpu> _cpu;
    std::unique_ptr<x86::Win32Process> _process;
    const x86::Win32Process::Module *_module = nullptr;
    uint32_t _decompressData = 0;
    uint32_t _binkDecompressTexture = 0;
    uint32_t _curveScratch = 0; // evaluateCurve's identity and result
};

} // namespace reluva::awe
