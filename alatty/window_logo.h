/*
 * Copyright (C) 2021 Kovid Goyal <kovid at kovidgoyal.net>
 *
 * Distributed under terms of the GPL3 license.
 */

#pragma once

#include "alatty-uthash.h"

typedef unsigned int window_logo_id_t;

typedef struct WindowLogo {
    unsigned int height, width;
    bool load_from_disk_ok;
    uint32_t texture_id;
    uint8_t* bitmap;
} WindowLogo;

typedef struct WindowLogoTable WindowLogoTable;

void
set_on_gpu_state(WindowLogo *logo, bool on_gpu);

WindowLogoTable*
alloc_window_logo_table(void);

void
free_window_logo_table(WindowLogoTable **table);
