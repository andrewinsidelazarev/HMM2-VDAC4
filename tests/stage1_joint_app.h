/* Codex: только граница двух DLL стенда; ABI прошивки и ELF не расширяется. */
#pragma once
#include "zm_app.h"
typedef struct {
    const zm_app_t *app;
    unsigned (*frames)(void);
    int (*world_ready)(void);
    int (*result)(void);
    int (*state)(void);
    int (*done)(void);
} JointApp;
