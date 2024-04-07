/*
 * Copyright (C) 2017 Kovid Goyal <kovid at kovidgoyal.net>
 *
 * Distributed under terms of the GPL3 license.
 */

#pragma once
#include "data-types.h"
#include "monotonic.h"
#include "screen.h"

#define OPT(name) global_state.opts.name

typedef enum { LEFT_EDGE, TOP_EDGE, RIGHT_EDGE, BOTTOM_EDGE } Edge;
typedef enum { REPEAT_MIRROR, REPEAT_CLAMP, REPEAT_DEFAULT } RepeatStrategy;
typedef enum {
  WINDOW_NORMAL,
  WINDOW_FULLSCREEN,
  WINDOW_MAXIMIZED,
  WINDOW_MINIMIZED
} WindowState;

typedef struct {
  char_type string[16];
  size_t len;
} UrlPrefix;

typedef enum AdjustmentUnit {
  POINT = 0,
  PERCENT = 1,
  PIXEL = 2
} AdjustmentUnit;

struct MenuItem {
  const char **location;
  size_t location_count;
  const char *definition;
};

typedef struct {
  monotonic_t cursor_blink_interval,
      cursor_stop_blinking_after, mouse_hide_wait, click_interval;
  double wheel_scroll_multiplier, touch_scroll_multiplier;
  int wheel_scroll_min_lines;
  CursorShape cursor_shape;
  float cursor_beam_thickness;
  float cursor_underline_thickness;
  unsigned int scrollback_pager_history_size;
  bool scrollback_fill_enlarged_window;
  char_type *select_by_word_characters;
  char_type *select_by_word_characters_forward;
  color_type background, foreground, active_border_color,
      inactive_border_color, tab_bar_background,
      tab_bar_margin_color;
  monotonic_t repaint_delay, input_delay;
  unsigned int hide_window_decorations;
  bool macos_hide_from_tasks, macos_quit_when_last_window_closed,
      macos_window_resizable, macos_traditional_fullscreen;
  unsigned int macos_option_as_alt;
  float macos_thicken_font;
  WindowTitleIn macos_show_window_title_in;
  float background_opacity, dim_opacity;
  float text_contrast, text_gamma_adjustment;
  bool text_old_gamma;

  char *default_window_logo;
  ImageAnchorPosition window_logo_position;

  bool dynamic_background_opacity;
  float inactive_text_alpha;
  Edge tab_bar_edge;
  unsigned long tab_bar_min_tabs;
  bool force_ltr;
  bool resize_in_steps;
  bool sync_to_monitor;
  bool close_on_child_death;
  bool debug_keyboard;
  struct {
    monotonic_t on_end, on_pause;
  } resize_debounce_time;
  MouseShape pointer_shape_when_grabbed;
  MouseShape default_pointer_shape;
  MouseShape pointer_shape_when_dragging;
  bool tab_bar_hidden;
  double font_size;
  struct {
    double outer, inner;
  } tab_bar_margin_height;
  int macos_colorspace;
  struct {
    float val;
    AdjustmentUnit unit;
  } underline_position, underline_thickness, strikethrough_position,
      strikethrough_thickness, cell_width, cell_height, baseline;
  int background_blur;
  long macos_titlebar_color;
  unsigned long wayland_titlebar_color;
  struct {
    struct MenuItem *entries;
    size_t count;
  } global_menu;
} Options;

typedef struct {
  ssize_t vao_idx;
  float xstart, ystart, dx, dy;
  Screen *screen;
} ScreenRenderData;

typedef struct {
  unsigned int left, top, right, bottom;
} WindowGeometry;

typedef struct {
  monotonic_t at;
  int button, modifiers;
  double x, y;
  unsigned long num;
} Click;

#define CLICK_QUEUE_SZ 3
typedef struct {
  Click clicks[CLICK_QUEUE_SZ];
  unsigned int length;
} ClickQueue;

typedef struct MousePosition {
  unsigned int cell_x, cell_y;
  double global_x, global_y;
  bool in_left_half_of_cell;
} MousePosition;

typedef struct WindowBarData {
  unsigned width, height;
  uint8_t *buf;
  bool needs_render;
} WindowBarData;

typedef struct {
  id_type id;
  bool visible, cursor_visible_at_last_render;
  unsigned int last_cursor_x, last_cursor_y;
  CursorShape last_cursor_shape;
  PyObject *title;
  ScreenRenderData render_data;
  MousePosition mouse_pos;
  struct {
    unsigned int left, top, right, bottom;
  } padding;
  WindowGeometry geometry;
  ClickQueue click_queues[8];
  monotonic_t last_drag_scroll_at;
  uint32_t last_special_key_pressed;
  WindowBarData title_bar_data;
} Window;

typedef struct {
  float left, top, right, bottom;
  uint32_t color;
} BorderRect;

