/*
 * VitaCybiko - PlayStation Vita / PSTV frontend for the Cybiko Xtreme core.
 *
 * Firmware is loaded from ux0:data/VitaCybiko/roms and is intentionally not
 * bundled with the VPK.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef VITA
#include <psp2/ctrl.h>
#include <psp2/power.h>
#endif

#include <SDL2/SDL.h>
#include <SDL2/SDL2_gfxPrimitives.h>

#include "core/cfs.h"
#include "core/emulator.h"
#include "core/speaker.h"
#include "frontend/input.h"

#define SCREEN_WIDTH  960
#define SCREEN_HEIGHT 544
#define PORTRAIT_WIDTH  544
#define PORTRAIT_HEIGHT 960

#define LCD_SCALE 3
#define LCD_W (CYBIKO_LCD_WIDTH * LCD_SCALE)
#define LCD_H (CYBIKO_LCD_HEIGHT * LCD_SCALE)
#define LCD_X ((PORTRAIT_WIDTH - LCD_W) / 2)
#define LCD_Y 56

#define SHELL_X 12
#define SHELL_Y 20
#define SHELL_W (PORTRAIT_WIDTH - SHELL_X * 2)
#define SHELL_H 900

#define SKIN_BUTTON_X (PORTRAIT_WIDTH - 112)
#define SKIN_BUTTON_Y 24
#define SKIN_BUTTON_W 88
#define SKIN_BUTTON_H 34

#define KEYBOARD_X 16
#define KEYBOARD_Y 420
#define KEYBOARD_W (PORTRAIT_WIDTH - KEYBOARD_X * 2)
#define KEYBOARD_ROW_H 54
#define KEYBOARD_GAP 8

#ifndef DATA_DIR
#define DATA_DIR  "ux0:data/VitaCybiko"
#endif
#define ROM_DIR   DATA_DIR "/roms"
#define APP_DIR   DATA_DIR "/apps"
#define BOOT_PATH ROM_DIR "/boot.bin"
#define FLASH_PATH ROM_DIR "/flash.bin"
#define NVRAM_PATH DATA_DIR "/save.nvram"

#define MAX_APPS            64
#define MAX_PATH_CHARS      512
#define AUTOSAVE_INTERVAL_MS 60000u
#define AUDIO_DEVICE_SAMPLES 512
#define AUDIO_FRAME_SAMPLES (SPEAKER_SAMPLE_RATE / CYBIKO_FPS)
#define AUDIO_TARGET_QUEUE_FRAMES 1u
#define AUDIO_MAX_QUEUE_FRAMES 4u
#define AUDIO_CONVERT_MAX_SAMPLES 8192
#ifdef VITA
#define EMULATION_CATCHUP_MAX_FRAMES 0
#else
#define EMULATION_CATCHUP_MAX_FRAMES 2
#endif

/* Defaults keep old host tests and the legacy Xtreme layout valid. Selection
 * replaces all paths together; Classic never uses the legacy Xtreme save. */
static char runtime_root[MAX_PATH_CHARS] = DATA_DIR;
static char runtime_app_dir[MAX_PATH_CHARS] = APP_DIR;
static char runtime_save_path[MAX_PATH_CHARS] = NVRAM_PATH;
static char runtime_boot_path[MAX_PATH_CHARS] = BOOT_PATH;
static char runtime_flash_path[MAX_PATH_CHARS] = FLASH_PATH;
static char runtime_dataflash_path[MAX_PATH_CHARS];

typedef enum {
    VK_REGULAR,
    VK_NUMBER
} vk_kind_t;

typedef struct {
    const char *label;
    vk_kind_t kind;
    int col;
    uint16_t mask;
    int units;
} virtual_key_t;

typedef struct {
    const virtual_key_t *keys;
    int count;
} virtual_row_t;

#define VK(label, col, mask, units)      { label, VK_REGULAR, col, mask, units }
#define VK_NUM(label, col, mask, units)  { label, VK_NUMBER,  col, mask, units }

static const virtual_key_t vk_row_numbers[] = {
    VK_NUM("1", 3, 0x0002, 1), VK_NUM("2", 3, 0x0040, 1),
    VK_NUM("3", 3, 0x0080, 1), VK_NUM("4", 2, 0x2000, 1),
    VK_NUM("5", 2, 0x4000, 1), VK_NUM("6", 1, 0x0020, 1),
    VK_NUM("7", 1, 0x0040, 1), VK_NUM("8", 0, 0x1000, 1),
    VK_NUM("9", 0, 0x2000, 1), VK_NUM("0", 9, 0x0010, 1),
};

static const virtual_key_t vk_row_qwerty[] = {
    VK("Q", 3, 0x0002, 1), VK("W", 3, 0x0040, 1),
    VK("E", 3, 0x0080, 1), VK("R", 2, 0x2000, 1),
    VK("T", 2, 0x4000, 1), VK("Y", 1, 0x0020, 1),
    VK("U", 1, 0x0040, 1), VK("I", 0, 0x1000, 1),
    VK("O", 0, 0x2000, 1), VK("P", 9, 0x0010, 1),
};

static const virtual_key_t vk_row_home[] = {
    VK("A", 3, 0x0004, 1), VK("S", 3, 0x0020, 1),
    VK("D", 2, 0x0100, 1), VK("F", 2, 0x1000, 1),
    VK("G", 1, 0x0002, 1), VK("H", 1, 0x0010, 1),
    VK("J", 1, 0x0080, 1), VK("K", 0, 0x0800, 1),
    VK("L", 0, 0x4000, 1), VK(".", 9, 0x0002, 1),
};

static const virtual_key_t vk_row_bottom[] = {
    VK("SH", 8, 0x8000, 2), VK("Z", 3, 0x0008, 1),
    VK("X", 3, 0x0010, 1), VK("C", 2, 0x0200, 1),
    VK("V", 2, 0x0800, 1), VK("B", 1, 0x0004, 1),
    VK("N", 1, 0x0008, 1), VK("M", 0, 0x0100, 1),
    VK("DEL", 5, 0x0100, 2), VK("ENT", 4, 0x0008, 2),
};

static const virtual_key_t vk_row_controls[] = {
    VK("FN", 7, 0x8000, 2), VK("SPACE", 4, 0x0040, 3),
    VK("TAB", 5, 0x0080, 2), VK("ESC", 5, 0x0400, 2),
    VK("SEL", 4, 0x0010, 2), VK("AS", 5, 0x0200, 1),
    VK("HELP", 9, 0x0001, 2), VK(";", 9, 0x0008, 1),
    VK(",", 0, 0x0400, 1),
};

static const virtual_key_t vk_row_nav[] = {
    VK("UP", 6, 0x0800, 1), VK("LT", 6, 0x4000, 1),
    VK("DN", 6, 0x2000, 1), VK("RT", 6, 0x1000, 1),
    VK("F1", 6, 0x0001, 1), VK("F2", 5, 0x0001, 1),
    VK("F3", 4, 0x0001, 1), VK("F4", 3, 0x0001, 1),
    VK("F5", 2, 0x0001, 1), VK("F6", 1, 0x0001, 1),
    VK("F7", 0, 0x0001, 1),
};

static const virtual_key_t vk_row_symbols[] = {
    VK("(", 2, 0x0400, 1), VK(")", 0, 0x0200, 1),
    VK("!", 9, 0x0004, 1), VK("MENU", 4, 0x0020, 2),
};

static const virtual_key_t vk_classic_symbols[] = {
    VK("[",10,1,1), VK("]",10,2,1), VK("/",10,4,1), VK("-",10,8,1),
    VK("=",10,16,1), VK("'",10,32,1), VK("\\",10,64,1), VK("`",10,128,1),
    VK("BKSP",10,256,2),
};
static virtual_row_t vk_rows[] = {
    { vk_row_numbers, (int)(sizeof(vk_row_numbers) / sizeof(vk_row_numbers[0])) },
    { vk_row_qwerty,  (int)(sizeof(vk_row_qwerty) / sizeof(vk_row_qwerty[0])) },
    { vk_row_home,    (int)(sizeof(vk_row_home) / sizeof(vk_row_home[0])) },
    { vk_row_bottom,  (int)(sizeof(vk_row_bottom) / sizeof(vk_row_bottom[0])) },
    { vk_row_controls,(int)(sizeof(vk_row_controls) / sizeof(vk_row_controls[0])) },
    { vk_row_nav,     (int)(sizeof(vk_row_nav) / sizeof(vk_row_nav[0])) },
    { vk_row_symbols, (int)(sizeof(vk_row_symbols) / sizeof(vk_row_symbols[0])) },
};

static const int VK_ROW_COUNT = (int)(sizeof(vk_rows) / sizeof(vk_rows[0]));

typedef struct {
    const char *name;
    Uint8 shell_r;
    Uint8 shell_g;
    Uint8 shell_b;
    Uint8 glow_r;
    Uint8 glow_g;
    Uint8 glow_b;
} skin_t;

static const skin_t skins[] = {
    { "ICE",    110, 210, 230, 180, 245, 255 },
    { "LIME",    92, 236, 132, 188, 255, 164 },
    { "ORANGE", 255, 142,  48, 255, 202,  86 },
    { "VIOLET", 196, 116, 255, 234, 180, 255 },
    { "SMOKE",  96, 116, 128, 176, 196, 204 },
};

static const int SKIN_COUNT = (int)(sizeof(skins) / sizeof(skins[0]));

typedef struct {
    SDL_Scancode scancode;
    int col;
    uint16_t mask;
} key_mapping_t;

