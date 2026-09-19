#include "core/cfs.h"
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/*  Helpers                                                           */
/* ------------------------------------------------------------------ */

/* CRC16 algorithm from MAME cybikoxt.cpp */
uint16_t cfs_compute_crc16(const uint8_t *data, int length)
{
    uint32_t val = 0;
    for (int i = 0; i < length; i++) {
        val = (val ^ data[i] ^ i) << 1;
        val = val | ((val >> 16) & 0x0001);
    }
    return (uint16_t)(val & 0xFFFF);
}

/* Return pointer to the start of page N within the image */
static uint8_t *page_ptr(cfs_image_t *img, int page)
{
    return &img->data[page * CFS_PAGE_SIZE];
}

static const uint8_t *page_ptr_const(const cfs_image_t *img, int page)
{
    return &img->data[page * CFS_PAGE_SIZE];
}

/* Return pointer to the 256-byte block data within a page */
static uint8_t *block_data(cfs_image_t *img, int page)
{
    return page_ptr(img, page) + 2;
}

static const uint8_t *block_data_const(const cfs_image_t *img, int page)
{
    return page_ptr_const(img, page) + 2;
}

/* Write big-endian CRC16 at the start of a page */
static void page_write_crc(cfs_image_t *img, int page)
{
    const uint8_t *blk = block_data_const(img, page);
    uint16_t crc = cfs_compute_crc16(blk, CFS_BLOCK_DATA_SIZE);
    uint8_t *p = page_ptr(img, page);
    p[0] = (uint8_t)(crc >> 8);
    p[1] = (uint8_t)(crc & 0xFF);
}

/* Write a big-endian u16 into a buffer */
static void write_be16(uint8_t *dst, uint16_t val)
{
    dst[0] = (uint8_t)(val >> 8);
    dst[1] = (uint8_t)(val & 0xFF);
}

/* Write a big-endian u32 into a buffer */
static void write_be32(uint8_t *dst, uint32_t val)
{
    dst[0] = (uint8_t)(val >> 24);
    dst[1] = (uint8_t)((val >> 16) & 0xFF);
    dst[2] = (uint8_t)((val >> 8) & 0xFF);
    dst[3] = (uint8_t)(val & 0xFF);
}

/* Read a big-endian u16 from a buffer */
static uint16_t read_be16(const uint8_t *src)
{
    return (uint16_t)((src[0] << 8) | src[1]);
}

static bool valid_file_args(const cfs_image_t *img, const char *name,
                            const uint8_t *file_data, size_t len)
{
    if (!img || !name || name[0] == '\0' || (!file_data && len != 0)) {
        return false;
    }

    size_t max_size = CFS_FIRST_BLOCK_CAPACITY +
        (size_t)(CFS_FILE_BLOCKS - 1) * CFS_CONT_BLOCK_CAPACITY;
    return len <= max_size;
}

static int blocks_for_size(size_t len)
{
    if (len <= CFS_FIRST_BLOCK_CAPACITY) {
        return 1;
    }
    return 1 + (int)((len - CFS_FIRST_BLOCK_CAPACITY +
                      CFS_CONT_BLOCK_CAPACITY - 1) /
                     CFS_CONT_BLOCK_CAPACITY);
}

/* File block page index: block_index 0..1999 maps to page 5..2004 */
static int file_block_page(int block_index)
{
    return CFS_BOOT_BLOCKS + block_index;
}

/* ------------------------------------------------------------------ */
/*  cfs_format                                                        */
/* ------------------------------------------------------------------ */

void cfs_format(cfs_image_t *img)
{
    /* 1. Fill entire image with 0xFF */
    memset(img->data, 0xFF, CFS_IMAGE_SIZE);

    /* 2. Boot blocks (pages 0-4): already all 0xFF, CRC = 0xFFFF
     *    which is 0xFF 0xFF big-endian -- already correct from memset. */

    /* 3. File blocks (pages 5-2004): mark as unused, compute CRC */
    for (int i = 0; i < CFS_FILE_BLOCKS; i++) {
        int page = file_block_page(i);
        uint8_t *blk = block_data(img, page);
        blk[0] = 0x7F;  /* clear bit 7 = unused */
        page_write_crc(img, page);
    }
}

