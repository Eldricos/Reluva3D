/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 The Reluva contributors
 *
 * A stand-in for a game's vm/cgame.qvm, for testing the engine side of
 * the client game's system calls (src/engines/idtech3/eliteforce/cgame_host.cpp)
 * with code the Quake III compiler generates, in Elite Force's numbering
 * (fake_cgame.asm). build.sh compiles it into fake_cgame_qvm.h. vmMain
 * must be the first function. */

typedef struct {
    int stringOffsets[1024];
    char stringData[16000];
    int dataCount;
} gameState_t;

typedef struct {
    int snapFlags, ping, serverTime;
    unsigned char areamask[32];
    int ps[115]; /* Elite Force's player state: 460 bytes */
    int numEntities;
    int entities[256][51]; /* entity states: 204 bytes */
    int numServerCommands, serverCommandSequence;
} snapshot_t; /* 52740 bytes */

void trap_Print(const char *text);
void trap_GetCurrentSnapshotNumber(int *number, int *serverTime);
int trap_GetSnapshot(int number, snapshot_t *snap);
int trap_GetServerCommand(int sequence);
int trap_Argc(void);
void trap_Argv(int n, char *buffer, int size);
void trap_AddCommand(const char *name);
void trap_GetGameState(gameState_t *gs);
void trap_GetGlconfig(int *glconfig);
int trap_R_RegisterModel(const char *name);
int trap_R_RegisterShader(const char *name);
void trap_S_StartBackgroundTrack(const char *intro, const char *loop);

static gameState_t gameState;
static int glconfig[1298]; /* Elite Force's glconfig_t: 5120 bytes of strings, a word, then Quake III's 17 */
static int model, shader;
static snapshot_t snap;
static char word[64];

int vmMain(int command, int arg0, int arg1, int arg2, int arg3, int arg4, int arg5, int arg6, int arg7, int arg8,
           int arg9, int arg10, int arg11) {
    const char *s;
    int n;
    switch (command) {
    case 0: /* init(serverMessageNum, serverCommandSequence, clientNum) */
        trap_Print("fake cgame\n");
        trap_GetGameState(&gameState);
        trap_GetGlconfig(glconfig);
        trap_AddCommand("+zoom");
        trap_AddCommand("weapnext");
        model = trap_R_RegisterModel("models/weaphits/explosion.md3");
        shader = trap_R_RegisterShader("powerups/quad");
        trap_S_StartBackgroundTrack("music/intro", "music/loop");
        return arg2; /* the client number */
    case 20: /* configstring arg0: its first two characters */
        s = gameState.stringData + gameState.stringOffsets[arg0];
        return s[0] * 1000 + s[1];
    case 21: /* its length */
        s = gameState.stringData + gameState.stringOffsets[arg0];
        for (n = 0; s[n]; n++)
            ;
        return n;
    case 22: return gameState.dataCount;
    case 23: return glconfig[1281 + 10] * 1000 + glconfig[1281 + 11]; /* vidWidth, vidHeight */
    case 24: return model * 100 + shader;
    case 25: /* the latest snapshot: its server time, or -1 if it isn't there */
        trap_GetCurrentSnapshotNumber(&n, &arg0);
        if (!trap_GetSnapshot(n, &snap))
            return -1;
        return snap.serverTime;
    case 26: return snap.numEntities * 1000 + snap.serverCommandSequence;
    case 27: return snap.ps[0] + snap.ps[114]; /* the player state's first and last words */
    case 28: return snap.entities[arg0][0] * 1000 + snap.entities[arg0][50];
    case 29: /* server command arg0: how many words, and the first letter of word arg1 */
        if (!trap_GetServerCommand(arg0))
            return -1;
        trap_Argv(arg1, word, sizeof(word));
        return trap_Argc() * 1000 + word[0];
    }
    return -1;
}