static const key_mapping_t key_map[] = {
    { SDL_SCANCODE_F7,        0, 0x0001 },
    { SDL_SCANCODE_M,         0, 0x0100 },
    { SDL_SCANCODE_K,         0, 0x0800 },
    { SDL_SCANCODE_I,         0, 0x1000 },
    { SDL_SCANCODE_O,         0, 0x2000 },
    { SDL_SCANCODE_COMMA,     0, 0x0400 },
    { SDL_SCANCODE_L,         0, 0x4000 },
    { SDL_SCANCODE_F6,        1, 0x0001 },
    { SDL_SCANCODE_G,         1, 0x0002 },
    { SDL_SCANCODE_B,         1, 0x0004 },
    { SDL_SCANCODE_N,         1, 0x0008 },
    { SDL_SCANCODE_H,         1, 0x0010 },
    { SDL_SCANCODE_Y,         1, 0x0020 },
    { SDL_SCANCODE_U,         1, 0x0040 },
    { SDL_SCANCODE_J,         1, 0x0080 },
    { SDL_SCANCODE_F5,        2, 0x0001 },
    { SDL_SCANCODE_D,         2, 0x0100 },
    { SDL_SCANCODE_C,         2, 0x0200 },
    { SDL_SCANCODE_V,         2, 0x0800 },
    { SDL_SCANCODE_F,         2, 0x1000 },
    { SDL_SCANCODE_R,         2, 0x2000 },
    { SDL_SCANCODE_T,         2, 0x4000 },
    { SDL_SCANCODE_F4,        3, 0x0001 },
    { SDL_SCANCODE_Q,         3, 0x0002 },
    { SDL_SCANCODE_A,         3, 0x0004 },
    { SDL_SCANCODE_Z,         3, 0x0008 },
    { SDL_SCANCODE_X,         3, 0x0010 },
    { SDL_SCANCODE_S,         3, 0x0020 },
    { SDL_SCANCODE_W,         3, 0x0040 },
    { SDL_SCANCODE_E,         3, 0x0080 },
    { SDL_SCANCODE_F3,        4, 0x0001 },
    { SDL_SCANCODE_RETURN,    4, 0x0008 },
    { SDL_SCANCODE_HOME,      4, 0x0010 },
    { SDL_SCANCODE_APPLICATION, 4, 0x0020 },
    { SDL_SCANCODE_SPACE,     4, 0x0040 },
    { SDL_SCANCODE_F2,        5, 0x0001 },
    { SDL_SCANCODE_TAB,       5, 0x0080 },
    { SDL_SCANCODE_BACKSPACE, 5, 0x0100 },
    { SDL_SCANCODE_DELETE,    5, 0x0100 },
    { SDL_SCANCODE_INSERT,    5, 0x0200 },
    { SDL_SCANCODE_ESCAPE,    5, 0x0400 },
    { SDL_SCANCODE_F1,        6, 0x0001 },
    { SDL_SCANCODE_UP,        6, 0x0800 },
    { SDL_SCANCODE_RIGHT,     6, 0x1000 },
    { SDL_SCANCODE_DOWN,      6, 0x2000 },
    { SDL_SCANCODE_LEFT,      6, 0x4000 },
    { SDL_SCANCODE_LCTRL,     7, 0x8000 },
    { SDL_SCANCODE_RCTRL,     7, 0x8000 },
    { SDL_SCANCODE_LSHIFT,    8, 0x8000 },
    { SDL_SCANCODE_RSHIFT,    8, 0x8000 },
    { SDL_SCANCODE_END,       9, 0x0001 },
    { SDL_SCANCODE_PERIOD,    9, 0x0002 },
    { SDL_SCANCODE_SEMICOLON, 9, 0x0008 },
    { SDL_SCANCODE_P,         9, 0x0010 },
};

static const key_mapping_t num_key_map[] = {
    { SDL_SCANCODE_1, 3, 0x0002 },
    { SDL_SCANCODE_2, 3, 0x0040 },
    { SDL_SCANCODE_3, 3, 0x0080 },
    { SDL_SCANCODE_4, 2, 0x2000 },
    { SDL_SCANCODE_5, 2, 0x4000 },
    { SDL_SCANCODE_6, 1, 0x0020 },
    { SDL_SCANCODE_7, 1, 0x0040 },
    { SDL_SCANCODE_8, 0, 0x1000 },
    { SDL_SCANCODE_9, 0, 0x2000 },
    { SDL_SCANCODE_0, 9, 0x0010 },
};

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *portrait_target;
    SDL_Texture *ui_cache;
    SDL_Texture *lcd_texture;
    SDL_AudioDeviceID audio_dev;
    SDL_AudioSpec audio_have;
    Uint32 audio_frame_bytes;
    Uint32 audio_target_queue_bytes;
    Uint32 audio_max_queue_bytes;
    uint8_t audio_silence_byte;
    bool audio_s16_stereo;
    SDL_GameController *controller;

    input_state_t physical_input;
    input_state_t touch_input;
    input_state_t controller_input;
    input_state_t virtual_input;
    bool physical_down[SDL_NUM_SCANCODES];
    const virtual_key_t *touch_vk;
    bool touch_shift_latched;
    bool touch_fn_latched;
    bool touch_skin_down;
    bool touch_layout_down;
    bool landscape;
    bool preferences_enabled;
    bool finger_down;
    SDL_FingerID finger_id;
    uint32_t lcd_pixels[CYBIKO_LCD_WIDTH * CYBIKO_LCD_HEIGHT];
    int16_t audio_s16_stereo_buf[AUDIO_CONVERT_MAX_SAMPLES * 2];
    bool ui_cache_valid;
    bool ui_cache_landscape;
    bool ui_cache_keyboard_mode;
    int ui_cache_skin_index;
    int ui_cache_vk_row;
    int ui_cache_vk_col;
    cybiko_model_t ui_cache_model;
    const virtual_key_t *ui_cache_active_vk;
    const virtual_key_t *ui_cache_touch_vk;
    bool ui_cache_active_vk_down;
    bool ui_cache_touch_fn_latched;
    bool ui_cache_touch_shift_latched;
    char ui_cache_status[128];

    bool keyboard_mode;
    int vk_row;
    int vk_col;
    const virtual_key_t *active_vk;
    bool active_vk_down;
    uint32_t previous_buttons;
    int skin_index;
    bool save_requested;
    bool backgrounded;
    bool focus_lost;
    bool quit_requested;
    cybiko_model_t model;
    char status[128];
} app_ctx_t;

static void advance_emulated_frame(cybiko_emu_t *emu, app_ctx_t *ctx);
static void cycle_skin(app_ctx_t *ctx, int direction);
static void release_all_inputs(app_ctx_t *ctx);
static void toggle_layout(app_ctx_t *ctx);
static void release_active_virtual_key(app_ctx_t *ctx);
static void release_controller_normal_keys(app_ctx_t *ctx);
static void screen_to_portrait_point(int screen_x, int screen_y, int *portrait_x,
                                     int *portrait_y);
static bool point_in_rect(int x, int y, const SDL_Rect *rect);
static const virtual_key_t *virtual_key_at(bool landscape, int x, int y, int *out_row,
                                           int *out_col);

static void set_virtual_key(input_state_t *input, const virtual_key_t *key, bool pressed)
{
    if (!key) {
        return;
    }

    if (key->kind == VK_NUMBER) {
        input_number(input, key->col, key->mask, pressed);
    } else {
        input_key(input, key->col, key->mask, pressed);
    }
}

static void release_active_virtual_key(app_ctx_t *ctx)
{
    if (ctx->active_vk_down) {
        set_virtual_key(&ctx->virtual_input, ctx->active_vk, false);
        ctx->active_vk_down = false;
        ctx->active_vk = NULL;
    }
}

static void release_touch_key(app_ctx_t *ctx)
{
    if (ctx->touch_vk && ctx->touch_vk->col != 7 && ctx->touch_vk->col != 8)
        set_virtual_key(&ctx->touch_input, ctx->touch_vk, false);
    ctx->touch_vk = NULL;
}

static void handle_portrait_touch(app_ctx_t *ctx, int portrait_x, int portrait_y,
                                  bool down)
{
    SDL_Rect skin_rect = {
        ctx->landscape ? 840 : SKIN_BUTTON_X, SKIN_BUTTON_Y, SKIN_BUTTON_W, SKIN_BUTTON_H
    };
    SDL_Rect layout_rect = {ctx->landscape ? 724 : 320, 24, 104, 34};

    if (!down) {
        ctx->touch_skin_down = false;
        ctx->touch_layout_down = false;
        release_touch_key(ctx);
        return;
    }

    if (ctx->touch_layout_down) return;
    if (point_in_rect(portrait_x, portrait_y, &layout_rect)) {
        toggle_layout(ctx);
        ctx->touch_layout_down = true;
        return;
    }

    if (point_in_rect(portrait_x, portrait_y, &skin_rect)) {
        release_touch_key(ctx);
        if (!ctx->touch_skin_down) cycle_skin(ctx, 1);
        ctx->touch_skin_down = true;
        return;
    }
    ctx->touch_skin_down = false;

    int row = 0;
    int col = 0;
    const virtual_key_t *key = virtual_key_at(ctx->landscape, portrait_x, portrait_y, &row, &col);
    if (!key) {
        release_touch_key(ctx);
        return;
    }

    ctx->keyboard_mode = true;
    ctx->vk_row = row;
    ctx->vk_col = col;

    if (ctx->touch_vk == key) {
        return;
    }

    release_touch_key(ctx);
    ctx->touch_vk = key;
    bool key_down = true;
    if (key->col == 7) key_down = ctx->touch_fn_latched = !ctx->touch_fn_latched;
    if (key->col == 8) key_down = ctx->touch_shift_latched = !ctx->touch_shift_latched;
    set_virtual_key(&ctx->touch_input, key, key_down);
}

static void handle_screen_touch(app_ctx_t *ctx, int screen_x, int screen_y,
                                bool down)
{
    int portrait_x = 0;
    int portrait_y = 0;
    if (ctx->landscape) { portrait_x = screen_x; portrait_y = screen_y; }
    else screen_to_portrait_point(screen_x, screen_y, &portrait_x, &portrait_y);
    handle_portrait_touch(ctx, portrait_x, portrait_y, down);
}

static void update_key_matrix_from_keyboard(app_ctx_t *ctx, SDL_KeyboardEvent *key)
{
    if (key->repeat) {
        return;
    }

    bool pressed = key->type == SDL_KEYDOWN;
    SDL_Scancode scancode = key->keysym.scancode;
    if (scancode < 0 || scancode >= SDL_NUM_SCANCODES) return;
    ctx->physical_down[scancode] = pressed;
    if (ctx->model != CYBIKO_XTREME) {
        static const SDL_Scancode classic_extra[] = {
            SDL_SCANCODE_LEFTBRACKET, SDL_SCANCODE_RIGHTBRACKET, SDL_SCANCODE_SLASH,
            SDL_SCANCODE_MINUS, SDL_SCANCODE_EQUALS, SDL_SCANCODE_APOSTROPHE,
            SDL_SCANCODE_BACKSLASH, SDL_SCANCODE_GRAVE, SDL_SCANCODE_BACKSPACE
        };
        for (unsigned i = 0; i < sizeof(classic_extra)/sizeof(classic_extra[0]); ++i)
            if (scancode == classic_extra[i]) {
                input_key(&ctx->physical_input, 10, (uint16_t)(1u << i), pressed);
                return;
            }
    }

    for (size_t i = 0; i < sizeof(num_key_map) / sizeof(num_key_map[0]); i++) {
        if (num_key_map[i].scancode == scancode) {
            input_number(&ctx->physical_input, num_key_map[i].col, num_key_map[i].mask, pressed);
            return;
        }
    }

    for (size_t i = 0; i < sizeof(key_map) / sizeof(key_map[0]); i++) {
        if (key_map[i].scancode == scancode) {
            bool any_down = false;
            for (size_t j = 0; j < sizeof(key_map) / sizeof(key_map[0]); ++j) {
                if (ctx->model != CYBIKO_XTREME && key_map[j].scancode == SDL_SCANCODE_BACKSPACE) continue;
                if (key_map[j].col == key_map[i].col && key_map[j].mask == key_map[i].mask)
                    any_down |= ctx->physical_down[key_map[j].scancode];
            }
            input_key(&ctx->physical_input, key_map[i].col, key_map[i].mask, any_down);
            return;
        }
    }
}

