/* Exercise the actual event and storage code with host SDL; no firmware needed. */
#define main vita_frontend_main
#define DATA_DIR "runtime"
#include "../src/vita/main.c"
#undef main
#include "acutest.h"
#include <unistd.h>

static void tick_inputs(app_ctx_t *ctx)
{
    input_tick(&ctx->physical_input);
    input_tick(&ctx->touch_input);
    input_tick(&ctx->controller_input);
    input_tick(&ctx->virtual_input);
}

static uint16_t read_column(app_ctx_t *ctx, int col)
{
    uint16_t matrix[CYBIKO_KEYBOARD_COLUMNS];
    hal_keyboard_poll(ctx, matrix, CYBIKO_KEYBOARD_COLUMNS);
    return matrix[col];
}

static void touch_key(app_ctx_t *ctx, int row, int col, bool down)
{
    SDL_Rect rect;
    virtual_key_rect_layout(ctx->landscape, row, col, &rect);
    handle_portrait_touch(ctx, rect.x + rect.w / 2, rect.y + rect.h / 2, down);
}

static void test_touch_hold_survives_controller_poll(void)
{
    app_ctx_t ctx = {0};
    bool running = true;
    TEST_ASSERT(SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) == 0);
    touch_key(&ctx, 3, 9, true); /* Enter */
    for (int frame = 0; frame < 30; ++frame) {
        update_controller_input(&ctx, &running);
        TEST_CHECK(read_column(&ctx, 4) & 8);
        tick_inputs(&ctx);
    }
    touch_key(&ctx, 3, 9, false);
    TEST_CHECK(!(read_column(&ctx, 4) & 8));
    if (ctx.controller) SDL_GameControllerClose(ctx.controller);
    SDL_Quit();
}

static void test_physical_alias_and_controller_release(void)
{
    app_ctx_t ctx = {0};
    SDL_KeyboardEvent key = {0};
    key.type = SDL_KEYDOWN;
    key.keysym.scancode = SDL_SCANCODE_LSHIFT;
    update_key_matrix_from_keyboard(&ctx, &key);
    key.keysym.scancode = SDL_SCANCODE_RSHIFT;
    update_key_matrix_from_keyboard(&ctx, &key);
    for (int i = 0; i < 5; ++i) tick_inputs(&ctx);
    key.type = SDL_KEYUP;
    key.keysym.scancode = SDL_SCANCODE_LSHIFT;
    update_key_matrix_from_keyboard(&ctx, &key);
    release_controller_normal_keys(&ctx);
    TEST_CHECK(read_column(&ctx, 8) == 0x8000);
    key.keysym.scancode = SDL_SCANCODE_RSHIFT;
    update_key_matrix_from_keyboard(&ctx, &key);
    TEST_CHECK(read_column(&ctx, 8) == 0);
}

static void test_touch_modifiers_and_skin(void)
{
    app_ctx_t ctx = {0};
    touch_key(&ctx, 4, 0, true); /* Fn */
    touch_key(&ctx, 4, 0, false);
    for (int i = 0; i < 8; ++i) tick_inputs(&ctx);
    TEST_CHECK(read_column(&ctx, 7) == 0x8000);
    touch_key(&ctx, 1, 0, true); /* Q */
    TEST_CHECK(read_column(&ctx, 3) == 2);
    TEST_CHECK(read_column(&ctx, 7) == 0x8000);
    touch_key(&ctx, 1, 0, false);
    touch_key(&ctx, 4, 0, true);
    touch_key(&ctx, 4, 0, false);
    TEST_CHECK(read_column(&ctx, 7) == 0);
    for (int i = 0; i < 10; ++i)
        handle_portrait_touch(&ctx, SKIN_BUTTON_X + 1, SKIN_BUTTON_Y + 1, true);
    TEST_CHECK(ctx.skin_index == 1);
}

static void test_storage_preserves_existing_data(void)
{
    char cwd[MAX_PATH_CHARS];
    TEST_ASSERT(getcwd(cwd, sizeof(cwd)) != NULL);
    char temporary[] = "/tmp/vitacybiko-storage-XXXXXX";
    TEST_ASSERT(mkdtemp(temporary) != NULL);
    TEST_ASSERT(chdir(temporary) == 0);
    TEST_ASSERT(ensure_dir(DATA_DIR));
    TEST_ASSERT(ensure_dir(APP_DIR));
    cybiko_hal_t hal = {0};
    cybiko_emu_t *emu = cybiko_create(&hal);
    TEST_ASSERT(emu != NULL);
    char paths[MAX_APPS][MAX_PATH_CHARS] = {{0}};
    static const uint8_t broken[] = {1, 2, 3, 4};
    TEST_ASSERT(write_file(NVRAM_PATH, broken, sizeof(broken)));
    TEST_CHECK(!load_nvram_and_apps(emu, paths, 0));
    size_t length = 0;
    uint8_t *saved = load_file(NVRAM_PATH, &length, false);
    TEST_ASSERT(saved != NULL);
    TEST_CHECK(length == sizeof(broken));
    TEST_CHECK(memcmp(saved, broken, sizeof(broken)) == 0);
    free(saved);

    cfs_image_t *cfs = malloc(sizeof(*cfs));
    TEST_ASSERT(cfs != NULL);
    cfs_format(cfs);
    cfs->data[CFS_PAGE_SIZE * CFS_BOOT_BLOCKS + 10] ^= 1;
    TEST_ASSERT(write_file(NVRAM_PATH, cfs->data, CFS_IMAGE_SIZE));
    TEST_CHECK(!load_nvram_and_apps(emu, paths, 0));
    saved = load_file(NVRAM_PATH, &length, false);
    TEST_ASSERT(saved != NULL);
    TEST_CHECK(length == CFS_IMAGE_SIZE);
    TEST_CHECK(memcmp(saved, cfs->data, CFS_IMAGE_SIZE) == 0);
    free(saved);

    cfs_format(cfs);
    uint8_t *full = calloc(CYBIKO_NVRAM_SIZE, 1);
    TEST_ASSERT(full != NULL);
    memcpy(full, cfs->data, CFS_IMAGE_SIZE);
    full[CYBIKO_NVRAM_SIZE - 1] = 0xA5;
    TEST_ASSERT(write_file(NVRAM_PATH, full, CYBIKO_NVRAM_SIZE));
    TEST_CHECK(load_nvram_and_apps(emu, paths, 0));
    TEST_CHECK(cybiko_get_nvram(emu, NULL)[CYBIKO_NVRAM_SIZE - 1] == 0xA5);
    TEST_ASSERT(save_nvram(emu));
    TEST_ASSERT(save_nvram(emu)); /* Replacement path, not just first creation. */
    snprintf(paths[0], sizeof(paths[0]), "%s/missing.app", APP_DIR);
    TEST_CHECK(!load_nvram_and_apps(emu, paths, 1));
    saved = load_file(NVRAM_PATH, &length, false);
    TEST_ASSERT(saved != NULL);
    TEST_CHECK(length == CYBIKO_NVRAM_SIZE);
    TEST_CHECK(memcmp(saved, full, CYBIKO_NVRAM_SIZE) == 0);
    free(saved);
    free(full);
    free(cfs);
    cybiko_destroy(emu);
    TEST_CHECK(remove(NVRAM_PATH) == 0);
    TEST_CHECK(rmdir(APP_DIR) == 0);
    TEST_CHECK(rmdir(DATA_DIR) == 0);
    TEST_ASSERT(chdir(cwd) == 0);
    TEST_CHECK(rmdir(temporary) == 0);
}

