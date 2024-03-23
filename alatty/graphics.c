/*
 * graphics.c
 * Copyright (C) 2017 Kovid Goyal <kovid at kovidgoyal.net>
 *
 * Distributed under terms of the GPL3 license.
 */

#include "graphics.h"
#include "png-reader.h"

static void
print_png_read_error(png_read_data *d, const char *code, const char* msg) {
    if (d->error.used >= d->error.capacity) {
        size_t cap = MAX(2 * d->error.capacity, 1024 + d->error.used);
        d->error.buf = realloc(d->error.buf, cap);
        if (!d->error.buf) return;
        d->error.capacity = cap;
    }
    d->error.used += snprintf(d->error.buf + d->error.used, d->error.capacity - d->error.used, "%s: %s ", code, msg);
}

bool
png_from_data(void *png_data, size_t png_data_sz, const char *path_for_error_messages, uint8_t** data, unsigned int* width, unsigned int* height, size_t* sz) {
    png_read_data d = {.err_handler=print_png_read_error};
    inflate_png_inner(&d, png_data, png_data_sz);
    if (!d.ok) {
        log_error("Failed to decode PNG image at: %s with error: %s", path_for_error_messages, d.error.used > 0 ? d.error.buf : "");
        free(d.decompressed); free(d.row_pointers); free(d.error.buf);
        return false;
    }
    *data = d.decompressed;
    free(d.row_pointers); free(d.error.buf);
    *sz = d.sz;
    *height = d.height; *width = d.width;
    return true;
}

bool
png_from_file_pointer(FILE *fp, const char *path_for_error_messages, uint8_t** data, unsigned int* width, unsigned int* height, size_t* sz) {
    size_t capacity = 16*1024, pos = 0;
    unsigned char *buf = malloc(capacity);
    if (!buf) { log_error("Out of memory reading PNG file at: %s", path_for_error_messages); fclose(fp); return false; }
    while (!feof(fp)) {
        if (capacity - pos < 1024) {
            capacity *= 2;
            unsigned char *new_buf = realloc(buf, capacity);
            if (!new_buf) {
                free(buf);
                log_error("Out of memory reading PNG file at: %s", path_for_error_messages); fclose(fp); return false;
            }
            buf = new_buf;
        }
        pos += fread(buf + pos, sizeof(char), capacity - pos, fp);
        int saved_errno = errno;
        if (ferror(fp) && saved_errno != EINTR) {
            log_error("Failed while reading from file: %s with error: %s", path_for_error_messages, strerror(saved_errno));
            free(buf);
            return false;
        }
    }
    bool ret = png_from_data(buf, pos, path_for_error_messages, data, width, height, sz);
    free(buf);
    return ret;
}

bool
png_path_to_bitmap(const char* path, uint8_t** data, unsigned int* width, unsigned int* height, size_t* sz) {
    FILE* fp = fopen(path, "r");
    if (fp == NULL) {
        log_error("The PNG image: %s could not be opened with error: %s", path, strerror(errno));
        return false;
    }
    bool ret = png_from_file_pointer(fp, path, data, width, height, sz);
    fclose(fp); fp = NULL;
    return ret;
}

