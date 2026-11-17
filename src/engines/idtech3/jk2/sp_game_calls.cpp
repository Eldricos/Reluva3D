// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Jedi Outcast's single-player engine functions beyond the Expansion
// Pack's table, and its client game's calls
// (docs/engines/idtech3/games/jk2/systems/sp-interface.md).

#include "engines/idtech3/eliteforce/sp_game_host.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

namespace reluva::idtech3::eliteforce {

namespace {

// Ghoul2's functions, slots 42 to 102 of the table jk2sp.exe fills at
// 0x4368e0, in the GPL source's order (the count matches).
constexpr const char *kGhoul2Slots[] = {
    "G2API_PrecacheGhoul2Model",
    "G2API_InitGhoul2Model",
    "G2API_SetLodBias",
    "G2API_SetSkin",
    "G2API_SetShader",
    "G2API_RemoveGhoul2Model",
    "G2API_SetSurfaceOnOff",
    "G2API_SetRootSurface",
    "G2API_RemoveSurface",
    "G2API_AddSurface",
    "G2API_SetBoneAnim",
    "G2API_GetBoneAnim",
    "G2API_GetBoneAnimIndex",
    "G2API_GetAnimRange",
    "G2API_GetAnimRangeIndex",
    "G2API_PauseBoneAnim",
    "G2API_PauseBoneAnimIndex",
    "G2API_IsPaused",
    "G2API_StopBoneAnim",
    "G2API_SetBoneAngles",
    "G2API_SetBoneAnglesMatrix",
    "G2API_StopBoneAngles",
    "G2API_RemoveBone",
    "G2API_RemoveBolt",
    "G2API_AddBolt",
    "G2API_AddBoltSurfNum",
    "G2API_AttachG2Model",
    "G2API_DetachG2Model",
    "G2API_AttachEnt",
    "G2API_DetachEnt",
    "G2API_GetBoltMatrix",
    "G2API_ListSurfaces",
    "G2API_ListBones",
    "G2API_HaveWeGhoul2Models",
    "G2API_SetGhoul2ModelFlags",
    "G2API_GetGhoul2ModelFlags",
    "G2API_GetAnimFileName",
    "G2API_CollisionDetect",
    "G2API_GiveMeVectorFromMatrix",
    "G2API_CopyGhoul2Instance",
    "G2API_CleanGhoul2Models",
    "TheGhoul2InfoArray",
    "G2API_GetParentSurface",
    "G2API_GetSurfaceIndex",
    "G2API_GetSurfaceName",
    "G2API_GetGLAName",
    "G2API_SetNewOrigin",
    "G2API_GetBoneIndex",
    "G2API_StopBoneAnglesIndex",
    "G2API_StopBoneAnimIndex",
    "G2API_SetBoneAnglesIndex",
    "G2API_SetBoneAnglesMatrixIndex",
    "G2API_SetBoneAnimIndex",
    "G2API_SaveGhoul2Models",
    "G2API_LoadGhoul2Models",
    "G2API_LoadSaveCodeDestructGhoul2Info",
    "G2API_FreeSaveBuffer",
    "G2API_GetAnimFileNameIndex",
    "G2API_GetSurfaceRenderStatus",
    "RE_RegisterSkin",
    "RE_GetAnimationCFG",
};
constexpr int kFirstGhoul2Slot = 42;

uint32_t floatBits(float v) {
    uint32_t u;
    std::memcpy(&u, &v, 4);
    return u;
}

} // namespace

int SpGameHost::jediGhoul2Slot(std::string_view name) {
    for (int i = 0; i < int(std::size(kGhoul2Slots)); ++i)
        if (name == kGhoul2Slots[i])
            return kFirstGhoul2Slot + i;
    return -1;
}

void SpGameHost::addJediImports() {
    // Each light style full until the client game sets it, as Raven's
    // engines start them.
    for (auto &style : _lightStyles)
        style = {255, 255, 255, 255};
    // Not answered yet: the game stops there, saying which.
    for (int i = 0; i < int(std::size(kGhoul2Slots)); ++i) {
        const int slot = kFirstGhoul2Slot + i;
        const std::string name = kGhoul2Slots[i];
        const uint32_t address = _cpu->addNative("engine[" + name + "]", [this, slot, name](x86::Cpu &c) {
            if (trace)
                trace(slot, c.read32(c.reg[x86::Cpu::ESP]));
            c.abort("jk2gamex86.dll called " + name + " (slot " + std::to_string(slot) +
                    "), which Reluva doesn't answer yet");
            return 0u;
        });
        _cpu->write32(_imports + 4 * uint32_t(slot), address);
    }
    addGhoul2Imports();
}

uint32_t SpGameHost::jediCgameCall(x86::Cpu &c) {
    // syscall(number, args...), numbered as jk2sp.exe's handler (0x401570,
    // its jump table at 0x401fd0) has them. Where a call and what it
    // passes are Elite Force's, its handling is shared: Jedi Outcast's
    // number n is Elite Force's n - 1 from 27 to 43 (CM_SnapPVS at 26 is
    // new), and n - 8 from 51 to 62 and n - 10 from 66 to 80 (the fonts,
    // and three more at 63, 64 and 74, in between).
    auto a = [&c](int n) { return c.arg(n + 1); };
    auto str = [&](int n, size_t max = 1024) { return c.string(a(n), max); };
    auto f = [&](int n) {
        const uint32_t u = a(n);
        float v;
        std::memcpy(&v, &u, 4);
        return v;
    };
    const int number = int(c.arg(0));
    _cgameCallCounts.add(number);
    if (number <= 25)
        return cgameCall(c, number);
    if ((number >= 27 && number <= 34) || (number >= 39 && number <= 43))
        return cgameCall(c, number - 1);
    if (number >= 51 && number <= 62)
        return cgameCall(c, number - 8);
    if ((number >= 66 && number <= 73) || (number >= 75 && number <= 80))
        return cgameCall(c, number - 10);
    auto fonts = [&]() -> jk2::FontSet & {
        if (!_fonts)
            _fonts = std::make_unique<jk2::FontSet>(_files, _shaders);
        return *_fonts;
    };
    auto strings = [&]() -> raven::StringTable & {
        if (!_stringsLoaded) {
            _strings.load(_files);
            _stringsLoaded = true;
        }
        return _strings;
    };
    switch (number) {
    case 26: { // CM_SnapPVS(origin, area mask out): the snapshot's mask as seen
        // from there (0x412360): 32 bytes, the areas not joined to its own
        const std::array<uint8_t, 32> mask = hiddenAreas(readVec(a(0)));
        if (c.valid(a(1), uint32_t(mask.size())))
            std::memcpy(c.memory().data() + a(1), mask.data(), mask.size());
        return 0;
    }
    case 35: // force feedback (three of one argument, one of none): none
    case 36:
    case 37:
    case 38:
        return 0;
    case 44: // R_RegisterFont(name) -> handle, 0 if there's none
        return uint32_t(fonts().registerFont(str(0, 64)));
    case 45: { // R_Font_StrLenPixels(text, font, scale)
        const raven::Font *ft = fonts().font(int32_t(a(1)));
        return ft ? uint32_t(ft->textWidth(str(0, 4096), f(2))) : 0;
    }
    case 46: { // R_Font_StrLenChars(text): colour codes and line breaks left out
        const std::string text = str(0, 4096);
        uint32_t n = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '^')
                ++i;
            else if (text[i] != '\n' && text[i] != '\r')
                ++n;
        }
        return n;
    }
    case 47: { // R_Font_HeightPixels(font, scale)
        const raven::Font *ft = fonts().font(int32_t(a(0)));
        return ft ? uint32_t(int32_t(std::floor(float(ft->pointSize) * f(1) + 0.5f))) : 0;
    }
    case 48: { // R_Font_DrawString(x, y, text, rgba, font, letters at most or -1, scale)
        std::array<float, 4> colour{1, 1, 1, 1};
        if (a(3) && c.valid(a(3), 16))
            for (int i = 0; i < 4; ++i)
                colour[size_t(i)] = readFloat(a(3) + 4 * uint32_t(i));
        fonts().drawString(int32_t(a(0)), int32_t(a(1)), str(2, 4096), colour, int32_t(a(4)), int32_t(a(5)), f(6),
                           int32_t(_process->milliseconds()),
                           [this](float x, float y, float w, float h, float s1, float t1, float s2, float t2,
                                  int32_t shader, const std::array<float, 4> &col) {
                               _pictures.push_back({x, y, w, h, s1, t1, s2, t2, shader, col});
                           });
        return 0;
    }
    case 49: // Language_IsAsian() (0x462900): Korean, Chinese and Japanese, which Reluva hasn't
        return 0;
    case 50: { // AnyLanguage_ReadCharFromString(&text, &isTrailingPunctuation) (0x462700)
        // A western language's: a byte, the text moved past it; trailing
        // punctuation is what a line may not start with.
        const uint32_t at = a(0);
        if (!c.valid(at, 4))
            return 0;
        const uint32_t text = c.read32(at);
        const uint8_t ch = c.read8(text);
        c.write32(at, text + 1);
        if (const uint32_t flag = a(1); flag && c.valid(flag, 4))
            c.write32(flag, std::strchr("!?,.;:", ch) && ch ? 1u : 0u);
        return ch;
    }
    case 63: { // R_DrawRotatePic2(x, y, w, h, s1, t1, s2, t2, angle, shader): turned about its centre
        // A picture turns about its top-right corner; moved so that turn is
        // one about its centre.
        const float x = f(0);
        const float y = f(1);
        const float w = f(2);
        const float h = f(3);
        const float angle = f(8);
        const float rad = angle * 3.14159265358979f / 180.0f;
        const float cs = std::cos(rad);
        const float sn = std::sin(rad);
        const float dx = -w / 2;
        const float dy = h / 2;
        const float tx = dx - (cs * dx - sn * dy);
        const float ty = dy - (sn * dx + cs * dy);
        _pictures.push_back(
            {x - w / 2 + tx, y - h / 2 + ty, w, h, f(4), f(5), f(6), f(7), int32_t(a(9)), _color, angle});
        return 0;
    }
    case 64: // R_LAGoggles(): the light amplification goggles' look
    case 65: // R_Scissor(x, y, w, h)
        return 0;
    case 74: // SetUserCmdAngles(pitch, yaw, roll): added to the player's view, as a ride's
        _cmdAngles = {f(0), f(1), f(2)};
        return 0;
    case 81: // two points the engine writes out for recording a path
    case 82: // G2_ListModelBones, G2_ListModelSurfaces: printing for developers
    case 83:
        return 0;
    case 84: // G2_HaveWeGhoul2Models(CGhoul2Info_v &)
        return ghoul2HasModels(a(0)) ? 1u : 0u;
    case 85: // G2_SetModels(CGhoul2Info_v &, models, skins)
        ghoul2SetSkins(a(0), a(2));
        return 0;
    case 86: { // R_GetLightStyle(style, rgba out)
        const uint32_t style = a(0);
        if (style < _lightStyles.size() && c.valid(a(1), 4))
            for (uint32_t i = 0; i < 4; ++i)
                c.write8(a(1) + i, _lightStyles[style][i]);
        return 0;
    }
    case 87: { // R_SetLightStyle(style, rgba as an int)
        const uint32_t style = a(0);
        if (style < _lightStyles.size())
            for (uint32_t i = 0; i < 4; ++i)
                _lightStyles[style][i] = uint8_t(a(1) >> (8 * i));
        return 0;
    }
    case 89: // R_WorldEffectCommand(text): the weather system's
        return 0;
    // The menus, as jk2sp.exe's (0x49f970 on): a file read in a parse
    // session, its tokens asked for one at a time, each menuDef read from
    // where the session is.
    case 97: // Menu_Reset()
        _menuDefs.reset();
        return 0;
    case 98: // Menu_New(buffer): the menuDef at the session's place
        if (_menuLexer)
            _menuDefs.parse(*_menuLexer);
        return 0;
    case 99:    // PC_ParseInt(int *)
    case 101: { // PC_ParseFloat(float *)
        const std::string t = _menuLexer ? std::string(_menuLexer->next().value_or("")) : std::string();
        if (c.valid(a(0), 4))
            c.write32(a(0), number == 99 ? uint32_t(std::atoi(t.c_str())) : floatBits(std::strtof(t.c_str(), nullptr)));
        return 0;
    }
    case 100: { // PC_ParseString(char *): the next token, quotes taken off
        const std::string t = _menuLexer ? std::string(_menuLexer->next().value_or("")) : std::string();
        c.writeString(a(0), t, 1024);
        return 0;
    }
    case 102: { // PC_StartParseSession(file, char **buffer) -> the file's length, 0 if there's none
        std::vector<uint8_t> data;
        if (_files.read(str(0, 64), data) || data.empty())
            return 0;
        _menuText.assign(data.begin(), data.end());
        _menuLexer = std::make_unique<Lexer>(_menuText);
        if (_menuBuffer)
            _process->memory().free(_menuBuffer);
        _menuBuffer = _process->memory().alloc(uint32_t(_menuText.size()) + 1);
        c.writeString(_menuBuffer, _menuText, uint32_t(_menuText.size()) + 1);
        if (c.valid(a(1), 4))
            c.write32(a(1), _menuBuffer);
        return uint32_t(_menuText.size());
    }
    case 103: // PC_EndParseSession(buffer)
        _menuLexer.reset();
        if (_menuBuffer)
            _process->memory().free(_menuBuffer);
        _menuBuffer = 0;
        return 0;
    case 104: { // PC_ParseExt(char **token): the next token, "" at the end
        if (!_menuToken)
            _menuToken = _process->memory().alloc(1024);
        const std::string t = _menuLexer ? std::string(_menuLexer->next().value_or("")) : std::string();
        c.writeString(_menuToken, t, 1024);
        if (c.valid(a(0), 4))
            c.write32(a(0), _menuToken);
        return 0;
    }
    case 105: // Menu_PaintAll(): the visible menus' items (the HUD's have none)
    case 106: // String_Init()
        return 0;
    case 107: { // Menu_GetInfo(name, int *x, int *y) -> whether there's such a menu; its place, whole
        const jk2::MenuDef *menu = _menuDefs.find(str(0, 64));
        if (!menu)
            return 0;
        if (c.valid(a(1), 4))
            c.write32(a(1), uint32_t(int32_t(menu->rect[0])));
        if (c.valid(a(2), 4))
            c.write32(a(2), uint32_t(int32_t(menu->rect[1])));
        return 1;
    }
    case 111: { // UI_GetItemText(menu, item, buffer) -> whether there's such an item; its text
        const jk2::MenuDef *menu = _menuDefs.find(str(0, 64));
        const std::string itemName = fs::toLower(str(1, 64));
        if (!menu)
            return 0;
        for (const jk2::MenuItemDef &item : menu->items)
            if (fs::toLower(item.name()) == itemName) {
                const jk2::MenuEntry *text = item.find("text");
                c.writeString(a(2), text && !text->args.empty() ? text->args[0] : std::string(), 256);
                return 1;
            }
        return 0;
    }
    case 95: { // Z_Malloc(size, tag), zeroed
        const uint32_t p = _process->memory().alloc(std::max(a(0), 1u));
        if (!p)
            c.abort("Z_Malloc: out of memory (" + std::to_string(a(0)) + " bytes)");
        else
            std::memset(c.memory().data() + p, 0, a(0));
        return p;
    }
    case 96: // Z_Free(pointer)
        _process->memory().free(a(0));
        return 0;
    case 108: { // SP_Register(package, flags) -> whether it's there
        const std::string name = fs::toLower(str(0, 256));
        for (const raven::StringPackage &p : strings().packages())
            if (fs::toLower(p.reference) == name)
                return 1;
        return 0;
    }
    case 109:   // SP_GetStringTextString(reference, buffer, size) -> its length
    case 110: { // SP_GetStringText(id, buffer, size): the id the package's number, then the entry's
        const int language = std::clamp(_cvars.integer("sp_language"), 0, 2);
        std::string out;
        if (number == 109) {
            if (const std::string *text = strings().find(str(0, 256), raven::StringTable::Language(language)))
                out = *text;
        } else {
            const uint32_t id = a(0) & 0xffff;
            for (const raven::StringPackage &p : strings().packages())
                if (uint32_t(p.id) == id >> 8)
                    for (const raven::StringPackage::Entry &e : p.entries)
                        if (uint32_t(e.index) == (id & 0xff))
                            out = e.text[size_t(language)].empty() ? e.text[0] : e.text[size_t(language)];
        }
        if (a(1))
            c.writeString(a(1), out, a(2));
        return uint32_t(out.size());
    }
    default:
        c.abort("jk2gamex86.dll's client game made call " + std::to_string(number) +
                ", which Reluva doesn't answer yet");
        return 0;
    }
}

