/* Recompute opt-in ARM presentation records using the host implementation.
 * This checks CPU synthesis, not Vita3K GPU output or physical panel scanout. */
#include "frontend/motion.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned read32(const uint8_t *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8 |
           (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: %s motion-trace-v1.bin\n", argv[0]); return 2; }
    FILE *file = fopen(argv[1], "rb");
    if (!file) { perror("motion trace"); return 2; }
    uint8_t header[12];
    if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
        memcmp(header, "VCMT0001", 8) || !read32(header + 8) || read32(header + 8) > 64) {
        fclose(file); return 2;
    }
    unsigned count = read32(header + 8), mismatches = 0, metadata_mismatches = 0;
    enum { RECORD_SIZE = 32 + MOTION_PIXELS * 2 + MOTION_PIXELS * 9 };
    uint8_t *record = malloc(RECORD_SIZE), *output = malloc(MOTION_PIXELS * 9);
    motion_pair_t *pair = malloc(sizeof(*pair));
    int result = 0;
    if (!record || !output || !pair) { result = 2; goto done; }
    for (unsigned i = 0; i < count; ++i) {
        if (fread(record, 1, RECORD_SIZE, file) != RECORD_SIZE ||
            read32(record + 8) > 256 || (read32(record + 20) & ~7u) ||
            read32(record + 24) != 480 || read32(record + 28) != 300) {
            result = 2; goto done;
        }
        motion_estimate(pair, record + 32, record + 32 + MOTION_PIXELS);
        unsigned flags = pair->translated | (pair->cut << 1) | (pair->local_rejected << 2);
        if ((uint32_t)pair->dx != read32(record + 12) ||
            (uint32_t)pair->dy != read32(record + 16) || flags != read32(record + 20)) {
            fprintf(stderr, "record %u motion metadata mismatch\n", i);
            ++metadata_mismatches;
        }
        motion_synthesize_scaled(pair, read32(record + 8), 3, output);
        unsigned errors = 0;
        for (unsigned k = 0; k < MOTION_PIXELS * 9; ++k)
            errors += output[k] != record[32 + MOTION_PIXELS * 2 + k];
        if (errors) {
            fprintf(stderr, "record %u phase=%u mismatched_pixels=%u\n", i, read32(record + 8), errors);
            ++mismatches;
        }
    }
    if (fgetc(file) != EOF || ferror(file)) { result = 2; goto done; }
    printf("records=%u host_arm_output_mismatches=%u motion_metadata_mismatches=%u\n",
           count, mismatches, metadata_mismatches);
    result = mismatches || metadata_mismatches ? 1 : 0;
done:
    free(record); free(output); free(pair);
    fclose(file);
    return result;
}