static void test_render_and_background_events(void)
{
    app_ctx_t *ctx = calloc(1, sizeof(*ctx));
    TEST_ASSERT(ctx != NULL);
    TEST_ASSERT(init_sdl(ctx));
    uint8_t pixels[CYBIKO_LCD_WIDTH * CYBIKO_LCD_HEIGHT];
    memset(pixels, 0xff, sizeof(pixels));
    hal_render_frame(ctx, pixels, CYBIKO_LCD_WIDTH, CYBIKO_LCD_HEIGHT);
    snprintf(ctx->status, sizeof(ctx->status), "Frontend test - no firmware loaded");
    render_frame(ctx);
    const char *screenshot = getenv("VITACYBIKO_TEST_SCREENSHOT");
    if (screenshot) {
        SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_WIDTH, SCREEN_HEIGHT, 32, SDL_PIXELFORMAT_ARGB8888);
        TEST_ASSERT(surface != NULL);
        TEST_CHECK(SDL_RenderReadPixels(ctx->renderer, NULL, surface->format->format,
                                       surface->pixels, surface->pitch) == 0);
        TEST_CHECK(SDL_SaveBMP(surface, screenshot) == 0);
        SDL_FreeSurface(surface);
    }
    bool running = true;
    touch_key(ctx, 4, 0, true);
    SDL_Event event = {0};
    event.type = SDL_APP_WILLENTERBACKGROUND;
    TEST_ASSERT(SDL_PushEvent(&event) == 1);
    process_sdl_events(ctx, &running);
    TEST_CHECK(ctx->save_requested);
    TEST_CHECK(ctx->backgrounded);
    TEST_CHECK(!ctx->touch_fn_latched);
    TEST_CHECK(read_column(ctx, 7) == 0);
    event.type = SDL_APP_DIDENTERFOREGROUND;
    TEST_ASSERT(SDL_PushEvent(&event) == 1);
    process_sdl_events(ctx, &running);
    TEST_CHECK(!ctx->backgrounded);
    render_message_screen(ctx, "Cannot load save or app set",
                          "Original save preserved. Check save and apps folder.",
                          "Use at most 64 apps that fit the Cybiko filesystem.");
    cleanup(ctx);
}

static void test_focus_loss_and_mouse_buttons(void)
{
    app_ctx_t *ctx = calloc(1, sizeof(*ctx));
    TEST_ASSERT(ctx != NULL);
    TEST_ASSERT(init_sdl(ctx));
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    bool running = true;
    SDL_KeyboardEvent key = {0};
    key.type = SDL_KEYDOWN;
    key.keysym.scancode = SDL_SCANCODE_LSHIFT;
    update_key_matrix_from_keyboard(ctx, &key);
    touch_key(ctx, 4, 0, true);
    apply_controller_normal_keys(ctx, 1u << SDL_CONTROLLER_BUTTON_A);
    ctx->previous_buttons = 123;
    const uint8_t silence[128] = {0};
    if (ctx->audio_dev)
        TEST_ASSERT(SDL_QueueAudio(ctx->audio_dev, silence, sizeof(silence)) == 0);

    SDL_Event event = {0};
    event.type = SDL_WINDOWEVENT;
    event.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    TEST_ASSERT(SDL_PushEvent(&event) == 1);
    process_sdl_events(ctx, &running);
    TEST_CHECK(ctx->focus_lost);
    TEST_CHECK(ctx->save_requested);
    TEST_CHECK(!ctx->touch_fn_latched);
    TEST_CHECK(!ctx->physical_down[SDL_SCANCODE_LSHIFT]);
    TEST_CHECK(ctx->previous_buttons == 0);
    for (int col = 0; col < CYBIKO_KEYBOARD_COLUMNS; ++col)
        TEST_CHECK(read_column(ctx, col) == 0);
    if (ctx->audio_dev) {
        TEST_CHECK(SDL_GetQueuedAudioSize(ctx->audio_dev) == 0);
        TEST_CHECK(SDL_GetAudioDeviceStatus(ctx->audio_dev) == SDL_AUDIO_PAUSED);
    }

    event.type = SDL_KEYDOWN;
    event.key = key;
    TEST_ASSERT(SDL_PushEvent(&event) == 1);
    process_sdl_events(ctx, &running);
    TEST_CHECK(read_column(ctx, 8) == 0);
    /* Foreground notification must not override independent focus loss. */
    event.type = SDL_APP_DIDENTERFOREGROUND;
    TEST_ASSERT(SDL_PushEvent(&event) == 1);
    process_sdl_events(ctx, &running);
    TEST_CHECK(ctx->focus_lost);
    update_controller_input(ctx, &running);
    TEST_CHECK(running);
    event.type = SDL_WINDOWEVENT;
    event.window.event = SDL_WINDOWEVENT_FOCUS_GAINED;
    TEST_ASSERT(SDL_PushEvent(&event) == 1);
    process_sdl_events(ctx, &running);
    TEST_CHECK(!ctx->focus_lost);
    if (ctx->audio_dev)
        TEST_CHECK(SDL_GetAudioDeviceStatus(ctx->audio_dev) == SDL_AUDIO_PAUSED);
    TEST_CHECK(!ctx->audio_started);

    SDL_Rect rect;
    virtual_key_rect_layout(false, 3, 9, &rect); /* Enter */
    event.type = SDL_MOUSEBUTTONDOWN;
    event.button.x = SCREEN_WIDTH - 1 - (rect.y + rect.h / 2);
    event.button.y = rect.x + rect.w / 2;
    event.button.button = SDL_BUTTON_RIGHT;
    TEST_ASSERT(SDL_PushEvent(&event) == 1);
    process_sdl_events(ctx, &running);
    TEST_CHECK(read_column(ctx, 4) == 0);
    event.button.button = SDL_BUTTON_LEFT;
    TEST_ASSERT(SDL_PushEvent(&event) == 1);
    process_sdl_events(ctx, &running);
    TEST_CHECK(read_column(ctx, 4) & 8);
    event.type = SDL_MOUSEBUTTONUP;
    event.button.button = SDL_BUTTON_RIGHT;
    TEST_ASSERT(SDL_PushEvent(&event) == 1);
    process_sdl_events(ctx, &running);
    for (int i = 0; i < 8; ++i) tick_inputs(ctx);
    TEST_CHECK(read_column(ctx, 4) & 8);
    event.button.button = SDL_BUTTON_LEFT;
    TEST_ASSERT(SDL_PushEvent(&event) == 1);
    process_sdl_events(ctx, &running);
    TEST_CHECK(read_column(ctx, 4) == 0);
    cleanup(ctx);
}

