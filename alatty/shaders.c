/*
 * shaders.c
 * Copyright (C) 2017 Kovid Goyal <kovid at kovidgoyal.net>
 *
 * Distributed under terms of the GPL3 license.
 */

#include "fonts.h"
#include "gl.h"
#include "colors.h"
#include <stddef.h>
#include "srgb_gamma.h"
#include "uniforms_generated.h"

#define BLEND_ONTO_OPAQUE  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);  // blending onto opaque colors
#define BLEND_ONTO_OPAQUE_WITH_OPAQUE_OUTPUT  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);  // blending onto opaque colors with final color having alpha 1
#define BLEND_PREMULT glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);  // blending of pre-multiplied colors

enum { CELL_PROGRAM, CELL_BG_PROGRAM, CELL_SPECIAL_PROGRAM, CELL_FG_PROGRAM, BORDERS_PROGRAM, GRAPHICS_PROGRAM, GRAPHICS_PREMULT_PROGRAM, GRAPHICS_ALPHA_MASK_PROGRAM, BGIMAGE_PROGRAM, TINT_PROGRAM, NUM_PROGRAMS };
enum { SPRITE_MAP_UNIT, GRAPHICS_UNIT, BGIMAGE_UNIT };

// Sprites {{{
typedef struct {
    unsigned int cell_width, cell_height;
    int xnum, ynum, x, y, z, last_num_of_layers, last_ynum;
    GLuint texture_id;
    GLint max_texture_size, max_array_texture_layers;
} SpriteMap;

static const SpriteMap NEW_SPRITE_MAP = { .xnum = 1, .ynum = 1, .last_num_of_layers = 1, .last_ynum = -1 };
static GLint max_texture_size = 0, max_array_texture_layers = 0;

static GLfloat
srgb_color(uint8_t color) {
    return srgb_lut[color];
}

SPRITE_MAP_HANDLE
alloc_sprite_map(unsigned int cell_width, unsigned int cell_height) {
    if (!max_texture_size) {
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &(max_texture_size));
        glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &(max_array_texture_layers));
#ifdef __APPLE__
        // Since on Apple we could have multiple GPUs, with different capabilities,
        // upper bound the values according to the data from https://developer.apple.com/graphicsimaging/opengl/capabilities/
        max_texture_size = MIN(8192, max_texture_size);
        max_array_texture_layers = MIN(512, max_array_texture_layers);
#endif
        sprite_tracker_set_limits(max_texture_size, max_array_texture_layers);
    }
    SpriteMap *ans = calloc(1, sizeof(SpriteMap));
    if (!ans) fatal("Out of memory allocating a sprite map");
    *ans = NEW_SPRITE_MAP;
    ans->max_texture_size = max_texture_size;
    ans->max_array_texture_layers = max_array_texture_layers;
    ans->cell_width = cell_width; ans->cell_height = cell_height;
    return (SPRITE_MAP_HANDLE)ans;
}

SPRITE_MAP_HANDLE
free_sprite_map(SPRITE_MAP_HANDLE sm) {
    SpriteMap *sprite_map = (SpriteMap*)sm;
    if (sprite_map) {
        if (sprite_map->texture_id) free_texture(&sprite_map->texture_id);
        free(sprite_map);
    }
    return NULL;
}

static bool copy_image_warned = false;

static void
copy_image_sub_data(GLuint src_texture_id, GLuint dest_texture_id, unsigned int width, unsigned int height, unsigned int num_levels) {
    if (!GLAD_GL_ARB_copy_image) {
        // ARB_copy_image not available, do a slow roundtrip copy
        if (!copy_image_warned) {
            copy_image_warned = true;
            log_error("WARNING: Your system's OpenGL implementation does not have glCopyImageSubData, falling back to a slower implementation");
        }
        size_t sz = (size_t)width * height * num_levels;
        pixel *src = malloc(sz * sizeof(pixel));
        if (src == NULL) { fatal("Out of memory."); }
        glBindTexture(GL_TEXTURE_2D_ARRAY, src_texture_id);
        glGetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, GL_UNSIGNED_BYTE, src);
        glBindTexture(GL_TEXTURE_2D_ARRAY, dest_texture_id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, width, height, num_levels, GL_RGBA, GL_UNSIGNED_BYTE, src);
        free(src);
    } else {
        glCopyImageSubData(src_texture_id, GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, dest_texture_id, GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, width, height, num_levels);
    }
}


