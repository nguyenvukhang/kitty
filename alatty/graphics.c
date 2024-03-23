/*
 * graphics.c
 * Copyright (C) 2017 Kovid Goyal <kovid at kovidgoyal.net>
 *
 * Distributed under terms of the GPL3 license.
 */

#include "graphics.h"
#include "state.h"
#include "disk-cache.h"

#include <fcntl.h>
#include <stdlib.h>

#include <structmember.h>
#include "png-reader.h"
PyTypeObject GraphicsManager_Type;

#define DEFAULT_STORAGE_LIMIT 320u * (1024u * 1024u)
#define REPORT_ERROR(...) { log_error(__VA_ARGS__); }
#define RAII_CoalescedFrameData(name, initializer) __attribute__((cleanup(cfd_free))) CoalescedFrameData name = initializer

// caching {{{
#define CACHE_KEY_BUFFER_SIZE 32

static size_t
cache_key(const ImageAndFrame x, char *key) {
    return snprintf(key, CACHE_KEY_BUFFER_SIZE, "%llx:%x", x.image_id, x.frame_id);
}
#define CK(x) key, cache_key(x, key)

static bool
remove_from_cache(GraphicsManager *self, const ImageAndFrame x) {
    char key[CACHE_KEY_BUFFER_SIZE];
    return remove_from_disk_cache(self->disk_cache, CK(x));
}

static bool
read_from_cache(const GraphicsManager *self, const ImageAndFrame x, void **data, size_t *sz) {
    char key[CACHE_KEY_BUFFER_SIZE];
    return read_from_disk_cache_simple(self->disk_cache, CK(x), data, sz, false);
}
#undef CK
// }}}


static inline id_type
next_id(id_type *counter) {
    id_type ans = ++(*counter);
    if (UNLIKELY(ans == 0)) ans = ++(*counter);
    return ans;
}

GraphicsManager*
grman_alloc(void) {
    GraphicsManager *self = (GraphicsManager *)GraphicsManager_Type.tp_alloc(&GraphicsManager_Type, 0);
    self->render_data.capacity = 64;
    self->render_data.item = calloc(self->render_data.capacity, sizeof(self->render_data.item[0]));
    self->storage_limit = DEFAULT_STORAGE_LIMIT;
    if (self->render_data.item == NULL) {
        PyErr_NoMemory();
        Py_CLEAR(self); return NULL;
    }
    self->disk_cache = create_disk_cache();
    if (!self->disk_cache) { Py_CLEAR(self); return NULL; }
    return self;
}

static void
free_refs_data(Image *img) {
    if (img->refs) {
        ImageRef *s, *tmp;
        HASH_ITER(hh, img->refs, s, tmp) {
            HASH_DEL(img->refs, s);
            free(s);
        }
    }
    img->refs = NULL;
}

static void
free_image_resources(GraphicsManager *self, Image *img) {
    if (img->texture_id) free_texture(&img->texture_id);
    ImageAndFrame key = { .image_id=img->internal_id, .frame_id = img->root_frame.id };
    if (!remove_from_cache(self, key) && PyErr_Occurred()) PyErr_Print();
    for (unsigned i = 0; i < img->extra_framecnt; i++) {
        key.frame_id = img->extra_frames[i].id;
        if (!remove_from_cache(self, key) && PyErr_Occurred()) PyErr_Print();
    }
    if (img->extra_frames) {
        free(img->extra_frames);
        img->extra_frames = NULL;
    }
    free_refs_data(img);
    self->used_storage -= img->used_storage;
}

static void
free_image(GraphicsManager *self, Image *img) {
    HASH_DEL(self->images, img);
    free_image_resources(self, img);
    free(img);
}