bool cfs_validate(const cfs_image_t *img)
{
    if (!img) {
        return false;
    }

    for (int page = 0; page < CFS_PAGE_COUNT; page++) {
        const uint8_t *raw = page_ptr_const(img, page);
        uint16_t stored_crc = read_be16(raw);
        uint16_t computed_crc =
            cfs_compute_crc16(block_data_const(img, page), CFS_BLOCK_DATA_SIZE);
        bool erased_boot_page = page < CFS_BOOT_BLOCKS;
        if (erased_boot_page) {
            for (int i = 0; i < CFS_PAGE_SIZE; i++) {
                if (raw[i] != 0xFF) {
                    erased_boot_page = false;
                    break;
                }
            }
        }
        if (!erased_boot_page && stored_crc != computed_crc) {
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/*  cfs_add_file                                                      */
/* ------------------------------------------------------------------ */

bool cfs_add_file(cfs_image_t *img, const char *name,
                  const uint8_t *file_data, size_t len)
{
    if (!valid_file_args(img, name, file_data, len)) {
        return false;
    }

    /* Calculate blocks needed */
    int blocks_needed = blocks_for_size(len);

    /* Find free blocks */
    int free_blocks[2048];
    int free_count = 0;

    for (int i = 0; i < CFS_FILE_BLOCKS && free_count < blocks_needed; i++) {
        const uint8_t *blk = block_data_const(img, file_block_page(i));
        if ((blk[0] & 0x80) == 0) {
            free_blocks[free_count++] = i;
        }
    }

    if (free_count < blocks_needed) {
        return false;  /* not enough space */
    }

    /* Timestamp: seconds since 1900-01-01 */
    uint32_t timestamp = (uint32_t)(time(NULL)) + 2208988800U;

    /* Filename length (capped at 58 chars to fit bytes 7-64) */
    size_t name_len = strlen(name);
    if (name_len > 58) {
        name_len = 58;
    }

    size_t data_offset = 0;  /* how much file data we have written */

    for (int n = 0; n < blocks_needed; n++) {
        int block_idx = free_blocks[n];
        int page = file_block_page(block_idx);
        uint8_t *blk = block_data(img, page);

        /* Clear block to 0x00 so unused bytes are deterministic */
        memset(blk, 0x00, CFS_BLOCK_DATA_SIZE);

        /* Common header: bytes 0-5 */
        blk[0] = 0x80;                              /* flags: BLOCK_USED */
        /* [1] data_count -- filled below */
        write_be16(&blk[2], (uint16_t)free_blocks[0]); /* file_id = first block index */
        write_be16(&blk[4], (uint16_t)n);               /* part_id */

        if (n == 0) {
            /* Part 0: file header block */
            blk[6] = 0x20;  /* type marker */

            /* Filename at bytes 7-64 (null-terminated) */
            memcpy(&blk[7], name, name_len);
            blk[7 + name_len] = '\0';

            /* Timestamp at bytes 74-77 */
            write_be32(&blk[74], timestamp);

            /* File data at bytes 78-255 (up to 178 bytes) */
            size_t chunk = len;
            if (chunk > CFS_FIRST_BLOCK_CAPACITY) {
                chunk = CFS_FIRST_BLOCK_CAPACITY;
            }
            if (chunk > 0) {
                memcpy(&blk[78], file_data, chunk);
            }
            blk[1] = (uint8_t)chunk;
            data_offset = chunk;
        } else {
            /* Continuation block: data at bytes 6-255 (up to 250 bytes) */
            size_t remaining = len - data_offset;
            size_t chunk = remaining;
            if (chunk > CFS_CONT_BLOCK_CAPACITY) {
                chunk = CFS_CONT_BLOCK_CAPACITY;
            }
            if (chunk > 0) {
                memcpy(&blk[6], &file_data[data_offset], chunk);
            }
            blk[1] = (uint8_t)chunk;
            data_offset += chunk;
        }

        /* Update page CRC */
        page_write_crc(img, page);
    }

    return true;
}

/* Replace every existing file with the same on-disk name, or add a new one.
 * Capacity is checked before any old blocks are released, so a failed update
 * leaves the filesystem unchanged. */
bool cfs_put_file(cfs_image_t *img, const char *name,
                  const uint8_t *file_data, size_t len)
{
    if (!valid_file_args(img, name, file_data, len)) {
        return false;
    }

    char disk_name[59] = {0};
    size_t name_len = strlen(name);
    if (name_len > 58) {
        name_len = 58;
    }
    memcpy(disk_name, name, name_len);

    uint16_t replaced_ids[CFS_FILE_BLOCKS];
    int replaced_count = 0;
    int free_count = 0;

    for (int i = 0; i < CFS_FILE_BLOCKS; i++) {
        const uint8_t *blk = block_data_const(img, file_block_page(i));
        if ((blk[0] & 0x80) == 0) {
            free_count++;
        } else if (read_be16(&blk[4]) == 0 &&
                   strncmp((const char *)&blk[7], disk_name, 59) == 0) {
            replaced_ids[replaced_count++] = read_be16(&blk[2]);
        }
    }

    int replaced_blocks = 0;
    for (int i = 0; i < CFS_FILE_BLOCKS; i++) {
        const uint8_t *blk = block_data_const(img, file_block_page(i));
        if ((blk[0] & 0x80) == 0) {
            continue;
        }
        uint16_t file_id = read_be16(&blk[2]);
        for (int n = 0; n < replaced_count; n++) {
            if (file_id == replaced_ids[n]) {
                replaced_blocks++;
                break;
            }
        }
    }

    if (free_count + replaced_blocks < blocks_for_size(len)) {
        return false;
    }

    for (int i = 0; i < CFS_FILE_BLOCKS; i++) {
        uint8_t *blk = block_data(img, file_block_page(i));
        if ((blk[0] & 0x80) == 0) {
            continue;
        }
        uint16_t file_id = read_be16(&blk[2]);
        for (int n = 0; n < replaced_count; n++) {
            if (file_id == replaced_ids[n]) {
                memset(blk, 0xFF, CFS_BLOCK_DATA_SIZE);
                blk[0] = 0x7F;
                page_write_crc(img, file_block_page(i));
                break;
            }
        }
    }

    return cfs_add_file(img, name, file_data, len);
}

/* ------------------------------------------------------------------ */
/*  cfs_list_files                                                    */
/* ------------------------------------------------------------------ */

int cfs_list_files(const cfs_image_t *img, char names[][64], int max_files)
{
    int count = 0;

    for (int i = 0; i < CFS_FILE_BLOCKS && count < max_files; i++) {
        const uint8_t *blk = block_data_const(img, file_block_page(i));

        /* Check: used block with part_id == 0 */
        if ((blk[0] & 0x80) && read_be16(&blk[4]) == 0) {
            /* Extract filename from bytes 7-64 */
            memset(names[count], 0, 64);
            memcpy(names[count], &blk[7], 58);
            names[count][58] = '\0';  /* ensure termination */
            count++;
        }
    }

    return count;
}