static void
realloc_sprite_texture(FONTS_DATA_HANDLE fg) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
    // We use GL_NEAREST otherwise glyphs that touch the edge of the cell
    // often show a border between cells
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    unsigned int xnum, ynum, z, znum, width, height, src_ynum;
    sprite_tracker_current_layout(fg, &xnum, &ynum, &z);
    znum = z + 1;
    SpriteMap *sprite_map = (SpriteMap*)fg->sprite_map;
    width = xnum * sprite_map->cell_width; height = ynum * sprite_map->cell_height;
    glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_SRGB8_ALPHA8, width, height, znum);
    if (sprite_map->texture_id) {
        // need to re-alloc
        src_ynum = MAX(1, sprite_map->last_ynum);
        copy_image_sub_data(sprite_map->texture_id, tex, width, src_ynum * sprite_map->cell_height, sprite_map->last_num_of_layers);
        glDeleteTextures(1, &sprite_map->texture_id);
    }
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    sprite_map->last_num_of_layers = znum;
    sprite_map->last_ynum = ynum;
    sprite_map->texture_id = tex;
}

static void
ensure_sprite_map(FONTS_DATA_HANDLE fg) {
    SpriteMap *sprite_map = (SpriteMap*)fg->sprite_map;
    if (!sprite_map->texture_id) realloc_sprite_texture(fg);
    // We have to rebind since we don't know if the texture was ever bound
    // in the context of the current OSWindow
    glActiveTexture(GL_TEXTURE0 + SPRITE_MAP_UNIT);
    glBindTexture(GL_TEXTURE_2D_ARRAY, sprite_map->texture_id);
}

void
send_sprite_to_gpu(FONTS_DATA_HANDLE fg, unsigned int x, unsigned int y, unsigned int z, pixel *buf) {
    SpriteMap *sprite_map = (SpriteMap*)fg->sprite_map;
    unsigned int xnum, ynum, znum;
    sprite_tracker_current_layout(fg, &xnum, &ynum, &znum);
    if ((int)znum >= sprite_map->last_num_of_layers || (znum == 0 && (int)ynum > sprite_map->last_ynum)) realloc_sprite_texture(fg);
    glBindTexture(GL_TEXTURE_2D_ARRAY, sprite_map->texture_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    x *= sprite_map->cell_width; y *= sprite_map->cell_height;
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, x, y, z, sprite_map->cell_width, sprite_map->cell_height, 1, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, buf);
}

void
send_image_to_gpu(GLuint *tex_id, const void* data, GLsizei width, GLsizei height, bool is_opaque, bool is_4byte_aligned, bool linear, RepeatStrategy repeat) {
    if (!(*tex_id)) { glGenTextures(1, tex_id);  }
    glBindTexture(GL_TEXTURE_2D, *tex_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, is_4byte_aligned ? 4 : 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    RepeatStrategy r;
    switch (repeat) {
        case REPEAT_MIRROR:
            r = GL_MIRRORED_REPEAT; break;
        case REPEAT_CLAMP: {
            static const GLfloat border_color[4] = {0};
            glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border_color);
            r = GL_CLAMP_TO_BORDER;
            break;
        }
        default:
            r = GL_REPEAT;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, r);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, r);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB_ALPHA, width, height, 0, is_opaque ? GL_RGB : GL_RGBA, GL_UNSIGNED_BYTE, data);
}

// }}}

// Cell {{{

typedef struct CellRenderData {
    struct {
        GLfloat xstart, ystart, dx, dy, width, height;
    } gl;
    float x_ratio, y_ratio;
} CellRenderData;

typedef struct {
    UniformBlock render_data;
    ArrayInformation color_table;
    CellUniforms uniforms;
} CellProgramLayout;
static CellProgramLayout cell_program_layouts[NUM_PROGRAMS];

typedef struct {
    GraphicsUniforms uniforms;
} GraphicsProgramLayout;
static GraphicsProgramLayout graphics_program_layouts[NUM_PROGRAMS];