static void
dealloc(GraphicsManager* self) {
    if (self->images) {
        Image *img, *tmp;
        HASH_ITER(hh, self->images, img, tmp) {
            free_image(self, img);
        }
        self->images = NULL;
    }
    free(self->render_data.item);
    Py_CLEAR(self->disk_cache);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static Image*
img_by_client_id(const GraphicsManager *self, uint32_t id) {
    for (Image *img = self->images; img != NULL; img = img->hh.next) {
        if (img->client_id == id) return img;
    }
    return NULL;
}

static void
remove_image(GraphicsManager *self, Image *img) {
    free_image(self, img);
    self->layers_dirty = true;
}

// Loading image data {{{

// Decode formats {{{
#define ABRT(code, ...) { set_command_failed_response(#code, __VA_ARGS__); goto err; }

#undef ABRT
// }}}

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


#define ABRT(code, ...) { set_command_failed_response(code, __VA_ARGS__); self->currently_loading.loading_completed_successfully = false; free_load_data(&self->currently_loading); return NULL; }

#define MAX_DATA_SZ (4u * 100000000u)
enum FORMATS { RGB=24, RGBA=32, PNG=100 };

#define INIT_CHUNKED_LOAD { \
    self->currently_loading.start_command.more = g->more; \
    self->currently_loading.start_command.payload_sz = g->payload_sz; \
    g = &self->currently_loading.start_command; \
    tt = g->transmission_type ? g->transmission_type : 'd'; \
    fmt = g->format ? g->format : RGBA; \
}
#define MAX_IMAGE_DIMENSION 10000u

static void
upload_to_gpu(GraphicsManager *self, Image *img, const bool is_opaque, const bool is_4byte_aligned, const uint8_t *data) {
    if (!self->context_made_current_for_this_command) {
        if (!self->window_id) return;
        if (!make_window_context_current(self->window_id)) return;
        self->context_made_current_for_this_command = true;
    }
    // We use linear interpolation as the image may be resized on the GPU if r/c is specified or unicode placeholders are used.
    send_image_to_gpu(&img->texture_id, data, img->width, img->height, is_opaque, is_4byte_aligned, true, REPEAT_CLAMP);
}

// }}}

// Displaying images {{{

static void
update_src_rect(ImageRef *ref, Image *img) {
    // The src rect in OpenGL co-ords [0, 1] with origin at top-left corner of image
    ref->src_rect.left = (float)ref->src_x / (float)img->width;
    ref->src_rect.right = (float)(ref->src_x + ref->src_width) / (float)img->width;
    ref->src_rect.top = (float)ref->src_y / (float)img->height;
    ref->src_rect.bottom = (float)(ref->src_y + ref->src_height) / (float)img->height;
}

static void
update_dest_rect(ImageRef *ref, uint32_t num_cols, uint32_t num_rows, CellPixelSize cell) {
    uint32_t t;
    if (num_cols == 0) {
        t = (uint32_t)(ref->src_width + ref->cell_x_offset);
        num_cols = t / cell.width;
        if (t > num_cols * cell.width) num_cols += 1;
    }
    if (num_rows == 0) {
        t = (uint32_t)(ref->src_height + ref->cell_y_offset);
        num_rows = t / cell.height;
        if (t > num_rows * cell.height) num_rows += 1;
    }
    ref->effective_num_rows = num_rows;
    ref->effective_num_cols = num_cols;
}

static ImageRef*
create_ref(Image *img, ImageRef *clone_from) {
    ImageRef *ans = calloc(1, sizeof(ImageRef));
    if (!ans) fatal("Out of memory creating ImageRef");
    if (clone_from) {
        *ans = *clone_from;
        memset(&ans->hh, 0, sizeof(ans->hh));
    }
    ans->internal_id = next_id(&img->ref_id_counter);
    HASH_ADD(hh, img->refs, internal_id, sizeof(ans->internal_id), ans);
    return ans;
}

static inline bool
is_cell_image(const ImageRef *self) { return self->virtual_ref_id != 0; }

