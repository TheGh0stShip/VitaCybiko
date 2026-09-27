// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/cfs.h"

#include <stdio.h>
#include <stdlib.h>

#define CLASSIC_CFS_SIZE (2048u * 264u)

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: %s IMAGE [CLASSIC_FACTORY_IMAGE]\n", argv[0]);
        return 2;
    }
    FILE *file = fopen(argv[1], "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        if (file) fclose(file);
        return 2;
    }
    long end = ftell(file);
    if (end < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 2;
    }
    size_t size = (size_t)end;
    uint8_t *data = malloc(size ? size : 1);
    if (!data || fread(data, 1, size, file) != size || fclose(file) != 0) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        free(data);
        return 2;
    }

    bool valid = false;
    const char *format = "unknown";
    if (size == CLASSIC_CFS_SIZE) {
        format = "Classic AT45DB041";
        uint8_t *factory = NULL;
        size_t factory_size = 0;
        if (argc == 3) {
            FILE *reference = fopen(argv[2], "rb");
            if (reference && fseek(reference, 0, SEEK_END) == 0) {
                long reference_end = ftell(reference);
                if (reference_end > 0 && fseek(reference, 0, SEEK_SET) == 0) {
                    factory_size = (size_t)reference_end;
                    factory = malloc(factory_size);
                    if (!factory || fread(factory, 1, factory_size, reference) != factory_size) {
                        free(factory);
                        factory = NULL;
                        factory_size = 0;
                    }
                }
            }
            if (reference) fclose(reference);
        }
        uint32_t image_crc = cfs_classic_crc32(data, size);
        valid = image_crc == 0x3816d0abu || image_crc == 0xe485880fu ||
            cfs_validate_classic_with_reference(data, size, factory, factory_size);
        free(factory);
    } else if (size == CFS_IMAGE_SIZE) {
        format = "Xtreme CFS";
        valid = cfs_validate((const cfs_image_t *)data);
    }
    printf("format=%s size=%zu integrity=%s\n", format, size,
           valid ? "valid" : "invalid");
    free(data);
    return valid ? 0 : 1;
}