static void
init_cell_program(void) {
    for (int i = CELL_PROGRAM; i < BORDERS_PROGRAM; i++) {
        cell_program_layouts[i].render_data.index = block_index(i, "CellRenderData");
        cell_program_layouts[i].render_data.size = block_size(i, cell_program_layouts[i].render_data.index);
        cell_program_layouts[i].color_table.size = get_uniform_information(i, "color_table[0]", GL_UNIFORM_SIZE);
        cell_program_layouts[i].color_table.offset = get_uniform_information(i, "color_table[0]", GL_UNIFORM_OFFSET);
        cell_program_layouts[i].color_table.stride = get_uniform_information(i, "color_table[0]", GL_UNIFORM_ARRAY_STRIDE);
        get_uniform_locations_cell(i, &cell_program_layouts[i].uniforms);
        bind_program(i);
        glUniform1fv(cell_program_layouts[i].uniforms.gamma_lut, arraysz(srgb_lut), srgb_lut);
    }

    // Sanity check to ensure the attribute location binding worked
#define C(p, name, expected) { int aloc = attrib_location(p, #name); if (aloc != expected && aloc != -1) fatal("The attribute location for %s is %d != %d in program: %d", #name, aloc, expected, p); }
    for (int p = CELL_PROGRAM; p < BORDERS_PROGRAM; p++) {
        C(p, colors, 0); C(p, sprite_coords, 1); C(p, is_selected, 2);
    }
#undef C
    for (int i = GRAPHICS_PROGRAM; i <= GRAPHICS_ALPHA_MASK_PROGRAM; i++) {
        get_uniform_locations_graphics(i, &graphics_program_layouts[i].uniforms);
    }
}

#define CELL_BUFFERS enum { cell_data_buffer, selection_buffer, uniform_buffer };

ssize_t
create_cell_vao(void) {
    ssize_t vao_idx = create_vao();
#define A(name, size, dtype, offset, stride) \
    add_attribute_to_vao(CELL_PROGRAM, vao_idx, #name, \
            /*size=*/size, /*dtype=*/dtype, /*stride=*/stride, /*offset=*/offset, /*divisor=*/1);
#define A1(name, size, dtype, offset) A(name, size, dtype, (void*)(offsetof(GPUCell, offset)), sizeof(GPUCell))

    add_buffer_to_vao(vao_idx, GL_ARRAY_BUFFER);
    A1(sprite_coords, 4, GL_UNSIGNED_SHORT, sprite_x);
    A1(colors, 3, GL_UNSIGNED_INT, fg);

    add_buffer_to_vao(vao_idx, GL_ARRAY_BUFFER);
    A(is_selected, 1, GL_UNSIGNED_BYTE, NULL, 0);

    size_t bufnum = add_buffer_to_vao(vao_idx, GL_UNIFORM_BUFFER);
    alloc_vao_buffer(vao_idx, cell_program_layouts[CELL_PROGRAM].render_data.size, bufnum, GL_STREAM_DRAW);

    return vao_idx;
#undef A
#undef A1
}

ssize_t
create_graphics_vao(void) {
    ssize_t vao_idx = create_vao();
    add_buffer_to_vao(vao_idx, GL_ARRAY_BUFFER);
    add_attribute_to_vao(GRAPHICS_PROGRAM, vao_idx, "src", 4, GL_FLOAT, 0, NULL, 0);
    return vao_idx;
}

#define IS_SPECIAL_COLOR(name) (screen->color_profile->overridden.name.type == COLOR_IS_SPECIAL || (screen->color_profile->overridden.name.type == COLOR_NOT_SET && screen->color_profile->configured.name.type == COLOR_IS_SPECIAL))

static void
pick_cursor_color(Line *line, ColorProfile *color_profile, color_type cell_fg, color_type cell_bg, index_type cell_color_x, color_type *cursor_fg, color_type *cursor_bg, color_type default_fg, color_type default_bg) {
    ARGB32 fg, bg, dfg, dbg;
    (void) line; (void) color_profile; (void) cell_color_x;
    fg.rgb = cell_fg; bg.rgb = cell_bg;
    *cursor_fg = cell_bg; *cursor_bg = cell_fg;
    double cell_contrast = rgb_contrast(fg, bg);
    if (cell_contrast < 2.5) {
        dfg.rgb = default_fg; dbg.rgb = default_bg;
        if (rgb_contrast(dfg, dbg) > cell_contrast) {
            *cursor_fg = default_bg; *cursor_bg = default_fg;
        }
    }
}

