// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Jedi Outcast's single-player engine functions beyond the Expansion
// Pack's table, and its client game's calls
// (docs/engines/idtech3/games/jk2/systems/sp-interface.md).

#include "engines/idtech3/eliteforce/sp_game_host.h"

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

} // namespace

void SpGameHost::addJediImports() {
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
}

uint32_t SpGameHost::jediCgameCall(x86::Cpu &c) {
    // syscall(number, ...): numbered as jk2sp.exe's handler (0x401570) has
    // them, 0 to 111. None is answered yet.
    c.abort("jk2gamex86.dll's client game made call " + std::to_string(c.arg(0)) + ", which Reluva doesn't answer yet");
    return 0;
}

} // namespace reluva::idtech3::eliteforce
