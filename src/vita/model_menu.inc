/* Included after SDL/event helpers. The boot selector stays in landscape. */
static const cybiko_model_t menu_models[] = {CYBIKO_CLASSIC_V1, CYBIKO_CLASSIC_V2, CYBIKO_XTREME};
typedef struct { int selected, pressed; bool finger_down; SDL_FingerID finger; } model_menu_state_t;

static int model_menu_row(int x, int y)
{
    if (x < 120 || x >= 840 || y < 120) return -1;
    int row = (y - 120) / 100;
    return row < 3 && (y - 120) % 100 < 84 ? row : -1;
}

/* Return 1 to boot, -1 to quit, 0 to keep selecting. */
static int model_menu_event(app_ctx_t *ctx, model_menu_state_t *menu, const SDL_Event *event)
{
    if (event->type == SDL_QUIT || event->type == SDL_APP_TERMINATING) return -1;
    if (event->type == SDL_APP_WILLENTERBACKGROUND || event->type == SDL_APP_DIDENTERFOREGROUND) {
        ctx->backgrounded = event->type == SDL_APP_WILLENTERBACKGROUND;
        menu->pressed = -1; menu->finger_down = false;
        update_suspend_state(ctx);
    }
    if (event->type == SDL_WINDOWEVENT) {
        if (event->window.event == SDL_WINDOWEVENT_FOCUS_LOST || event->window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
            ctx->focus_lost = event->window.event == SDL_WINDOWEVENT_FOCUS_LOST;
            menu->pressed = -1; menu->finger_down = false;
            update_suspend_state(ctx);
        }
    }
    if (ctx->backgrounded || ctx->focus_lost) return 0;
    if (event->type == SDL_KEYDOWN && !event->key.repeat) {
        switch (event->key.keysym.scancode) {
        case SDL_SCANCODE_UP: menu->selected = (menu->selected + 2) % 3; break;
        case SDL_SCANCODE_DOWN: menu->selected = (menu->selected + 1) % 3; break;
        case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return 1;
        case SDL_SCANCODE_ESCAPE: return -1;
        default: break;
        }
    }
    int row = -1;
    bool down = false, up = false;
    if ((event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEBUTTONUP) &&
        event->button.button == SDL_BUTTON_LEFT && !menu->finger_down) {
        row = model_menu_row(event->button.x, event->button.y);
        down = event->type == SDL_MOUSEBUTTONDOWN; up = !down;
    } else if (event->type == SDL_FINGERDOWN && !menu->finger_down) {
        menu->finger_down = true; menu->finger = event->tfinger.fingerId;
        row = model_menu_row((int)(event->tfinger.x * SCREEN_WIDTH), (int)(event->tfinger.y * SCREEN_HEIGHT));
        down = true;
    } else if (event->type == SDL_FINGERUP && menu->finger_down && menu->finger == event->tfinger.fingerId) {
        menu->finger_down = false;
        row = model_menu_row((int)(event->tfinger.x * SCREEN_WIDTH), (int)(event->tfinger.y * SCREEN_HEIGHT));
        up = true;
    }
    if (down) { menu->pressed = row; if (row >= 0) menu->selected = row; }
    if (up) { bool boot = row >= 0 && menu->pressed == row; menu->pressed = -1; if (boot) return 1; }
    if (event->type == SDL_CONTROLLERDEVICEADDED) open_first_controller(ctx);
    if (event->type == SDL_CONTROLLERDEVICEREMOVED) close_controller_if_removed(ctx, event->cdevice.which);
    return 0;
}

static void render_model_menu(app_ctx_t *ctx, int selected)
{
    SDL_SetRenderTarget(ctx->renderer, NULL);
    SDL_SetRenderDrawColor(ctx->renderer, 12, 17, 25, 255);
    SDL_RenderClear(ctx->renderer);
    stringRGBA(ctx->renderer, 120, 45, "VitaCybiko v" VITACYBIKO_VERSION " / CHOOSE YOUR DEVICE", 230, 238, 245, 255);
    stringRGBA(ctx->renderer, 120, 74, "Separate firmware and saves for each model. No firmware is bundled.", 150, 173, 190, 255);
    stringRGBA(ctx->renderer, 760, 74, "VCYB00001", 95, 222, 245, 255);
    for (int row = 0; row < 3; ++row) {
        const cybiko_machine_t *m = cybiko_machine(menu_models[row]);
        int y = 120 + row * 100;
        roundedBoxRGBA(ctx->renderer, 120, y, 840, y + 84, 12, row == selected ? 35 : 23, row == selected ? 77 : 31, row == selected ? 101 : 43, 255);
        roundedRectangleRGBA(ctx->renderer, 120, y, 840, y + 84, 12, row == selected ? 105 : 53, row == selected ? 221 : 68, row == selected ? 245 : 84, 255);
        stringRGBA(ctx->renderer, 145, y + 19, m->name, 240, 245, 250, 255);
        stringRGBA(ctx->renderer, 145, y + 49,
                   row == 0 ? "Original / stock hardware - experimental" :
                   row == 1 ? "Revised Classic / CyOS v1.3.58 - desktop verified" :
                              "Xtreme / CyOS v1.5.08 - awaiting runtime validation", 175, 200, 215, 255);
    }
    stringRGBA(ctx->renderer, 120, 440, "D-pad / arrows: choose    Cross / Enter / tap: boot    Circle / Esc: exit", 190, 210, 220, 255);
    stringRGBA(ctx->renderer, 120, 473, "In device: Start+Select saves and returns here.", 155, 178, 195, 255);
    stringRGBA(ctx->renderer, 120, 501, "Local device emulation. Wireless links and USB accessories unavailable.", 235, 185, 108, 255);
    SDL_RenderPresent(ctx->renderer);
}

static int choose_model(app_ctx_t *ctx)
{
    if (ctx->quit_requested) return -1;
    release_all_inputs(ctx);
    ctx->keyboard_mode = false;
    ctx->save_requested = false;
    model_menu_state_t menu = { .pressed = -1 };
    for (int i = 0; i < 3; ++i) if (menu_models[i] == ctx->model) menu.selected = i;
    open_first_controller(ctx);
    uint32_t previous = read_controller_buttons(ctx);
    bool redraw = true;
    for (;;) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            int before = menu.selected;
            bool was_suspended = ctx->backgrounded || ctx->focus_lost;
            int result = model_menu_event(ctx, &menu, &event);
            if (result) return result < 0 ? -1 : (int)menu_models[menu.selected];
            if (menu.selected != before ||
                was_suspended != (ctx->backgrounded || ctx->focus_lost)) {
                redraw = true;
            }
        }
        uint32_t buttons = read_controller_buttons(ctx);
        uint32_t pressed = buttons & ~previous;
        previous = buttons;
        if (ctx->backgrounded || ctx->focus_lost) { SDL_Delay(20); continue; }
        if (button_is_down(pressed, SDL_CONTROLLER_BUTTON_B)) return -1;
        if (button_is_down(pressed, SDL_CONTROLLER_BUTTON_DPAD_UP)) {
            menu.selected = (menu.selected + 2) % 3;
            redraw = true;
        }
        if (button_is_down(pressed, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) {
            menu.selected = (menu.selected + 1) % 3;
            redraw = true;
        }
        if (button_is_down(pressed, SDL_CONTROLLER_BUTTON_A)) return (int)menu_models[menu.selected];
        if (redraw) {
            render_model_menu(ctx, menu.selected);
            redraw = false;
        }
        SDL_Delay(8);
    }
}