static void
cell_update_uniform_block(ssize_t vao_idx, Screen *screen, int uniform_buffer, const CellRenderData *crd, CursorRenderInfo *cursor, bool inverted, OSWindow *os_window) {
    struct GPUCellRenderData {
        GLfloat xstart, ystart, dx, dy, sprite_dx, sprite_dy, background_opacity, use_cell_bg_for_selection_fg, use_cell_fg_for_selection_color, use_cell_for_selection_bg;

        GLuint default_fg, default_bg, highlight_fg, highlight_bg, cursor_fg, cursor_bg, inverted;

        GLuint xnum, ynum, cursor_fg_sprite_idx;
        GLfloat cursor_x, cursor_y, cursor_w;
    };
    // Send the uniform data
    struct GPUCellRenderData *rd = (struct GPUCellRenderData*)map_vao_buffer(vao_idx, uniform_buffer, GL_WRITE_ONLY);
    if (UNLIKELY(screen->color_profile->dirty || screen->reload_all_gpu_data)) {
        copy_color_table_to_buffer(screen->color_profile, (GLuint*)rd, cell_program_layouts[CELL_PROGRAM].color_table.offset / sizeof(GLuint), cell_program_layouts[CELL_PROGRAM].color_table.stride / sizeof(GLuint));
    }
#define COLOR(name) colorprofile_to_color(screen->color_profile, screen->color_profile->overridden.name, screen->color_profile->configured.name).rgb
    rd->default_fg = COLOR(default_fg); rd->default_bg = COLOR(default_bg);
    rd->highlight_fg = COLOR(highlight_fg); rd->highlight_bg = COLOR(highlight_bg);
    // selection
    if (IS_SPECIAL_COLOR(highlight_fg)) {
        if (IS_SPECIAL_COLOR(highlight_bg)) {
            rd->use_cell_bg_for_selection_fg = 1.f; rd->use_cell_fg_for_selection_color = 0.f;
        } else {
            rd->use_cell_bg_for_selection_fg = 0.f; rd->use_cell_fg_for_selection_color = 1.f;
        }
    } else {
        rd->use_cell_bg_for_selection_fg = 0.f; rd->use_cell_fg_for_selection_color = 0.f;
    }
    rd->use_cell_for_selection_bg = IS_SPECIAL_COLOR(highlight_bg) ? 1. : 0.;
    // Cursor position
    enum { BLOCK_IDX = 0, BEAM_IDX = NUM_UNDERLINE_STYLES + 3, UNDERLINE_IDX = NUM_UNDERLINE_STYLES + 4, UNFOCUSED_IDX = NUM_UNDERLINE_STYLES + 5 };
    if (cursor->is_visible) {
        rd->cursor_x = cursor->x, rd->cursor_y = cursor->y;
        if (cursor->is_focused) {
            switch(cursor->shape) {
                default:
                    rd->cursor_fg_sprite_idx = BLOCK_IDX; break;
                case CURSOR_BEAM:
                    rd->cursor_fg_sprite_idx = BEAM_IDX; break;
                case CURSOR_UNDERLINE:
                    rd->cursor_fg_sprite_idx = UNDERLINE_IDX; break;
            }
        } else rd->cursor_fg_sprite_idx = UNFOCUSED_IDX;
        color_type cell_fg = rd->default_fg, cell_bg = rd->default_bg;
        index_type cell_color_x = cursor->x;
        bool cursor_ok = cursor->x < screen->columns && cursor->y < screen->lines;
        bool reversed = false;
        if (cursor_ok) {
            linebuf_init_line(screen->linebuf, cursor->y);
            colors_for_cell(screen->linebuf->line, screen->color_profile, &cell_color_x, &cell_fg, &cell_bg, &reversed);
        }
        if (IS_SPECIAL_COLOR(cursor_color)) {
            if (cursor_ok) pick_cursor_color(screen->linebuf->line, screen->color_profile, cell_fg, cell_bg, cell_color_x, &rd->cursor_fg, &rd->cursor_bg, rd->default_fg, rd->default_bg);
            else { rd->cursor_fg = rd->default_bg; rd->cursor_bg = rd->default_fg; }
            if (cell_bg == cell_fg) {
                rd->cursor_fg = rd->default_bg; rd->cursor_bg = rd->default_fg;
            } else { rd->cursor_fg = cell_bg; rd->cursor_bg = cell_fg; }
        } else {
            rd->cursor_bg = COLOR(cursor_color);
            if (IS_SPECIAL_COLOR(cursor_text_color)) rd->cursor_fg = cell_bg;
            else rd->cursor_fg = COLOR(cursor_text_color);
        }
    } else rd->cursor_x = screen->columns, rd->cursor_y = screen->lines;
    rd->cursor_w = rd->cursor_x;
    if (
            (rd->cursor_fg_sprite_idx == BLOCK_IDX || rd->cursor_fg_sprite_idx == UNDERLINE_IDX) &&
            screen_current_char_width(screen) > 1
    ) rd->cursor_w += 1;

    rd->xnum = screen->columns; rd->ynum = screen->lines;

    rd->xstart = crd->gl.xstart; rd->ystart = crd->gl.ystart; rd->dx = crd->gl.dx; rd->dy = crd->gl.dy;
    unsigned int x, y, z;
    sprite_tracker_current_layout(os_window->fonts_data, &x, &y, &z);
    rd->sprite_dx = 1.0f / (float)x; rd->sprite_dy = 1.0f / (float)y;
    rd->inverted = inverted ? 1 : 0;
    rd->background_opacity = os_window->is_semi_transparent ? os_window->background_opacity : 1.0f;

#undef COLOR

    unmap_vao_buffer(vao_idx, uniform_buffer); rd = NULL;
}