// Create a real image ref for a virtual image ref (placement) positioned in the
// given cells. This is used for images positioned using Unicode placeholders.
//
// The image is resized to fit a box of cells with dimensions
// `image_ref->columns` by `image_ref->rows`. The parameters `img_col`,
// `img_row, `columns`, `rows` describe a part of this box that we want to
// display.
//
// Parameters:
// - `self` - the graphics manager
// - `screen_row` - the starting row of the screen
// - `screen_col` - the starting column of the screen
// - `image_id` - the id of the image
// - `placement_id` - the id of the placement (0 to find it automatically), it
//                    must be a virtual placement
// - `img_col` - the column of the image box we want to start with (base 0)
// - `img_row` - the row of the image box we want to start with (base 0)
// - `columns` - the number of columns we want to display
// - `rows` - the number of rows we want to display
// - `cell` - the size of a screen cell
Image *grman_put_cell_image(GraphicsManager *self, uint32_t screen_row,
                            uint32_t screen_col, uint32_t image_id,
                            uint32_t placement_id, uint32_t img_col,
                            uint32_t img_row, uint32_t columns, uint32_t rows,
                            CellPixelSize cell) {
    Image *img = img_by_client_id(self, image_id);
    if (img == NULL) return NULL;

    ImageRef *virt_img_ref = NULL;
    if (placement_id) {
        // Find the placement by the id. It must be a virtual placement.
        for (ImageRef *r = img->refs; r != NULL; r = r->hh.next) {
            if (r->is_virtual_ref && r->client_id == placement_id) {
                virt_img_ref = r;
                break;
            }
        }
    } else {
        // Find the first virtual image placement.
        for (ImageRef *r = img->refs; r != NULL; r = r->hh.next) {
            if (r->is_virtual_ref) {
                virt_img_ref = r;
                break;
            }
        }
    }

    if (!virt_img_ref) return NULL;

    // Create the ref structure on stack first. We will not create a real
    // reference if the image is completely out of bounds.
    ImageRef ref = {0};
    ref.virtual_ref_id = virt_img_ref->internal_id;

    uint32_t img_rows = virt_img_ref->num_rows;
    uint32_t img_columns = virt_img_ref->num_cols;
    // If the number of columns or rows for the image is not set, compute them
    // in such a way that the image is as close as possible to its natural size.
    if (img_columns == 0)
        img_columns = (img->width + cell.width - 1) / cell.width;
    if (img_rows == 0) img_rows = (img->height + cell.height - 1) / cell.height;

    ref.start_row = screen_row;
    ref.start_column = screen_col;
    ref.num_cols = columns;
    ref.num_rows = rows;

    // The image is fit to the destination box of size
    //    (cell.width * img_columns) by (cell.height * img_rows)
    // The conversion from source (image) coordinates to destination (box)
    // coordinates is done by the following formula:
    //    x_dst = x_src * x_scale + x_offset
    //    y_dst = y_src * y_scale + y_offset
    float x_offset, y_offset, x_scale, y_scale;

    // Fit the image to the box while preserving aspect ratio
    if (img->width * img_rows * cell.height > img->height * img_columns * cell.width) {
        // Fit to width and center vertically.
        x_offset = 0;
        x_scale = (float)(img_columns * cell.width) / MAX(1u, img->width);
        y_scale = x_scale;
        y_offset = (img_rows * cell.height - img->height * y_scale) / 2;
    } else {
        // Fit to height and center horizontally.
        y_offset = 0;
        y_scale = (float)(img_rows * cell.height) / MAX(1u, img->height);
        x_scale = y_scale;
        x_offset = (img_columns * cell.width - img->width * x_scale) / 2;
    }

    // Now we can compute source (image) coordinates from destination (box)
    // coordinates by formula:
    //     x_src = (x_dst - x_offset) / x_scale
    //     y_src = (y_dst - y_offset) / y_scale

    // Destination (box) coordinates of the rectangle we want to display.
    uint32_t x_dst = img_col * cell.width;
    uint32_t y_dst = img_row * cell.height;
    uint32_t w_dst = columns * cell.width;
    uint32_t h_dst = rows * cell.height;

    // Compute the source coordinates of the rectangle.
    ref.src_x = (x_dst - x_offset) / x_scale;
    ref.src_y = (y_dst - y_offset) / y_scale;
    ref.src_width = w_dst / x_scale;
    ref.src_height = h_dst / y_scale;

    // If the top left corner is out of bounds of the source image, we can
    // adjust cell offsets and the starting row/column. And if the rectangle is
    // completely out of bounds, we can avoid creating a real reference. This
    // is just an optimization, the image will be displayed correctly even if we
    // do not do this.
    if (ref.src_x < 0) {
        ref.src_width += ref.src_x;
        ref.cell_x_offset = (uint32_t)(-ref.src_x * x_scale);
        ref.src_x = 0;
        uint32_t col_offset = ref.cell_x_offset / cell.width;
        ref.cell_x_offset %= cell.width;
        ref.start_column += col_offset;
        if (ref.num_cols <= col_offset)
            return img;
        ref.num_cols -= col_offset;
    }
    if (ref.src_y < 0) {
        ref.src_height += ref.src_y;
        ref.cell_y_offset = (uint32_t)(-ref.src_y * y_scale);
        ref.src_y = 0;
        uint32_t row_offset = ref.cell_y_offset / cell.height;
        ref.cell_y_offset %= cell.height;
        ref.start_row += row_offset;
        if (ref.num_rows <= row_offset)
            return img;
        ref.num_rows -= row_offset;
    }

    // For the bottom right corner we can remove only completely empty rows and
    // columns.
    if (ref.src_x + ref.src_width > img->width) {
        float redundant_w = ref.src_x + ref.src_width - img->width;
        uint32_t redundant_cols = (uint32_t)(redundant_w * x_scale) / cell.width;
        if (ref.num_cols <= redundant_cols)
            return img;
        ref.src_width -= redundant_cols * cell.width / x_scale;
        ref.num_cols -= redundant_cols;
    }
    if (ref.src_y + ref.src_height > img->height) {
        float redundant_h = ref.src_y + ref.src_height - img->height;
        uint32_t redundant_rows = (uint32_t)(redundant_h * y_scale) / cell.height;
        if (ref.num_rows <= redundant_rows)
            return img;
        ref.src_height -= redundant_rows * cell.height / y_scale;
        ref.num_rows -= redundant_rows;
    }

    // The cursor will be drawn on top of the image.
    ref.z_index = -1;

    // Create a real ref.
    ImageRef *real_ref = create_ref(img, &ref);

    img->atime = monotonic();
    self->layers_dirty = true;

    update_src_rect(real_ref, img);
    update_dest_rect(real_ref, ref.num_cols, ref.num_rows, cell);
    return img;
}