typedef struct {
  BorderRect *rect_buf;
  unsigned int num_border_rects, capacity;
  bool is_dirty;
  ssize_t vao_idx;
} BorderRects;

typedef struct {
  id_type id;
  unsigned int active_window, num_windows, capacity;
  Window *windows;
  BorderRects border_rects;
} Tab;

enum RENDER_STATE {
  RENDER_FRAME_NOT_REQUESTED,
  RENDER_FRAME_REQUESTED,
  RENDER_FRAME_READY
};
typedef enum {
  NO_CLOSE_REQUESTED,
  CONFIRMABLE_CLOSE_REQUESTED,
  CLOSE_BEING_CONFIRMED,
  IMPERATIVE_CLOSE_REQUESTED
} CloseRequest;

typedef struct {
  monotonic_t last_resize_event_at;
  bool in_progress;
  bool from_os_notification;
  bool os_says_resize_complete;
  unsigned int width, height, num_of_resize_events;
} LiveResizeInfo;

typedef struct WindowChromeState {
  color_type color;
  bool use_system_color;
  unsigned system_color;
  int background_blur;
  unsigned hide_window_decorations;
  bool show_title_in_titlebar;
  bool resizable;
  int macos_colorspace;
  float background_opacity;
} WindowChromeState;

typedef struct {
  void *handle;
  id_type id;
  monotonic_t created_at;
  struct {
    int x, y, w, h;
    bool is_set, was_maximized;
  } before_fullscreen;
  int viewport_width, viewport_height, window_width, window_height,
      content_area_width, content_area_height;
  double viewport_x_ratio, viewport_y_ratio;
  Tab *tabs;
  unsigned int active_tab, num_tabs, capacity, last_active_tab, last_num_tabs,
      last_active_window_id;
  bool focused_at_last_render, needs_render;
  ScreenRenderData tab_bar_render_data;
  struct {
    color_type left, right;
  } tab_bar_edge_color;
  bool tab_bar_data_updated;
  bool is_focused;
  monotonic_t cursor_blink_zero_time, last_mouse_activity_at;
  double mouse_x, mouse_y;
  double logical_dpi_x, logical_dpi_y, font_sz_in_pts;
  bool mouse_button_pressed[32];
  PyObject *window_title;
  bool viewport_size_dirty, viewport_updated_at_least_once;
  monotonic_t viewport_resized_at;
  LiveResizeInfo live_resize;
  bool has_pending_resizes, is_semi_transparent, shown_once, is_damaged,
      ignore_resize_events;
  unsigned int clear_count;
  WindowChromeState last_window_chrome;
  float background_opacity;
  FONTS_DATA_HANDLE fonts_data;
  id_type temp_font_group_id;
  enum RENDER_STATE render_state;
  monotonic_t last_render_frame_received_at;
  uint64_t render_calls;
  id_type last_focused_counter;
  CloseRequest close_request;
} OSWindow;

typedef struct {
  Options opts;

  id_type os_window_id_counter, tab_id_counter, window_id_counter;
  PyObject *boss;
  OSWindow *os_windows;
  size_t num_os_windows, capacity;
  OSWindow *callback_os_window;
  bool is_wayland;
  bool has_render_frames;
  bool debug_rendering, debug_font_fallback;
  bool has_pending_resizes, has_pending_closes;
  bool check_for_active_animated_images;
  struct {
    double x, y;
  } default_dpi;
  id_type active_drag_in_window, tracked_drag_in_window;
  int active_drag_button, tracked_drag_button;
  CloseRequest quit_request;
  bool redirect_mouse_handling;
} GlobalState;

extern GlobalState global_state;

