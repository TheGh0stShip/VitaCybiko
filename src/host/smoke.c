/* Headless firmware smoke test. LCD activity is not proof of a complete boot. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/cfs.h"
#include "core/emulator.h"

typedef struct {
    unsigned frames;
    unsigned active_frames;
    unsigned audio_samples;
    unsigned changed_frames;
    unsigned enter_frame;
    bool classic;
    uint8_t pixels[CYBIKO_LCD_WIDTH * CYBIKO_LCD_HEIGHT];
} smoke_ctx_t;

static uint8_t *load_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "cannot open '%s': %s\n", path, strerror(errno));
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long length = ftell(file);
    /* Xtreme raw RAM checkpoints are 2 MiB; Classic dataflash is 528 KiB. */
    if (length < 0 || length > 0x200000 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    uint8_t *data = malloc(length > 0 ? (size_t)length : 1);
    if (!data || fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return data;
}

static void render_frame(void *opaque, const uint8_t *pixels, int width, int height)
{
    smoke_ctx_t *ctx = opaque;
    if (width != CYBIKO_LCD_WIDTH || height != CYBIKO_LCD_HEIGHT) {
        return;
    }
    ctx->frames++;
    if (memcmp(ctx->pixels, pixels, sizeof(ctx->pixels))) ctx->changed_frames++;
    bool active = false;
    for (int i = 0; i < width * height; i++) {
        ctx->pixels[i] = pixels[i];
        if (pixels[i] != 0xFF) {
            active = true;
        }
    }
    if (active) {
        ctx->active_frames++;
    }
}

static void keyboard_poll(void *opaque, uint16_t *matrix, int columns)
{
    smoke_ctx_t *ctx = opaque;
    memset(matrix, 0, (size_t)columns * sizeof(*matrix));
    if (ctx->enter_frame && ctx->frames >= ctx->enter_frame &&
        ctx->frames < ctx->enter_frame + 12) {
        int column = ctx->classic ? 5 : 4;
        if (columns > column) matrix[column] = ctx->classic ? 0x0004 : 0x0008;
    }
}

static void audio_output(void *opaque, const uint8_t *samples, int count)
{
    smoke_ctx_t *ctx = opaque;
    if (samples && count > 0) {
        ctx->audio_samples += (unsigned)count;
    }
}

static bool write_screenshot(const char *path, const smoke_ctx_t *ctx)
{
    FILE *file = fopen(path, "wb");
    if (!file) {
        return false;
    }
    int header = fprintf(file, "P5\n%d %d\n255\n",
                         CYBIKO_LCD_WIDTH, CYBIKO_LCD_HEIGHT);
    size_t written = fwrite(ctx->pixels, 1, sizeof(ctx->pixels), file);
    int close_result = fclose(file);
    return header > 0 && written == sizeof(ctx->pixels) && close_result == 0;
}

static bool write_raw_file(const char *path, const uint8_t *data, size_t size)
{
    FILE *file = fopen(path, "wb");
    if (!file) return false;
    size_t written = fwrite(data, 1, size, file);
    int close_result = fclose(file);
    return written == size && close_result == 0;
}

int main(int argc, char **argv)
{
    cybiko_model_t model = CYBIKO_XTREME;
    if (argc > 1 && (!strcmp(argv[1], "--classic-v1") || !strcmp(argv[1], "--classic-v2"))) {
        model = !strcmp(argv[1], "--classic-v1") ? CYBIKO_CLASSIC_V1 : CYBIKO_CLASSIC_V2;
        --argc; ++argv;
    }
    bool classic = model != CYBIKO_XTREME;
    int frames_arg = classic ? 4 : 3;
    if (argc < frames_arg || argc > frames_arg + 2) {
        fprintf(stderr,
                "usage: %s [--classic-v1|--classic-v2] boot.bin flash.bin [dataflash.bin for Classic] [frames] [screen.pgm]\n"
                "Classic V1 uses '-' for flash.bin (no parallel flash).\n",
                argv[0]);
        return 2;
    }

    char *end = NULL;
    errno = 0;
    long requested = argc > frames_arg ? strtol(argv[frames_arg], &end, 10) : 600;
    if (errno || (argc > frames_arg && (end == argv[frames_arg] || *end)) || requested <= 0 || requested > 36000) {
        fprintf(stderr, "frames must be between 1 and 36000\n");
        return 2;
    }
    int target_frames = (int)requested;

    size_t boot_size = 0;
    size_t flash_size = 0;
    uint8_t *boot = load_file(argv[1], &boot_size);
    uint8_t *flash = model == CYBIKO_CLASSIC_V1 ? NULL : load_file(argv[2], &flash_size);
    size_t serial_size = 0;
    uint8_t *serial = classic ? load_file(argv[3], &serial_size) : NULL;
    cfs_image_t *cfs = malloc(sizeof(*cfs));
    smoke_ctx_t ctx = {0};
    ctx.classic = classic;
    const char *enter_frame = getenv("CYBIKO_SMOKE_ENTER_FRAME");
    if (enter_frame) ctx.enter_frame = (unsigned)strtoul(enter_frame, NULL, 10);
    cybiko_hal_t hal = {
        .render_frame = render_frame,
        .audio_output = audio_output,
        .keyboard_poll = keyboard_poll,
        .ctx = &ctx,
    };
    cybiko_emu_t *emu = cybiko_create_model(&hal, model);

    if (!boot || (!flash && model != CYBIKO_CLASSIC_V1) || (classic && !serial) || !cfs || !emu) {
        fprintf(stderr, "failed to allocate smoke-test resources\n");
        free(cfs);
        free(serial);
        free(flash);
        free(boot);
        cybiko_destroy(emu);
        return 3;
    }

    if (!cybiko_check_firmware(model, boot, boot_size, flash, flash_size)) {
        fprintf(stderr,
                "unsupported firmware for selected model "
                "(boot CRC32 %08X, flash CRC32 %08X)\n",
                cybiko_machine(model)->boot_crc, cybiko_machine(model)->flash_crc);
        free(cfs);
        free(serial);
        free(flash);
        free(boot);
        cybiko_destroy(emu);
        return 4;
    }

    cfs_format(cfs);
    if (!cybiko_load_boot_rom(emu, boot, boot_size) ||
        (model != CYBIKO_CLASSIC_V1 && !cybiko_load_flash_rom(emu, flash, flash_size)) ||
        (classic ? !cybiko_load_dataflash(emu, serial, serial_size) :
                   !cybiko_load_nvram(emu, cfs->data, CFS_IMAGE_SIZE))) {
        fprintf(stderr,
                "firmware rejected (expected %u-byte boot and %u-byte flash)\n",
                (unsigned)CYBIKO_BOOT_ROM_SIZE,
                (unsigned)CYBIKO_FLASH_ROM_SIZE);
        free(cfs);
        free(serial);
        free(flash);
        free(boot);
        cybiko_destroy(emu);
        return 4;
    }

    /* Optional raw checkpoints let the benchmark reproduce a device boot. */
    const char *ram_path = getenv("CYBIKO_SMOKE_RAM");
    const char *clock_path = getenv("CYBIKO_SMOKE_CLOCK");
    if (ram_path) {
        size_t size = 0;
        uint8_t *data = load_file(ram_path, &size);
        bool ok = data && size == cybiko_machine(model)->ram_size &&
                  cybiko_load_nvram(emu, data, size);
        free(data);
        if (!ok) { fprintf(stderr, "invalid raw RAM checkpoint\n"); return 4; }
    }
    if (clock_path) {
        size_t size = 0;
        uint8_t *data = load_file(clock_path, &size);
        bool ok = data && cybiko_load_clock(emu, data, size, 0);
        free(data);
        if (!ok) { fprintf(stderr, "invalid raw clock checkpoint\n"); return 4; }
    }
    cybiko_reset(emu);
    clock_t started = clock();
    for (int i = 0; i < target_frames && cybiko_is_running(emu); i++) {
        cybiko_run_frame(emu);
    }

    const char *dump_ram_path = getenv("CYBIKO_SMOKE_DUMP_RAM");
    if (dump_ram_path) {
        size_t dump_size = 0;
        const uint8_t *dump = cybiko_get_nvram(emu, &dump_size);
        if (!dump || !write_raw_file(dump_ram_path, dump, dump_size)) {
            fprintf(stderr, "failed to write raw RAM dump '%s'\n", dump_ram_path);
            free(cfs);
            free(serial);
            free(flash);
            free(boot);
            cybiko_destroy(emu);
            return 6;
        }
    }

    printf("cpu_seconds=%.6f changed_frames=%u\n",
           (double)(clock() - started) / CLOCKS_PER_SEC, ctx.changed_frames);
    cybiko_cpu_stats_t stats = {0};
    if (cybiko_get_cpu_stats(emu, &stats)) {
        printf("semantic_fast_blocks=%llu semantic_fast_cycles=%llu semantic_fast_rejects=%llu semantic_fast_cached_rejects=%llu semantic_fast_backoff_skips=%llu\n",
               (unsigned long long)stats.semantic_fast_blocks,
               (unsigned long long)stats.semantic_fast_cycles,
               (unsigned long long)stats.semantic_fast_rejects,
               (unsigned long long)stats.semantic_fast_cached_rejects,
               (unsigned long long)stats.semantic_fast_backoff_skips);
        printf("semantic_mutable_fast_blocks=%llu semantic_mutable_fast_cycles=%llu\n",
               (unsigned long long)stats.semantic_mutable_fast_blocks,
               (unsigned long long)stats.semantic_mutable_fast_cycles);
        printf("semantic_fast_reject_guard=%llu semantic_fast_reject_irq=%llu semantic_fast_reject_window=%llu semantic_fast_reject_cached=%llu semantic_fast_reject_unsupported_block=%llu semantic_fast_reject_unsupported_exit=%llu semantic_fast_reject_cycle_budget=%llu semantic_fast_reject_branch_resolve=%llu semantic_fast_reject_target=%llu\n",
               (unsigned long long)stats.semantic_fast_reject_guard,
               (unsigned long long)stats.semantic_fast_reject_irq,
               (unsigned long long)stats.semantic_fast_reject_window,
               (unsigned long long)stats.semantic_fast_reject_cached,
               (unsigned long long)stats.semantic_fast_reject_unsupported_block,
               (unsigned long long)stats.semantic_fast_reject_unsupported_exit,
               (unsigned long long)stats.semantic_fast_reject_cycle_budget,
               (unsigned long long)stats.semantic_fast_reject_branch_resolve,
               (unsigned long long)stats.semantic_fast_reject_target);
        printf("semantic_mutable_reject_cached=%llu semantic_mutable_reject_unsupported_block=%llu semantic_mutable_reject_static_nonplain=%llu semantic_mutable_reject_unsupported_exit=%llu semantic_mutable_reject_cycle_budget=%llu semantic_mutable_reject_execute=%llu semantic_mutable_reject_target=%llu\n",
               (unsigned long long)stats.semantic_mutable_reject_cached,
               (unsigned long long)stats.semantic_mutable_reject_unsupported_block,
               (unsigned long long)stats.semantic_mutable_reject_static_nonplain,
               (unsigned long long)stats.semantic_mutable_reject_unsupported_exit,
               (unsigned long long)stats.semantic_mutable_reject_cycle_budget,
               (unsigned long long)stats.semantic_mutable_reject_execute,
               (unsigned long long)stats.semantic_mutable_reject_target);
        printf("semantic_mutable_execute_nonplain_read=%llu semantic_mutable_execute_nonplain_write=%llu semantic_mutable_execute_semantic_run=%llu semantic_mutable_execute_branch_resolve=%llu semantic_mutable_execute_return_read=%llu semantic_mutable_execute_call_write=%llu semantic_mutable_execute_other=%llu\n",
               (unsigned long long)stats.semantic_mutable_execute_nonplain_read,
               (unsigned long long)stats.semantic_mutable_execute_nonplain_write,
               (unsigned long long)stats.semantic_mutable_execute_semantic_run,
               (unsigned long long)stats.semantic_mutable_execute_branch_resolve,
               (unsigned long long)stats.semantic_mutable_execute_return_read,
               (unsigned long long)stats.semantic_mutable_execute_call_write,
               (unsigned long long)stats.semantic_mutable_execute_other);
        printf("semantic_mutable_prefix_blocks=%llu semantic_mutable_prefix_cycles=%llu\n",
               (unsigned long long)stats.semantic_mutable_prefix_blocks,
               (unsigned long long)stats.semantic_mutable_prefix_cycles);
        printf("hot_plain_memory_2b_instructions=%llu hot_plain_memory_4b_instructions=%llu\n",
               (unsigned long long)stats.hot_plain_memory_2b_instructions,
               (unsigned long long)stats.hot_plain_memory_4b_instructions);
    }

    bool screenshot_ok = argc <= frames_arg + 1 || write_screenshot(argv[frames_arg + 1], &ctx);
    bool passed = ctx.frames == (unsigned)target_frames &&
                  ctx.active_frames > 0 && screenshot_ok;
    printf("firmware-smoke: %s frames=%u active=%u audio_samples=%u running=%s\n",
           passed ? "PASS" : "FAIL", ctx.frames, ctx.active_frames,
           ctx.audio_samples, cybiko_is_running(emu) ? "yes" : "no");
    printf("model=%s pc=%06X (LCD activity only; inspect screenshot for boot progress)\n",
           cybiko_machine(model)->name, cybiko_get_program_counter(emu));

    free(cfs);
    free(serial);
    free(flash);
    free(boot);
    cybiko_destroy(emu);
    return passed ? 0 : 5;
}
