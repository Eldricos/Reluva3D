// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/play/cineractive.h"

#include <algorithm>
#include <cmath>

namespace reluva::storm3d {

namespace {

gfx::Vec3 plus(const gfx::Vec3 &a, const gfx::Vec3 &b) {
    return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}

gfx::Vec3 minus(const gfx::Vec3 &a, const gfx::Vec3 &b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

gfx::Vec3 times(const gfx::Vec3 &a, float k) {
    return {a[0] * k, a[1] * k, a[2] * k};
}

float lengthOf(const gfx::Vec3 &a) {
    return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
}

gfx::Vec3 normalised(const gfx::Vec3 &a) {
    const float n = lengthOf(a);
    return n > 0 ? times(a, 1 / n) : a;
}

// An offset in a controller's own axes, made world's.
gfx::Vec3 turnedBy(const gfx::Vec3 &offset, const gfx::Vec3 &direction) {
    const std::array<gfx::Vec3, 3> axes = Cineractive::frameOf(direction);
    return plus(plus(times(axes[0], offset[0]), times(axes[1], offset[1])), times(axes[2], offset[2]));
}

} // namespace

std::array<gfx::Vec3, 3> Cineractive::frameOf(const gfx::Vec3 &direction) {
    // Right level, unless looking almost straight up or down; up from the
    // two, so the camera never rolls.
    const gfx::Vec3 front = normalised(direction);
    const gfx::Vec3 right = front[0] * front[0] + front[2] * front[2] < 0.02f
                                ? gfx::Vec3{1, 0, 0}
                                : normalised(gfx::Vec3{front[2], 0, -front[0]});
    const gfx::Vec3 up{front[1] * right[2] - front[2] * right[1], front[2] * right[0] - front[0] * right[2],
                       front[0] * right[1] - front[1] * right[0]};
    return {right, up, front};
}

void Cineractive::Controller::evaluate(const Locate &locate) {
    done = true;
    switch (mode) {
    case Mode::kFixed:
        position = point;
        direction = {0, 0, 1};
        return;
    case Mode::kFollow:
        // Its origin and heading, each frame, without lag; a unit gone
        // leaves them as they were.
        if (auto placed = locate ? locate(handle) : std::nullopt) {
            position = placed->at;
            direction = placed->front;
        }
        return;
    case Mode::kPath:
        break;
    }
    const size_t n = path.size();
    if (n == 0)
        return;
    if (n <= first) {
        position = path[n - 1];
        return;
    }
    const float u = float(first) + t;
    const auto i = size_t(std::max(0.0f, std::floor(u)));
    const float f = u - std::floor(u);
    if (u < float(first)) {
        position = path[first];
        done = false;
    } else if (i >= n - 1) {
        // Stopped at its last point, done.
        position = path[n - 1];
    } else {
        // A Hermite curve through every point, each segment a whole second
        // at speed 1 whatever its length; the ends' neighbours mirrored.
        done = false;
        const gfx::Vec3 &p1 = path[i];
        const gfx::Vec3 &p2 = path[i + 1];
        const gfx::Vec3 p0 = i == first ? minus(times(p1, 2), p2) : path[i - 1];
        const gfx::Vec3 p3 = i == n - 2 ? minus(times(p2, 2), p1) : path[i + 2];
        const float span = lengthOf(minus(p2, p1));
        const gfx::Vec3 m1 = times(normalised(minus(p2, p0)), span);
        const gfx::Vec3 m2 = times(normalised(minus(p3, p1)), span);
        const float f2 = f * f;
        const float f3 = f2 * f;
        position = plus(plus(times(p1, 2 * f3 - 3 * f2 + 1), times(m1, f3 - 2 * f2 + f)),
                        plus(times(m2, f3 - f2), times(p2, -2 * f3 + 3 * f2)));
        direction = plus(plus(times(p1, 6 * f2 - 6 * f), times(m1, 3 * f2 - 4 * f + 1)),
                         plus(times(m2, 3 * f2 - 2 * f), times(p2, 6 * f - 6 * f2)));
    }
    if (relative && first >= 1)
        position = minus(position, path[first - 1]);
}

void Cineractive::start() {
    if (!_on) {
        _skipped = false;
        _on = true;
    }
}

void Cineractive::end() {
    _on = false;
    _camera.reset();
}

void Cineractive::afterShot(const Locate &locate) {
    _eye.t = 0;
    _look.t = 0;
    _eye.evaluate(locate);
    _look.evaluate(locate);
    if (!_on)
        return;
    // A shot puts both offsets back to none; an offset is set after it.
    _eyeOffset = Controller{};
    _lookOffset = Controller{};
    _turnEyeOffset = false;
    _turnLookOffset = false;
    build();
}

void Cineractive::shotFollow(int32_t eye, int32_t look, const Locate &locate) {
    if (!locate || !locate(eye) || !locate(look))
        return;
    _eye = Controller{Controller::Mode::kFollow};
    _eye.handle = eye;
    _look = Controller{Controller::Mode::kFollow};
    _look.handle = look;
    afterShot(locate);
}

void Cineractive::shotAt(const gfx::Vec3 &eye, int32_t look, const Locate &locate) {
    if (!locate || !locate(look))
        return;
    _eye = Controller{Controller::Mode::kFixed, eye};
    _look = Controller{Controller::Mode::kFollow};
    _look.handle = look;
    afterShot(locate);
}

void Cineractive::shotAt(const gfx::Vec3 &eye, const gfx::Vec3 &look, const Locate &locate) {
    _eye = Controller{Controller::Mode::kFixed, eye};
    _look = Controller{Controller::Mode::kFixed, look};
    afterShot(locate);
}

void Cineractive::shotAlong(const std::vector<gfx::Vec3> &eye, float speed, const gfx::Vec3 &look,
                            const Locate &locate) {
    _eye = Controller{Controller::Mode::kPath};
    _eye.path = eye;
    _eye.speed = speed;
    _look = Controller{Controller::Mode::kFixed, look};
    afterShot(locate);
}

void Cineractive::shotLookingAlong(const gfx::Vec3 &eye, const std::vector<gfx::Vec3> &look, float speed,
                                   const Locate &locate) {
    _eye = Controller{Controller::Mode::kFixed, eye};
    _look = Controller{Controller::Mode::kPath};
    _look.path = look;
    _look.speed = speed;
    afterShot(locate);
}

void Cineractive::shot(const Controller &eye, const Controller &look, const Locate &locate) {
    _eye = eye;
    _look = look;
    afterShot(locate);
}

void Cineractive::cameraOffset(const std::vector<gfx::Vec3> &path, float speed, bool turned, const Locate &locate) {
    _eyeOffset = Controller{Controller::Mode::kPath};
    _eyeOffset.path = path;
    _eyeOffset.first = 1;
    _eyeOffset.relative = true;
    _eyeOffset.speed = speed;
    _eyeOffset.evaluate(locate);
    _turnEyeOffset = turned;
}

void Cineractive::cameraOffset(const gfx::Vec3 &by, bool turned, const Locate &locate) {
    _eyeOffset = Controller{Controller::Mode::kFixed, by};
    _eyeOffset.evaluate(locate);
    _turnEyeOffset = turned;
}

void Cineractive::targetOffset(const std::vector<gfx::Vec3> &path, float speed, bool turned, const Locate &locate) {
    _lookOffset = Controller{Controller::Mode::kPath};
    _lookOffset.path = path;
    _lookOffset.first = 1;
    _lookOffset.relative = true;
    _lookOffset.speed = speed;
    _lookOffset.evaluate(locate);
    _turnLookOffset = turned;
}

void Cineractive::targetOffset(const gfx::Vec3 &by, bool turned, const Locate &locate) {
    _lookOffset = Controller{Controller::Mode::kFixed, by};
    _lookOffset.evaluate(locate);
    _turnLookOffset = turned;
}

void Cineractive::update(float seconds, const Locate &locate) {
    if (!_on)
        return;
    for (Controller *c : {&_eye, &_look, &_eyeOffset, &_lookOffset}) {
        c->t += c->speed * seconds;
        c->evaluate(locate);
    }
    build();
    // The fade's wait, then its change over its seconds (0x4957e0).
    if (_fade.mode != 0) {
        if (_fade.delay > 0) {
            _fade.delay = std::max(0.0f, _fade.delay - seconds);
        } else {
            const float step = _fade.seconds > 0 ? seconds / _fade.seconds : 1;
            _fade.alpha += _fade.mode == 1 ? step : -step;
            if (_fade.alpha < 0)
                _fade = Fade{};
            _fade.alpha = std::min(_fade.alpha, 1.0f);
        }
    }
}

void Cineractive::build() {
    const gfx::Vec3 eyeOffset = _turnEyeOffset ? turnedBy(_eyeOffset.position, _eye.direction) : _eyeOffset.position;
    const gfx::Vec3 lookOffset =
        _turnLookOffset ? turnedBy(_lookOffset.position, _look.direction) : _lookOffset.position;
    const gfx::Vec3 eye = plus(_eye.position, eyeOffset);
    const gfx::Vec3 direction = minus(plus(_look.position, lookOffset), eye);
    // Looking nowhere, the camera stays as it was.
    if (std::abs(direction[0]) + std::abs(direction[1]) + std::abs(direction[2]) <= 0.0001f)
        return;
    const std::array<gfx::Vec3, 3> axes = frameOf(direction);
    MapScene::Camera camera;
    camera.eye = eye;
    camera.forward = axes[2];
    camera.up = axes[1];
    _camera = camera;
}

bool Cineractive::done() const {
    return _eye.done && _look.done && _eyeOffset.done && _lookOffset.done;
}

void Cineractive::fade(bool out, float seconds, float delay, bool white) {
    _fade.mode = out ? 1 : 2;
    _fade.alpha = out ? 0 : 1;
    _fade.seconds = seconds;
    _fade.delay = delay;
    _fade.white = white;
}

std::optional<std::pair<gfx::Vec3, float>> Cineractive::fadeOver() const {
    if (!_on || _fade.mode == 0)
        return std::nullopt;
    return std::pair{_fade.white ? gfx::Vec3{1, 1, 1} : gfx::Vec3{0, 0, 0}, _fade.alpha};
}

} // namespace reluva::storm3d