static bool
cell_prepare_to_render(ssize_t vao_idx, Screen *screen, FONTS_DATA_HANDLE fonts_data) {
    size_t sz;
    CELL_BUFFERS;
    void *address;
    bool changed = false;

    ensure_sprite_map(fonts_data);

    bool cursor_pos_changed = screen->cursor->x != screen->last_rendered.cursor_x
                           || screen->cursor->y != screen->last_rendered.cursor_y;
    bool screen_resized = screen->last_rendered.columns != screen->columns || screen->last_rendered.lines != screen->lines;

    if (screen->reload_all_gpu_data || screen->scroll_changed || screen->is_dirty || screen_resized || cursor_pos_changed) {
        sz = sizeof(GPUCell) * screen->lines * screen->columns;
        address = alloc_and_map_vao_buffer(vao_idx, sz, cell_data_buffer, GL_STREAM_DRAW, GL_WRITE_ONLY);
        screen_update_cell_data(screen, address, fonts_data, cursor_pos_changed);
        unmap_vao_buffer(vao_idx, cell_data_buffer); address = NULL;
        changed = true;
    }

    if (cursor_pos_changed) {
        screen->last_rendered.cursor_x = screen->cursor->x;
        screen->last_rendered.cursor_y = screen->cursor->y;
    }

    if (screen->reload_all_gpu_data || screen_resized || screen_is_selection_dirty(screen)) {
        sz = (size_t)screen->lines * screen->columns;
        address = alloc_and_map_vao_buffer(vao_idx, sz, selection_buffer, GL_STREAM_DRAW, GL_WRITE_ONLY);
        screen_apply_selection(screen, address, sz);
        unmap_vao_buffer(vao_idx, selection_buffer); address = NULL;
        changed = true;
    }

    screen->last_rendered.scrolled_by = screen->scrolled_by;
    screen->last_rendered.columns = screen->columns;
    screen->last_rendered.lines = screen->lines;
    return changed;
}

static float prev_inactive_text_alpha = -1;

static void
set_cell_uniforms(float current_inactive_text_alpha, bool force) {
    static bool constants_set = false;
    if (!constants_set || force) {
        float text_contrast = 1.0f + OPT(text_contrast) * 0.01f;
        float text_gamma_adjustment = OPT(text_gamma_adjustment) < 0.01f ? 1.0f : 1.0f / OPT(text_gamma_adjustment);

        for (int i = GRAPHICS_PROGRAM; i <= GRAPHICS_PREMULT_PROGRAM; i++) {
            bind_program(i); glUniform1i(graphics_program_layouts[i].uniforms.image, GRAPHICS_UNIT);
        }
        for (int i = CELL_PROGRAM; i <= CELL_FG_PROGRAM; i++) {
            bind_program(i); const CellUniforms *cu = &cell_program_layouts[i].uniforms;
            switch(i) {
                case CELL_PROGRAM: case CELL_FG_PROGRAM:
                    glUniform1i(cu->sprites, SPRITE_MAP_UNIT);
                    glUniform1f(cu->dim_opacity, OPT(dim_opacity));
                    glUniform1f(cu->text_contrast, text_contrast);
                    glUniform1f(cu->text_gamma_adjustment, text_gamma_adjustment);
                    break;
            }
        }
        constants_set = true;
    }
    if (current_inactive_text_alpha != prev_inactive_text_alpha || force) {
        prev_inactive_text_alpha = current_inactive_text_alpha;
        for (int i = GRAPHICS_PROGRAM; i <= GRAPHICS_PREMULT_PROGRAM; i++) {
            bind_program(i); glUniform1f(graphics_program_layouts[i].uniforms.inactive_text_alpha, current_inactive_text_alpha);
        }
#define S(prog, loc) bind_program(prog); glUniform1f(cell_program_layouts[prog].uniforms.inactive_text_alpha, current_inactive_text_alpha);
        S(CELL_PROGRAM, cploc); S(CELL_FG_PROGRAM, cfploc);
#undef S
    }
}