static void test_audio_queue_bounds_latency(void)
{
    app_ctx_t *ctx = calloc(1, sizeof(*ctx));
    TEST_ASSERT(ctx != NULL);
    TEST_ASSERT(init_sdl(ctx));
    if (ctx->audio_dev) {
        uint8_t frame[SPEAKER_SAMPLE_RATE / CYBIKO_FPS];
        uint8_t stale[65536];
        memset(frame, 128, sizeof(frame));
        memset(stale, 128, sizeof(stale));
        TEST_ASSERT(ctx->audio_frame_bytes > 0);
        TEST_ASSERT(ctx->audio_max_queue_bytes <= sizeof(stale));

        reset_audio(ctx);
        hal_audio_output(ctx, frame, (int)sizeof(frame));
        TEST_CHECK(SDL_GetQueuedAudioSize(ctx->audio_dev) ==
                   ctx->audio_frame_bytes);
        TEST_CHECK(!ctx->audio_started);
        TEST_CHECK(SDL_GetAudioDeviceStatus(ctx->audio_dev) == SDL_AUDIO_PAUSED);
        /* Keep the dummy device paused to test exact byte counts, separately
         * from the start/resume threshold. */
        ctx->audio_started = true;
        hal_audio_output(ctx, frame, (int)sizeof(frame));
        TEST_CHECK(SDL_GetQueuedAudioSize(ctx->audio_dev) ==
                   ctx->audio_frame_bytes * 2u);

        SDL_ClearQueuedAudio(ctx->audio_dev);
        TEST_ASSERT(SDL_QueueAudio(ctx->audio_dev, stale,
                                   ctx->audio_max_queue_bytes) == 0);
        hal_audio_output(ctx, frame, (int)sizeof(frame));
        TEST_CHECK(SDL_GetQueuedAudioSize(ctx->audio_dev) ==
                   ctx->audio_max_queue_bytes);
        TEST_CHECK(ctx->audio_dropped_frames == 1);

        /* An underrun must queue only the supplied sound, never extra silence. */
        SDL_ClearQueuedAudio(ctx->audio_dev);
        hal_audio_output(ctx, frame, (int)sizeof(frame));
        TEST_CHECK(SDL_GetQueuedAudioSize(ctx->audio_dev) == ctx->audio_frame_bytes);
        TEST_CHECK(ctx->audio_underruns == 1);
        reset_audio(ctx);
        hal_audio_output(ctx, frame, (int)sizeof(frame));
        hal_audio_output(ctx, frame, (int)sizeof(frame));
        TEST_CHECK(ctx->audio_started);
        TEST_CHECK(SDL_GetAudioDeviceStatus(ctx->audio_dev) == SDL_AUDIO_PLAYING);
    }
    cleanup(ctx);
}

