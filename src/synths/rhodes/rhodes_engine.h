/*
 * rhodes_engine.h - Rhodes Electric Piano synthesizer engine
 */

#ifndef RHODES_ENGINE_H
#define RHODES_ENGINE_H

#include "../../shared/bristol_common.h"

#define RHODES_MAX_VOICES 8

typedef struct {
    double phase;
    double phase2;
    bristol_env_t amp_env;
    int note;
    float velocity;
    float freq;
    int active;
} rhodes_voice_t;

typedef struct {
    float sample_rate;
    rhodes_voice_t voices[RHODES_MAX_VOICES];

    /* Tine/Tone */
    float tine_level;
    float tone_level;
    float brightness;

    /* Envelope */
    float decay;
    float release;

    /* Tremolo */
    float tremolo_depth;
    float tremolo_rate;
    float tremolo_phase;

    float master_volume;
    float pitch_bend;
    float bend_range;
} rhodes_engine_t;

void rhodes_engine_init(rhodes_engine_t *e);
void rhodes_engine_reset(rhodes_engine_t *e);
void rhodes_engine_note_on(rhodes_engine_t *e, int note, float velocity);
void rhodes_engine_note_off(rhodes_engine_t *e, int note);
void rhodes_engine_pitch_bend(rhodes_engine_t *e, float bend);
void rhodes_engine_all_notes_off(rhodes_engine_t *e);
void rhodes_engine_render(rhodes_engine_t *e, float *output, int frames);

#endif
