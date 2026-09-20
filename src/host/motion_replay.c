/* Offline validation of the exact LCD motion code used by the Vita frontend.
 * Input/output: raw 160x100 gray8 at 60 Hz. Does not execute guest instructions.
 * Firmware-derived recordings remain local; do not bundle them with releases. */
#include "frontend/motion.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned consistent_vectors(const motion_vector_t *forward, const motion_vector_t *backward)
{
    unsigned count = 0;
    for (int i = 0; i < MOTION_BLOCKS; ++i) {
        motion_vector_t v = forward[i];
        if (!v.valid || (!v.x && !v.y)) continue;
        int x = (i % MOTION_COLS) * 8 + 4 + v.x;
        int y = (i / MOTION_COLS) * 8 + 4 + v.y;
        if (x < 0 || x >= MOTION_W || y < 0 || y >= MOTION_H) continue;
        motion_vector_t b = backward[(y / 8) * MOTION_COLS + x / 8];
        if (b.valid && abs(v.x + b.x) <= 1 && abs(v.y + b.y) <= 1) ++count;
    }
    return count;
}

static int write_pgm(const char *prefix, const char *name,
                     const uint8_t *pixels, unsigned scale)
{
    char path[1024];
    int n = snprintf(path, sizeof(path), "%s-%s.pgm", prefix, name);
    if (n < 0 || (size_t)n >= sizeof(path)) return 0;
    FILE *file = fopen(path, "wb");
    if (!file) return 0;
    size_t size = MOTION_PIXELS * scale * scale;
    int ok = fprintf(file, "P5\n%u %u\n255\n", MOTION_W * scale, MOTION_H * scale) > 0 &&
             fwrite(pixels, 1, size, file) == size;
    if (fclose(file)) ok = 0;
    return ok;
}

int main(int argc, char **argv)
{
    if (argc != 4 && argc != 5) {
        fprintf(stderr, "usage: %s input.gray output.gray metrics.csv [1|3 output scale]\n", argv[0]);
        return 2;
    }
    unsigned scale = argc == 5 ? (unsigned)atoi(argv[4]) : 1;
    if (scale != 1 && scale != 3) return 2;
    const char *trace_frame = getenv("CYBIKO_MOTION_TRACE_FRAME");
    const char *trace_prefix = getenv("CYBIKO_MOTION_TRACE_PREFIX");
    unsigned long trace_index = 0;
    if ((trace_frame != NULL) != (trace_prefix != NULL)) return 2;
    if (trace_frame) {
        char *end;
        trace_index = strtoul(trace_frame, &end, 10);
        if (end == trace_frame || *end || *trace_frame == '-') return 2;
    }
    FILE *in = fopen(argv[1], "rb"), *out = fopen(argv[2], "wb"), *log = fopen(argv[3], "w");
    if (!in || !out || !log) { perror("motion replay file"); return 2; }
    motion_pair_t *pair = malloc(sizeof(*pair));
    motion_presenter_t *presenter = calloc(1, sizeof(*presenter));
    if (!pair || !presenter) return 2;
    uint8_t previous[MOTION_PIXELS], current[MOTION_PIXELS];
    size_t output_size = MOTION_PIXELS * scale * scale;
    uint8_t *result = malloc(output_size);
    if (!result) return 2;
    unsigned frame = 0, changes = 0, generated = 0;
    double estimation = 0, synthesis = 0;
    fprintf(log, "frame,changed,translated,dx,dy,moving_blocks,cut,phase,estimate_ms,synthesize_ms,consistent_forward,consistent_backward,pair_start_us,pair_end_us\n");
    size_t bytes;
    while ((bytes = fread(current, 1, sizeof(current), in)) == sizeof(current)) {
        uint64_t us = (uint64_t)frame * 1000000 / 60;
        bool changed = frame && memcmp(previous, current, sizeof(current));
        double estimate_ms = 0;
        if (changed) {
            clock_t start = clock();
            motion_estimate(pair, previous, current);
            estimate_ms = (clock() - start) * 1000.0 / CLOCKS_PER_SEC;
            estimation += estimate_ms;
            motion_presenter_accept(presenter, pair, us);
            ++changes;
        }
        unsigned phase = motion_presenter_phase(presenter, us);
        clock_t start = clock();
        if (presenter->valid) motion_synthesize_scaled(&presenter->pair, phase, scale, result);
        else for (unsigned y = 0; y < MOTION_H * scale; ++y)
            for (unsigned x = 0; x < MOTION_W * scale; ++x)
                result[y * MOTION_W * scale + x] = current[(y / scale) * MOTION_W + x / scale];
        double synth_ms = (clock() - start) * 1000.0 / CLOCKS_PER_SEC;
        synthesis += synth_ms;
        if (presenter->valid && phase > 0 && phase < 256) {
            bool differs_before = false, differs_after = false;
            for (unsigned y = 0; y < MOTION_H * scale; ++y)
                for (unsigned x = 0; x < MOTION_W * scale; ++x) {
                    int k = (y / scale) * MOTION_W + x / scale;
                    uint8_t v = result[y * MOTION_W * scale + x];
                    differs_before |= v != presenter->pair.before[k];
                    differs_after |= v != presenter->pair.after[k];
                }
            if (differs_before && differs_after) ++generated;
        }
        fprintf(log, "%u,%d,%d,%d,%d,%u,%d,%u,%.6f,%.6f,%u,%u,%llu,%llu\n", frame, changed,
            presenter->pair.translated, presenter->pair.dx, presenter->pair.dy,
            presenter->pair.moving_blocks, presenter->pair.cut, phase, estimate_ms, synth_ms,
            consistent_vectors(presenter->pair.forward, presenter->pair.backward),
            consistent_vectors(presenter->pair.backward, presenter->pair.forward),
            (unsigned long long)presenter->received_us,
            (unsigned long long)presenter->selected_end_us);
        if (trace_prefix && frame == trace_index && presenter->valid) {
            if (!write_pgm(trace_prefix, "before", presenter->pair.before, 1) ||
                !write_pgm(trace_prefix, "after", presenter->pair.after, 1) ||
                !write_pgm(trace_prefix, "output", result, scale)) return 3;
        }
        if (fwrite(result, 1, output_size, out) != output_size) return 3;
        memcpy(previous, current, sizeof(previous));
        ++frame;
    }
    int failed = bytes != 0 || ferror(in) || ferror(log) || ferror(out);
    failed |= fclose(in) != 0;
    failed |= fclose(out) != 0;
    failed |= fclose(log) != 0;
    fprintf(stderr, "frames=%u source_changes=%u intermediate_frames=%u estimate_ms=%.3f synthesis_ms=%.3f (host CPU, not Vita timings)\n",
            frame, changes, generated, estimation, synthesis);
    free(pair); free(presenter); free(result);
    return failed ? 3 : 0;
}
