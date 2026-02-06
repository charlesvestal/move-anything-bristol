/*
 * Bristol .mem file loader
 *
 * Reads Bristol synth patch files (.mem format) at runtime.
 * Based on Bristol synthesizer memory format from brightonMini.h
 *
 * Format:
 *   - Bytes 0-31:  algo name (char[32]) - synth type e.g. "mini", "juno"
 *   - Bytes 32-63: patch name (char[32])
 *   - Bytes 64-65: count (short)
 *   - Bytes 66-67: vers (short) - must be 0
 *   - Bytes 68-69: active (short) - number of float parameters
 *   - Bytes 70-71: pad (short)
 *   - Bytes 72+:   float parameters (IEEE 754 little-endian)
 *
 * Copyright (c) 2024 Move Anything
 * License: GPL-3.0 (same as Bristol)
 */

#ifndef BRISTOL_MEM_LOADER_H
#define BRISTOL_MEM_LOADER_H

#include <stdint.h>

#define BRISTOL_MEM_HEADER_SIZE 72
#define BRISTOL_MEM_NAME_SIZE 32
#define BRISTOL_MAX_PARAMS 128

#ifdef __cplusplus
extern "C" {
#endif

/* Bristol memory file header */
typedef struct bristol_mem_header {
    char algo[32];      /* Algorithm/synth name */
    char name[32];      /* Patch name */
    int16_t count;
    int16_t vers;       /* Must be 0 */
    int16_t active;     /* Number of parameters */
    int16_t pad;
} bristol_mem_header_t;

/* Loaded patch data */
typedef struct bristol_patch {
    char algo[32];
    char name[32];
    int param_count;
    float params[BRISTOL_MAX_PARAMS];
} bristol_patch_t;

/* Load a single .mem file
 * Returns 0 on success, -1 on error */
int bristol_load_mem_file(const char *path, bristol_patch_t *patch);

/* Scan directory for .mem files and load them
 * Returns number of patches loaded, or -1 on error
 * patches array should be pre-allocated with max_patches entries */
int bristol_scan_presets(const char *dir_path, bristol_patch_t *patches, int max_patches);

#ifdef __cplusplus
}
#endif

#endif /* BRISTOL_MEM_LOADER_H */