void
blank_canvas(float background_opacity, color_type color) {
    // See https://github.com/glfw/glfw/issues/1538 for why we use pre-multiplied alpha
#define C(shift) srgb_color((color >> shift) & 0xFF)
    glClearColor(C(16), C(8), C(0), background_opacity);
#undef C
    glClear(GL_COLOR_BUFFER_BIT);
}

bool
send_cell_data_to_gpu(ssize_t vao_idx, Screen *screen, OSWindow *os_window) {
    bool changed = false;
    if (os_window->fonts_data) {
        if (cell_prepare_to_render(vao_idx, screen, os_window->fonts_data)) changed = true;
    }
    return changed;
}

void
draw_cells(ssize_t vao_idx, const ScreenRenderData *srd, OSWindow *os_window, bool is_active_window, bool can_be_focused) {
    float x_ratio = 1., y_ratio = 1.;
    if (os_window->live_resize.in_progress) {
        x_ratio = (float) os_window->viewport_width / (float) os_window->live_resize.width;
        y_ratio = (float) os_window->viewport_height / (float) os_window->live_resize.height;
    }
    Screen *screen = srd->screen;
    CELL_BUFFERS;
    bool inverted = screen_invert_colors(screen);
    CellRenderData crd = {
        .gl={.xstart = srd->xstart, .ystart = srd->ystart, .dx = srd->dx * x_ratio, .dy = srd->dy * y_ratio},
        .x_ratio=x_ratio, .y_ratio=y_ratio
    };
    crd.gl.width = crd.gl.dx * screen->columns; crd.gl.height = crd.gl.dy * screen->lines;
    cell_update_uniform_block(vao_idx, screen, uniform_buffer, &crd, &screen->cursor_render_info, inverted, os_window);

    bind_vao_uniform_buffer(vao_idx, uniform_buffer, cell_program_layouts[CELL_PROGRAM].render_data.index);
    bind_vertex_array(vao_idx);

    float current_inactive_text_alpha = (!can_be_focused || screen->cursor_render_info.is_focused) && is_active_window ? 1.0f : (float)OPT(inactive_text_alpha);
    set_cell_uniforms(current_inactive_text_alpha, screen->reload_all_gpu_data);
    screen->reload_all_gpu_data = false;

    bind_program(CELL_PROGRAM);
    glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, 4, screen->lines * screen->columns);
}
// }}}

// Borders {{{

typedef struct BorderProgramLayout {
    BorderUniforms uniforms;
} BorderProgramLayout;
static BorderProgramLayout border_program_layout;

static void
init_borders_program(void) {
    get_uniform_locations_border(BORDERS_PROGRAM, &border_program_layout.uniforms);
    bind_program(BORDERS_PROGRAM);
    glUniform1fv(border_program_layout.uniforms.gamma_lut, 256, srgb_lut);
}

ssize_t
create_border_vao(void) {
    ssize_t vao_idx = create_vao();

    add_buffer_to_vao(vao_idx, GL_ARRAY_BUFFER);
    add_attribute_to_vao(BORDERS_PROGRAM, vao_idx, "rect",
            /*size=*/4, /*dtype=*/GL_FLOAT, /*stride=*/sizeof(BorderRect), /*offset=*/(void*)offsetof(BorderRect, left), /*divisor=*/1);
    add_attribute_to_vao(BORDERS_PROGRAM, vao_idx, "rect_color",
            /*size=*/1, /*dtype=*/GL_UNSIGNED_INT, /*stride=*/sizeof(BorderRect), /*offset=*/(void*)(offsetof(BorderRect, color)), /*divisor=*/1);

    return vao_idx;
}

void
draw_borders(ssize_t vao_idx, unsigned int num_border_rects, BorderRect *rect_buf, bool rect_data_is_dirty, uint32_t viewport_width, uint32_t viewport_height, color_type active_window_bg, unsigned int num_visible_windows, bool all_windows_have_same_bg, OSWindow *w) {
    float background_opacity = w->is_semi_transparent ? w->background_opacity: 1.0f;
    float tint_opacity = background_opacity;
    float tint_premult = background_opacity;

    if (num_border_rects) {
        bind_vertex_array(vao_idx);
        bind_program(BORDERS_PROGRAM);
        if (rect_data_is_dirty) {
            const size_t sz = sizeof(BorderRect) * num_border_rects;
            void *borders_buf_address = alloc_and_map_vao_buffer(vao_idx, sz, 0, GL_STATIC_DRAW, GL_WRITE_ONLY);
            if (borders_buf_address) memcpy(borders_buf_address, rect_buf, sz);
            unmap_vao_buffer(vao_idx, 0);
        }
        color_type default_bg = (num_visible_windows > 1 && !all_windows_have_same_bg) ? OPT(background) : active_window_bg;
        GLuint colors[9] = {
            default_bg, OPT(active_border_color), OPT(inactive_border_color),
            0, 0, OPT(tab_bar_background), OPT(tab_bar_margin_color),
            w->tab_bar_edge_color.left, w->tab_bar_edge_color.right
        };
        glUniform1uiv(border_program_layout.uniforms.colors, arraysz(colors), colors);
        glUniform1f(border_program_layout.uniforms.background_opacity, background_opacity);
        glUniform1f(border_program_layout.uniforms.tint_opacity, tint_opacity);
        glUniform1f(border_program_layout.uniforms.tint_premult, tint_premult);
        glUniform2ui(border_program_layout.uniforms.viewport, viewport_width, viewport_height);
        glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, 4, num_border_rects);
        unbind_vertex_array();
        unbind_program();
    }
}