static void remove_ref(Image *img, ImageRef *ref);

void
scale_rendered_graphic(ImageRenderData *rd, float xstart, float ystart, float x_scale, float y_scale) {
    // Scale the graphic so that it appears at the same position and size during a live resize
    // this means scale factors are applied to both the position and size of the graphic.
    float width = rd->dest_rect.right - rd->dest_rect.left, height = rd->dest_rect.bottom - rd->dest_rect.top;
    rd->dest_rect.left = xstart + (rd->dest_rect.left - xstart) * x_scale;
    rd->dest_rect.right = rd->dest_rect.left + width * x_scale;
    rd->dest_rect.top = ystart + (rd->dest_rect.top - ystart) * y_scale;
    rd->dest_rect.bottom = rd->dest_rect.top + height * y_scale;
}

void
gpu_data_for_image(ImageRenderData *ans, float left, float top, float right, float bottom) {
    // For dest rect: x-axis is from -1 to 1, y axis is from 1 to -1
    static const ImageRef source_rect = { .src_rect = { .left=0, .top=0, .bottom=1, .right=1 }};
    ans->src_rect = source_rect.src_rect;
    ans->dest_rect = (ImageRect){ .left = left, .right = right, .top = top, .bottom = bottom };
    ans->group_count = 1;
}

// }}}

// Animation {{{
#define DEFAULT_GAP 40

static Frame*
current_frame(Image *img) {
    if (img->current_frame_index > img->extra_framecnt) return NULL;
    return img->current_frame_index ? img->extra_frames + img->current_frame_index - 1 : &img->root_frame;
}

static Frame*
frame_for_id(Image *img, const uint32_t frame_id) {
    if (img->root_frame.id == frame_id) return &img->root_frame;
    for (unsigned i = 0; i < img->extra_framecnt; i++) {
        if (img->extra_frames[i].id == frame_id) return img->extra_frames + i;
    }
    return NULL;
}

typedef struct {
    uint8_t *buf;
    bool is_4byte_aligned, is_opaque;
} CoalescedFrameData;

static void
blend_on_opaque(uint8_t *under_px, const uint8_t *over_px) {
    const float alpha = (float)over_px[3] / 255.f;
    const float alpha_op = 1.f - alpha;
    for (unsigned i = 0; i < 3; i++) under_px[i] = (uint8_t)(over_px[i] * alpha + under_px[i] * alpha_op);
}

static void
alpha_blend(uint8_t *dest_px, const uint8_t *src_px) {
    if (src_px[3]) {
        const float dest_a = (float)dest_px[3] / 255.f, src_a = (float)src_px[3] / 255.f;
        const float alpha = src_a + dest_a * (1.f - src_a);
        dest_px[3] = (uint8_t)(255 * alpha);
        if (!dest_px[3]) { dest_px[0] = 0; dest_px[1] = 0; dest_px[2] = 0; return; }
        for (unsigned i = 0; i < 3; i++) dest_px[i] = (uint8_t)((src_px[i] * src_a + dest_px[i] * dest_a * (1.f - src_a))/alpha);
    }
}

