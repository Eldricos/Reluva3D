// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Scenes page's options: the game's settings (Music, Sound, Gamma,
// Captions, Shadows, Water, Fog, Rain, AntiAlias), what they change, and
// the options screen (Menu.Options's PUZZ options: 0x42bd40). See
// docs/engines/awe/games/motoe/systems/options.md.

#include "engines/awe/pages/viewers.h"

#include "common/fs.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace reluva::awe {

namespace {

// The settings' names, as the game's registry values, in the order of the
// screen's controls 1..9 (options1..9); and their defaults (0x42bae0).
constexpr const char *kNames[GameOptions::kCount] = {"Music", "Sound", "Gamma", "Captions", "Shadows",
                                                     "Water", "Fog",   "Rain",  "AntiAlias"};
constexpr int kDefaults[GameOptions::kCount] = {7, 7, 5, 1, 1, 1, 1, 1, 0};

// The sliders' thumbs: x = 694 + 23 * value (0..10).
constexpr int kThumbLeft = 694;
constexpr int kThumbStep = 23;

} // namespace

GameOptions::GameOptions() {
    std::copy(std::begin(kDefaults), std::end(kDefaults), std::begin(values));
}

std::filesystem::path ScenePage::optionsFile() const {
    return _ctx.savePath.empty() ? std::filesystem::path() : _ctx.savePath / "options.ini";
}

void ScenePage::loadOptions() {
    // Reluva keeps them in the game's folder as Name=value lines (the game
    // keeps them in the registry: HKCU\Software\DreamCatcher\Agatha
    // Christie\...\Settings).
    _options = GameOptions();
    std::ifstream in(optionsFile());
    for (std::string line; std::getline(in, line);) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        for (int i = 0; i < GameOptions::kCount; i++)
            if (fs::toLower(line.substr(0, eq)) == fs::toLower(kNames[i]))
                _options.values[i] = std::atoi(line.c_str() + eq + 1);
    }
    applyVolumes(_options.get(GameOptions::kMusic), _options.get(GameOptions::kSound));
}

void ScenePage::saveOptions() const {
    if (optionsFile().empty())
        return;
    std::error_code ec;
    std::filesystem::create_directories(optionsFile().parent_path(), ec);
    std::ofstream out(optionsFile());
    for (int i = 0; i < GameOptions::kCount; i++)
        out << kNames[i] << '=' << _options.values[i] << '\n';
}

void ScenePage::applyVolumes(int music, int sound) {
    // Sounds whose path holds "music" go by Music, all the rest (speech
    // too) by Sound; tenths of full (0x4254b0).
    if (!_ctx.mixer)
        return;
    _ctx.mixer->setTypeVolume(audio::SoundType::kMusic, std::clamp(music, 0, 10) * audio::kMaxVolume / 10);
    for (audio::SoundType t : {audio::SoundType::kEffects, audio::SoundType::kSpeech})
        _ctx.mixer->setTypeVolume(t, std::clamp(sound, 0, 10) * audio::kMaxVolume / 10);
}

int ScenePage::brightnessLevel() const {
    // On the options screen, the brightness being tried.
    return _optionsMenu ? _optionsMenu->staged[2] : _options.get(GameOptions::kGamma);
}

void ScenePage::applyBrightness() {
    // Gamma g: a linear ramp of slope 32g + 97 over 257 (5 leaves colours
    // as they are), clipped (0x402040): the pictures here, the characters
    // as they're drawn.
    applyBrightness(_ctx.screen.pixels);
}

void ScenePage::applyBrightness(std::vector<uint8_t> &rgba) const {
    const int g = brightnessLevel();
    if (g == 5)
        return;
    const int slope = 32 * g + 97;
    for (size_t i = 0; i + 3 < rgba.size(); i += 4)
        for (size_t c = 0; c < 3; c++)
            rgba[i + c] = uint8_t(std::min(255, rgba[i + c] * slope / 257));
}

void ScenePage::applyAntiAliasing() {
    // 0x490060: 4x if the device has it, else 2x, else none; the game
    // resets its device to change it, Reluva makes its frame again.
    if (_renderer)
        _renderer->setMultisampling(_options.get(GameOptions::kAntiAlias) ? 4 : 1);
}

bool ScenePage::fogged(const Ac2Record &o) const {
    // "Fog, Haze, and Clouds" off hides the fog and cloud pictures (those
    // with SPRT da4 set: 0x4372e0, 0x437ac0).
    if (_options.get(GameOptions::kFog))
        return false;
    const std::string file = fs::toLower(o.text("file"));
    const Ac2Record *sprt = o.part("SPRT");
    return sprt && sprt->i("scroll") != 0 &&
           (file.find("elements\\fog") != std::string::npos || file.find("skies\\clouds") != std::string::npos);
}

bool ScenePage::frozen(const Ac2Record &o) const {
    // "Animated Effects" off stops the videos flagged BNKX de4 (0x406230).
    if (_options.get(GameOptions::kWater))
        return false;
    const Ac2Record *bnkx = o.part("BNKX");
    return bnkx && bnkx->i("effect") != 0;
}

// --- The options screen -----------------------------------------------------------

