// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/storm3d/scene/map_scene.h"
#include "graphics/gfx3d.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace reluva::storm3d {

/**
 * A mission script's cut scene (the game's "cineractive"): the camera its
 * shots set, as the program's camera manager runs it (0x661588): four
 * controllers, the eye, what it looks at, and an offset to each, each a
 * fixed point, a unit followed, or a curve along a path; the screen's fade.
 * See docs/engines/storm3d/games/armada/systems/mission-scripts.md.
 */
class Cineractive {
public:
    /** Where a unit is, by its handle, and which way it faces; none if it's gone. */
    struct Placed {
        gfx::Vec3 at{};
        gfx::Vec3 front{0, 0, 1};
    };
    using Locate = std::function<std::optional<Placed>(int32_t handle)>;

    /** One of the four (0x5c bytes in the program). */
    struct Controller {
        enum class Mode : uint8_t { kFixed = 1, kFollow = 2, kPath = 3 };
        Mode mode = Mode::kFixed;
        gfx::Vec3 point{};
        int32_t handle = 0;
        std::vector<gfx::Vec3> path;
        size_t first = 0;      // the path's point it starts at
        bool relative = false; // less the point before the first: an offset's
        float t = 0;           // segments along the path
        float speed = 0;       // segments a second
        gfx::Vec3 position{};
        gfx::Vec3 direction{0, 0, 1};
        bool done = true;
        /** Where it is now (0x4ee0b0); a path's as the program's curve has it (0x4ee520). */
        void evaluate(const Locate &locate);
    };

    /** Takes the camera and the interface for the script (0x4eba20); a skip's votes cleared. */
    void start();
    /** Gives them back (0x4ed020). */
    void end();
    /** Whether a cut scene holds the camera. */
    bool on() const { return _on; }

    /**
     * The shots (CineractiveShot's overloads): the eye and what it looks at,
     * each a unit followed, a path's point, or along a path at `speed`
     * segments a second. During a cut scene each puts the offsets back to
     * none (0x4ec6b0 and on).
     */
    ///@{
    void shotFollow(int32_t eye, int32_t look, const Locate &locate);
    void shotAt(const gfx::Vec3 &eye, int32_t look, const Locate &locate);
    void shotAt(const gfx::Vec3 &eye, const gfx::Vec3 &look, const Locate &locate);
    void shotAlong(const std::vector<gfx::Vec3> &eye, float speed, const gfx::Vec3 &look, const Locate &locate);
    void shotLookingAlong(const gfx::Vec3 &eye, const std::vector<gfx::Vec3> &look, float speed, const Locate &locate);
    /** Any other pairing: each a fixed point, a unit followed, or a path at its speed. */
    void shot(const Controller &eye, const Controller &look, const Locate &locate);
    ///@}
    /**
     * The eye's offset along a path, from its second point, less its first
     * (CineractiveCameraOffset, 0x4ebb00); turned into the eye's own axes
     * with `turned`. The target's offset (CineractiveTargetOffset) the same.
     */
    ///@{
    void cameraOffset(const std::vector<gfx::Vec3> &path, float speed, bool turned, const Locate &locate);
    void cameraOffset(const gfx::Vec3 &by, bool turned, const Locate &locate);
    void targetOffset(const std::vector<gfx::Vec3> &path, float speed, bool turned, const Locate &locate);
    void targetOffset(const gfx::Vec3 &by, bool turned, const Locate &locate);
    ///@}

    /** A frame's real `seconds` on (0x4ecec0): the controllers move, the camera's built. */
    void update(float seconds, const Locate &locate);
    /** Whether every controller is done: isCineractiveDone. */
    bool done() const;
    /** The camera it gives, while it holds it. */
    std::optional<MapScene::Camera> camera() const { return _on ? _camera : std::nullopt; }
    /** Space: the player votes to skip it; with one player, it's skipped (isCineractiveCancel). */
    void skip() { _skipped = true; }
    bool skipped() const { return _skipped; }

    /**
     * FadeOut and FadeIn: after `delay` seconds, the screen goes to the
     * colour (black, or white if `white`) over `seconds`, or from it; a
     * fade out stays until a fade in or none (0x439100, 0x495850).
     */
    void fade(bool out, float seconds, float delay, bool white);
    void removeFade() { _fade = Fade{}; }
    /** The colour over the screen now and how opaque, 0 to 1; none without a fade. */
    std::optional<std::pair<gfx::Vec3, float>> fadeOver() const;

    /** The look-along frame the program makes (0x5534f0): right, up, front; it never rolls. */
    static std::array<gfx::Vec3, 3> frameOf(const gfx::Vec3 &direction);

private:
    struct Fade {
        int mode = 0; // 1 out, 2 in
        bool white = false;
        float alpha = 0;
        float seconds = 0;
        float delay = 0;
    };
    void afterShot(const Locate &locate);
    void build();

    bool _on = false;
    bool _skipped = false;
    Controller _eye;
    Controller _look;
    Controller _eyeOffset;
    Controller _lookOffset;
    bool _turnEyeOffset = false;
    bool _turnLookOffset = false;
    std::optional<MapScene::Camera> _camera;
    Fade _fade;
};

} // namespace reluva::storm3d