typedef struct {
    bool needs_blending;
    uint32_t over_px_sz, under_px_sz;
    uint32_t over_width, over_height, under_width, under_height, over_offset_x, over_offset_y, under_offset_x, under_offset_y;
    uint32_t stride;
} ComposeData;

#define COPY_RGB under_px[0] = over_px[0]; under_px[1] = over_px[1]; under_px[2] = over_px[2];
#define COPY_PIXELS \
    if (d.needs_blending) { \
        if (d.under_px_sz == 3) { \
            ROW_ITER PIX_ITER blend_on_opaque(under_px, over_px); }} \
        } else { \
            ROW_ITER PIX_ITER alpha_blend(under_px, over_px); }} \
        } \
    } else { \
        if (d.under_px_sz == 4) { \
            if (d.over_px_sz == 4) { \
                ROW_ITER PIX_ITER COPY_RGB under_px[3] = over_px[3]; }} \
            } else { \
                ROW_ITER PIX_ITER COPY_RGB under_px[3] = 255; }} \
            } \
        } else { \
            ROW_ITER PIX_ITER COPY_RGB }} \
        } \
    } \


static void
compose(const ComposeData d, uint8_t *under_data, const uint8_t *over_data) {
    const bool can_copy_rows = !d.needs_blending && d.over_px_sz == d.under_px_sz;
    unsigned min_row_sz = d.over_offset_x < d.under_width ? d.under_width - d.over_offset_x : 0;
    min_row_sz = MIN(min_row_sz, d.over_width);
#define ROW_ITER for (unsigned y = 0; y + d.over_offset_y < d.under_height && y < d.over_height; y++) { \
        uint8_t *under_row = under_data + (y + d.over_offset_y) * d.under_px_sz * d.under_width + d.under_px_sz * d.over_offset_x; \
        const uint8_t *over_row = over_data + y * d.over_px_sz * d.over_width;
#define END_ITER }
    if (can_copy_rows) {
        ROW_ITER memcpy(under_row, over_row, (size_t)d.over_px_sz * min_row_sz); END_ITER
        return;
    }
#define PIX_ITER for (unsigned x = 0; x < min_row_sz; x++) { \
        uint8_t *under_px = under_row + (d.under_px_sz * x); \
        const uint8_t *over_px = over_row + (d.over_px_sz * x);
    COPY_PIXELS
#undef COPY_RGB
#undef PIX_ITER
#undef ROW_ITER
#undef END_ITER
}

static CoalescedFrameData
get_coalesced_frame_data_standalone(const Image *img, const Frame *f, uint8_t *frame_data) {
    CoalescedFrameData ans = {0};
    bool is_full_frame = f->width == img->width && f->height == img->height && !f->x && !f->y;
    if (is_full_frame) {
        ans.buf = frame_data;
        ans.is_4byte_aligned = f->is_4byte_aligned;
        ans.is_opaque = f->is_opaque;
        return ans;
    }
    const unsigned bytes_per_pixel = f->is_opaque ? 3 : 4;
    uint8_t *base;
    if (f->bgcolor) {
        base = malloc((size_t)img->width * img->height * bytes_per_pixel);
        if (base) {
            uint8_t *p = base;
            const uint8_t r = (f->bgcolor >> 24) & 0xff,
                  g = (f->bgcolor >> 16) & 0xff, b = (f->bgcolor >> 8) & 0xff, a = f->bgcolor & 0xff;
            if (bytes_per_pixel == 4) {
                for (uint32_t i = 0; i < img->width * img->height; i++) {
                    *(p++) = r; *(p++) = g; *(p++) = b; *(p++) = a;
                }
            } else {
                for (uint32_t i = 0; i < img->width * img->height; i++) {
                    *(p++) = r; *(p++) = g; *(p++) = b;
                }
            }
        }
    } else base = calloc((size_t)img->width * img->height, bytes_per_pixel);
    if (!base) { free(frame_data); return ans; }
    ComposeData d = {
        .over_px_sz = bytes_per_pixel, .under_px_sz = bytes_per_pixel,
        .over_width = f->width, .over_height = f->height, .over_offset_x = f->x, .over_offset_y = f->y,
        .under_width = img->width, .under_height = img->height,
        .needs_blending = f->alpha_blend && !f->is_opaque
    };
    compose(d, base, frame_data);
    ans.buf = base;
    ans.is_4byte_aligned = bytes_per_pixel == 4 || (img->width % 4) == 0;
    ans.is_opaque = f->is_opaque;
    free(frame_data);
    return ans;
}


