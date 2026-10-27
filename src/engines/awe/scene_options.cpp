// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Scenes page's options: the game's settings (Music, Sound, Gamma,
// Captions, Shadows, Water, Fog, Rain, AntiAlias), what they change, and
// the options screen (Menu.Options's PUZZ options: 0x42bd40). See
// docs/formats/awe/options.md.

#include "engines/awe/viewers.h"

#include "common/fs.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace reluva::awe {

namespace {

// The settings' names, as the game's registry values, in the order of the
// screen's controls 1..9 (options1..9); and their defaults (0x42bae0).
constexpr const char* kNames[GameOptions::kCount] = {"Music", "Sound", "Gamma", "Captions", "Shadows",
                                                     "Water", "Fog",   "Rain",  "AntiAlias"};
constexpr int kDefaults[GameOptions::kCount] = {7, 7, 5, 1, 1, 1, 1, 1, 0};

// The sliders' thumbs: x = 694 + 23 * value (0..10).
constexpr int kThumbLeft = 694, kThumbStep = 23;

} // namespace

GameOptions::GameOptions() { std::copy(std::begin(kDefaults), std::end(kDefaults), std::begin(values)); }

std::filesystem::path ScenePage::optionsFile() const {
    return ctx_.savePath.empty() ? std::filesystem::path() : ctx_.savePath / "options.ini";
}

void ScenePage::loadOptions() {
    // Reluva keeps them in the game's folder as Name=value lines (the game
    // keeps them in the registry: HKCU\Software\DreamCatcher\Agatha
    // Christie\...\Settings).
    options_ = GameOptions();
    std::ifstream in(optionsFile());
    for (std::string line; std::getline(in, line);) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        for (int i = 0; i < GameOptions::kCount; i++)
            if (fs::toLower(line.substr(0, eq)) == fs::toLower(kNames[i]))
                options_.values[i] = std::atoi(line.c_str() + eq + 1);
    }
    applyVolumes(options_.get(GameOptions::Music), options_.get(GameOptions::Sound));
}

void ScenePage::saveOptions() const {
    if (optionsFile().empty())
        return;
    std::error_code ec;
    std::filesystem::create_directories(optionsFile().parent_path(), ec);
    std::ofstream out(optionsFile());
    for (int i = 0; i < GameOptions::kCount; i++)
        out << kNames[i] << '=' << options_.values[i] << '\n';
}

void ScenePage::applyVolumes(int music, int sound) {
    // Sounds whose path holds "music" go by Music, all the rest (speech
    // too) by Sound; tenths of full (0x4254b0).
    if (!ctx_.mixer)
        return;
    ctx_.mixer->setTypeVolume(audio::SoundType::Music, std::clamp(music, 0, 10) * audio::kMaxVolume / 10);
    for (audio::SoundType t : {audio::SoundType::Effects, audio::SoundType::Speech})
        ctx_.mixer->setTypeVolume(t, std::clamp(sound, 0, 10) * audio::kMaxVolume / 10);
}

int ScenePage::brightnessLevel() const {
    // On the options screen, the brightness being tried.
    return optionsMenu_ ? optionsMenu_->staged[2] : options_.get(GameOptions::Gamma);
}

void ScenePage::applyBrightness() {
    // Gamma g: a linear ramp of slope 32g + 97 over 257 (5 leaves colours
    // as they are), clipped (0x402040): the pictures here, the characters
    // as they're drawn.
    const int g = brightnessLevel();
    if (g == 5)
        return;
    const int slope = 32 * g + 97;
    for (size_t i = 0; i + 3 < ctx_.screen.pixels.size(); i += 4)
        for (size_t c = 0; c < 3; c++)
            ctx_.screen.pixels[i + c] = uint8_t(std::min(255, ctx_.screen.pixels[i + c] * slope / 257));
}

void ScenePage::applyAntiAliasing() {
    // 0x490060: 4x if the device has it, else 2x, else none; the game
    // resets its device to change it, Reluva makes its frame again.
    if (renderer_)
        renderer_->setMultisampling(options_.get(GameOptions::AntiAlias) ? 4 : 1);
}

bool ScenePage::fogged(const Ac2Record& o) const {
    // "Fog, Haze, and Clouds" off hides the fog and cloud pictures (those
    // with SPRT da4 set: 0x4372e0, 0x437ac0).
    if (options_.get(GameOptions::Fog))
        return false;
    const std::string file = fs::toLower(o.text("file"));
    const Ac2Record* sprt = o.part("SPRT");
    return sprt && sprt->i("da4") != 0 &&
           (file.find("elements\\fog") != std::string::npos || file.find("skies\\clouds") != std::string::npos);
}

