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

static const char *branch_kind_name(h8s_block_branch_kind_t kind)
{
    switch (kind) {
    case H8S_BLOCK_BRANCH_NONE: return "none";
    case H8S_BLOCK_BRANCH_BCC8: return "bcc8";
    case H8S_BLOCK_BRANCH_BCC16: return "bcc16";
    case H8S_BLOCK_BRANCH_BSR8: return "bsr8";
    case H8S_BLOCK_BRANCH_BSR16: return "bsr16";
    case H8S_BLOCK_BRANCH_JMP_ABS24: return "jmp_abs24";
    case H8S_BLOCK_BRANCH_JSR_ABS24: return "jsr_abs24";
    case H8S_BLOCK_BRANCH_INDIRECT: return "indirect";
    case H8S_BLOCK_BRANCH_RETURN: return "return";
    case H8S_BLOCK_BRANCH_TRAP: return "trap";
    case H8S_BLOCK_BRANCH_SLEEP: return "sleep";
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

static void print_top_semantic_opcode_gaps(const unsigned long long counts[65536],
                                           unsigned long long total)
{
    bool printed[65536] = {0};
    for (unsigned rank = 0; rank < 20; ++rank) {
        unsigned best = 0;
        unsigned long long best_count = 0;
        for (unsigned op = 0; op < 65536; ++op) {
            if (!printed[op] && counts[op] > best_count) {
                best_count = counts[op];
                best = op;
            }
        }
        if (!best_count) break;
        printed[best] = true;
        printf("semantic_gap_opcode_top%02u_op=0x%04x count=%llu share=%.2f%%\n",
               rank + 1, best, best_count,
               total ? 100.0 * (double)best_count / (double)total : 0.0);
    }
}

static void print_top_branch_targets(const unsigned long long *counts,
                                     size_t count, unsigned long long total)
{
    bool *printed = calloc(count ? count : 1, sizeof(*printed));
    if (!printed) return;
    for (unsigned rank = 0; rank < 12; ++rank) {
        size_t best = 0;
        unsigned long long best_count = 0;
        for (size_t i = 0; i < count; ++i) {
            if (!printed[i] && counts[i] > best_count) {
                best_count = counts[i];
                best = i;
            }
        }
        if (!best_count) break;
        printed[best] = true;
        printf("branch_target_top%02u_pc=0x%06zx count=%llu share=%.2f%%\n",
               rank + 1, best * 2, best_count,
               total ? 100.0 * (double)best_count / (double)total : 0.0);
    }
    free(printed);
}

static bool target_semantic_supported(const uint8_t *rom, size_t rom_size,
                                      unsigned max_instructions, uint32_t target)
{
    if ((target & 1u) != 0 || (size_t)target + 1 >= rom_size) return false;
    h8s_block_t target_block;
    if (!h8s_analyze_rom_block(rom, rom_size, target, max_instructions, &target_block))
        return false;
    return h8s_semantic_block_supported(&target_block);
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

static bool parse_pc(const char *text, uint32_t *pc)
{
    if (!text || !pc) return false;
    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 0);
    if (errno || !end || *end || value > 0xfffffful) return false;
    *pc = (uint32_t)value;
    return true;
}

static const char *yesno(bool value)
{
    return value ? "yes" : "no";
}

static const char *byte_reg_name(unsigned reg)
{
    static const char *names[] = {
        "R0H", "R1H", "R2H", "R3H", "R4H", "R5H", "R6H", "R7H",
        "R0L", "R1L", "R2L", "R3L", "R4L", "R5L", "R6L", "R7L"
    };
    return reg < sizeof(names) / sizeof(names[0]) ? names[reg] : "?";
}

static const char *word_reg_name(unsigned reg)
{
    static const char *names[] = {
        "R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7",
        "E0", "E1", "E2", "E3", "E4", "E5", "E6", "E7"
    };
    return reg < sizeof(names) / sizeof(names[0]) ? names[reg] : "?";
}

static const char *er_name(unsigned reg)
{
    static const char *names[] = {
        "ER0", "ER1", "ER2", "ER3", "ER4", "ER5", "ER6", "ER7"
    };
    return reg < sizeof(names) / sizeof(names[0]) ? names[reg] : "?";
}

static void describe_instruction(char *out, size_t out_size,
                                 const h8s_block_instruction_t *insn)
{
    if (!out || !out_size || !insn) return;
    out[0] = '\0';
    uint16_t op = insn->op;
    uint8_t hi = (uint8_t)(op >> 8);
    uint8_t lo = (uint8_t)op;

    if ((hi >> 4) == 0x2) {
        snprintf(out, out_size, "MOV.B @0xffff%02x,%s (MMIO/absolute byte read)",
                 lo, byte_reg_name(hi & 0xf));
        return;
    }
    if ((hi >> 4) == 0x3) {
        snprintf(out, out_size, "MOV.B %s,@0xffff%02x (MMIO/absolute byte write)",
                 byte_reg_name(hi & 0xf), lo);
        return;
    }
    if (hi == 0x68) {
        bool write = (lo & 0x80) != 0;
        unsigned er = (lo >> 4) & 0x7;
        unsigned br = lo & 0xf;
        snprintf(out, out_size, write ? "MOV.B %s,@%s" : "MOV.B @%s,%s",
                 write ? byte_reg_name(br) : er_name(er),
                 write ? er_name(er) : byte_reg_name(br));
        return;
    }
    if (op == 0x0100 && insn->bytes >= 4) {
        uint16_t op2 = (uint16_t)(insn->imm >> 16);
        if (op2 == 0) op2 = (uint16_t)insn->imm;
        uint8_t hi2 = (uint8_t)(op2 >> 8);
        uint8_t lo2 = (uint8_t)op2;
        if (hi2 == 0x6b) {
            bool write = (lo2 & 0x80) != 0;
            bool addr24 = (lo2 & 0x20) != 0;
            unsigned wr = lo2 & 0xf;
            uint32_t addr = insn->imm & 0xffffffu;
            if (addr24) {
                if (write)
                    snprintf(out, out_size, "MOV.W %s,@0x%06x",
                             word_reg_name(wr), addr);
                else
                    snprintf(out, out_size, "MOV.W @0x%06x,%s",
                             addr, word_reg_name(wr));
            } else {
                snprintf(out, out_size, "MOV.W absolute d:16 form op2=0x%04x", op2);
            }
            return;
        }
        snprintf(out, out_size, "prefixed 0x0100 memory form op2=0x%04x", op2);
        return;
    }
}

static void inspect_pc(const uint8_t *rom, size_t rom_size, unsigned max_instructions,
                       uint32_t pc)
{
    h8s_block_t block;
    printf("inspect_pc=0x%06x\n", pc);
    if ((pc & 1u) != 0 || (size_t)pc + 1 >= rom_size ||
        !h8s_analyze_rom_block(rom, rom_size, pc, max_instructions, &block)) {
        printf("inspect_valid=no\n");
        return;
    }

    printf("inspect_valid=yes start=0x%06x bytes=%u instructions=%u stop=%s stop_pc=0x%06x executable=%s semantic=%s executable_prefix=%u\n",
           block.start, block.bytes, block.instructions, stop_name(block.stop),
           block.stop_pc, yesno(block.executable),
           yesno(h8s_semantic_block_supported(&block)),
           block.executable_prefix_instructions);
    if (block.stop == H8S_BLOCK_STOP_BRANCH) {
        printf("inspect_branch kind=%s op=0x%04x bytes=%u conditional=%s cond=0x%x has_target=%s fallthrough=0x%06x target=0x%06x\n",
               branch_kind_name(block.branch_kind), block.branch_op,
               block.branch_bytes, yesno(block.branch_conditional),
               block.branch_condition, yesno(block.branch_has_target),
               block.branch_fallthrough, block.branch_target);
    }
    uint32_t insn_pc = block.start;
    for (unsigned i = 0; i < block.instructions; ++i) {
        const h8s_block_instruction_t *insn = &block.decoded[i];
        bool supported = h8s_semantic_instruction_supported(insn->op);
        if (insn->op == 0x01f0 && insn->bytes == 4) {
            uint8_t hi2 = (uint8_t)(insn->imm >> 8);
            supported = hi2 == 0x64 || hi2 == 0x65 || hi2 == 0x66;
        }
        char desc[128];
        describe_instruction(desc, sizeof(desc), insn);
        printf("inspect_insn%02u pc=0x%06x op=0x%04x bytes=%u imm=0x%08x semantic=%s",
               i + 1, insn_pc,
               insn->op, insn->bytes, insn->imm, yesno(supported));
        if (desc[0]) printf(" desc=\"%s\"", desc);
        printf("\n");
        insn_pc += insn->bytes;
    }
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
    if (argc < 2) {
        fprintf(stderr, "usage: %s rom.bin [max_instructions] [--inspect pc...]\n", argv[0]);
        return 2;
    }

    size_t rom_size = 0;
    uint8_t *rom = load_file(argv[1], &rom_size);
    if (!rom) {
        fprintf(stderr, "failed to read %s\n", argv[1]);
        return 1;
    }

    unsigned max_instructions = 32;
    int argi = 2;
    if (argi < argc && strcmp(argv[argi], "--inspect") != 0) {
        max_instructions = parse_uint(argv[argi], 32);
        argi++;
    }
    if (argi < argc && strcmp(argv[argi], "--inspect") == 0) {
        if (++argi >= argc) {
            fprintf(stderr, "--inspect requires at least one PC\n");
            free(rom);
            return 2;
        }
        for (; argi < argc; ++argi) {
            uint32_t pc = 0;
            if (!parse_pc(argv[argi], &pc)) {
                fprintf(stderr, "invalid PC: %s\n", argv[argi]);
                free(rom);
                return 2;
            }
            inspect_pc(rom, rom_size, max_instructions, pc);
        }
        free(rom);
        return 0;
    }
    unsigned long long blocks = 0;
    unsigned long long instructions = 0;
    unsigned long long executable_blocks = 0;
    unsigned long long executable_prefix_instructions = 0;
    unsigned long long semantic_blocks = 0;
    unsigned long long semantic_instructions = 0;
    unsigned long long semantic_gap_blocks = 0;
    unsigned long long semantic_gap_instructions = 0;
    unsigned long long semantic_gap_hi[256] = {0};
    unsigned long long semantic_gap_op[65536] = {0};
    unsigned long long bytes = 0;
    unsigned long long stops[H8S_BLOCK_STOP_UNSUPPORTED + 1] = {0};
    unsigned long long branch_kinds[H8S_BLOCK_BRANCH_SLEEP + 1] = {0};
    unsigned long long branch_conditional = 0;
    unsigned long long branch_static_targets = 0;
    unsigned long long branch_targets_in_rom = 0;
    unsigned long long branch_targets_even = 0;
    unsigned long long chain_edges = 0;
    unsigned long long chain_edges_in_rom_even = 0;
    unsigned long long chain_edges_semantic_target = 0;
    unsigned long long chain_conditional_edges = 0;
    unsigned long long chain_unconditional_edges = 0;
    size_t target_slots = (rom_size + 1) / 2;
    unsigned long long *branch_target_hits = calloc(target_slots ? target_slots : 1,
                                                    sizeof(*branch_target_hits));
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
                    semantic_gap_op[op]++;
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
        if (block.stop == H8S_BLOCK_STOP_BRANCH &&
            block.branch_kind <= H8S_BLOCK_BRANCH_SLEEP) {
            branch_kinds[block.branch_kind]++;
            if (block.branch_conditional) branch_conditional++;
            if (block.branch_has_target) {
                branch_static_targets++;
                if ((size_t)block.branch_target < rom_size) {
                    branch_targets_in_rom++;
                    if ((block.branch_target & 1u) == 0) {
                        branch_targets_even++;
                        if (branch_target_hits)
                            branch_target_hits[block.branch_target / 2]++;
                    }
                }
            }
            if (block.branch_kind == H8S_BLOCK_BRANCH_BCC8 ||
                block.branch_kind == H8S_BLOCK_BRANCH_BCC16) {
                uint32_t edge_targets[] = {block.branch_fallthrough, block.branch_target};
                for (unsigned edge = 0; edge < 2; ++edge) {
                    chain_edges++;
                    chain_conditional_edges++;
                    if ((edge_targets[edge] & 1u) == 0 && (size_t)edge_targets[edge] + 1 < rom_size) {
                        chain_edges_in_rom_even++;
                        if (target_semantic_supported(rom, rom_size, max_instructions, edge_targets[edge]))
                            chain_edges_semantic_target++;
                    }
                }
            } else if (block.branch_kind == H8S_BLOCK_BRANCH_BSR8 ||
                       block.branch_kind == H8S_BLOCK_BRANCH_BSR16 ||
                       block.branch_kind == H8S_BLOCK_BRANCH_JMP_ABS24 ||
                       block.branch_kind == H8S_BLOCK_BRANCH_JSR_ABS24) {
                chain_edges++;
                chain_unconditional_edges++;
                if (block.branch_has_target &&
                    (block.branch_target & 1u) == 0 &&
                    (size_t)block.branch_target + 1 < rom_size) {
                    chain_edges_in_rom_even++;
                    if (target_semantic_supported(rom, rom_size, max_instructions, block.branch_target))
                        chain_edges_semantic_target++;
                }
            }
        }
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
    print_top_semantic_opcode_gaps(semantic_gap_op, semantic_gap_instructions);
    printf("longest_block_pc=0x%06x longest_instructions=%u\n", longest_pc, longest);
    for (unsigned i = 0; i <= H8S_BLOCK_STOP_UNSUPPORTED; ++i)
        printf("stop_%s=%llu\n", stop_name((h8s_block_stop_t)i), stops[i]);
    printf("branch_conditional=%llu branch_static_targets=%llu branch_targets_in_rom=%llu branch_targets_even=%llu\n",
           branch_conditional, branch_static_targets, branch_targets_in_rom, branch_targets_even);
    printf("chain_edges=%llu chain_conditional_edges=%llu chain_unconditional_edges=%llu chain_edges_in_rom_even=%llu chain_edges_semantic_target=%llu\n",
           chain_edges, chain_conditional_edges, chain_unconditional_edges,
           chain_edges_in_rom_even, chain_edges_semantic_target);
    for (unsigned i = 0; i <= H8S_BLOCK_BRANCH_SLEEP; ++i)
        printf("branch_%s=%llu\n", branch_kind_name((h8s_block_branch_kind_t)i), branch_kinds[i]);
    if (branch_target_hits) {
        print_top_branch_targets(branch_target_hits, target_slots, branch_targets_even);
        free(branch_target_hits);
    }

    free(rom);
    return 0;
}