static CoalescedFrameData
get_coalesced_frame_data_impl(GraphicsManager *self, Image *img, const Frame *f, unsigned count) {
    CoalescedFrameData ans = {0};
    if (count > 32) return ans;  // prevent stack overflows, infinite recursion
    size_t frame_data_sz; void *frame_data;
    ImageAndFrame key = {.image_id = img->internal_id, .frame_id = f->id};
    if (!read_from_cache(self, key, &frame_data, &frame_data_sz)) return ans;
    if (!f->base_frame_id) return get_coalesced_frame_data_standalone(img, f, frame_data);
    Frame *base = frame_for_id(img, f->base_frame_id);
    if (!base) { free(frame_data); return ans; }
    CoalescedFrameData base_data = get_coalesced_frame_data_impl(self, img, base, count + 1);
    if (!base_data.buf) { free(frame_data); return ans; }
    ComposeData d = {
        .over_px_sz = f->is_opaque ? 3 : 4,
        .under_px_sz = base_data.is_opaque ? 3 : 4,
        .over_width = f->width, .over_height = f->height, .over_offset_x = f->x, .over_offset_y = f->y,
        .under_width = img->width, .under_height = img->height,
        .needs_blending = f->alpha_blend && !f->is_opaque
    };
    compose(d, base_data.buf, frame_data);
    free(frame_data);
    return base_data;
}

static CoalescedFrameData
get_coalesced_frame_data(GraphicsManager *self, Image *img, const Frame *f) {
    return get_coalesced_frame_data_impl(self, img, f, 0);
}

static void
update_current_frame(GraphicsManager *self, Image *img, const CoalescedFrameData *data) {
    bool needs_load = data == NULL;
    CoalescedFrameData cfd;
    if (needs_load) {
        Frame *f = current_frame(img);
        if (f == NULL) return;
        cfd = get_coalesced_frame_data(self, img, f);
        if (!cfd.buf) {
            if (PyErr_Occurred()) PyErr_Print();
            return;
        }
        data = &cfd;
    }
    upload_to_gpu(self, img, data->is_opaque, data->is_4byte_aligned, data->buf);
    if (needs_load) free(data->buf);
    img->current_frame_shown_at = monotonic();
}

#undef ABRT

static bool
image_is_animatable(const Image *img) {
    return img->animation_state != ANIMATION_STOPPED && img->extra_framecnt && img->is_drawn && img->animation_duration && (
            !img->max_loops || img->current_loop < img->max_loops);
}

bool
scan_active_animations(GraphicsManager *self, const monotonic_t now, monotonic_t *minimum_gap, bool os_window_context_set) {
    bool dirtied = false;
    *minimum_gap = MONOTONIC_T_MAX;
    if (!self->has_images_needing_animation) return dirtied;
    self->has_images_needing_animation = false;
    self->context_made_current_for_this_command = os_window_context_set;
    for (Image *img = self->images; img != NULL; img = img->hh.next) {
        if (image_is_animatable(img)) {
            Frame *f = current_frame(img);
            if (f) {
                self->has_images_needing_animation = true;
                monotonic_t next_frame_at = img->current_frame_shown_at + ms_to_monotonic_t(f->gap);
                if (now >= next_frame_at) {
                    do {
                        uint32_t next = (img->current_frame_index + 1) % (img->extra_framecnt + 1);
                        if (!next) {
                            if (img->animation_state == ANIMATION_LOADING) goto skip_image;
                            if (++img->current_loop >= img->max_loops && img->max_loops) goto skip_image;
                        }
                        img->current_frame_index = next;
                    } while (!current_frame(img)->gap);
                    dirtied = true;
                    update_current_frame(self, img, NULL);
                    f = current_frame(img);
                    next_frame_at = img->current_frame_shown_at + ms_to_monotonic_t(f->gap);
                }
                if (next_frame_at > now && next_frame_at - now < *minimum_gap) *minimum_gap = next_frame_at - now;
            }
        }
        skip_image:;
    }
    return dirtied;
}
// }}}

