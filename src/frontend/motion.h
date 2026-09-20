#ifndef VITACYBIKO_MOTION_H
#define VITACYBIKO_MOTION_H

#include <stdbool.h>
#include <stdint.h>

enum { MOTION_W = 160, MOTION_H = 100, MOTION_PIXELS = 16000,
       MOTION_BLOCK = 8, MOTION_COLS = 20, MOTION_ROWS = 13,
       MOTION_BLOCKS = MOTION_COLS * MOTION_ROWS };

typedef struct { int8_t x, y; uint8_t valid; } motion_vector_t;

/* Immutable after estimation. Can be copied across a frame-boundary mailbox.
 * No allocations, floating point, SDL calls, or shared mutable globals. */
typedef struct {
    uint8_t before[MOTION_PIXELS], after[MOTION_PIXELS];
    motion_vector_t forward[MOTION_BLOCKS], backward[MOTION_BLOCKS];
    uint8_t stationary[MOTION_BLOCKS];
    uint8_t stationary_rows[MOTION_H];
    int8_t row_dx[MOTION_H];
    int dx, dy;
    int x0, y0, x1, y1;
    unsigned moving_blocks;
    bool translated, cut, local_rejected;
} motion_pair_t;

void motion_estimate(motion_pair_t *pair, const uint8_t *before,
                     const uint8_t *after);
/* phase is a fixed-point fraction: 0 = exact before, 256 = exact after.
 * In-between images are spatially warped, not an unaligned frame crossfade. */
void motion_synthesize(const motion_pair_t *pair, unsigned phase, uint8_t *out);
/* Scale 3 evaluates motion at Vita LCD pixel resolution, preserving sharp
 * source pixels instead of magnifying a one-guest-pixel blur threefold. */
void motion_synthesize_scaled(const motion_pair_t *pair, unsigned phase,
                              unsigned scale, uint8_t *out);
/* Vita hot path: for optimized scale-3 translated scrolls, synthesize directly
 * into ARGB texture pixels and skip the intermediate indexed frame. Returns
 * false when the generic indexed synthesizer should be used instead. */
bool motion_synthesize_scaled_argb_fast(const motion_pair_t *pair, unsigned phase,
                                        const uint32_t palette[256],
                                        uint32_t *out);
bool motion_synthesize_scaled_argb_fast_pitch(const motion_pair_t *pair, unsigned phase,
                                              const uint32_t palette[256],
                                              uint32_t *out, unsigned pitch_pixels);

typedef struct {
    motion_pair_t pair;
    uint64_t start_us, end_us;
} motion_slot_t;

enum { MOTION_HISTORY = 16, MOTION_DELAY_US = 200000 };

typedef struct {
    motion_pair_t pair;
    motion_slot_t history[MOTION_HISTORY];
    uint64_t received_us, duration_us;
    uint64_t last_source_us, selected_end_us;
    unsigned head, count;
    bool valid;
} motion_presenter_t;

void motion_presenter_reset(motion_presenter_t *presenter);
void motion_presenter_accept(motion_presenter_t *presenter,
                             const motion_pair_t *pair, uint64_t now_us);
unsigned motion_presenter_phase(motion_presenter_t *presenter,
                                uint64_t now_us);

#endif
