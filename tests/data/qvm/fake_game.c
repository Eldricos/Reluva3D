/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 The Reluva contributors
 *
 * A stand-in for a game's vm/qagame.qvm, for testing the engine side of
 * the game logic's system calls (src/engines/idtech3/eliteforce/game_host.cpp)
 * with code the Quake III compiler generates, in Elite Force's numbering
 * (fake_game.asm). Init reads the level's entities the way the original
 * game code does, token by token, keeps each one's classname and links
 * it. build.sh compiles it into fake_game_qvm.h. vmMain must be first. */

typedef struct {
    int number;
    int inUse;
    char classname[32];
    int pad[2];
} entity_t; /* 48 bytes */

void trap_Print(const char *text);
void trap_Error(const char *text);
int trap_Cvar_VariableIntegerValue(const char *name);
void trap_Cvar_VariableStringBuffer(const char *name, char *buffer, int size);
void trap_LocateGameData(entity_t *entities, int count, int entitySize, void *clients, int clientSize);
void trap_SetConfigstring(int index, const char *text);
void trap_GetConfigstring(int index, char *buffer, int size);
void trap_LinkEntity(entity_t *ent);
void trap_UnlinkEntity(entity_t *ent);
int trap_GetEntityToken(char *buffer, int size);
float ceil(float f);

static entity_t entities[64];
static int clients[8];
static int numEntities;
static int frames;
static char token[64];
static char key[64];
static char text[64];

static int same(const char *a, const char *b);
static void copy(char *to, const char *from, int size);
static int spawnOne(void);

int vmMain(int command, int arg0, int arg1, int arg2, int arg3, int arg4, int arg5, int arg6, int arg7, int arg8,
           int arg9, int arg10, int arg11) {
    int r;
    switch (command) {
    case 0: /* init(levelTime, randomSeed, restart) */
        trap_Print("------- Game Initialization -------\n");
        trap_Cvar_VariableStringBuffer("mapname", text, sizeof(text));
        trap_SetConfigstring(12, text);
        trap_LocateGameData(entities, 0, sizeof(entity_t), clients, sizeof(int));
        while ((r = spawnOne()) == 1)
            ;
        if (r < 0)
            trap_Error("broken entity text");
        if (!same(entities[0].classname, "worldspawn"))
            trap_Error("the first entity isn't worldspawn");
        trap_UnlinkEntity(&entities[0]); /* the world isn't linked */
        return numEntities;
    case 8: /* run a frame(levelTime) */
        frames++;
        return frames;
    case 20: return numEntities;
    case 21: /* an entity's classname, back through a configstring */
        trap_SetConfigstring(300 + arg0, entities[arg0].classname);
        trap_GetConfigstring(300 + arg0, text, sizeof(text));
        return text[0] * 1000 + text[1];
    case 22: return trap_Cvar_VariableIntegerValue("sv_maxclients") * 100 + (int)ceil(2.25f);
    case 23: return frames;
    }
    return -1;
}

static int same(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void copy(char *to, const char *from, int size) {
    int i;
    for (i = 0; i < size - 1 && from[i]; i++)
        to[i] = from[i];
    to[i] = 0;
}

/* One { key value ... } block: 1 if there was one, 0 at the end, -1 if broken. */
static int spawnOne(void) {
    entity_t *ent;
    if (!trap_GetEntityToken(token, sizeof(token)))
        return 0;
    if (!same(token, "{"))
        return -1;
    ent = &entities[numEntities];
    ent->number = numEntities;
    ent->inUse = 1;
    ent->classname[0] = 0;
    while (1) {
        if (!trap_GetEntityToken(key, sizeof(key)))
            return -1;
        if (same(key, "}"))
            break;
        if (!trap_GetEntityToken(token, sizeof(token)))
            return -1;
        if (same(key, "classname"))
            copy(ent->classname, token, sizeof(ent->classname));
    }
    numEntities++;
    trap_LocateGameData(entities, numEntities, sizeof(entity_t), clients, sizeof(int));
    trap_LinkEntity(ent);
    return 1;
}
