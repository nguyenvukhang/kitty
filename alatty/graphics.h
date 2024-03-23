/*
 * Copyright (C) 2017 Kovid Goyal <kovid at kovidgoyal.net>
 *
 * Distributed under terms of the GPL3 license.
 */

#pragma once
#include "data-types.h"

typedef struct {
    int32_t amt, limit;
    index_type margin_top, margin_bottom;
    bool has_margins;
} ScrollData;

static inline float
gl_size(const unsigned int sz, const unsigned int viewport_size) {
    // convert pixel sz to OpenGL coordinate system.
    const float px = 2.f / viewport_size;
    return px * sz;
}

static inline float
gl_pos_x(const unsigned int px_from_left_margin, const unsigned int viewport_size) {
    const float px = 2.f / viewport_size;
    return -1.f + px_from_left_margin * px;
}

static inline float
gl_pos_y(const unsigned int px_from_top_margin, const unsigned int viewport_size) {
    const float px = 2.f / viewport_size;
    return 1.f - px_from_top_margin * px;
}


bool png_from_file_pointer(FILE* fp, const char *path, uint8_t** data, unsigned int* width, unsigned int* height, size_t* sz);
bool png_path_to_bitmap(const char *path, uint8_t** data, unsigned int* width, unsigned int* height, size_t* sz);
bool png_from_data(void *png_data, size_t png_data_sz, const char *path_for_error_messages, uint8_t** data, unsigned int* width, unsigned int* height, size_t* sz);