// Image lifetime/scrolling {{{

static void
remove_ref(Image *img, ImageRef *ref) {
    HASH_DEL(img->refs, ref);
    free(ref);
}

static void
filter_refs(GraphicsManager *self, const void* data, bool free_images, bool (*filter_func)(const ImageRef*, Image*, const void*, CellPixelSize), CellPixelSize cell, bool only_first_image) {
    bool matched = false;
    Image *img, *tmp;
    HASH_ITER(hh, self->images, img, tmp) {
        if (img->refs) {
            ImageRef *ref, *tmp;
            HASH_ITER(hh, img->refs, ref, tmp) {
                if (filter_func(ref, img, data, cell)) {
                    remove_ref(img, ref);
                    self->layers_dirty = true;
                    matched = true;
                }
            }
        }
        if (!img->refs && (free_images || img->client_id == 0)) remove_image(self, img);
        if (only_first_image && matched) break;
    }
}


static void
modify_refs(GraphicsManager *self, const void* data, bool (*filter_func)(ImageRef*, Image*, const void*, CellPixelSize), CellPixelSize cell) {
    Image *img, *tmp;
    HASH_ITER(hh, self->images, img, tmp) {
        if (img->refs) {
            ImageRef *ref, *tmp;
            HASH_ITER(hh, img->refs, ref, tmp) {
                if (filter_func(ref, img, data, cell)) remove_ref(img, ref);
            }
        }
        if (!img->refs && img->client_id == 0 && img->client_number == 0) {
            // references have all scrolled off the history buffer and the image has no way to reference it
            // to create new references so remove it.
            remove_image(self, img);
        }
    }
}


static bool
scroll_filter_func(ImageRef *ref, Image UNUSED *img, const void *data, CellPixelSize cell UNUSED) {
    if (ref->is_virtual_ref) return false;
    ScrollData *d = (ScrollData*)data;
    ref->start_row += d->amt;
    return ref->start_row + (int32_t)ref->effective_num_rows <= d->limit;
}

static bool
ref_within_region(const ImageRef *ref, index_type margin_top, index_type margin_bottom) {
    return ref->start_row >= (int32_t)margin_top && ref->start_row + (int32_t)ref->effective_num_rows - 1 <= (int32_t)margin_bottom;
}

static bool
ref_outside_region(const ImageRef *ref, index_type margin_top, index_type margin_bottom) {
    return ref->start_row + (int32_t)ref->effective_num_rows <= (int32_t)margin_top || ref->start_row > (int32_t)margin_bottom;
}

static bool
scroll_filter_margins_func(ImageRef* ref, Image* img, const void* data, CellPixelSize cell) {
    if (ref->is_virtual_ref) return false;
    ScrollData *d = (ScrollData*)data;
    if (ref_within_region(ref, d->margin_top, d->margin_bottom)) {
        ref->start_row += d->amt;
        if (ref_outside_region(ref, d->margin_top, d->margin_bottom)) return true;
        // Clip the image if scrolling has resulted in part of it being outside the page area
        uint32_t clip_amt, clipped_rows;
        if (ref->start_row < (int32_t)d->margin_top) {
            // image moved up
            clipped_rows = d->margin_top - ref->start_row;
            clip_amt = cell.height * clipped_rows;
            if (ref->src_height <= clip_amt) return true;
            ref->src_y += clip_amt; ref->src_height -= clip_amt;
            ref->effective_num_rows -= clipped_rows;
            update_src_rect(ref, img);
            ref->start_row += clipped_rows;
        } else if (ref->start_row + (int32_t)ref->effective_num_rows - 1 > (int32_t)d->margin_bottom) {
            // image moved down
            clipped_rows = ref->start_row + ref->effective_num_rows - 1 - d->margin_bottom;
            clip_amt = cell.height * clipped_rows;
            if (ref->src_height <= clip_amt) return true;
            ref->src_height -= clip_amt;
            ref->effective_num_rows -= clipped_rows;
            update_src_rect(ref, img);
        }
        return ref_outside_region(ref, d->margin_top, d->margin_bottom);
    }
    return false;
}

