/*
 * window_logo.c
 * Copyright (C) 2021 Kovid Goyal <kovid at kovidgoyal.net>
 *
 * Distributed under terms of the GPL3 license.
 */

#include "window_logo.h"
#include "state.h"


typedef struct WindowLogoItem {
    WindowLogo wl;
    unsigned int refcnt;
    char *path;
    window_logo_id_t id;
    UT_hash_handle hh_id;
    UT_hash_handle hh_path;
} WindowLogoItem;

struct WindowLogoTable {
    WindowLogoItem *by_id, *by_path;
};

static void
free_window_logo(WindowLogoTable *table, WindowLogoItem **itemref) {
    WindowLogoItem *item = *itemref;
    free(item->path);
    free(item->wl.bitmap);
    if (item->wl.texture_id) free_texture(&item->wl.texture_id);
    HASH_DELETE(hh_id, table->by_id, item);
    HASH_DELETE(hh_path, table->by_path, item);
    free(item); itemref = NULL;
}

static void
send_logo_to_gpu(WindowLogo *s) {
    send_image_to_gpu(&s->texture_id, s->bitmap, s->width, s->height, false, true, true, REPEAT_CLAMP);
    free(s->bitmap); s->bitmap = NULL;
}


void
set_on_gpu_state(WindowLogo *s, bool on_gpu) {
    if (s->load_from_disk_ok) {
        if (on_gpu) { if (!s->texture_id) send_logo_to_gpu(s); }
        else if (s->texture_id) free_texture(&s->texture_id);
    }
}

WindowLogoTable*
alloc_window_logo_table(void) {
    return calloc(1, sizeof(WindowLogoTable));
}

void
free_window_logo_table(WindowLogoTable **table) {
    WindowLogoItem *current, *tmp;
    HASH_ITER(hh_id, (*table)->by_id, current, tmp) {
        free_window_logo(*table, &current);
    }
    HASH_CLEAR(hh_path, (*table)->by_path);
    HASH_CLEAR(hh_id, (*table)->by_id);
    free(*table); *table = NULL;
}