#define call_boss(name, ...)                                                   \
  if (global_state.boss) {                                                     \
    PyObject *cret_ =                                                          \
        PyObject_CallMethod(global_state.boss, #name, __VA_ARGS__);            \
    if (cret_ == NULL) {                                                       \
      PyErr_Print();                                                           \
    } else                                                                     \
      Py_DECREF(cret_);                                                        \
  }

void gl_init(void);
void remove_vao(ssize_t vao_idx);
bool remove_os_window(id_type os_window_id);
void *make_os_window_context_current(OSWindow *w);
void set_os_window_size(OSWindow *os_window, int x, int y);
void get_os_window_size(OSWindow *os_window, int *w, int *h, int *fw, int *fh);
void get_os_window_pos(OSWindow *os_window, int *x, int *y);
void set_os_window_pos(OSWindow *os_window, int x, int y);
void get_os_window_content_scale(OSWindow *os_window, double *xdpi,
                                 double *ydpi, float *xscale, float *yscale);
void update_os_window_references(void);
void mark_os_window_for_close(OSWindow *w, CloseRequest cr);
void update_os_window_viewport(OSWindow *window, bool notify_boss);
bool should_os_window_be_rendered(OSWindow *w);
void wakeup_main_loop(void);
void swap_window_buffers(OSWindow *w);
bool make_window_context_current(id_type);
void hide_mouse(OSWindow *w);
bool is_mouse_hidden(OSWindow *w);
void destroy_os_window(OSWindow *w);
void focus_os_window(OSWindow *w, bool also_raise,
                     const char *activation_token);
void run_with_activation_token_in_os_window(OSWindow *w, PyObject *callback);
OSWindow *os_window_for_alatty_window(id_type);
OSWindow *os_window_for_id(id_type);
OSWindow *add_os_window(void);
OSWindow *current_os_window(void);
void os_window_regions(OSWindow *, Region *main, Region *tab_bar);
bool drag_scroll(Window *, OSWindow *);
void draw_borders(ssize_t vao_idx, unsigned int num_border_rects,
                  BorderRect *rect_buf, bool rect_data_is_dirty,
                  uint32_t viewport_width, uint32_t viewport_height, color_type,
                  unsigned int, bool, OSWindow *w);
ssize_t create_cell_vao(void);
ssize_t create_border_vao(void);
bool send_cell_data_to_gpu(ssize_t, Screen *, OSWindow *);
void draw_cells(ssize_t, const ScreenRenderData *, OSWindow *, bool, bool);
void update_surface_size(int, int, uint32_t);
void free_texture(uint32_t *);
void free_framebuffer(uint32_t *);
void send_sprite_to_gpu(FONTS_DATA_HANDLE fg, unsigned int, unsigned int,
                        unsigned int, pixel *);
void blank_canvas(float, color_type);
void blank_os_window(OSWindow *);
void set_os_window_chrome(OSWindow *w);
FONTS_DATA_HANDLE load_fonts_data(double, double, double);
void send_prerendered_sprites_for_window(OSWindow *w);
#ifdef __APPLE__
void get_cocoa_key_equivalent(uint32_t, int, char *key, size_t key_sz, int *);
typedef enum {
  PREFERENCES_WINDOW,
  NEW_OS_WINDOW,
  NEW_OS_WINDOW_WITH_WD,
  NEW_TAB_WITH_WD,
  CLOSE_OS_WINDOW,
  CLOSE_TAB,
  NEW_TAB,
  NEXT_TAB,
  PREVIOUS_TAB,
  DETACH_TAB,
  LAUNCH_URLS,
  NEW_WINDOW,
  CLOSE_WINDOW,
  RESET_TERMINAL,
  CLEAR_TERMINAL_AND_SCROLLBACK,
  RELOAD_CONFIG,
  TOGGLE_MACOS_SECURE_KEYBOARD_ENTRY,
  TOGGLE_FULLSCREEN,
  HIDE,
  HIDE_OTHERS,
  MINIMIZE,
  QUIT,
  USER_MENU_ACTION,

  NUM_COCOA_PENDING_ACTIONS
} CocoaPendingAction;
void set_cocoa_pending_action(CocoaPendingAction action, const char *);
#endif
void request_frame_render(OSWindow *w);
void request_tick_callback(void);
typedef void (*timer_callback_fun)(id_type, void *);
typedef void (*tick_callback_fun)(void *);
id_type add_main_loop_timer(monotonic_t interval, bool repeats,
                            timer_callback_fun callback, void *callback_data,
                            timer_callback_fun free_callback);
void remove_main_loop_timer(id_type timer_id);
void update_main_loop_timer(id_type timer_id, monotonic_t interval,
                            bool enabled);
void run_main_loop(tick_callback_fun, void *);
void stop_main_loop(void);
void os_window_update_size_increments(OSWindow *window);
void fake_scroll(Window *w, int amount, bool upwards);
Window *window_for_window_id(id_type alatty_window_id);
bool mouse_set_last_visited_cmd_output(Window *w);
bool mouse_select_cmd_output(Window *w);
bool move_cursor_to_mouse_if_at_shell_prompt(Window *w);
void mouse_selection(Window *w, int code, int button);
const char *format_mods(unsigned mods);
void send_pending_click_to_window_id(id_type, void *);
void send_pending_click_to_window(Window *, void *);
void get_platform_dependent_config_values(void *glfw_window);
bool draw_window_title(OSWindow *window, const char *text, color_type fg,
                       color_type bg, uint8_t *output_buf, size_t width,
                       size_t height);
uint8_t *draw_single_ascii_char(const char ch, size_t *result_width,
                                size_t *result_height);
bool is_os_window_fullscreen(OSWindow *);
void update_ime_focus(OSWindow *osw, bool focused);
void update_ime_position(Window *w, Screen *screen);
bool update_ime_position_for_window(id_type window_id, bool force,
                                    int update_focus);
void set_ignore_os_keyboard_processing(bool enabled);
void change_live_resize_state(OSWindow *, bool);
bool render_os_window(OSWindow *w, monotonic_t now, bool ignore_render_frames);
void update_mouse_pointer_shape(void);
void adjust_window_size_for_csd(OSWindow *w, int width, int height,
                                int *adjusted_width, int *adjusted_height);