void SpGameHost::writeJediGlconfig(uint32_t address) {
    // glconfig_t as jk2sp.exe copies it (84 bytes, 0x401020): four text
    // pointers, then the numbers.
    if (!_cpu->valid(address, 84))
        return;
    std::memset(_cpu->memory().data() + address, 0, 84);
    _cpu->write32(address, text("Reluva"));
    _cpu->write32(address + 4, text("Reluva"));
    _cpu->write32(address + 8, text("1.0"));
    _cpu->write32(address + 12, text(""));
    _cpu->write32(address + 16, 2048); // maxTextureSize
    _cpu->write32(address + 20, 2);    // texture units
    _cpu->write32(address + 24, 32);   // colour bits
    _cpu->write32(address + 28, 24);   // depth bits
    _cpu->write32(address + 32, 8);    // stencil bits
    _cpu->write32(address + 36, 1);    // deviceSupportsGamma
    _cpu->write32(address + 44, 1);    // textureEnvAddAvailable
    _cpu->write32(address + 56, 640);  // vidWidth
    _cpu->write32(address + 60, 480);  // vidHeight
    _cpu->write32(address + 64, floatBits(640.0f / 480.0f));
    _cpu->write32(address + 68, 60); // displayFrequency
}

} // namespace reluva::idtech3::eliteforce