static void test_import_pack_and_library_paths(void)
{
    const char *pack = getenv("VITACYBIKO_TEST_CD_PACK");
    char cwd[MAX_PATH_CHARS];
    TEST_ASSERT(getcwd(cwd, sizeof(cwd)) != NULL);
    char temporary[] = "/tmp/vitacybiko-pack-XXXXXX";
    TEST_ASSERT(mkdtemp(temporary) != NULL);
    TEST_ASSERT(chdir(temporary) == 0);
    TEST_ASSERT(ensure_dir(DATA_DIR));
    TEST_ASSERT(ensure_dir(APP_DIR));
    TEST_ASSERT(ensure_dir(APP_DIR "/Libraries"));
    if (pack) {
        /* Point the importer at the real extracted CD files, without modifying them. */
        TEST_ASSERT(rmdir(APP_DIR "/Libraries") == 0);
        TEST_ASSERT(rmdir(APP_DIR) == 0);
        TEST_ASSERT(symlink(pack, APP_DIR) == 0);
    } else {
        uint8_t data[] = {0x43, 0x79};
        TEST_ASSERT(write_file(APP_DIR "/test.app", data, sizeof(data)));
        TEST_ASSERT(write_file(APP_DIR "/email_dl.dl", data, sizeof(data)));
        TEST_ASSERT(write_file(APP_DIR "/Libraries/sound.dl", data, sizeof(data)));
    }
    char paths[MAX_APPS][MAX_PATH_CHARS] = {{0}};
    int count = discover_apps(paths);
    TEST_CHECK(count == (pack ? 15 : 3));
    cybiko_hal_t hal = {0};
    cybiko_emu_t *emu = cybiko_create(&hal);
    TEST_ASSERT(emu != NULL);
    TEST_ASSERT(load_nvram_and_apps(emu, paths, count));
    TEST_ASSERT(save_nvram(emu));
    TEST_ASSERT(load_nvram_and_apps(emu, paths, count)); /* Reimport without duplicates. */
    cfs_image_t *cfs = malloc(sizeof(*cfs));
    TEST_ASSERT(cfs != NULL);
    memcpy(cfs->data, cybiko_get_nvram(emu, NULL), CFS_IMAGE_SIZE);
    TEST_CHECK(cfs_validate(cfs));
    char names[MAX_APPS][64];
    TEST_CHECK(cfs_list_files(cfs, names, MAX_APPS) == count);
    bool found_library = false, found_email_library = false;
    for (int i = 0; i < count; ++i) {
        found_library |= strcmp(names[i], "Libraries/sound.dl") == 0;
        found_email_library |= strcmp(names[i], "email_dl.dl") == 0;
    }
    TEST_CHECK(found_library);
    TEST_CHECK(found_email_library);
    free(cfs);
    cybiko_destroy(emu);
    if (pack) {
        TEST_CHECK(unlink(APP_DIR) == 0); /* Only the test-created symlink. */
    } else {
        TEST_CHECK(remove(APP_DIR "/test.app") == 0);
        TEST_CHECK(remove(APP_DIR "/email_dl.dl") == 0);
        TEST_CHECK(remove(APP_DIR "/Libraries/sound.dl") == 0);
        TEST_CHECK(rmdir(APP_DIR "/Libraries") == 0);
        TEST_CHECK(rmdir(APP_DIR) == 0);
    }
    TEST_CHECK(remove(NVRAM_PATH) == 0);
    TEST_CHECK(rmdir(DATA_DIR) == 0);
    TEST_ASSERT(chdir(cwd) == 0);
    TEST_CHECK(rmdir(temporary) == 0);
}

static void test_model_menu_selection(void)
{
    app_ctx_t *ctx = calloc(1, sizeof(*ctx));
    TEST_ASSERT(ctx != NULL);
    TEST_ASSERT(init_sdl(ctx));
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    model_menu_state_t menu = {.selected = 0, .pressed = -1};
    SDL_Event event = {0};
    event.type = SDL_KEYDOWN;
    event.key.keysym.scancode = SDL_SCANCODE_DOWN;
    TEST_CHECK(model_menu_event(ctx, &menu, &event) == 0);
    TEST_CHECK(menu_models[menu.selected] == CYBIKO_CLASSIC_V2);
    event.key.keysym.scancode = SDL_SCANCODE_RETURN;
    TEST_CHECK(model_menu_event(ctx, &menu, &event) == 1);
    event.type = SDL_MOUSEBUTTONDOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = 400; event.button.y = 350;
    TEST_CHECK(model_menu_event(ctx, &menu, &event) == 0);
    TEST_CHECK(menu_models[menu.selected] == CYBIKO_XTREME);
    event.type = SDL_MOUSEBUTTONUP;
    event.button.y = 410; /* Gap below the button cancels the press. */
    TEST_CHECK(model_menu_event(ctx, &menu, &event) == 0);
    TEST_CHECK(menu.pressed == -1);
    event.type = SDL_MOUSEBUTTONDOWN;
    event.button.y = 150;
    TEST_CHECK(model_menu_event(ctx, &menu, &event) == 0);
    event.type = SDL_MOUSEBUTTONUP;
    TEST_CHECK(model_menu_event(ctx, &menu, &event) == 1);
    TEST_CHECK(menu_models[menu.selected] == CYBIKO_CLASSIC_V1);
    TEST_CHECK(model_menu_row(840, 120) == -1);
    TEST_CHECK(model_menu_row(120, 420) == -1);
    TEST_CHECK(model_menu_row(120, -1) == -1);
    ctx->backgrounded = true;
    event.type = SDL_KEYDOWN; event.key.keysym.scancode = SDL_SCANCODE_RETURN;
    TEST_CHECK(model_menu_event(ctx, &menu, &event) == 0);
    ctx->backgrounded = false;
    ctx->model = CYBIKO_CLASSIC_V2;
    TEST_ASSERT(SDL_PushEvent(&event) == 1);
    TEST_CHECK(choose_model(ctx) == CYBIKO_CLASSIC_V2);
    render_model_menu(ctx, 1);
    const char *screenshot = getenv("VITACYBIKO_TEST_MENU_SCREENSHOT");
    if (screenshot) {
        SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_WIDTH, SCREEN_HEIGHT, 32, SDL_PIXELFORMAT_ARGB8888);
        TEST_ASSERT(surface != NULL);
        TEST_CHECK(SDL_RenderReadPixels(ctx->renderer, NULL, surface->format->format, surface->pixels, surface->pitch) == 0);
        TEST_CHECK(SDL_SaveBMP(surface, screenshot) == 0);
        SDL_FreeSurface(surface);
    }
    SDL_KeyboardEvent key = {.type = SDL_KEYDOWN};
    key.keysym.scancode = SDL_SCANCODE_BACKSPACE;
    update_key_matrix_from_keyboard(ctx, &key);
    TEST_CHECK(read_column(ctx, 6) == 4);
    TEST_CHECK(read_column(ctx, 0) == 0);
    cleanup(ctx);
}

