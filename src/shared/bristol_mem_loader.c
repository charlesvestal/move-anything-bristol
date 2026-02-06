/*
 * Bristol .mem file loader implementation
 *
 * Copyright (c) 2024 Move Anything
 * License: GPL-3.0 (same as Bristol)
 */

#include "bristol_mem_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

int bristol_load_mem_file(const char *path, bristol_patch_t *patch) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }

    /* Read header */
    bristol_mem_header_t header;
    if (fread(&header, 1, sizeof(header), fp) != sizeof(header)) {
        fclose(fp);
        return -1;
    }

    /* Version must be 0 */
    if (header.vers != 0) {
        fclose(fp);
        return -1;
    }

    /* Copy header info */
    memcpy(patch->algo, header.algo, 32);
    patch->algo[31] = '\0';
    memcpy(patch->name, header.name, 32);
    patch->name[31] = '\0';

    /* Determine parameter count */
    int param_count = header.active;
    if (param_count <= 0 || param_count > BRISTOL_MAX_PARAMS) {
        /* Try to determine from file size */
        struct stat st;
        if (fstat(fileno(fp), &st) == 0) {
            param_count = (st.st_size - BRISTOL_MEM_HEADER_SIZE) / sizeof(float);
            if (param_count > BRISTOL_MAX_PARAMS) {
                param_count = BRISTOL_MAX_PARAMS;
            }
        } else {
            fclose(fp);
            return -1;
        }
    }
    patch->param_count = param_count;

    /* Read parameters */
    size_t read_count = fread(patch->params, sizeof(float), param_count, fp);
    if ((int)read_count < param_count) {
        /* Zero any unread parameters */
        for (int i = read_count; i < param_count; i++) {
            patch->params[i] = 0.0f;
        }
    }

    fclose(fp);
    return 0;
}

/* Compare function for sorting patches by name */
static int patch_name_cmp(const void *a, const void *b) {
    const bristol_patch_t *pa = (const bristol_patch_t *)a;
    const bristol_patch_t *pb = (const bristol_patch_t *)b;
    return strcmp(pa->name, pb->name);
}

int bristol_scan_presets(const char *dir_path, bristol_patch_t *patches, int max_patches) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return -1;
    }

    int count = 0;
    struct dirent *entry;
    char path[512];

    while ((entry = readdir(dir)) != NULL && count < max_patches) {
        /* Skip non-.mem files */
        const char *ext = strrchr(entry->d_name, '.');
        if (!ext || strcmp(ext, ".mem") != 0) {
            continue;
        }

        /* Build full path */
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);

        /* Load the patch */
        if (bristol_load_mem_file(path, &patches[count]) == 0) {
            /* Use filename if no name in patch */
            if (patches[count].name[0] == '\0' ||
                strcmp(patches[count].name, "no name") == 0) {
                /* Strip .mem extension for name */
                strncpy(patches[count].name, entry->d_name, 31);
                patches[count].name[31] = '\0';
                char *dot = strrchr(patches[count].name, '.');
                if (dot) *dot = '\0';
            }
            count++;
        }
    }

    closedir(dir);

    /* Sort patches by name */
    if (count > 1) {
        qsort(patches, count, sizeof(bristol_patch_t), patch_name_cmp);
    }

    return count;
}
