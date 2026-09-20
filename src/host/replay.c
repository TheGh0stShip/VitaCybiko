/* Deterministic, headless core benchmark. This intentionally includes internal
 * state, like test_scheduler, and is never linked into the Vita application.
 * Use the cybiko-smoke arguments/checkpoint environment variables. Optional:
 * CYBIKO_REPLAY_LOG: per-frame timing and state fingerprints (CSV).
 * CYBIKO_REPLAY_KEYS: text file containing frame,column,hex_mask records,
 * ordered by frame (zero-based). A mask remains held until another record.
 * These host timings are NOT a physical-Vita FPS measurement. */
#include "../../third_party/cybiko-c-emulator/src/core/emulator.c"
#include <time.h>
#include <errno.h>

static FILE *replay_log, *replay_keys, *replay_video;
static unsigned replay_frame_index;
static unsigned next_key_frame, next_key_column, next_key_mask;
static bool have_key;
static uint16_t replay_matrix[CYBIKO_KEYBOARD_COLUMNS];
static uint32_t replay_audio_hash;
static void (*original_audio)(void *, const uint8_t *, int);

static uint32_t fingerprint(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint32_t hash = 2166136261u;
    while (size--) hash = (hash ^ *bytes++) * 16777619u;
    return hash;
}

static void read_next_key(void)
{
    int count = fscanf(replay_keys, "%u,%u,%x", &next_key_frame,
                       &next_key_column, &next_key_mask);
    have_key = count == 3;
    if ((!have_key && count != EOF) ||
        (have_key && (next_key_frame < replay_frame_index ||
                      next_key_column >= CYBIKO_KEYBOARD_COLUMNS ||
                      next_key_mask > 0xffff))) {
        fprintf(stderr, "invalid or unordered replay input record\n");
        exit(2);
    }
}

static void replay_keyboard(void *opaque, uint16_t *matrix, int columns)
{
    (void)opaque;
    while (have_key && next_key_frame == replay_frame_index) {
        replay_matrix[next_key_column] = next_key_mask;
        read_next_key();
    }
    memcpy(matrix, replay_matrix, (size_t)columns * sizeof(*matrix));
}

static void replay_audio(void *opaque, const uint8_t *samples, int count)
{
    replay_audio_hash = fingerprint(samples, (size_t)count);
    if (original_audio) original_audio(opaque, samples, count);
}

static void replay_frame(cybiko_emu_t *emu)
{
    if (!replay_frame_index) {
        const char *path = getenv("CYBIKO_REPLAY_LOG");
        if (path) {
            replay_log = fopen(path, "w");
            if (!replay_log) { perror("replay log"); exit(2); }
            fprintf(replay_log, "frame,core_ms,pc,ccr,registers,ram,lcd,audio,cycles\n");
        }
        path = getenv("CYBIKO_REPLAY_KEYS");
        if (path) {
            replay_keys = fopen(path, "r");
            if (!replay_keys) { perror("replay keys"); exit(2); }
            read_next_key();
            emu->hal.keyboard_poll = replay_keyboard;
        }
        path = getenv("CYBIKO_REPLAY_VIDEO");
        if (path) {
            /* Raw 160x100 gray8 frames at the guest's 60 Hz frame cadence.
             * Includes duplicates, so animation timing is preserved. */
            replay_video = fopen(path, "wb");
            if (!replay_video) { perror("replay video"); exit(2); }
        }
        original_audio = emu->hal.audio_output;
        emu->hal.audio_output = replay_audio;
        /* Stable date when no user-provided raw clock was requested. */
        if (!getenv("CYBIKO_SMOKE_CLOCK")) {
            memset(emu->rtc.data, 0, sizeof(emu->rtc.data));
            emu->rtc.data[5] = emu->rtc.data[6] = 1;
        }
    }
    /* Advance guest RTC deterministically, not according to benchmark speed.
     * Alternating integer deltas avoid losing 40 ns each emulated second. */
    rtc_advance(&emu->rtc,
        ((uint64_t)(replay_frame_index + 1) * 1000000000u / CYBIKO_FPS) -
        ((uint64_t)replay_frame_index * 1000000000u / CYBIKO_FPS));
    emu->rtc.last_tick_ns = UINT64_MAX;
    clock_t start = clock();
    cybiko_run_frame(emu);
    double ms = (double)(clock() - start) * 1000.0 / CLOCKS_PER_SEC;
    if (replay_video && fwrite(emu->lcd.frame_buffer, 1,
        CYBIKO_LCD_WIDTH * CYBIKO_LCD_HEIGHT, replay_video) !=
        CYBIKO_LCD_WIDTH * CYBIKO_LCD_HEIGHT) {
        perror("replay video write"); exit(2);
    }
    if (replay_log) {
        fprintf(replay_log, "%u,%.6f,%06x,%02x,%08x,%08x,%08x,%08x,%llu\n",
            replay_frame_index, ms, emu->cpu.pc, emu->cpu.ccr,
            fingerprint(emu->cpu.er, sizeof(emu->cpu.er)),
            fingerprint(emu->bus.external_ram.data, emu->bus.external_ram.size),
            fingerprint(hd66421_render(&emu->lcd), CYBIKO_LCD_WIDTH * CYBIKO_LCD_HEIGHT),
            replay_audio_hash, (unsigned long long)emu->cpu.cycle_count);
    }
    ++replay_frame_index;
}

#define cybiko_run_frame replay_frame
#define main smoke_main
#include "smoke.c"
#undef main

int main(int argc, char **argv)
{
    int result = smoke_main(argc, argv);
    if (replay_log && (ferror(replay_log) || fflush(replay_log))) result = 6;
    if (replay_log && fclose(replay_log)) result = 6;
    if (replay_keys) fclose(replay_keys);
    if (replay_video && fclose(replay_video)) result = 6;
    return result;
}
