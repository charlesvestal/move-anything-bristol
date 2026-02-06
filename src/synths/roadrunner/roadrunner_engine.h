/*
 * roadrunner_engine.h - Crumar Roadrunner Electric Piano engine
 */

#ifndef ROADRUNNER_ENGINE_H
#define ROADRUNNER_ENGINE_H

#include "../../shared/bristol_common.h"

#define ROADRUNNER_MAX_VOICES 8

typedef struct {
    double phase;
    bristol_env_t amp_env;
    int note;
    float velocity;
    float freq;
    int active;
} roadrunner_voice_t;

typedef struct {
    float sample_rate;
    roadrunner_voice_t voices[ROADRUNNER_MAX_VOICES];

    /* Tone controls */
    float brightness;
    float decay;
    float release;

    /* Vibrato */
    float vibrato_depth;
    float vibrato_rate;
    float vibrato_phase;

    float master_volume;
    float pitch_bend;
    float bend_range;
} roadrunner_engine_t;

void roadrunner_engine_init(roadrunner_engine_t *e);
void roadrunner_engine_reset(roadrunner_engine_t *e);
void roadrunner_engine_note_on(roadrunner_engine_t *e, int note, float velocity);
void roadrunner_engine_note_off(roadrunner_engine_t *e, int note);
void roadrunner_engine_pitch_bend(roadrunner_engine_t *e, float bend);
void roadrunner_engine_all_notes_off(roadrunner_engine_t *e);
void roadrunner_engine_render(roadrunner_engine_t *e, float *output, int frames);

#endif