static void open_first_controller(app_ctx_t *ctx)
{
    if (ctx->controller) {
        return;
    }

    int count = SDL_NumJoysticks();
    for (int i = 0; i < count; i++) {
        if (SDL_IsGameController(i)) {
            ctx->controller = SDL_GameControllerOpen(i);
            if (ctx->controller) {
                return;
            }
        }
    }
}

static void close_controller_if_removed(app_ctx_t *ctx, SDL_JoystickID id)
{
    if (!ctx->controller) {
        return;
    }

    SDL_Joystick *joy = SDL_GameControllerGetJoystick(ctx->controller);
    if (joy && SDL_JoystickInstanceID(joy) == id) {
        release_active_virtual_key(ctx);
        release_controller_normal_keys(ctx);
        SDL_GameControllerClose(ctx->controller);
        ctx->controller = NULL;
        ctx->previous_buttons = 0;
    }
}

static bool button_is_down(uint32_t buttons, SDL_GameControllerButton button)
{
    return (buttons & (1u << (uint32_t)button)) != 0;
}

static uint32_t read_controller_buttons(app_ctx_t *ctx)
{
    uint32_t buttons = 0;
#ifdef VITA
    SceCtrlData pad;
    memset(&pad, 0, sizeof(pad));
    if (sceCtrlPeekBufferPositive(0, &pad, 1) > 0) {
        if (pad.buttons & SCE_CTRL_UP)
            buttons |= (1u << (uint32_t)SDL_CONTROLLER_BUTTON_DPAD_UP);
        if (pad.buttons & SCE_CTRL_RIGHT)
            buttons |= (1u << (uint32_t)SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
        if (pad.buttons & SCE_CTRL_DOWN)
            buttons |= (1u << (uint32_t)SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        if (pad.buttons & SCE_CTRL_LEFT)
            buttons |= (1u << (uint32_t)SDL_CONTROLLER_BUTTON_DPAD_LEFT);
        if (pad.buttons & SCE_CTRL_CROSS)
            buttons |= (1u << (uint32_t)SDL_CONTROLLER_BUTTON_A);
        if (pad.buttons & SCE_CTRL_CIRCLE)
            buttons |= (1u << (uint32_t)SDL_CONTROLLER_BUTTON_B);
        if (pad.buttons & SCE_CTRL_SQUARE)
            buttons |= (1u << (uint32_t)SDL_CONTROLLER_BUTTON_X);
        if (pad.buttons & SCE_CTRL_TRIANGLE)
            buttons |= (1u << (uint32_t)SDL_CONTROLLER_BUTTON_Y);
        if (pad.buttons & SCE_CTRL_LTRIGGER)
            buttons |= (1u << (uint32_t)SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
        if (pad.buttons & SCE_CTRL_RTRIGGER)
            buttons |= (1u << (uint32_t)SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
        if (pad.buttons & SCE_CTRL_START)
            buttons |= (1u << (uint32_t)SDL_CONTROLLER_BUTTON_START);
        if (pad.buttons & SCE_CTRL_SELECT)
            buttons |= (1u << (uint32_t)SDL_CONTROLLER_BUTTON_BACK);
    }
#endif

    if (!ctx->controller) {
        return buttons;
    }

    for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX && i < 31; i++) {
        if (SDL_GameControllerGetButton(ctx->controller,
                                        (SDL_GameControllerButton)i)) {
            buttons |= (1u << (uint32_t)i);
        }
    }
    return buttons;
}

static void release_controller_normal_keys(app_ctx_t *ctx)
{
    input_key(&ctx->controller_input, 6, 0x0800, false);
    input_key(&ctx->controller_input, 6, 0x1000, false);
    input_key(&ctx->controller_input, 6, 0x2000, false);
    input_key(&ctx->controller_input, 6, 0x4000, false);
    input_key(&ctx->controller_input, 4, 0x0008, false);
    input_key(&ctx->controller_input, 5, 0x0400, false);
    input_key(&ctx->controller_input, 4, 0x0040, false);
    input_key(&ctx->controller_input, 5, 0x0100, false);
    input_key(&ctx->controller_input, 8, 0x8000, false);
    input_key(&ctx->controller_input, 7, 0x8000, false);
    input_key(&ctx->controller_input, 5, 0x0080, false);
}

static void apply_controller_normal_keys(app_ctx_t *ctx, uint32_t buttons)
{
    input_key(&ctx->controller_input, 6, 0x0800,
                        button_is_down(buttons, SDL_CONTROLLER_BUTTON_DPAD_UP));
    input_key(&ctx->controller_input, 6, 0x1000,
                        button_is_down(buttons, SDL_CONTROLLER_BUTTON_DPAD_RIGHT));
    input_key(&ctx->controller_input, 6, 0x2000,
                        button_is_down(buttons, SDL_CONTROLLER_BUTTON_DPAD_DOWN));
    input_key(&ctx->controller_input, 6, 0x4000,
                        button_is_down(buttons, SDL_CONTROLLER_BUTTON_DPAD_LEFT));
    input_key(&ctx->controller_input, 4, 0x0008,
                        button_is_down(buttons, SDL_CONTROLLER_BUTTON_A));
    input_key(&ctx->controller_input, 5, 0x0400,
                        button_is_down(buttons, SDL_CONTROLLER_BUTTON_B));
    input_key(&ctx->controller_input, 4, 0x0040,
                        button_is_down(buttons, SDL_CONTROLLER_BUTTON_X));
    input_key(&ctx->controller_input, 5, 0x0100,
                        button_is_down(buttons, SDL_CONTROLLER_BUTTON_Y));
    input_key(&ctx->controller_input, 8, 0x8000,
                        button_is_down(buttons, SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
    input_key(&ctx->controller_input, 7, 0x8000,
                        button_is_down(buttons, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));
    input_key(&ctx->controller_input, 5, 0x0080,
                        button_is_down(buttons, SDL_CONTROLLER_BUTTON_START));
}

static void move_virtual_key(app_ctx_t *ctx, int row_delta, int col_delta)
{
    if (row_delta != 0) {
        ctx->vk_row += row_delta;
        if (ctx->vk_row < 0) {
            ctx->vk_row = VK_ROW_COUNT - 1;
        } else if (ctx->vk_row >= VK_ROW_COUNT) {
            ctx->vk_row = 0;
        }
        if (ctx->vk_col >= vk_rows[ctx->vk_row].count) {
            ctx->vk_col = vk_rows[ctx->vk_row].count - 1;
        }
    }

    if (col_delta != 0) {
        int count = vk_rows[ctx->vk_row].count;
        ctx->vk_col += col_delta;
        if (ctx->vk_col < 0) {
            ctx->vk_col = count - 1;
        } else if (ctx->vk_col >= count) {
            ctx->vk_col = 0;
        }
    }
}

static void update_controller_input(app_ctx_t *ctx, bool *running)
{
    if (ctx->backgrounded || ctx->focus_lost) return;
    open_first_controller(ctx);

    uint32_t buttons = read_controller_buttons(ctx);
    uint32_t pressed = buttons & ~ctx->previous_buttons;

    bool start_down = button_is_down(buttons, SDL_CONTROLLER_BUTTON_START);
    bool back_down = button_is_down(buttons, SDL_CONTROLLER_BUTTON_BACK);

    if (start_down && back_down) {
        *running = false;
        ctx->previous_buttons = buttons;
        return;
    }

    if (back_down && button_is_down(pressed, SDL_CONTROLLER_BUTTON_Y)) {
        toggle_layout(ctx);
        ctx->previous_buttons = buttons;
        return;
    }

    if (back_down &&
        button_is_down(pressed, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) {
        cycle_skin(ctx, -1);
        ctx->previous_buttons = buttons;
        return;
    }

    if (back_down &&
        button_is_down(pressed, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) {
        cycle_skin(ctx, 1);
        ctx->previous_buttons = buttons;
        return;
    }

    if (ctx->keyboard_mode) {
        release_controller_normal_keys(ctx);

        if (button_is_down(pressed, SDL_CONTROLLER_BUTTON_BACK) ||
            button_is_down(pressed, SDL_CONTROLLER_BUTTON_B)) {
            release_active_virtual_key(ctx);
            ctx->keyboard_mode = false;
            ctx->previous_buttons = buttons;
            return;
        }

        if (button_is_down(pressed, SDL_CONTROLLER_BUTTON_DPAD_UP)) {
            move_virtual_key(ctx, -1, 0);
        }
        if (button_is_down(pressed, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) {
            move_virtual_key(ctx, 1, 0);
        }
        if (button_is_down(pressed, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) {
            move_virtual_key(ctx, 0, -1);
        }
        if (button_is_down(pressed, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) {
            move_virtual_key(ctx, 0, 1);
        }

        bool cross_down = button_is_down(buttons, SDL_CONTROLLER_BUTTON_A);
        const virtual_key_t *selected =
            &vk_rows[ctx->vk_row].keys[ctx->vk_col];
        if (cross_down && !ctx->active_vk_down) {
            ctx->active_vk = selected;
            ctx->active_vk_down = true;
            set_virtual_key(&ctx->virtual_input, ctx->active_vk, true);
        } else if (!cross_down && ctx->active_vk_down) {
            release_active_virtual_key(ctx);
        }
    } else {
        if (button_is_down(pressed, SDL_CONTROLLER_BUTTON_BACK)) {
            if (button_is_down(buttons, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) {
                cycle_skin(ctx, -1);
                ctx->previous_buttons = buttons;
                return;
            }
            if (button_is_down(buttons, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) {
                cycle_skin(ctx, 1);
                ctx->previous_buttons = buttons;
                return;
            }
            release_controller_normal_keys(ctx);
            ctx->keyboard_mode = true;
            ctx->previous_buttons = buttons;
            return;
        }
        apply_controller_normal_keys(ctx, buttons);
    }

    ctx->previous_buttons = buttons;
}

static bool ensure_dir(const char *path)
{
    struct stat info;
    if (stat(path, &info) == 0) {
        return S_ISDIR(info.st_mode);
    }
    if (mkdir(path, 0777) == 0) {
        return true;
    }
    if (errno == EEXIST && stat(path, &info) == 0) {
        return S_ISDIR(info.st_mode);
    }
    return false;
}

static bool ensure_dir_tree(const char *path)
{
    if (!path || !*path) return false;
    char tmp[MAX_PATH_CHARS];
    int n = snprintf(tmp, sizeof(tmp), "%s", path);
    if (n <= 0 || n >= (int)sizeof(tmp)) return false;

    size_t len = strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/') {
        tmp[--len] = '\0';
    }

    for (char *p = tmp + 1; *p; ++p) {
        if (*p != '/') continue;
        *p = '\0';
        if (!ensure_dir(tmp)) {
            *p = '/';
            return false;
        }
        *p = '/';
    }
    return ensure_dir(tmp);
}

static bool ensure_runtime_dirs(void)
{
    return ensure_dir_tree(DATA_DIR);
}

static uint8_t *load_file(const char *path, size_t *out_size, bool quiet)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (!quiet) {
            fprintf(stderr, "cannot open '%s': %s\n", path, strerror(errno));
        }
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        if (!quiet) {
            fprintf(stderr, "cannot seek '%s'\n", path);
        }
        fclose(f);
        return NULL;
    }

    long len = ftell(f);
    if (len < 0 || len > CYBIKO_NVRAM_SIZE) {
        if (!quiet) {
            fprintf(stderr, "cannot determine size of '%s'\n", path);
        }
        fclose(f);
        return NULL;
    }
    rewind(f);

    uint8_t *buf = malloc((size_t)len > 0 ? (size_t)len : 1);
    if (!buf) {
        if (!quiet) {
            fprintf(stderr, "out of memory loading '%s'\n", path);
        }
        fclose(f);
        return NULL;
    }

    size_t nread = fread(buf, 1, (size_t)len, f);
    fclose(f);

    if (nread != (size_t)len) {
        if (!quiet) {
            fprintf(stderr, "short read on '%s'\n", path);
        }
        free(buf);
        return NULL;
    }

    *out_size = (size_t)len;
    return buf;
}

static bool write_file(const char *path, const uint8_t *data, size_t len)
{
    char temp_path[MAX_PATH_CHARS];
    int path_len = snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);
    if (path_len < 0 || (size_t)path_len >= sizeof(temp_path)) {
        fprintf(stderr, "save path is too long: '%s'\n", path);
        return false;
    }

    FILE *f = fopen(temp_path, "wb");
    if (!f) {
        fprintf(stderr, "cannot write '%s': %s\n", temp_path, strerror(errno));
        return false;
    }

    size_t nwritten = fwrite(data, 1, len, f);
    int flush_result = fflush(f);
    int close_result = fclose(f);
    bool ok = nwritten == len && flush_result == 0 && close_result == 0;
    if (!ok) {
        fprintf(stderr, "cannot complete save '%s'\n", temp_path);
        remove(temp_path);
        return false;
    }

    if (rename(temp_path, path) != 0) {
        fprintf(stderr, "cannot install save '%s': %s\n", path, strerror(errno));
        remove(temp_path);
        return false;
    }
    return true;
}

static void save_preferences(const app_ctx_t *ctx)
{
    if (!ctx->preferences_enabled) return;
    uint8_t data[12] = {'V','C','F','G',1,(uint8_t)ctx->model,
                         (uint8_t)ctx->landscape,(uint8_t)ctx->skin_index};
    uint32_t crc = cybiko_crc32(data, 8);
    for (int i = 0; i < 4; ++i) data[8+i] = (uint8_t)(crc >> (8*i));
    if (!write_file(DATA_DIR "/preferences.dat", data, sizeof(data)))
        fprintf(stderr, "Cannot save frontend preferences\n");
}

static void load_preferences(app_ctx_t *ctx)
{
    size_t size = 0;
    uint8_t *data = load_file(DATA_DIR "/preferences.dat", &size, true);
    if (data && size == 12 && !memcmp(data, "VCFG\1", 5) &&
        data[5] < CYBIKO_MODEL_COUNT && data[6] <= 1 && data[7] < SKIN_COUNT) {
        uint32_t crc = 0;
        for (int i = 0; i < 4; ++i) crc |= (uint32_t)data[8+i] << (8*i);
        if (crc == cybiko_crc32(data, 8)) {
            ctx->model = (cybiko_model_t)data[5];
            ctx->landscape = data[6] != 0;
            ctx->skin_index = data[7];
        }
    }
    free(data);
}

static bool has_import_suffix(const char *name)
{
    size_t len = strlen(name);
    if (len > 3 && name[len - 3] == '.' &&
        tolower((unsigned char)name[len - 2]) == 'd' &&
        tolower((unsigned char)name[len - 1]) == 'l') return true;
    if (len < 5) {
        return false;
    }

    const char *ext = name + len - 4;
    return tolower((unsigned char)ext[0]) == '.' &&
           tolower((unsigned char)ext[1]) == 'a' &&
           tolower((unsigned char)ext[2]) == 'p' &&
           tolower((unsigned char)ext[3]) == 'p';
}

static int compare_path_strings(const void *a, const void *b)
{
    const char *pa = (const char *)a;
    const char *pb = (const char *)b;
    return strcmp(pa, pb);
}

static int discover_directory(const char *directory,
                              char paths[MAX_APPS][MAX_PATH_CHARS], int count,
                              bool include_libraries)
{
    DIR *dir = opendir(directory);
    if (!dir) {
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (include_libraries && strcmp(entry->d_name, "Libraries") == 0) {
            char library_path[MAX_PATH_CHARS];
            int len = snprintf(library_path, sizeof(library_path), "%s/Libraries", directory);
            if (len < 0 || (size_t)len >= sizeof(library_path)) { closedir(dir); return -1; }
            count = discover_directory(library_path, paths, count, false);
            if (count < 0) {
                closedir(dir);
                return -1;
            }
            continue;
        }
        if (entry->d_name[0] == '.' || !has_import_suffix(entry->d_name)) {
            continue;
        }
        if (count == MAX_APPS) {
            fprintf(stderr, "too many import files in '%s' (maximum %d)\n", APP_DIR, MAX_APPS);
            closedir(dir);
            return -1;
        }
        int len = snprintf(paths[count], MAX_PATH_CHARS, "%s/%s", directory, entry->d_name);
        if (len < 0 || len >= MAX_PATH_CHARS) {
            closedir(dir);
            return -1;
        }
        count++;
    }
    closedir(dir);
    return count;
}

static int discover_apps(char paths[MAX_APPS][MAX_PATH_CHARS])
{
    int count = discover_directory(runtime_app_dir, paths, 0, true);
    if (count > 0) qsort(paths, (size_t)count, MAX_PATH_CHARS, compare_path_strings);
    return count;
}

static bool load_nvram_and_apps(cybiko_emu_t *emu,
                                char app_paths[MAX_APPS][MAX_PATH_CHARS],
                                int app_count)
{
    cfs_image_t *cfs = malloc(sizeof(*cfs));
    if (!cfs) {
        return false;
    }

    /* Only a genuinely absent save may be initialized. Any other failure
       aborts startup before autosave can overwrite the original. */
    struct stat save_stat;
    bool save_exists = stat(runtime_save_path, &save_stat) == 0;
    if (!save_exists && errno != ENOENT) {
        free(cfs);
        return false;
    }
    size_t nvram_size = 0;
    uint8_t *nvram_data = load_file(runtime_save_path, &nvram_size, true);
    if (save_exists) {
        if (!nvram_data || (nvram_size != CFS_IMAGE_SIZE &&
                            nvram_size != CYBIKO_NVRAM_SIZE)) {
            fprintf(stderr, "save NVRAM unreadable or wrong size; original preserved\n");
            free(nvram_data);
            free(cfs);
            return false;
        }
        memcpy(cfs->data, nvram_data, CFS_IMAGE_SIZE);
        if (!cfs_validate(cfs)) {
            fprintf(stderr, "save NVRAM failed integrity checks; original preserved\n");
            free(nvram_data);
            free(cfs);
            return false;
        } else if (!cybiko_load_nvram(emu, nvram_data, nvram_size)) {
            free(nvram_data);
            free(cfs);
            return false;
        }
    } else {
        cfs_format(cfs);
    }
    free(nvram_data);

    for (int i = 0; i < app_count; i++) {
        size_t app_size = 0;
        uint8_t *app_data = load_file(app_paths[i], &app_size, false);
        if (!app_data) {
            free(cfs);
            return false;
        }

        /* Preserve the CD's Libraries/ relative path in the guest filename. */
        const char *app_name = app_paths[i] + strlen(runtime_app_dir) + 1;
        if (strlen(app_name) > 58 || !cfs_put_file(cfs, app_name, app_data, app_size)) {
            fprintf(stderr, "failed to inject '%s'\n", app_name);
            free(app_data);
            free(cfs);
            return false;
        }
        free(app_data);
    }

    bool ok = cybiko_load_nvram(emu, cfs->data, CFS_IMAGE_SIZE);
    free(cfs);
    return ok;
}

static bool save_nvram(cybiko_emu_t *emu)
{
    size_t nvram_len = 0;
    const uint8_t *nvram = cybiko_get_model(emu) == CYBIKO_XTREME ?
        cybiko_get_nvram(emu, &nvram_len) : cybiko_get_dataflash(emu, &nvram_len);
    if (!nvram || nvram_len == 0) {
        return false;
    }
    return write_file(runtime_save_path, nvram, nvram_len);
}

/* Battery-backed RTC: independently checksummed, endian-stable sidecar.
 * A missing file is a first install. A corrupt one is never silently replaced. */
static bool clock_path(char path[MAX_PATH_CHARS])
{
    int n = snprintf(path, MAX_PATH_CHARS, "%s/clock.dat", runtime_root);
    return n > 0 && n < MAX_PATH_CHARS;
}

static bool save_clock(cybiko_emu_t *emu)
{
    uint8_t data[36] = {'V','R','T','C',1,0,0,0};
    char path[MAX_PATH_CHARS];
    time_t now = time(NULL);
    if (now < 0 || !clock_path(path)) return false;
    cybiko_get_clock(emu, data + 8);
    for (int i = 0; i < 8; ++i) data[24+i] = (uint8_t)((uint64_t)now >> (8*i));
    uint32_t crc = cybiko_crc32(data, 32);
    for (int i = 0; i < 4; ++i) data[32+i] = (uint8_t)(crc >> (8*i));
    return write_file(path, data, sizeof(data));
}

static bool load_clock(cybiko_emu_t *emu)
{
    char path[MAX_PATH_CHARS]; struct stat info;
    if (!clock_path(path)) return false;
    if (stat(path, &info) != 0) return errno == ENOENT;
    size_t size = 0;
    uint8_t *data = load_file(path, &size, false);
    bool ok = data && size == 36 && memcmp(data, "VRTC\1\0\0\0", 8) == 0;
    uint64_t saved = 0; uint32_t crc = 0;
    if (ok) {
        for (int i = 0; i < 8; ++i) saved |= (uint64_t)data[24+i] << (8*i);
        for (int i = 0; i < 4; ++i) crc |= (uint32_t)data[32+i] << (8*i);
        time_t now = time(NULL);
        uint64_t elapsed = now >= 0 && (uint64_t)now > saved ? (uint64_t)now - saved : 0;
        ok = now >= 0 && saved <= (uint64_t)now + 86400 &&
             crc == cybiko_crc32(data, 32) && cybiko_load_clock(emu, data + 8, 16, elapsed);
    }
    free(data);
    return ok;
}

/* Classic also retains SRAM while batteries are fitted. CyOS needs that saved
 * calendar state as well as the RTC to preserve its displayed time on restart.
 * Bind the sidecar to the flash checkpoint; never boot mismatched cached data. */
static void put_u32le(uint8_t *p, uint32_t value)
{
    for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8*i));
}

static uint32_t get_u32le(const uint8_t *p)
{
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value |= (uint32_t)p[i] << (8*i);
    return value;
}

static bool classic_ram_path(char path[MAX_PATH_CHARS])
{
    int n = snprintf(path, MAX_PATH_CHARS, "%s/ram.dat", runtime_root);
    return n > 0 && n < MAX_PATH_CHARS;
}

static bool save_classic_ram(cybiko_emu_t *emu)
{
    cybiko_model_t model = cybiko_get_model(emu);
    if (model == CYBIKO_XTREME) return true;
    size_t size = 0, flash_size = 0;
    const uint8_t *ram = cybiko_get_nvram(emu, &size);
    const uint8_t *flash = cybiko_get_dataflash(emu, &flash_size);
    char path[MAX_PATH_CHARS];
    if (!ram || !flash || !classic_ram_path(path)) return false;
    uint8_t *data = malloc(size + 24);
    if (!data) return false;
    memcpy(data, "VCRM\1\0\0\0", 8);
    data[5] = (uint8_t)model;
    put_u32le(data + 8, (uint32_t)size);
    put_u32le(data + 12, cybiko_crc32(flash, flash_size));
    put_u32le(data + 16, cybiko_crc32(ram, size));
    put_u32le(data + 20, cybiko_crc32(data, 20));
    memcpy(data + 24, ram, size);
    bool ok = write_file(path, data, size + 24);
    free(data);
    return ok;
}

static bool load_classic_ram(cybiko_emu_t *emu)
{
    cybiko_model_t model = cybiko_get_model(emu);
    if (model == CYBIKO_XTREME) return true;
    char path[MAX_PATH_CHARS]; struct stat info;
    if (!classic_ram_path(path)) return false;
    if (stat(path, &info) != 0) return errno == ENOENT; /* Legacy/cold install. */
    size_t size = 0, flash_size = 0;
    const uint8_t *flash = cybiko_get_dataflash(emu, &flash_size);
    uint8_t *data = load_file(path, &size, false);
    size_t ram_size = cybiko_machine(model)->ram_size;
    bool ok = data && flash && size == ram_size + 24 &&
        !memcmp(data, "VCRM\1", 5) && data[5] == model && !data[6] && !data[7] &&
        get_u32le(data + 8) == ram_size &&
        get_u32le(data + 20) == cybiko_crc32(data, 20) &&
        get_u32le(data + 12) == cybiko_crc32(flash, flash_size) &&
        get_u32le(data + 16) == cybiko_crc32(data + 24, ram_size) &&
        cybiko_load_nvram(emu, data + 24, ram_size);
    free(data);
    return ok;
}

static bool save_session(cybiko_emu_t *emu)
{
    bool storage_ok = save_nvram(emu);
    if (storage_ok) storage_ok = save_classic_ram(emu);
    bool clock_ok = save_clock(emu);
    return storage_ok && clock_ok;
}

static bool select_model_paths(cybiko_model_t model)
{
    const cybiko_machine_t *m = cybiko_machine(model);
    if (!m) return false;
    snprintf(runtime_root, sizeof(runtime_root), "%s/%s", DATA_DIR, m->directory);
    /* Existing installs remain usable, but only for Xtreme. */
    if (model == CYBIKO_XTREME) {
        struct stat info;
        char candidate[MAX_PATH_CHARS];
        snprintf(candidate, sizeof(candidate), "%s/xtreme/roms/boot.bin", DATA_DIR);
        if (stat(candidate, &info) != 0 && errno == ENOENT && stat(BOOT_PATH, &info) == 0)
            snprintf(runtime_root, sizeof(runtime_root), "%s", DATA_DIR);
    }
    const char *suffixes[] = {"/apps", "/save.nvram", "/roms/boot.bin", "/roms/flash.bin", "/roms/dataflash.bin"};
    char *paths[] = {runtime_app_dir, runtime_save_path, runtime_boot_path, runtime_flash_path, runtime_dataflash_path};
    for (unsigned i = 0; i < 5; ++i) {
        const char *suffix = i == 1 && model != CYBIKO_XTREME ? "/save.flash" : suffixes[i];
        int n = snprintf(paths[i], MAX_PATH_CHARS, "%s%s", runtime_root, suffix);
        if (n < 0 || n >= MAX_PATH_CHARS) return false;
    }
    char rom_dir[MAX_PATH_CHARS];
    int n = snprintf(rom_dir, sizeof(rom_dir), "%s/roms", runtime_root);
    return n > 0 && n < MAX_PATH_CHARS && ensure_dir_tree(runtime_root) &&
           ensure_dir_tree(rom_dir) && ensure_dir_tree(runtime_app_dir);
}

static bool load_classic_storage(cybiko_emu_t *emu)
{
    struct stat info;
    bool saved = stat(runtime_save_path, &info) == 0;
    if (!saved && errno != ENOENT) return false;
    size_t size = 0;
    uint8_t *data = load_file(saved ? runtime_save_path : runtime_dataflash_path, &size, false);
    /* Never format an absent Classic image: V1 stores the OS in this flash,
       and both Classics store their bundled apps here. */
    bool ok = data && cybiko_load_dataflash(emu, data, size);
    free(data);
    return ok;
}

static void hal_render_frame(void *ctx_ptr, const uint8_t *pixels, int width, int height)
{
    app_ctx_t *ctx = ctx_ptr;
    if (!ctx->lcd_texture || width != CYBIKO_LCD_WIDTH ||
        height != CYBIKO_LCD_HEIGHT) {
        return;
    }

    for (int i = 0; i < width * height; i++) {
        uint8_t g = pixels[i];
        uint8_t r = (uint8_t)(36 + (g * 174) / 255);
        uint8_t gg = (uint8_t)(45 + (g * 176) / 255);
        uint8_t b = (uint8_t)(38 + (g * 158) / 255);
        ctx->lcd_pixels[i] = 0xff000000u |
                             ((uint32_t)r << 16) |
                             ((uint32_t)gg << 8) |
                             (uint32_t)b;
    }

    SDL_UpdateTexture(ctx->lcd_texture, NULL, ctx->lcd_pixels,
                      width * (int)sizeof(uint32_t));
}

static void hal_audio_output(void *ctx_ptr, const uint8_t *samples, int count)
{
    app_ctx_t *ctx = ctx_ptr;
    if (!ctx->audio_dev || !samples || count <= 0) {
        return;
    }

    if (count > AUDIO_CONVERT_MAX_SAMPLES) {
        samples += count - AUDIO_CONVERT_MAX_SAMPLES;
        count = AUDIO_CONVERT_MAX_SAMPLES;
    }

    const void *payload = samples;
    Uint32 bytes = (Uint32)count;
    if (ctx->audio_s16_stereo) {
        for (int i = 0; i < count; ++i) {
            int16_t value = (int16_t)(((int)samples[i] - 128) << 8);
            ctx->audio_s16_stereo_buf[i * 2] = value;
            ctx->audio_s16_stereo_buf[i * 2 + 1] = value;
        }
        payload = ctx->audio_s16_stereo_buf;
        bytes = (Uint32)count * 2u * (Uint32)sizeof(int16_t);
    }

    Uint32 queued = SDL_GetQueuedAudioSize(ctx->audio_dev);
    if (queued > ctx->audio_max_queue_bytes ||
        queued + bytes > ctx->audio_max_queue_bytes) {
        SDL_ClearQueuedAudio(ctx->audio_dev);
        queued = 0;
    }

    if (queued < ctx->audio_target_queue_bytes) {
        uint8_t silence[4096];
        memset(silence, ctx->audio_silence_byte, sizeof(silence));
        Uint32 padding = ctx->audio_target_queue_bytes - queued;
        while (padding > 0) {
            Uint32 chunk = padding < sizeof(silence) ? padding : (Uint32)sizeof(silence);
            if (SDL_QueueAudio(ctx->audio_dev, silence, chunk) != 0) {
                fprintf(stderr, "audio prebuffer failed: %s\n", SDL_GetError());
                return;
            }
            padding -= chunk;
        }
    }

    if (SDL_QueueAudio(ctx->audio_dev, payload, bytes) != 0) {
        fprintf(stderr, "audio queue failed: %s\n", SDL_GetError());
    }
}

static void advance_emulated_frame(cybiko_emu_t *emu, app_ctx_t *ctx)
{
    cybiko_run_frame(emu);
    input_tick(&ctx->physical_input);
    input_tick(&ctx->controller_input);
    input_tick(&ctx->touch_input);
    input_tick(&ctx->virtual_input);
}

static void hal_keyboard_poll(void *ctx_ptr, uint16_t *matrix, int num_columns)
{
    app_ctx_t *ctx = ctx_ptr;
    int cols = num_columns < CYBIKO_KEYBOARD_COLUMNS
        ? num_columns
        : CYBIKO_KEYBOARD_COLUMNS;
    memset(matrix, 0, (size_t)num_columns * sizeof(uint16_t));
    void (*merge)(const input_state_t *, uint16_t *, int) =
        ctx->model == CYBIKO_XTREME ? input_merge : input_merge_classic;
    merge(&ctx->physical_input, matrix, cols);
    merge(&ctx->controller_input, matrix, cols);
    merge(&ctx->touch_input, matrix, cols);
    merge(&ctx->virtual_input, matrix, cols);
}

static void hal_serial_output(void *ctx_ptr, uint8_t byte)
{
    (void)ctx_ptr;
    fputc(byte, stderr);
}

static int text_width(const char *text)
{
    return (int)strlen(text) * 8;
}

static void cycle_skin(app_ctx_t *ctx, int direction)
{
    ctx->skin_index += direction;
    if (ctx->skin_index < 0) {
        ctx->skin_index = SKIN_COUNT - 1;
    } else if (ctx->skin_index >= SKIN_COUNT) {
        ctx->skin_index = 0;
    }
    snprintf(ctx->status, sizeof(ctx->status), "skin %s", skins[ctx->skin_index].name);
    save_preferences(ctx);
}

static void screen_to_portrait_point(int screen_x, int screen_y, int *portrait_x,
                                     int *portrait_y)
{
    *portrait_x = screen_y;
    *portrait_y = PORTRAIT_HEIGHT - 1 - screen_x;
}

static bool point_in_rect(int x, int y, const SDL_Rect *rect)
{
    return x >= rect->x && x < rect->x + rect->w &&
           y >= rect->y && y < rect->y + rect->h;
}

static void virtual_key_rect_layout(bool landscape, int row, int col, SDL_Rect *rect)
{
    const virtual_row_t *vk_row = &vk_rows[row];
    int units = 0;
    for (int i = 0; i < vk_row->count; i++) {
        units += vk_row->keys[i].units;
    }

    int width = landscape ? 424 : KEYBOARD_W;
    int gap = landscape ? 5 : KEYBOARD_GAP;
    int height = landscape ? 44 : KEYBOARD_ROW_H;
    int key_unit_w = (width - gap * (vk_row->count - 1)) / units;
    int used_w = key_unit_w * units + gap * (vk_row->count - 1);
    int x = (landscape ? 520 : KEYBOARD_X) + (width - used_w) / 2;
    int y = (landscape ? 112 : KEYBOARD_Y) + row * (height + gap);

    for (int i = 0; i < col; i++) {
        x += key_unit_w * vk_row->keys[i].units + gap;
    }

    rect->x = x;
    rect->y = y;
    rect->w = key_unit_w * vk_row->keys[col].units;
    rect->h = height;
}

static const virtual_key_t *virtual_key_at(bool landscape, int x, int y, int *out_row, int *out_col)
{
    for (int row = 0; row < VK_ROW_COUNT; row++) {
        for (int col = 0; col < vk_rows[row].count; col++) {
            SDL_Rect rect;
            virtual_key_rect_layout(landscape, row, col, &rect);
            if (point_in_rect(x, y, &rect)) {
                if (out_row) {
                    *out_row = row;
                }
                if (out_col) {
                    *out_col = col;
                }
                return &vk_rows[row].keys[col];
            }
        }
    }
    return NULL;
}

static void render_virtual_keyboard(app_ctx_t *ctx)
{
    SDL_Renderer *r = ctx->renderer;

    for (int row = 0; row < VK_ROW_COUNT; row++) {
        const virtual_row_t *vk_row = &vk_rows[row];
        for (int col = 0; col < vk_row->count; col++) {
            const virtual_key_t *key = &vk_row->keys[col];
            SDL_Rect rect;
            virtual_key_rect_layout(ctx->landscape, row, col, &rect);
            int x = rect.x, y = rect.y, w = rect.w;
            bool selected = ctx->keyboard_mode &&
                row == ctx->vk_row && col == ctx->vk_col;
            bool active = (ctx->active_vk_down && ctx->active_vk == key) || ctx->touch_vk == key ||
                          (key->col == 7 && ctx->touch_fn_latched) ||
                          (key->col == 8 && ctx->touch_shift_latched);

            Uint8 br = selected ? 42 : 28;
            Uint8 bg = selected ? 90 : 34;
            Uint8 bb = selected ? 116 : 40;
            if (active) {
                br = 176;
                bg = 116;
                bb = 42;
            }

            boxRGBA(r, (Sint16)x, (Sint16)y,
                    (Sint16)(x + w), (Sint16)(y + rect.h),
                    br, bg, bb, 230);
            rectangleRGBA(r, (Sint16)x, (Sint16)y,
                          (Sint16)(x + w), (Sint16)(y + rect.h),
                          selected ? 230 : 90,
                          selected ? 235 : 100,
                          selected ? 220 : 106,
                          255);

            int tx = x + (w - text_width(key->label)) / 2;
            int ty = y + (rect.h - 8) / 2;
            stringRGBA(r, (Sint16)tx, (Sint16)ty, key->label,
                       235, 238, 230, 255);

        }
    }
}

static void present_portrait_target(app_ctx_t *ctx)
{
    SDL_SetRenderTarget(ctx->renderer, NULL);
    SDL_SetRenderDrawColor(ctx->renderer, 8, 9, 10, 255);
    SDL_RenderClear(ctx->renderer);

    SDL_Rect dst = {
        (SCREEN_WIDTH - PORTRAIT_WIDTH) / 2,
        (SCREEN_HEIGHT - PORTRAIT_HEIGHT) / 2,
        PORTRAIT_WIDTH,
        PORTRAIT_HEIGHT
    };
    SDL_RenderCopyEx(ctx->renderer, ctx->portrait_target, NULL, &dst,
                     90.0, NULL, SDL_FLIP_NONE);
    SDL_RenderPresent(ctx->renderer);
}

static void render_layout_button(app_ctx_t *ctx)
{
    int x = ctx->landscape ? 724 : 320;
    roundedBoxRGBA(ctx->renderer, x, 24, x + 104, 58, 8, 28, 42, 48, 255);
    roundedRectangleRGBA(ctx->renderer, x, 24, x + 104, 58, 8, 120, 170, 180, 255);
    stringRGBA(ctx->renderer, x + 16, 37, ctx->landscape ? "PORTRAIT" : "LANDSCAPE", 232, 238, 232, 255);
}

static bool ui_cache_matches(const app_ctx_t *ctx)
{
    return ctx->ui_cache_valid &&
           ctx->ui_cache_landscape == ctx->landscape &&
           ctx->ui_cache_keyboard_mode == ctx->keyboard_mode &&
           ctx->ui_cache_skin_index == ctx->skin_index &&
           ctx->ui_cache_vk_row == ctx->vk_row &&
           ctx->ui_cache_vk_col == ctx->vk_col &&
           ctx->ui_cache_model == ctx->model &&
           ctx->ui_cache_active_vk == ctx->active_vk &&
           ctx->ui_cache_touch_vk == ctx->touch_vk &&
           ctx->ui_cache_active_vk_down == ctx->active_vk_down &&
           ctx->ui_cache_touch_fn_latched == ctx->touch_fn_latched &&
           ctx->ui_cache_touch_shift_latched == ctx->touch_shift_latched &&
           strcmp(ctx->ui_cache_status, ctx->status) == 0;
}

static void remember_ui_cache_state(app_ctx_t *ctx)
{
    ctx->ui_cache_valid = true;
    ctx->ui_cache_landscape = ctx->landscape;
    ctx->ui_cache_keyboard_mode = ctx->keyboard_mode;
    ctx->ui_cache_skin_index = ctx->skin_index;
    ctx->ui_cache_vk_row = ctx->vk_row;
    ctx->ui_cache_vk_col = ctx->vk_col;
    ctx->ui_cache_model = ctx->model;
    ctx->ui_cache_active_vk = ctx->active_vk;
    ctx->ui_cache_touch_vk = ctx->touch_vk;
    ctx->ui_cache_active_vk_down = ctx->active_vk_down;
    ctx->ui_cache_touch_fn_latched = ctx->touch_fn_latched;
    ctx->ui_cache_touch_shift_latched = ctx->touch_shift_latched;
    snprintf(ctx->ui_cache_status, sizeof(ctx->ui_cache_status), "%s", ctx->status);
}

static void render_landscape_cache(app_ctx_t *ctx)
{
    const skin_t *skin = &skins[ctx->skin_index];
    SDL_SetRenderTarget(ctx->renderer, ctx->ui_cache);
    SDL_SetRenderDrawColor(ctx->renderer, 12, 17, 23, 255);
    SDL_RenderClear(ctx->renderer);
    roundedBoxRGBA(ctx->renderer, 12, 70, 508, 426, 20,
                   skin->shell_r, skin->shell_g, skin->shell_b, 120);
    roundedRectangleRGBA(ctx->renderer, 12, 70, 508, 426, 20,
                         skin->glow_r, skin->glow_g, skin->glow_b, 200);
    stringRGBA(ctx->renderer, 24, 30, "VitaCybiko", 232, 240, 246, 255);
    stringRGBA(ctx->renderer, 24, 48, cybiko_machine(ctx->model)->name, 155, 183, 199, 255);
    stringRGBA(ctx->renderer, 532, 86, "DEVICE KEYBOARD", 184, 211, 218, 255);
    render_virtual_keyboard(ctx);
    render_layout_button(ctx);
    roundedBoxRGBA(ctx->renderer, 840, 24, 928, 58, 8, 28, 42, 48, 255);
    char label[24]; snprintf(label, sizeof(label), "SKIN %s", skin->name);
    stringRGBA(ctx->renderer, 884 - text_width(label) / 2, 37, label, 232, 238, 232, 255);
    stringRGBA(ctx->renderer, 24, 446, "Cross: Enter   Circle: Esc   Square: Space", 180, 204, 213, 255);
    stringRGBA(ctx->renderer, 24, 466, "Select: keyboard   Start+Select: save/menu", 180, 204, 213, 255);
    stringRGBA(ctx->renderer, 24, 486, "Select+Triangle: layout   Select+L/R: skin", 151, 177, 187, 255);
    stringRGBA(ctx->renderer, 24, 520, ctx->status, 154, 190, 178, 255);
    remember_ui_cache_state(ctx);
}

static void render_portrait_cache(app_ctx_t *ctx)
{
    const skin_t *skin = &skins[ctx->skin_index];

    SDL_SetRenderTarget(ctx->renderer, ctx->ui_cache);
    SDL_SetRenderDrawColor(ctx->renderer, 12, 14, 15, 255);
    SDL_RenderClear(ctx->renderer);

    roundedBoxRGBA(ctx->renderer,
                   SHELL_X, SHELL_Y,
                   SHELL_X + SHELL_W, SHELL_Y + SHELL_H,
                   32,
                   skin->shell_r, skin->shell_g, skin->shell_b, 92);
    roundedRectangleRGBA(ctx->renderer,
                         SHELL_X + 1, SHELL_Y + 1,
                         SHELL_X + SHELL_W - 1, SHELL_Y + SHELL_H - 1,
                         32,
                         skin->glow_r, skin->glow_g, skin->glow_b, 230);
    roundedRectangleRGBA(ctx->renderer,
                         SHELL_X + 8, SHELL_Y + 8,
                         SHELL_X + SHELL_W - 8, SHELL_Y + SHELL_H - 8,
                         26,
                         skin->glow_r, skin->glow_g, skin->glow_b, 96);

    roundedBoxRGBA(ctx->renderer,
                   LCD_X - 16, LCD_Y - 16,
                   LCD_X + LCD_W + 16, LCD_Y + LCD_H + 16,
                   18, 16, 20, 20, 210);
    roundedRectangleRGBA(ctx->renderer,
                         LCD_X - 16, LCD_Y - 16,
                         LCD_X + LCD_W + 16, LCD_Y + LCD_H + 16,
                         18,
                         skin->glow_r, skin->glow_g, skin->glow_b, 190);

    stringRGBA(ctx->renderer, 30, 30, cybiko_machine(ctx->model)->name, 230, 238, 232, 255);
    stringRGBA(ctx->renderer, 30, 44,
               ctx->keyboard_mode ? "TOUCH KEYS" : "CONTROLLER RUN",
               150, 166, 172, 255);

    roundedBoxRGBA(ctx->renderer,
                   SKIN_BUTTON_X, SKIN_BUTTON_Y,
                   SKIN_BUTTON_X + SKIN_BUTTON_W,
                   SKIN_BUTTON_Y + SKIN_BUTTON_H,
                   8, 22, 28, 30, 210);
    roundedRectangleRGBA(ctx->renderer,
                         SKIN_BUTTON_X, SKIN_BUTTON_Y,
                         SKIN_BUTTON_X + SKIN_BUTTON_W,
                         SKIN_BUTTON_Y + SKIN_BUTTON_H,
                         8, skin->glow_r, skin->glow_g, skin->glow_b, 220);
    char skin_label[32];
    snprintf(skin_label, sizeof(skin_label), "SKIN %s", skin->name);
    stringRGBA(ctx->renderer,
               SKIN_BUTTON_X + (SKIN_BUTTON_W - text_width(skin_label)) / 2,
               SKIN_BUTTON_Y + 13,
               skin_label, 232, 238, 232, 255);

    render_virtual_keyboard(ctx);
    render_layout_button(ctx);

    stringRGBA(ctx->renderer, 28, 876,
               "Turn Vita counterclockwise. Select+L/R skin.",
               166, 184, 180, 255);
    stringRGBA(ctx->renderer, 28, 896, ctx->status, 134, 154, 156, 255);

    remember_ui_cache_state(ctx);
}

static void update_ui_cache(app_ctx_t *ctx)
{
    if (!ctx->ui_cache || ui_cache_matches(ctx)) {
        return;
    }
    if (ctx->landscape) render_landscape_cache(ctx);
    else render_portrait_cache(ctx);
}

static void render_landscape(app_ctx_t *ctx)
{
    update_ui_cache(ctx);
    SDL_SetRenderTarget(ctx->renderer, NULL);
    SDL_Rect src = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
    SDL_Rect dst = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
    SDL_RenderCopy(ctx->renderer, ctx->ui_cache, &src, &dst);
    SDL_Rect lcd = {20, 96, 480, 300};
    SDL_RenderCopy(ctx->renderer, ctx->lcd_texture, NULL, &lcd);
    rectangleRGBA(ctx->renderer, 19, 95, 500, 396, 160, 182, 162, 255);
    SDL_RenderPresent(ctx->renderer);
}

static void render_frame(app_ctx_t *ctx)
{
    if (ctx->landscape) { render_landscape(ctx); return; }

    update_ui_cache(ctx);
    SDL_SetRenderTarget(ctx->renderer, ctx->portrait_target);
    SDL_Rect src = {0, 0, PORTRAIT_WIDTH, PORTRAIT_HEIGHT};
    SDL_Rect dst = {0, 0, PORTRAIT_WIDTH, PORTRAIT_HEIGHT};
    SDL_RenderCopy(ctx->renderer, ctx->ui_cache, &src, &dst);
    SDL_Rect lcd = { LCD_X, LCD_Y, LCD_W, LCD_H };
    SDL_RenderCopy(ctx->renderer, ctx->lcd_texture, NULL, &lcd);
    rectangleRGBA(ctx->renderer, LCD_X - 1, LCD_Y - 1,
                  LCD_X + LCD_W + 1, LCD_Y + LCD_H + 1,
                  205, 220, 198, 255);
    present_portrait_target(ctx);
}

static void render_message_screen(app_ctx_t *ctx, const char *line1,
                                  const char *line2, const char *line3)
{
    SDL_SetRenderTarget(ctx->renderer, NULL);
    SDL_SetRenderDrawColor(ctx->renderer, 18, 20, 22, 255);
    SDL_RenderClear(ctx->renderer);
    const char *lines[] = {line1, line2, line3,
        "Start+Select or Escape: model menu. Firmware not bundled."};
    for (int i = 0; i < 4; ++i)
        stringRGBA(ctx->renderer, (SCREEN_WIDTH - text_width(lines[i])) / 2,
                   175 + i * 45, lines[i], 200, 218, 225, 255);
    SDL_RenderPresent(ctx->renderer);
}

static void release_all_inputs(app_ctx_t *ctx)
{
    release_touch_key(ctx);
    release_active_virtual_key(ctx);
    input_reset(&ctx->physical_input, ctx->model);
    input_reset(&ctx->controller_input, ctx->model);
    input_reset(&ctx->touch_input, ctx->model);
    input_reset(&ctx->virtual_input, ctx->model);
    memset(ctx->physical_down, 0, sizeof(ctx->physical_down));
    ctx->finger_down = false;
    ctx->touch_fn_latched = false;
    ctx->touch_shift_latched = false;
    ctx->touch_skin_down = false;
    ctx->touch_layout_down = false;
    ctx->previous_buttons = 0;
}

static void toggle_layout(app_ctx_t *ctx)
{
    bool finger_down = ctx->finger_down;
    SDL_FingerID finger_id = ctx->finger_id;
    release_all_inputs(ctx);
    ctx->finger_down = finger_down;
    ctx->finger_id = finger_id;
    ctx->landscape = !ctx->landscape;
    ctx->keyboard_mode = false;
    save_preferences(ctx);
}

static void update_suspend_state(app_ctx_t *ctx)
{
    bool suspended = ctx->backgrounded || ctx->focus_lost;
    if (suspended) {
        release_all_inputs(ctx);
        ctx->save_requested = true;
    }
    if (ctx->audio_dev) {
        SDL_PauseAudioDevice(ctx->audio_dev, 1);
        SDL_ClearQueuedAudio(ctx->audio_dev);
        if (!suspended) SDL_PauseAudioDevice(ctx->audio_dev, 0);
    }
}

static void process_sdl_events(app_ctx_t *ctx, bool *running)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if ((ctx->backgrounded || ctx->focus_lost) &&
            (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP ||
             event.type == SDL_FINGERDOWN || event.type == SDL_FINGERMOTION ||
             event.type == SDL_FINGERUP || event.type == SDL_MOUSEBUTTONDOWN ||
             event.type == SDL_MOUSEBUTTONUP || event.type == SDL_MOUSEMOTION)) {
            continue;
        }
        switch (event.type) {
        case SDL_QUIT:
        case SDL_APP_TERMINATING:
            ctx->quit_requested = true;
            ctx->save_requested = true;
            *running = false;
            break;
        case SDL_APP_WILLENTERBACKGROUND:
            ctx->backgrounded = true;
            update_suspend_state(ctx);
            break;
        case SDL_APP_DIDENTERFOREGROUND:
            ctx->backgrounded = false;
            update_suspend_state(ctx);
            break;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
                event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                ctx->focus_lost = event.window.event == SDL_WINDOWEVENT_FOCUS_LOST;
                update_suspend_state(ctx);
            }
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            update_key_matrix_from_keyboard(ctx, &event.key);
            break;
        case SDL_FINGERDOWN:
            if (ctx->finger_down) break;
            ctx->finger_down = true;
            ctx->finger_id = event.tfinger.fingerId;
            /* fall through */
        case SDL_FINGERMOTION:
            if (!ctx->finger_down || ctx->finger_id != event.tfinger.fingerId) break;
            handle_screen_touch(ctx,
                                (int)(event.tfinger.x * SCREEN_WIDTH),
                                (int)(event.tfinger.y * SCREEN_HEIGHT),
                                true);
            break;
        case SDL_FINGERUP:
            if (!ctx->finger_down || ctx->finger_id != event.tfinger.fingerId) break;
            ctx->finger_down = false;
            handle_screen_touch(ctx,
                                (int)(event.tfinger.x * SCREEN_WIDTH),
                                (int)(event.tfinger.y * SCREEN_HEIGHT),
                                false);
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button == SDL_BUTTON_LEFT)
                handle_screen_touch(ctx, event.button.x, event.button.y, true);
            break;
        case SDL_MOUSEBUTTONUP:
            if (event.button.button == SDL_BUTTON_LEFT)
                handle_screen_touch(ctx, event.button.x, event.button.y, false);
            break;
        case SDL_MOUSEMOTION:
            if (event.motion.state & SDL_BUTTON_LMASK) {
                handle_screen_touch(ctx, event.motion.x, event.motion.y, true);
            }
            break;
        case SDL_CONTROLLERDEVICEADDED:
            open_first_controller(ctx);
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            close_controller_if_removed(ctx, event.cdevice.which);
            break;
        default:
            break;
        }
    }
}

static void message_loop(app_ctx_t *ctx, const char *line1,
                         const char *line2, const char *line3)
{
    bool running = true;
    while (running) {
        process_sdl_events(ctx, &running);
        if (ctx->physical_down[SDL_SCANCODE_ESCAPE]) running = false;
        update_controller_input(ctx, &running);
        if (running && !ctx->backgrounded && !ctx->focus_lost)
            render_message_screen(ctx, line1, line2, line3);
        SDL_Delay(16);
    }
}

#include "model_menu.inc"

static bool init_sdl(app_ctx_t *ctx)
{
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    ctx->window = SDL_CreateWindow("VitaCybiko",
                                   SDL_WINDOWPOS_UNDEFINED,
                                   SDL_WINDOWPOS_UNDEFINED,
                                   SCREEN_WIDTH,
                                   SCREEN_HEIGHT,
                                   SDL_WINDOW_SHOWN);
    if (!ctx->window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    ctx->renderer = SDL_CreateRenderer(ctx->window, -1, SDL_RENDERER_ACCELERATED);
    if (!ctx->renderer) {
        ctx->renderer = SDL_CreateRenderer(ctx->window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!ctx->renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_RenderSetLogicalSize(ctx->renderer, SCREEN_WIDTH, SCREEN_HEIGHT);

    ctx->portrait_target = SDL_CreateTexture(ctx->renderer,
                                             SDL_PIXELFORMAT_ARGB8888,
                                             SDL_TEXTUREACCESS_TARGET,
                                             PORTRAIT_WIDTH,
                                             PORTRAIT_HEIGHT);
    if (!ctx->portrait_target) {
        fprintf(stderr, "SDL_CreateTexture target failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetTextureBlendMode(ctx->portrait_target, SDL_BLENDMODE_NONE);

    ctx->ui_cache = SDL_CreateTexture(ctx->renderer,
                                      SDL_PIXELFORMAT_ARGB8888,
                                      SDL_TEXTUREACCESS_TARGET,
                                      SCREEN_WIDTH,
                                      PORTRAIT_HEIGHT);
    if (!ctx->ui_cache) {
        fprintf(stderr, "SDL_CreateTexture ui cache failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetTextureBlendMode(ctx->ui_cache, SDL_BLENDMODE_NONE);

    ctx->lcd_texture = SDL_CreateTexture(ctx->renderer,
                                         SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING,
                                         CYBIKO_LCD_WIDTH,
                                         CYBIKO_LCD_HEIGHT);
    if (!ctx->lcd_texture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_AudioSpec want;
    SDL_AudioSpec have;
    SDL_zero(want);
    want.freq = SPEAKER_SAMPLE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = AUDIO_DEVICE_SAMPLES;

    ctx->audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!ctx->audio_dev) {
        SDL_zero(want);
        want.freq = SPEAKER_SAMPLE_RATE;
        want.format = AUDIO_U8;
        want.channels = 1;
        want.samples = AUDIO_DEVICE_SAMPLES;
        ctx->audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    }
    if (!ctx->audio_dev) {
        fprintf(stderr, "audio disabled: %s\n", SDL_GetError());
    } else {
        ctx->audio_have = have;
        ctx->audio_s16_stereo =
            have.format == AUDIO_S16SYS && have.channels == 2;
        unsigned bytes_per_sample =
            (unsigned)(SDL_AUDIO_BITSIZE(have.format) / 8) *
            (unsigned)have.channels;
        ctx->audio_frame_bytes = AUDIO_FRAME_SAMPLES * bytes_per_sample;
        ctx->audio_target_queue_bytes =
            ctx->audio_frame_bytes * AUDIO_TARGET_QUEUE_FRAMES;
        ctx->audio_max_queue_bytes =
            ctx->audio_frame_bytes * AUDIO_MAX_QUEUE_FRAMES;
        ctx->audio_silence_byte = have.format == AUDIO_U8 ? 128 : 0;
    }

    open_first_controller(ctx);
    return true;
}

static void cleanup(app_ctx_t *ctx)
{
    if (!ctx) {
        return;
    }
    if (ctx->audio_dev) {
        SDL_CloseAudioDevice(ctx->audio_dev);
    }
    if (ctx->controller) {
        SDL_GameControllerClose(ctx->controller);
    }
    if (ctx->lcd_texture) {
        SDL_DestroyTexture(ctx->lcd_texture);
    }
    if (ctx->ui_cache) {
        SDL_DestroyTexture(ctx->ui_cache);
    }
    if (ctx->portrait_target) {
        SDL_DestroyTexture(ctx->portrait_target);
    }
    if (ctx->renderer) {
        SDL_DestroyRenderer(ctx->renderer);
    }
    if (ctx->window) {
        SDL_DestroyWindow(ctx->window);
    }
    SDL_Quit();
    free(ctx);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

#ifdef VITA
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
#endif

    bool dirs_ok = ensure_runtime_dirs();

    app_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        return 1;
    }

    snprintf(ctx->status, sizeof(ctx->status), "ux0:data/VitaCybiko");
    ctx->landscape = true;
    load_preferences(ctx);
    ctx->preferences_enabled = dirs_ok;

    if (!init_sdl(ctx)) {
        cleanup(ctx);
        return 1;
    }

    if (!dirs_ok) {
        message_loop(ctx, "Cannot create VitaCybiko data folders",
                     "Check that ux0:data is writable.", "");
        cleanup(ctx);
        return 1;
    }

select_model:
    if (ctx->audio_dev) { SDL_PauseAudioDevice(ctx->audio_dev, 1); SDL_ClearQueuedAudio(ctx->audio_dev); }
    int selection = choose_model(ctx);
    if (selection < 0) { cleanup(ctx); return 0; }
    ctx->model = (cybiko_model_t)selection;
    save_preferences(ctx);
    bool classic = ctx->model != CYBIKO_XTREME;
    vk_rows[VK_ROW_COUNT - 1].keys = classic ? vk_classic_symbols : vk_row_symbols;
    vk_rows[VK_ROW_COUNT - 1].count = classic ?
        (int)(sizeof(vk_classic_symbols)/sizeof(vk_classic_symbols[0])) :
        (int)(sizeof(vk_row_symbols)/sizeof(vk_row_symbols[0]));
    release_all_inputs(ctx);
    if (!select_model_paths(ctx->model)) {
        message_loop(ctx, "Cannot create model folders", "Check ux0:data/VitaCybiko is writable.", "");
        goto select_model;
    }
    const char *boot_path = argc >= 3 && !classic ? argv[1] : runtime_boot_path;
    const char *flash_path = argc >= 3 && !classic ? argv[2] : runtime_flash_path;
    size_t boot_size = 0;
    size_t flash_size = 0;
    uint8_t *boot_data = load_file(boot_path, &boot_size, true);
    uint8_t *flash_data = ctx->model == CYBIKO_CLASSIC_V1 ? NULL : load_file(flash_path, &flash_size, true);

    if (!boot_data || (!flash_data && ctx->model != CYBIKO_CLASSIC_V1)) {
        free(boot_data);
        free(flash_data);
        message_loop(ctx,
                     "Missing firmware for selected device", runtime_boot_path,
                     ctx->model == CYBIKO_CLASSIC_V1 ? "V1 also needs roms/dataflash.bin (contains CyOS)." : runtime_flash_path);
        goto select_model;
    }

    if (!cybiko_check_firmware(ctx->model, boot_data, boot_size, flash_data, flash_size)) {
        free(boot_data);
        free(flash_data);
        message_loop(ctx,
                     "Firmware does not match the selected model",
                     cybiko_machine(ctx->model)->name,
                     "Check model-specific firmware hashes in README.md.");
        goto select_model;
    }

    cybiko_hal_t hal = {
        .render_frame = hal_render_frame,
        .audio_output = hal_audio_output,
        .keyboard_poll = hal_keyboard_poll,
        .serial_output = hal_serial_output,
        .ctx = ctx,
    };

    cybiko_emu_t *emu = cybiko_create_model(&hal, ctx->model);
    if (!emu) {
        free(boot_data);
        free(flash_data);
        message_loop(ctx, "Failed to create Cybiko emulator", "", "");
        goto select_model;
    }

    bool roms_ok = cybiko_load_boot_rom(emu, boot_data, boot_size) &&
                   (ctx->model == CYBIKO_CLASSIC_V1 || cybiko_load_flash_rom(emu, flash_data, flash_size));
    free(boot_data);
    free(flash_data);

    if (!roms_ok) {
        cybiko_destroy(emu);
        message_loop(ctx,
                     "Firmware rejected by the emulator core",
                     "Check the model-specific firmware images.",
                     "");
        goto select_model;
    }

    char app_paths[MAX_APPS][MAX_PATH_CHARS];
    int app_count = classic ? 0 : discover_apps(app_paths);
    bool storage_ok = classic ? load_classic_storage(emu) :
                     app_count >= 0 && load_nvram_and_apps(emu, app_paths, app_count);
    if (!storage_ok) {
        cybiko_destroy(emu);
        message_loop(ctx, "Cannot load selected device's storage",
                     classic ? "Provide a 540672-byte roms/dataflash.bin or valid save.flash." :
                               "Original save preserved. Check save and apps folder.",
                     classic ? "Classic bundled apps come from that flash image." :
                               "Use at most 64 files that fit the Cybiko filesystem.");
        goto select_model;
    }

    if (!load_classic_ram(emu)) {
        cybiko_destroy(emu);
        message_loop(ctx, "Classic RAM checkpoint is invalid or mismatched",
                     "Back up the model folder. Restore matching save.flash and ram.dat.",
                     "Move ram.dat aside only for an intentional cold boot.");
        goto select_model;
    }
    if (!load_clock(emu)) {
        cybiko_destroy(emu);
        message_loop(ctx, "Clock save is invalid; original preserved",
                     "Back up the selected model's clock.dat before replacing it.", "");
        goto select_model;
    }
    snprintf(ctx->status, sizeof(ctx->status), classic ? "Classic / local device - wireless unavailable" : "%d imported file(s)", app_count);
    uint8_t blank_lcd[CYBIKO_LCD_WIDTH * CYBIKO_LCD_HEIGHT];
    memset(blank_lcd, 0xFF, sizeof(blank_lcd));
    hal_render_frame(ctx, blank_lcd, CYBIKO_LCD_WIDTH, CYBIKO_LCD_HEIGHT);

    cybiko_reset(emu);
    if (ctx->audio_dev) {
        SDL_PauseAudioDevice(ctx->audio_dev, 0);
    }

    bool running = true;
    uint32_t next_autosave = SDL_GetTicks() + AUTOSAVE_INTERVAL_MS;
    uint64_t perf_frequency = SDL_GetPerformanceFrequency();
    uint64_t frame_interval = perf_frequency / CYBIKO_FPS;
    uint64_t frame_deadline = SDL_GetPerformanceCounter();
    while (running) {
        process_sdl_events(ctx, &running);
        update_controller_input(ctx, &running);

        uint32_t now = SDL_GetTicks();
        if (ctx->save_requested ||
            (int32_t)(now - next_autosave) >= 0) {
            if (save_session(emu)) {
                snprintf(ctx->status, sizeof(ctx->status), classic ? "Classic flash, RAM and clock saved" : "Xtreme NVRAM and clock saved");
            } else {
                snprintf(ctx->status, sizeof(ctx->status), "Device save failed; check available storage");
            }
            ctx->save_requested = false;
            next_autosave = now + AUTOSAVE_INTERVAL_MS;
        }

        if (!running) {
            break;
        }

        if (ctx->backgrounded || ctx->focus_lost) {
            frame_deadline = SDL_GetPerformanceCounter();
            SDL_Delay(20);
            continue;
        }

        advance_emulated_frame(emu, ctx);

        uint64_t frame_now = SDL_GetPerformanceCounter();
        int catchup_frames = 0;
        while (catchup_frames < EMULATION_CATCHUP_MAX_FRAMES &&
               frame_now > frame_deadline + frame_interval) {
            frame_deadline += frame_interval;
            advance_emulated_frame(emu, ctx);
            catchup_frames++;
            frame_now = SDL_GetPerformanceCounter();
        }

        render_frame(ctx);

        frame_deadline += frame_interval;
        frame_now = SDL_GetPerformanceCounter();
        if (frame_now < frame_deadline) {
            uint64_t remaining_ms =
                (frame_deadline - frame_now) * 1000u / perf_frequency;
            if (remaining_ms > 0) {
                SDL_Delay((uint32_t)remaining_ms);
            }
        } else if (frame_now - frame_deadline > frame_interval * 4u) {
            /* Do not attempt a long catch-up burst after suspend or a stall. */
            frame_deadline = frame_now;
        }
    }

    if (!save_session(emu)) {
        fprintf(stderr, "failed to save NVRAM\n");
    }
    cybiko_destroy(emu);
    goto select_model;
}