bool ScenePage::frozen(const Ac2Record& o) const {
    // "Animated Effects" off stops the videos flagged BNKX de4 (0x406230).
    if (options_.get(GameOptions::Water))
        return false;
    const Ac2Record* bnkx = o.part("BNKX");
    return bnkx && bnkx->f("de4") != 0;
}

// --- The options screen -----------------------------------------------------------

namespace {

// The n-th object of the scene whose file is `name` (0x436660).
const Ac2Record* nth(const Ac2Scene& scene, const char* name, int n) {
    for (const Ac2Record* o : scene.objects()) {
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
    optionsMenu_ = std::make_unique<OptionsMenu>();
    OptionsMenu& m = *optionsMenu_;
    m.staged[0] = options_.get(GameOptions::Music);
    m.staged[1] = options_.get(GameOptions::Sound);
    m.staged[2] = options_.get(GameOptions::Gamma);
    m.staged[3] = options_.get(GameOptions::Captions);
    for (int i = 0; i < 5; i++)
        m.original[i] = options_.values[GameOptions::Shadows + i];
}

int ScenePage::thumbX(int slider) const {
    const OptionsMenu& m = *optionsMenu_;
    if (m.dragging == slider + 1) {
        // While dragged: the step nearest the cursor (0x42c350).
        const int ax = lastMouseX_ - 7;
        int k = 0;
        for (int i = 0; i < 10; i++)
            if (ax > kThumbLeft + kThumbStep * k + 11)
                k++;
        return kThumbLeft + kThumbStep * k;
    }
    return kThumbLeft + kThumbStep * m.staged[slider];
}

void ScenePage::optionsAction(const std::string& word) {
    // 0x42c3b0.
    OptionsMenu& m = *optionsMenu_;
    if (word == "ok") {
        options_.values[GameOptions::Music] = m.staged[0];
        options_.values[GameOptions::Sound] = m.staged[1];
        options_.values[GameOptions::Gamma] = m.staged[2];
        options_.values[GameOptions::Captions] = m.staged[3];
        saveOptions();
    } else if (word == "cancel") {
        applyVolumes(options_.get(GameOptions::Music), options_.get(GameOptions::Sound));
        for (int i = 0; i < 5; i++)
            options_.values[GameOptions::Shadows + i] = m.original[i];
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
            options_.values[GameOptions::Shadows + n - 4] ^= 1;
            applyAntiAliasing();
        }
    }
    draw();
}

void ScenePage::optionsRelease() {
    // Mouse up (0x42c220): the dragged slider's value is heard (or seen) at
    // once; the settings change only on OK.
    OptionsMenu& m = *optionsMenu_;
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
    const OptionsMenu& m = *optionsMenu_;
    const TextStyle* style = ctx_.fonts ? ctx_.fonts->style("save, load, and options screens") : nullptr;
    const Color colour =
        ctx_.fonts ? ctx_.fonts->colour("save, load, and options selected (argb format)", {255, 220, 130})
                   : Color{255, 220, 130};
    const Ac2Record* check = nth(scene_, "check.tga", 0);
    for (int i = 0; i < 9; i++) {
        const std::string label = interfaceText("options" + std::to_string(i + 1));
        const int w = style && style->font ? style->font->width(label, style->pixels) : 0;
        const int h = style && style->font ? style->font->lineHeight(style->pixels) : 0;
        if (i < 3) {
            const Ac2Record* track = nth(scene_, "sliderbar.jpg", i);
            const Ac2Record* thumb = nth(scene_, "slider.jpg", i);
            if (!track || !thumb || !style || !style->font)
                continue;
            const int tw = picture(track->text("file")).w;
            ctx_.fonts->draw(ctx_.screen, *style, int(track->f("x")) + (tw - w) / 2, int(thumb->f("y")) - h - 5,
                             label, colour);
            continue;
        }
        const Ac2Record* box = nth(scene_, "checkbox.tga", i - 3);
        if (!box)
            continue;
        const bool on = i == 3 ? m.staged[3] != 0 : options_.values[GameOptions::Shadows + i - 4] != 0;
        if (on && check)
            image::blit(ctx_.screen, picture(check->text("file")), int(box->f("x")) - 2, int(box->f("y")) - 2);
        if (style && style->font) {
            const int bh = picture(box->text("file")).h;
            ctx_.fonts->draw(ctx_.screen, *style, int(box->f("x")) - w - 10, int(box->f("y")) + (bh - h) / 2, label,
                             colour);
        }
    }
}

} // namespace reluva::awe