static void test_model_storage_isolation(void)
{
    char cwd[MAX_PATH_CHARS];
    TEST_ASSERT(getcwd(cwd, sizeof(cwd)) != NULL);
    char temporary[] = "/tmp/vitacybiko-models-XXXXXX";
    TEST_ASSERT(mkdtemp(temporary) != NULL);
    TEST_ASSERT(chdir(temporary) == 0);
    TEST_ASSERT(ensure_dir(DATA_DIR));
    cybiko_hal_t hal = {0};
    uint8_t *serial = malloc(DATAFLASH_SIZE);
    TEST_ASSERT(serial != NULL);
    memset(serial, 0x45, DATAFLASH_SIZE);
    for (int model = 0; model < CYBIKO_MODEL_COUNT; ++model) {
        TEST_ASSERT(select_model_paths((cybiko_model_t)model));
        TEST_CHECK(strstr(runtime_save_path, cybiko_machine((cybiko_model_t)model)->directory) != NULL);
        cybiko_emu_t *emu = cybiko_create_model(&hal, (cybiko_model_t)model);
        TEST_ASSERT(emu != NULL);
        if (model != CYBIKO_XTREME) {
            TEST_CHECK(!load_classic_storage(emu));
            TEST_ASSERT(write_file(runtime_dataflash_path, serial, DATAFLASH_SIZE));
            TEST_CHECK(load_classic_storage(emu));
            TEST_CHECK(save_nvram(emu));
            /* Corrupt existing save must not fall back to the valid factory image. */
            static const uint8_t bad[] = {1, 2, 3};
            TEST_ASSERT(write_file(runtime_save_path, bad, sizeof(bad)));
            TEST_CHECK(!load_classic_storage(emu));
            size_t size = 0;
            uint8_t *data = load_file(runtime_save_path, &size, false);
            TEST_ASSERT(data != NULL);
            TEST_CHECK(size == sizeof(bad));
            TEST_CHECK(memcmp(data, bad, sizeof(bad)) == 0);
            free(data);
            TEST_CHECK(remove(runtime_save_path) == 0);
            TEST_CHECK(remove(runtime_dataflash_path) == 0);
        }
        cybiko_destroy(emu);
        TEST_CHECK(rmdir(runtime_app_dir) == 0);
        char rom_dir[MAX_PATH_CHARS];
        snprintf(rom_dir, sizeof(rom_dir), "%s/%s/roms", DATA_DIR, cybiko_machine((cybiko_model_t)model)->directory);
        TEST_CHECK(rmdir(rom_dir) == 0);
        TEST_CHECK(rmdir(runtime_root) == 0);
    }
    free(serial);
    TEST_CHECK(rmdir(DATA_DIR) == 0);
    TEST_ASSERT(chdir(cwd) == 0);
    TEST_CHECK(rmdir(temporary) == 0);
    /* Restore legacy defaults for the existing tests. */
    snprintf(runtime_root, sizeof(runtime_root), "%s", DATA_DIR);
    snprintf(runtime_app_dir, sizeof(runtime_app_dir), "%s", APP_DIR);
    snprintf(runtime_save_path, sizeof(runtime_save_path), "%s", NVRAM_PATH);
    snprintf(runtime_boot_path, sizeof(runtime_boot_path), "%s", BOOT_PATH);
    snprintf(runtime_flash_path, sizeof(runtime_flash_path), "%s", FLASH_PATH);
}

static void test_landscape_touch_and_rotation(void)
{
    app_ctx_t ctx = {0};
    ctx.landscape = true;
    SDL_Rect rect;
    virtual_key_rect_layout(true, 3, 9, &rect);
    TEST_CHECK(rect.x >= 520 && rect.x + rect.w <= SCREEN_WIDTH);
    handle_screen_touch(&ctx, rect.x + rect.w / 2, rect.y + rect.h / 2, true);
    TEST_CHECK(read_column(&ctx, 4) & 8);
    handle_screen_touch(&ctx, rect.x, rect.y, false);
    for (int i = 0; i < 8; ++i) tick_inputs(&ctx);
    TEST_CHECK(read_column(&ctx, 4) == 0);
    ctx.finger_down = true; ctx.finger_id = 42;
    handle_screen_touch(&ctx, 740, 35, true);
    TEST_CHECK(!ctx.landscape);
    TEST_CHECK(ctx.finger_down && ctx.finger_id == 42);
    TEST_CHECK(ctx.touch_layout_down);
    handle_screen_touch(&ctx, 740, 35, true); /* Drag cannot toggle twice. */
    TEST_CHECK(!ctx.landscape);
    handle_screen_touch(&ctx, 740, 35, false);
    TEST_CHECK(!ctx.touch_layout_down);
    TEST_CHECK(read_column(&ctx, 4) == 0);
}

static void test_clock_storage(void)
{
    char temporary[] = "/tmp/vitacybiko-clock-XXXXXX";
    TEST_ASSERT(mkdtemp(temporary) != NULL);
    snprintf(runtime_root, sizeof(runtime_root), "%s", temporary);
    cybiko_hal_t hal = {0};
    cybiko_emu_t *emu = cybiko_create_model(&hal, CYBIKO_CLASSIC_V2);
    TEST_ASSERT(emu != NULL);
    TEST_CHECK(load_clock(emu)); /* Missing is first install. */
    uint8_t registers[16] = {0x80,0,0x12,0x34,0x15,0x59,0x09};
    uint8_t restored[16];
    TEST_CHECK(cybiko_load_clock(emu, registers, 16, 0));
    TEST_CHECK(save_clock(emu));
    TEST_CHECK(save_clock(emu)); /* Atomic replacement also works. */
    memset(restored, 0, sizeof(restored));
    TEST_CHECK(cybiko_load_clock(emu, restored, 16, 0));
    TEST_CHECK(load_clock(emu));
    cybiko_get_clock(emu, restored);
    TEST_CHECK(memcmp(registers, restored, 16) == 0);
    char path[MAX_PATH_CHARS]; TEST_CHECK(clock_path(path));
    size_t size = 0; uint8_t *data = load_file(path, &size, false);
    TEST_ASSERT(data && size == 36);
    data[12] ^= 1; /* Data changed without updating checksum. */
    TEST_CHECK(write_file(path, data, size));
    TEST_CHECK(!load_clock(emu));
    uint8_t *unchanged = load_file(path, &size, false);
    TEST_ASSERT(unchanged && size == 36);
    TEST_CHECK(memcmp(data, unchanged, size) == 0);
    free(unchanged); free(data);
    TEST_CHECK(remove(path) == 0);
    TEST_CHECK(rmdir(temporary) == 0);
    cybiko_destroy(emu);
    snprintf(runtime_root, sizeof(runtime_root), "%s", DATA_DIR);
}