// }}}

// Python API {{{

static bool
attach_shaders(PyObject *sources, GLuint program_id, GLenum shader_type) {
    RAII_ALLOC(const GLchar*, c_sources, calloc(PyTuple_GET_SIZE(sources), sizeof(GLchar*)));
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(sources); i++) {
        PyObject *temp = PyTuple_GET_ITEM(sources, i);
        if (!PyUnicode_Check(temp)) { PyErr_SetString(PyExc_TypeError, "shaders must be strings"); return false; }
        c_sources[i] = PyUnicode_AsUTF8(temp);
    }
    GLuint shader_id = compile_shaders(shader_type, PyTuple_GET_SIZE(sources), c_sources);
    if (shader_id == 0) return false;
    glAttachShader(program_id, shader_id);
    glDeleteShader(shader_id);
    return true;
}

static PyObject*
compile_program(PyObject UNUSED *self, PyObject *args) {
    PyObject *vertex_shaders, *fragment_shaders;
    int which, allow_recompile = 0;
    if (!PyArg_ParseTuple(args, "iO!O!|p", &which, &PyTuple_Type, &vertex_shaders, &PyTuple_Type, &fragment_shaders, &allow_recompile)) return NULL;
    if (which < 0 || which >= NUM_PROGRAMS) { PyErr_Format(PyExc_ValueError, "Unknown program: %d", which); return NULL; }
    Program *program = program_ptr(which);
    if (program->id != 0) {
        if (allow_recompile) { glDeleteProgram(program->id); program->id = 0; }
        else { PyErr_SetString(PyExc_ValueError, "program already compiled"); return NULL; }
    }
#define fail_compile() { glDeleteProgram(program->id); return NULL; }
    program->id = glCreateProgram();
    if (!attach_shaders(vertex_shaders, program->id, GL_VERTEX_SHADER)) fail_compile();
    if (!attach_shaders(fragment_shaders, program->id, GL_FRAGMENT_SHADER)) fail_compile();
    glLinkProgram(program->id);
    GLint ret = GL_FALSE;
    glGetProgramiv(program->id, GL_LINK_STATUS, &ret);
    if (ret != GL_TRUE) {
        GLsizei len;
        static char glbuf[4096];
        glGetProgramInfoLog(program->id, sizeof(glbuf), &len, glbuf);
        PyErr_Format(PyExc_ValueError, "Failed to link GLSL shaders:\n%s", glbuf);
        fail_compile();
    }
#undef fail_compile
    init_uniforms(which);
    return Py_BuildValue("I", program->id);
}

#define PYWRAP0(name) static PyObject* py##name(PYNOARG)
#define PYWRAP1(name) static PyObject* py##name(PyObject UNUSED *self, PyObject *args)
#define PA(fmt, ...) if(!PyArg_ParseTuple(args, fmt, __VA_ARGS__)) return NULL;
#define ONE_INT(name) PYWRAP1(name) { name(PyLong_AsSsize_t(args)); Py_RETURN_NONE; }
#define TWO_INT(name) PYWRAP1(name) { int a, b; PA("ii", &a, &b); name(a, b); Py_RETURN_NONE; }
#define NO_ARG(name) PYWRAP0(name) { name(); Py_RETURN_NONE; }
#define NO_ARG_INT(name) PYWRAP0(name) { return PyLong_FromSsize_t(name()); }

ONE_INT(bind_program)
NO_ARG(unbind_program)

PYWRAP0(create_vao) {
    int ans = create_vao();
    if (ans < 0) return NULL;
    return Py_BuildValue("i", ans);
}

