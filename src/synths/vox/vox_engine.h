/*
 * vox_engine.h - Vox Continental Combo Organ engine
 */

#ifndef VOX_ENGINE_H
#define VOX_ENGINE_H

#include "../../shared/bristol_common.h"

#define VOX_MAX_VOICES 12

typedef struct {
    double phase;
    int note;
    float velocity;
    float freq;
    int active;
    float level;
} vox_voice_t;

typedef struct {
    float sample_rate;
    vox_voice_t voices[VOX_MAX_VOICES];

    /* Drawbars - harmonic levels */
    float drawbar_16;   /* 16' */
    float drawbar_8;    /* 8' */
    float drawbar_4;    /* 4' */
    float drawbar_2;    /* 2' */
    float drawbar_mix;  /* Mixture */

    /* Vibrato */
    float vibrato_depth;
    float vibrato_rate;
    float vibrato_phase;

    /* Percussion */
    int percussion_on;
    float percussion_decay;

    float master_volume;
    float pitch_bend;
    float bend_range;
} vox_engine_t;

void vox_engine_init(vox_engine_t *e);
void vox_engine_reset(vox_engine_t *e);
void vox_engine_note_on(vox_engine_t *e, int note, float velocity);
void vox_engine_note_off(vox_engine_t *e, int note);
void vox_engine_pitch_bend(vox_engine_t *e, float bend);
void vox_engine_all_notes_off(vox_engine_t *e);
void vox_engine_render(vox_engine_t *e, float *output, int frames);

#endif