static void test_preferences_storage(void)
{
    char cwd[4096]; TEST_ASSERT(getcwd(cwd, sizeof(cwd)) != NULL);
    char temporary[] = "/tmp/vitacybiko-preferences-XXXXXX";
    TEST_ASSERT(mkdtemp(temporary) != NULL);
    TEST_ASSERT(chdir(temporary) == 0);
    TEST_ASSERT(mkdir(DATA_DIR, 0700) == 0);
    app_ctx_t original = {0}, restored = {0};
    original.preferences_enabled = true;
    original.model = CYBIKO_CLASSIC_V2;
    original.landscape = true;
    original.skin_index = 2;
    save_preferences(&original);
    load_preferences(&restored);
    TEST_CHECK(restored.model == original.model);
    TEST_CHECK(restored.landscape && restored.skin_index == 2);
    size_t size = 0;
    uint8_t *data = load_file(DATA_DIR "/preferences.dat", &size, false);
    TEST_ASSERT(data && size == 12);
    data[6] ^= 1;
    TEST_CHECK(write_file(DATA_DIR "/preferences.dat", data, size));
    restored.skin_index = 1;
    load_preferences(&restored);
    TEST_CHECK(restored.skin_index == 1); /* Bad checksum leaves defaults intact. */
    free(data);
    TEST_CHECK(remove(DATA_DIR "/preferences.dat") == 0);
    TEST_CHECK(rmdir(DATA_DIR) == 0);
    TEST_ASSERT(chdir(cwd) == 0);
    TEST_CHECK(rmdir(temporary) == 0);
}

static void test_classic_ram_storage(void)
{
    char temporary[] = "/tmp/vitacybiko-ram-XXXXXX";
    TEST_ASSERT(mkdtemp(temporary) != NULL);
    snprintf(runtime_root, sizeof(runtime_root), "%s", temporary);
    cybiko_hal_t hal = {0};
    cybiko_emu_t *emu = cybiko_create_model(&hal, CYBIKO_CLASSIC_V2);
    TEST_ASSERT(emu != NULL);
    TEST_CHECK(load_classic_ram(emu)); /* Missing is a legacy/cold boot. */
    size_t length;
    const uint8_t *ram = cybiko_get_nvram(emu, &length);
    uint8_t *pattern = malloc(length);
    TEST_ASSERT(pattern != NULL);
    memset(pattern, 0x5a, length);
    TEST_CHECK(cybiko_load_nvram(emu, pattern, length));
    TEST_CHECK(save_classic_ram(emu));
    TEST_CHECK(save_classic_ram(emu));
    memset(pattern, 0, length);
    TEST_CHECK(cybiko_load_nvram(emu, pattern, length));
    TEST_CHECK(load_classic_ram(emu));
    TEST_CHECK(ram[0] == 0x5a && ram[length - 1] == 0x5a);
    char path[MAX_PATH_CHARS]; TEST_ASSERT(classic_ram_path(path));
    size_t size = 0; uint8_t *data = load_file(path, &size, false);
    TEST_ASSERT(data && size == length + 24);
    data[24] ^= 1;
    TEST_CHECK(write_file(path, data, size));
    TEST_CHECK(!load_classic_ram(emu)); /* Corrupt payload never changes SRAM. */
    TEST_CHECK(ram[0] == 0x5a);
    size_t unchanged_size; uint8_t *unchanged = load_file(path, &unchanged_size, false);
    TEST_ASSERT(unchanged && unchanged_size == size);
    TEST_CHECK(!memcmp(data, unchanged, size));
    free(unchanged); free(data);
    TEST_CHECK(save_classic_ram(emu));
    uint8_t *flash = malloc(DATAFLASH_SIZE); TEST_ASSERT(flash != NULL);
    memset(flash, 0x39, DATAFLASH_SIZE);
    TEST_CHECK(cybiko_load_dataflash(emu, flash, DATAFLASH_SIZE));
    TEST_CHECK(!load_classic_ram(emu)); /* Wrong flash checkpoint preserved. */
    cybiko_emu_t *v1 = cybiko_create_model(&hal, CYBIKO_CLASSIC_V1);
    TEST_ASSERT(v1 != NULL);
    TEST_CHECK(!load_classic_ram(v1)); /* Model/size mismatch. */
    cybiko_destroy(v1); cybiko_destroy(emu);
    free(flash); free(pattern);
    TEST_CHECK(remove(path) == 0);
    TEST_CHECK(rmdir(temporary) == 0);
    snprintf(runtime_root, sizeof(runtime_root), "%s", DATA_DIR);
}

static void test_classic_input_timing_after_reset(void)
{
    app_ctx_t ctx = {0};
    ctx.model = CYBIKO_CLASSIC_V2;
    release_all_inputs(&ctx);
    TEST_CHECK(ctx.touch_input.classic && ctx.controller_input.classic);
    TEST_CHECK(ctx.physical_input.classic && ctx.virtual_input.classic);
    touch_key(&ctx, 4, 3, true); /* Esc, a deliberately very short tap. */
    touch_key(&ctx, 4, 3, false);
    for (int frame = 0; frame < 8; ++frame) {
        TEST_CHECK(read_column(&ctx, 0) & 2);
        tick_inputs(&ctx);
    }
    TEST_CHECK(!(read_column(&ctx, 0) & 2));
    ctx.model = CYBIKO_XTREME;
    release_all_inputs(&ctx);
    TEST_CHECK(!ctx.touch_input.classic && !ctx.controller_input.classic);
}