ONE_INT(bind_vertex_array)
NO_ARG(unbind_vertex_array)
TWO_INT(unmap_vao_buffer)

NO_ARG(init_borders_program)

NO_ARG(init_cell_program)

static PyObject*
sprite_map_set_limits(PyObject UNUSED *self, PyObject *args) {
    unsigned int w, h;
    if(!PyArg_ParseTuple(args, "II", &w, &h)) return NULL;
    sprite_tracker_set_limits(w, h);
    max_texture_size = w; max_array_texture_layers = h;
    Py_RETURN_NONE;
}



#define M(name, arg_type) {#name, (PyCFunction)name, arg_type, NULL}
#define MW(name, arg_type) {#name, (PyCFunction)py##name, arg_type, NULL}
static PyMethodDef module_methods[] = {
    M(compile_program, METH_VARARGS),
    M(sprite_map_set_limits, METH_VARARGS),
    MW(create_vao, METH_NOARGS),
    MW(bind_vertex_array, METH_O),
    MW(unbind_vertex_array, METH_NOARGS),
    MW(unmap_vao_buffer, METH_VARARGS),
    MW(bind_program, METH_O),
    MW(unbind_program, METH_NOARGS),
    MW(init_borders_program, METH_NOARGS),
    MW(init_cell_program, METH_NOARGS),

    {NULL, NULL, 0, NULL}        /* Sentinel */
};

bool
init_shaders(PyObject *module) {
#define C(x) if (PyModule_AddIntConstant(module, #x, x) != 0) { PyErr_NoMemory(); return false; }
    C(CELL_PROGRAM); C(CELL_BG_PROGRAM); C(CELL_SPECIAL_PROGRAM); C(CELL_FG_PROGRAM); C(BORDERS_PROGRAM); C(GRAPHICS_PROGRAM); C(GRAPHICS_PREMULT_PROGRAM); C(GRAPHICS_ALPHA_MASK_PROGRAM); C(BGIMAGE_PROGRAM); C(TINT_PROGRAM);
    C(GLSL_VERSION);
    C(GL_VERSION);
    C(GL_VENDOR);
    C(GL_SHADING_LANGUAGE_VERSION);
    C(GL_RENDERER);
    C(GL_TRIANGLE_FAN); C(GL_TRIANGLE_STRIP); C(GL_TRIANGLES); C(GL_LINE_LOOP);
    C(GL_COLOR_BUFFER_BIT);
    C(GL_VERTEX_SHADER);
    C(GL_FRAGMENT_SHADER);
    C(GL_TRUE);
    C(GL_FALSE);
    C(GL_COMPILE_STATUS);
    C(GL_LINK_STATUS);
    C(GL_TEXTURE0); C(GL_TEXTURE1); C(GL_TEXTURE2); C(GL_TEXTURE3); C(GL_TEXTURE4); C(GL_TEXTURE5); C(GL_TEXTURE6); C(GL_TEXTURE7); C(GL_TEXTURE8);
    C(GL_MAX_ARRAY_TEXTURE_LAYERS); C(GL_TEXTURE_BINDING_BUFFER); C(GL_MAX_TEXTURE_BUFFER_SIZE);
    C(GL_MAX_TEXTURE_SIZE);
    C(GL_TEXTURE_2D_ARRAY);
    C(GL_LINEAR); C(GL_CLAMP_TO_EDGE); C(GL_NEAREST);
    C(GL_TEXTURE_MIN_FILTER); C(GL_TEXTURE_MAG_FILTER);
    C(GL_TEXTURE_WRAP_S); C(GL_TEXTURE_WRAP_T);
    C(GL_UNPACK_ALIGNMENT);
    C(GL_R8); C(GL_RED); C(GL_UNSIGNED_BYTE); C(GL_UNSIGNED_SHORT); C(GL_R32UI); C(GL_RGB32UI); C(GL_RGBA);
    C(GL_TEXTURE_BUFFER); C(GL_STATIC_DRAW); C(GL_STREAM_DRAW); C(GL_DYNAMIC_DRAW);
    C(GL_SRC_ALPHA); C(GL_ONE_MINUS_SRC_ALPHA);
    C(GL_WRITE_ONLY); C(GL_READ_ONLY); C(GL_READ_WRITE);
    C(GL_BLEND); C(GL_FLOAT); C(GL_UNSIGNED_INT); C(GL_ARRAY_BUFFER); C(GL_UNIFORM_BUFFER);

#undef C
    if (PyModule_AddFunctions(module, module_methods) != 0) return false;
    return true;
}
// }}}