void
grman_scroll_images(GraphicsManager *self, const ScrollData *data, CellPixelSize cell) {
    if (self->images) {
        self->layers_dirty = true;
        modify_refs(self, data, data->has_margins ? scroll_filter_margins_func : scroll_filter_func, cell);
    }
}

static bool
cell_image_row_filter_func(const ImageRef *ref, Image UNUSED *img, const void *data, CellPixelSize cell UNUSED) {
    if (ref->is_virtual_ref || !is_cell_image(ref))
        return false;
    int32_t top = *(int32_t *)data;
    int32_t bottom = *((int32_t *)data + 1);
    return ref_within_region(ref, top, bottom);
}

static bool
cell_image_filter_func(const ImageRef *ref, Image UNUSED *img, const void *data UNUSED, CellPixelSize cell UNUSED) {
    return !ref->is_virtual_ref && is_cell_image(ref);
}

// Remove cell images within the given region.
void
grman_remove_cell_images(GraphicsManager *self, int32_t top, int32_t bottom) {
    CellPixelSize dummy = {0};
    int32_t data[] = {top, bottom};
    filter_refs(self, data, false, cell_image_row_filter_func, dummy, false);
}

void
grman_remove_all_cell_images(GraphicsManager *self) {
    CellPixelSize dummy = {0};
    filter_refs(self, NULL, false, cell_image_filter_func, dummy, false);
}


static bool
clear_filter_func(const ImageRef *ref, Image UNUSED *img, const void UNUSED *data, CellPixelSize cell UNUSED) {
    if (ref->is_virtual_ref) return false;
    return ref->start_row + (int32_t)ref->effective_num_rows > 0;
}

static bool
clear_all_filter_func(const ImageRef *ref UNUSED, Image UNUSED *img, const void UNUSED *data, CellPixelSize cell UNUSED) {
    if (ref->is_virtual_ref) return false;
    return true;
}

void
grman_clear(GraphicsManager *self, bool all, CellPixelSize cell) {
    filter_refs(self, NULL, true, all ? clear_all_filter_func : clear_filter_func, cell, false);
}

// }}}

// Boilerplate {{{
static PyObject *
new(PyTypeObject UNUSED *type, PyObject UNUSED *args, PyObject UNUSED *kwds) {
    PyObject *ans = (PyObject*)grman_alloc();
    if (ans == NULL) PyErr_NoMemory();
    return ans;
}

#define W(x) static PyObject* py##x(GraphicsManager UNUSED *self, PyObject *args)
#define PA(fmt, ...) if(!PyArg_ParseTuple(args, fmt, __VA_ARGS__)) return NULL;

static PyMethodDef methods[] = {
    {NULL}  /* Sentinel */
};

static PyObject*
get_image_count(GraphicsManager *self, void* closure UNUSED) {
    unsigned long ans = HASH_COUNT(self->images);
    return PyLong_FromUnsignedLong(ans);
}
static PyGetSetDef getsets[] = {
    {"image_count", (getter)get_image_count, NULL, NULL, NULL},
    {NULL},
};

static PyMemberDef members[] = {
    {"storage_limit", T_PYSSIZET, offsetof(GraphicsManager, storage_limit), 0, "storage_limit"},
    {"disk_cache", T_OBJECT_EX, offsetof(GraphicsManager, disk_cache), READONLY, "disk_cache"},
    {NULL},
};

PyTypeObject GraphicsManager_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fast_data_types.GraphicsManager",
    .tp_basicsize = sizeof(GraphicsManager),
    .tp_dealloc = (destructor)dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = "GraphicsManager",
    .tp_new = new,
    .tp_methods = methods,
    .tp_members = members,
    .tp_getset = getsets,
};

static PyMethodDef module_methods[] = {
    {NULL, NULL, 0, NULL}        /* Sentinel */
};


bool
init_graphics(PyObject *module) {
    if (PyType_Ready(&GraphicsManager_Type) < 0) return false;
    if (PyModule_AddObject(module, "GraphicsManager", (PyObject *)&GraphicsManager_Type) != 0) return false;
    if (PyModule_AddFunctions(module, module_methods) != 0) return false;
    if (PyModule_AddIntMacro(module, IMAGE_PLACEHOLDER_CHAR) != 0) return false;
    Py_INCREF(&GraphicsManager_Type);
    return true;
}
// }}}