static void test_runtime_dir_creation_on_prepared_storage(void)
{
    char cwd[MAX_PATH_CHARS];
    TEST_ASSERT(getcwd(cwd, sizeof(cwd)) != NULL);
    char temporary[] = "/tmp/vitacybiko-runtime-dirs-XXXXXX";
    TEST_ASSERT(mkdtemp(temporary) != NULL);
    TEST_ASSERT(chdir(temporary) == 0);

    struct stat info;
    TEST_CHECK(ensure_runtime_dirs());
    TEST_CHECK(stat(DATA_DIR, &info) == 0 && S_ISDIR(info.st_mode));
    TEST_CHECK(stat(ROM_DIR, &info) != 0 && errno == ENOENT);
    TEST_CHECK(stat(APP_DIR, &info) != 0 && errno == ENOENT);

    TEST_CHECK(select_model_paths(CYBIKO_CLASSIC_V2));
    TEST_CHECK(stat(DATA_DIR "/classic-v2/roms", &info) == 0 && S_ISDIR(info.st_mode));
    TEST_CHECK(stat(DATA_DIR "/classic-v2/apps", &info) == 0 && S_ISDIR(info.st_mode));
    snprintf(runtime_root, sizeof(runtime_root), "%s", DATA_DIR);

    TEST_ASSERT(chdir(cwd) == 0);
    char path[MAX_PATH_CHARS];
    snprintf(path, sizeof(path), "%s/%s", temporary, DATA_DIR "/classic-v2/apps");
    TEST_CHECK(rmdir(path) == 0);
    snprintf(path, sizeof(path), "%s/%s", temporary, DATA_DIR "/classic-v2/roms");
    TEST_CHECK(rmdir(path) == 0);
    snprintf(path, sizeof(path), "%s/%s", temporary, DATA_DIR "/classic-v2");
    TEST_CHECK(rmdir(path) == 0);
    snprintf(path, sizeof(path), "%s/%s", temporary, DATA_DIR);
    TEST_CHECK(rmdir(path) == 0);
    TEST_CHECK(rmdir(temporary) == 0);
}

static void test_presentation_gaps(void)
{
    present_timing_t timing = {0};
    record_present(&timing, 1000000, 16000);
    TEST_CHECK(timing.max_gap == 0 && timing.late == 0);
    record_present(&timing, 1016000, 16000);
    TEST_CHECK(timing.max_gap == 16000 && timing.late == 0);
    record_present(&timing, 1036000, 16000);
    TEST_CHECK(timing.max_gap == 20000 && timing.late == 0);
    record_present(&timing, 1068000, 16000);
    TEST_CHECK(timing.max_gap == 32000 && timing.late == 1);
    /* A new reporting window must retain the previous presentation stamp. */
    timing.max_gap = timing.late = 0;
    record_present(&timing, 1116000, 16000);
    TEST_CHECK(timing.max_gap == 48000 && timing.late == 1);
    /* Suspend/resume starts a new interval, not a multi-second late frame. */
    memset(&timing, 0, sizeof(timing));
    record_present(&timing, 9000000, 16000);
    TEST_CHECK(timing.max_gap == 0 && timing.late == 0);
}

static void test_catchup_respects_presentation_budget(void)
{
    /* 50 ms CPU work caused 150 ms between presentations in the old loop. */
    TEST_CHECK(catchup_frame_budget(50000, 16667, 50000, 1000) == 0);
    TEST_CHECK(catchup_frame_budget(100000, 16667, 10000, 3000) == 0);
    TEST_CHECK(catchup_frame_budget(50000, 16667, 4000, 3000) == 2);
    TEST_CHECK(catchup_frame_budget(16667, 16667, 4000, 3000) == 1);
    TEST_CHECK(catchup_frame_budget(0, 16667, 4000, 3000) == 0);
    TEST_CHECK(catchup_frame_budget(50000, 16667, 0, 0) == 0);
    TEST_CHECK(catchup_frame_budget(50000, 16667, 1000, 20000) == 0);
}

static void test_lcd_upload_preserves_colors_and_skips_duplicates(void)
{
    app_ctx_t *ctx = calloc(1, sizeof(*ctx));
    TEST_ASSERT(ctx != NULL);
    TEST_ASSERT(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 160, 100, 32,
                                                         SDL_PIXELFORMAT_ARGB8888);
    TEST_ASSERT(surface != NULL);
    SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
    TEST_ASSERT(renderer != NULL);
    ctx->lcd_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, 160, 100);
    TEST_ASSERT(ctx->lcd_texture != NULL);
    uint8_t pixels[160 * 100] = {0};
    pixels[1] = 128;
    pixels[2] = 255;
    hal_render_frame(ctx, pixels, 160, 100);
    TEST_CHECK(ctx->lcd_updates == 1);
    TEST_CHECK(ctx->lcd_pixels[0] == 0xff242d26);
    TEST_CHECK(ctx->lcd_pixels[1] == 0xff7b8575);
    TEST_CHECK(ctx->lcd_pixels[2] == 0xffd2ddc4);
    hal_render_frame(ctx, pixels, 160, 100);
    TEST_CHECK(ctx->lcd_updates == 1);
    pixels[0] = 255;
    hal_render_frame(ctx, pixels, 160, 100);
    TEST_CHECK(ctx->lcd_updates == 2);
    TEST_CHECK(ctx->lcd_pixels[0] == 0xffd2ddc4);
    SDL_DestroyTexture(ctx->lcd_texture);
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    SDL_Quit();
    free(ctx);
}

static void test_frame_log_bounds(void)
{
    uint8_t data[32];
    memset(data, 0xa5, sizeof(data));
    frame_log_t log = {.data = data, .capacity = 16};
    TEST_CHECK(append_frame_log(&log, "header\n"));
    TEST_CHECK(log.used == 7);
    TEST_CHECK(!append_frame_log(&log, "%020d", 1));
    TEST_CHECK(log.used == 7);
    for (size_t i = 16; i < sizeof(data); ++i) TEST_CHECK(data[i] == 0xa5);
    TEST_CHECK(append_frame_log(&log, "tail\n"));
    TEST_CHECK(log.used == 12);
    TEST_CHECK(!memcmp(data, "header\ntail\n", 12));
    TEST_CHECK(!append_frame_log(NULL, "ignored"));
}

