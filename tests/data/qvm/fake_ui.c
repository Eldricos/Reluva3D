/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 The Reluva contributors
 *
 * A stand-in for a game's vm/ui.qvm, for testing the engine side of the
 * menus' system calls (src/engines/idtech3/eliteforce/ui_host.cpp) with
 * code the Quake III compiler generates. It calls them the way the
 * original's menus are seen to (docs/formats/idtech3/eliteforce-syscalls.md),
 * in Elite Force's numbering (fake_ui.asm). build.sh compiles it into
 * fake_ui_qvm.h. vmMain must be the first function. */

typedef struct {
    int handle;
    int modificationCount;
    float value;
    int integer;
    char string[256];
} vmCvar_t;

void trap_Error(const char *text);
void trap_Print(const char *text);
void trap_Cvar_Set(const char *name, const char *value);
float trap_Cvar_VariableValue(const char *name);
void trap_Cvar_VariableStringBuffer(const char *name, char *buffer, int size);
int trap_FS_FOpenFile(const char *path, int *handle, int mode);
int trap_FS_Read(void *buffer, int length, int handle);
void trap_FS_FCloseFile(int handle);
int trap_FS_GetFileList(const char *folder, const char *extension, char *buffer, int size);
int trap_R_RegisterShaderNoMip(const char *name);
void trap_R_SetColor(const float *rgba);
void trap_R_DrawStretchPic(float x, float y, float w, float h, float s1, float t1, float s2, float t2, int shader);
void trap_GetGlconfig(int *glconfig);
void trap_Cvar_Register(vmCvar_t *cvar, const char *name, const char *value, int flags);
void trap_Cvar_Update(vmCvar_t *cvar);
char *strncpy(char *dest, const char *src, int count);
float sqrt(float f);

static vmCvar_t testCvar;
static int glconfig[2816 + 17]; /* Quake III's glconfig_t: 11264 bytes of strings, then 17 words */
static int shader;
static char file[64];
static char list[256];
static char text[64];

int vmMain(int command, int arg0, int arg1, int arg2, int arg3, int arg4, int arg5, int arg6, int arg7, int arg8,
           int arg9, int arg10, int arg11) {
    int handle, length, count;
    float color[4];
    switch (command) {
    case 0: return 4; /* API version */
    case 1:           /* init: returns the file's length * 100 + how many .txt files are listed */
        trap_Print("fake ui\n");
        trap_Cvar_Register(&testCvar, "ui_test", "7", 1);
        length = trap_FS_FOpenFile("scripts/test.txt", &handle, 0);
        if (length < 0)
            trap_Error("no scripts/test.txt");
        trap_FS_Read(file, length < 63 ? length : 63, handle);
        trap_FS_FCloseFile(handle);
        count = trap_FS_GetFileList("scripts", ".txt", list, sizeof(list));
        shader = trap_R_RegisterShaderNoMip("menu/test");
        trap_GetGlconfig(glconfig);
        return length * 100 + count;
    case 2: return testCvar.integer; /* after the engine changed it, once updated */
    case 3:
        trap_Cvar_Update(&testCvar);
        return testCvar.integer;
    case 4: return glconfig[2816 + 10] * 1000 + glconfig[2816 + 11]; /* vidWidth, vidHeight */
    case 5:                                                         /* refresh(time) */
        color[0] = 1;
        color[1] = 0.5f;
        color[2] = 0;
        color[3] = 1;
        trap_R_SetColor(color);
        trap_R_DrawStretchPic(10, 20, glconfig[2816 + 10] / 2, 30, 0, 0, 1, 0.5f, shader);
        trap_R_SetColor(0);
        trap_R_DrawStretchPic(0, 0, 1, 1, 0, 0, 1, 1, shader);
        return arg0;
    case 6: /* the file's first line, through the names list and strncpy */
        trap_Cvar_Set("ui_text", file);
        trap_Cvar_VariableStringBuffer("ui_text", text, 4);
        strncpy(list, text, 8);
        return list[0] * 1000000 + list[1] * 1000 + list[3] + list[7];
    case 7: return (int)(sqrt(trap_Cvar_VariableValue("ui_number")) * 10);
    case 8: return testCvar.modificationCount;
    case 9: trap_Error("boom"); return 1;
    }
    return -1;
}