namespace {

// The n-th object of the scene whose file is `name` (0x436660).
const Ac2Record *nth(const Ac2Scene &scene, const char *name, int n) {
    for (const Ac2Record *o : scene.objects()) {
        const std::string file = fs::toLower(o->text("file"));
        const size_t slash = file.find_last_of('\\');
        if ((slash == std::string::npos ? file : file.substr(slash + 1)) == name && n-- == 0)
            return o;
    }
    return nullptr;
}

} // namespace

void ScenePage::openOptions() {
    // 0x42be00: the staged values, and the toggles as they were (Cancel
    // puts them back).
    _optionsMenu = std::make_unique<OptionsMenu>();
    OptionsMenu &m = *_optionsMenu;
    m.staged[0] = _options.get(GameOptions::kMusic);
    m.staged[1] = _options.get(GameOptions::kSound);
    m.staged[2] = _options.get(GameOptions::kGamma);
    m.staged[3] = _options.get(GameOptions::kCaptions);
    for (int i = 0; i < 5; i++)
        m.original[i] = _options.values[GameOptions::kShadows + i];
}

int ScenePage::thumbX(int slider) const {
    const OptionsMenu &m = *_optionsMenu;
    if (m.dragging == slider + 1) {
        // While dragged: the step nearest the cursor (0x42c350).
        const int ax = _lastMouseX - 7;
        int k = 0;
        for (int i = 0; i < 10; i++)
            if (ax > kThumbLeft + kThumbStep * k + 11)
                k++;
        return kThumbLeft + kThumbStep * k;
    }
    return kThumbLeft + kThumbStep * m.staged[slider];
}

void ScenePage::optionsAction(const std::string &word) {
    // 0x42c3b0.
    OptionsMenu &m = *_optionsMenu;
    if (word == "ok") {
        _options.values[GameOptions::kMusic] = m.staged[0];
        _options.values[GameOptions::kSound] = m.staged[1];
        _options.values[GameOptions::kGamma] = m.staged[2];
        _options.values[GameOptions::kCaptions] = m.staged[3];
        saveOptions();
    } else if (word == "cancel") {
        applyVolumes(_options.get(GameOptions::kMusic), _options.get(GameOptions::kSound));
        for (int i = 0; i < 5; i++)
            _options.values[GameOptions::kShadows + i] = m.original[i];
        applyAntiAliasing();
        saveOptions();
    } else if (word.find("options") != std::string::npos && !word.empty()) {
        // optionsN: its last character. Sliders start a drag; Captions
        // waits for OK; the others change at once.
        const int n = word.back() - '1';
        if (n >= 0 && n <= 2)
            m.dragging = n + 1;
        else if (n == 3)
            m.staged[3] ^= 1;
        else if (n >= 4 && n <= 8) {
            _options.values[GameOptions::kShadows + n - 4] ^= 1;
            applyAntiAliasing();
        }
    }
    draw();
}

void ScenePage::optionsRelease() {
    // Mouse up (0x42c220): the dragged slider's value is heard (or seen) at
    // once; the settings change only on OK.
    OptionsMenu &m = *_optionsMenu;
    if (!m.dragging)
        return;
    const int slider = m.dragging - 1;
    m.staged[slider] = (thumbX(slider) - kThumbLeft) / kThumbStep;
    m.dragging = 0;
    if (slider == 0 || slider == 1) {
        applyVolumes(m.staged[0], m.staged[1]);
        if (slider == 1)
            playSound("soundtest");
    }
    draw();
}

void ScenePage::drawOptions() {
    // 0x42c090: the labels (font 7, colour 0), the ticks at the boxes.
    const OptionsMenu &m = *_optionsMenu;
    const TextStyle *style = _ctx.fonts ? _ctx.fonts->style("save, load, and options screens") : nullptr;
    const Color colour = _ctx.fonts
                             ? _ctx.fonts->colour("save, load, and options selected (argb format)", {255, 220, 130})
                             : Color{255, 220, 130};
    const Ac2Record *check = nth(_scene, "check.tga", 0);
    for (int i = 0; i < 9; i++) {
        const std::string label = interfaceText("options" + std::to_string(i + 1));
        const int w = style && style->font ? style->font->width(label, style->pixels) : 0;
        const int h = style && style->font ? style->font->lineHeight(style->pixels) : 0;
        if (i < 3) {
            const Ac2Record *track = nth(_scene, "sliderbar.jpg", i);
            const Ac2Record *thumb = nth(_scene, "slider.jpg", i);
            if (!track || !thumb || !style || !style->font)
                continue;
            const int tw = picture(track->text("file")).w;
            _ctx.fonts->draw(_ctx.screen, *style, int(track->f("x")) + (tw - w) / 2, int(thumb->f("y")) - h - 5, label,
                             colour);
            continue;
        }
        const Ac2Record *box = nth(_scene, "checkbox.tga", i - 3);
        if (!box)
            continue;
        const bool on = i == 3 ? m.staged[3] != 0 : _options.values[GameOptions::kShadows + i - 4] != 0;
        if (on && check)
            image::blitOver(_ctx.screen, picture(check->text("file")), int(box->f("x")) - 2, int(box->f("y")) - 2);
        if (style && style->font) {
            const int bh = picture(box->text("file")).h;
            _ctx.fonts->draw(_ctx.screen, *style, int(box->f("x")) - w - 10, int(box->f("y")) + (bh - h) / 2, label,
                             colour);
        }
    }
}

} // namespace reluva::awe