static void test_async_save_snapshot(void)
{
    TEST_ASSERT(SDL_Init(SDL_INIT_TIMER) == 0);
    char original_root[MAX_PATH_CHARS], original_save[MAX_PATH_CHARS];
    snprintf(original_root, sizeof(original_root), "%s", runtime_root);
    snprintf(original_save, sizeof(original_save), "%s", runtime_save_path);
    for (int model = 0; model < 3; ++model) {
        char directory[] = "/tmp/vitacybiko-async-save-XXXXXX";
        TEST_ASSERT(mkdtemp(directory) != NULL);
        snprintf(runtime_root, sizeof(runtime_root), "%s", directory);
        snprintf(runtime_save_path, sizeof(runtime_save_path), "%s/save.bin", directory);
        cybiko_hal_t hal = {0};
        cybiko_emu_t *emu = cybiko_create_model(&hal, (cybiko_model_t)model);
        TEST_ASSERT(emu != NULL);
        save_snapshot_t *job = capture_save_snapshot(emu);
        TEST_ASSERT(job != NULL);
        char trace_path[MAX_PATH_CHARS];
        snprintf(trace_path, sizeof(trace_path), "%s/performance.csv", directory);
        frame_log_t *log = create_frame_log(trace_path);
        TEST_ASSERT(log != NULL);
        TEST_CHECK(append_frame_log(log, "version,model\n01.13,%d\n", model));
        TEST_CHECK(access(trace_path, F_OK) != 0); /* No I/O in the frame loop. */
        TEST_CHECK(capture_frame_log(job, log));
        size_t trace_size = log->used;
        uint32_t trace_crc = cybiko_crc32(log->data, log->used);
        TEST_CHECK(append_frame_log(log, "later row must not change snapshot\n"));
        free_frame_log(log);
        uint32_t storage_crc = cybiko_crc32(job->storage, job->storage_size);
        uint32_t ram_crc = job->ram ? cybiko_crc32(job->ram + 24, job->ram_size) : 0;
        /* Job must not reference live emulator memory or mutable path globals. */
        cybiko_destroy(emu);
        snprintf(runtime_root, sizeof(runtime_root), "/missing/changed-model");
        snprintf(runtime_save_path, sizeof(runtime_save_path), "/missing/changed-model/save.bin");
        job->thread = SDL_CreateThread(write_save_snapshot, "snapshot-test", job);
        TEST_ASSERT(job->thread != NULL);
        char storage_path[MAX_PATH_CHARS], ram_path[MAX_PATH_CHARS], rtc_path[MAX_PATH_CHARS];
        snprintf(storage_path, sizeof(storage_path), "%s", job->storage_path);
        snprintf(ram_path, sizeof(ram_path), "%s", job->ram_path);
        snprintf(rtc_path, sizeof(rtc_path), "%s", job->rtc_path);
        bool ok = false; double elapsed = -1;
        TEST_CHECK(finish_async_save(&job, true, &ok, &elapsed));
        TEST_CHECK(ok && job == NULL && elapsed >= 0);
        TEST_CHECK(!finish_async_save(&job, false, &ok, &elapsed));
        size_t size = 0; uint8_t *data = load_file(storage_path, &size, false);
        TEST_ASSERT(data != NULL);
        TEST_CHECK(cybiko_crc32(data, size) == storage_crc);
        free(data);
        if (model != CYBIKO_XTREME) {
            data = load_file(ram_path, &size, false);
            TEST_ASSERT(data && size >= 24);
            TEST_CHECK(!memcmp(data, "VCRM\1", 5));
            TEST_CHECK(data[5] == model);
            TEST_CHECK(get_u32le(data + 12) == storage_crc);
            TEST_CHECK(get_u32le(data + 16) == ram_crc);
            TEST_CHECK(get_u32le(data + 20) == cybiko_crc32(data, 20));
            TEST_CHECK(cybiko_crc32(data + 24, size - 24) == ram_crc);
            free(data);
            TEST_CHECK(remove(ram_path) == 0);
        }
        data = load_file(rtc_path, &size, false);
        TEST_ASSERT(data && size == 36);
        TEST_CHECK(!memcmp(data, "VRTC\1\0\0\0", 8));
        TEST_CHECK(get_u32le(data + 32) == cybiko_crc32(data, 32));
        free(data);
        TEST_CHECK(remove(storage_path) == 0);
        TEST_CHECK(remove(rtc_path) == 0);
        data = load_file(trace_path, &size, false);
        TEST_ASSERT(data != NULL);
        TEST_CHECK(size == trace_size && cybiko_crc32(data, size) == trace_crc);
        free(data);
        TEST_CHECK(remove(trace_path) == 0);
        TEST_CHECK(rmdir(directory) == 0);
    }
    /* A failed worker must report failure and still be joinable/freeable. */
    cybiko_hal_t hal = {0};
    cybiko_emu_t *emu = cybiko_create_model(&hal, CYBIKO_CLASSIC_V1);
    save_snapshot_t *job = start_async_save(emu, NULL);
    TEST_ASSERT(job != NULL);
    bool ok = true; double elapsed;
    TEST_CHECK(finish_async_save(&job, true, &ok, &elapsed));
    TEST_CHECK(!ok && !job);
    cybiko_destroy(emu);
    snprintf(runtime_root, sizeof(runtime_root), "%s", original_root);
    snprintf(runtime_save_path, sizeof(runtime_save_path), "%s", original_save);
    SDL_Quit();
}

TEST_LIST = {
    {"frame_log_bounds", test_frame_log_bounds},
    {"presentation_gaps", test_presentation_gaps},
    {"async_save_snapshot", test_async_save_snapshot},
    {"lcd_colors_and_duplicate_uploads", test_lcd_upload_preserves_colors_and_skips_duplicates},
    {"catchup_presentation_budget", test_catchup_respects_presentation_budget},
    {"classic_ram_storage", test_classic_ram_storage},
    {"classic_input_timing_after_reset", test_classic_input_timing_after_reset},
    {"runtime_dir_creation_on_prepared_storage", test_runtime_dir_creation_on_prepared_storage},
    {"preferences_storage", test_preferences_storage},
    {"clock_storage", test_clock_storage},
    {"landscape_touch_and_rotation", test_landscape_touch_and_rotation},
    {"model_menu_selection", test_model_menu_selection},
    {"model_storage_isolation", test_model_storage_isolation},
    {"focus_loss_and_mouse_buttons", test_focus_loss_and_mouse_buttons},
    {"audio_queue_bounds_latency", test_audio_queue_bounds_latency},
    {"import_pack_and_library_paths", test_import_pack_and_library_paths},
    {"render_and_background_events", test_render_and_background_events},
    {"touch_hold_survives_controller_poll", test_touch_hold_survives_controller_poll},
    {"physical_alias_and_controller_release", test_physical_alias_and_controller_release},
    {"touch_modifiers_and_skin", test_touch_modifiers_and_skin},
    {"storage_preserves_existing_data", test_storage_preserves_existing_data},
    {NULL, NULL}
};
