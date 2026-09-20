#include "core/h8s_block.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *stop_name(h8s_block_stop_t stop)
{
    switch (stop) {
    case H8S_BLOCK_STOP_LIMIT: return "limit";
    case H8S_BLOCK_STOP_BRANCH: return "branch";
    case H8S_BLOCK_STOP_PREFIX: return "prefix";
    case H8S_BLOCK_STOP_TRUNCATED: return "truncated";
    case H8S_BLOCK_STOP_UNSUPPORTED: return "unsupported";
    }
    return "unknown";
}

static void print_top_semantic_gaps(const unsigned long long counts[256],
                                    unsigned long long total)
{
    bool printed[256] = {0};
    printf("semantic_gap_unsupported_instructions=%llu\n", total);
    for (unsigned rank = 0; rank < 12; ++rank) {
        unsigned best = 0;
        unsigned long long best_count = 0;
        for (unsigned hi = 0; hi < 256; ++hi) {
            if (!printed[hi] && counts[hi] > best_count) {
                best_count = counts[hi];
                best = hi;
            }
        }
        if (!best_count) break;
        printed[best] = true;
        printf("semantic_gap_top%02u_hi=0x%02x count=%llu share=%.2f%%\n",
               rank + 1, best, best_count,
               total ? 100.0 * (double)best_count / (double)total : 0.0);
    }
}

static unsigned parse_uint(const char *text, unsigned fallback)
{
    if (!text) return fallback;
    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 0);
    if (errno || !end || *end || value == 0 || value > 1000000ul) return fallback;
    return (unsigned)value;
}

static uint8_t *load_file(const char *path, size_t *size_out)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return NULL; }
    long size = ftell(file);
    if (size <= 0) { fclose(file); return NULL; }
    rewind(file);
    uint8_t *data = malloc((size_t)size);
    if (!data) { fclose(file); return NULL; }
    if (fread(data, 1, (size_t)size, file) != (size_t)size) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size_out = (size_t)size;
    return data;
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: %s rom.bin [max_instructions]\n", argv[0]);
        return 2;
    }

    size_t rom_size = 0;
    uint8_t *rom = load_file(argv[1], &rom_size);
    if (!rom) {
        fprintf(stderr, "failed to read %s\n", argv[1]);
        return 1;
    }

    unsigned max_instructions = parse_uint(argc == 3 ? argv[2] : NULL, 32);
    unsigned long long blocks = 0;
    unsigned long long instructions = 0;
    unsigned long long executable_blocks = 0;
    unsigned long long executable_prefix_instructions = 0;
    unsigned long long semantic_blocks = 0;
    unsigned long long semantic_instructions = 0;
    unsigned long long semantic_gap_blocks = 0;
    unsigned long long semantic_gap_instructions = 0;
    unsigned long long semantic_gap_hi[256] = {0};
    unsigned long long bytes = 0;
    unsigned long long stops[H8S_BLOCK_STOP_UNSUPPORTED + 1] = {0};
    unsigned longest = 0;
    uint32_t longest_pc = 0;

    for (uint32_t pc = 0; (size_t)pc + 1 < rom_size; pc += 2) {
        h8s_block_t block;
        if (!h8s_analyze_rom_block(rom, rom_size, pc, max_instructions, &block))
            continue;
        blocks++;
        instructions += block.instructions;
        executable_prefix_instructions += block.executable_prefix_instructions;
        if (block.executable && block.instructions > 0) executable_blocks++;
        if (h8s_semantic_block_supported(&block)) {
            semantic_blocks++;
            semantic_instructions += block.instructions;
        } else if (block.executable && block.instructions > 0) {
            bool counted_block = false;
            for (unsigned i = 0; i < block.instructions; ++i) {
                uint16_t op = block.decoded[i].op;
                if (!h8s_semantic_instruction_supported(op)) {
                    semantic_gap_hi[(uint8_t)(op >> 8)]++;
                    semantic_gap_instructions++;
                    if (!counted_block) {
                        semantic_gap_blocks++;
                        counted_block = true;
                    }
                }
            }
        }
        bytes += block.bytes;
        if (block.stop <= H8S_BLOCK_STOP_UNSUPPORTED) stops[block.stop]++;
        if (block.instructions > longest) {
            longest = block.instructions;
            longest_pc = pc;
        }
    }

    printf("rom=%s\n", argv[1]);
    printf("bytes=%zu max_instructions=%u candidate_starts=%llu\n",
           rom_size, max_instructions, blocks);
    printf("decoded_instructions=%llu decoded_bytes=%llu avg_instructions=%.2f avg_bytes=%.2f\n",
           instructions, bytes,
           blocks ? (double)instructions / (double)blocks : 0.0,
           blocks ? (double)bytes / (double)blocks : 0.0);
    printf("tier1_executable_blocks=%llu tier1_executable_prefix_instructions=%llu\n",
           executable_blocks, executable_prefix_instructions);
    printf("semantic_supported_blocks=%llu semantic_supported_instructions=%llu\n",
           semantic_blocks, semantic_instructions);
    printf("semantic_gap_blocks=%llu\n", semantic_gap_blocks);
    print_top_semantic_gaps(semantic_gap_hi, semantic_gap_instructions);
    printf("longest_block_pc=0x%06x longest_instructions=%u\n", longest_pc, longest);
    for (unsigned i = 0; i <= H8S_BLOCK_STOP_UNSUPPORTED; ++i)
        printf("stop_%s=%llu\n", stop_name((h8s_block_stop_t)i), stops[i]);

    free(rom);
    return 0;
}
