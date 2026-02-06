/*
 * solina_engine.h - Solina String Machine synthesizer engine
 */

#ifndef SOLINA_ENGINE_H
#define SOLINA_ENGINE_H

#include "../../shared/bristol_common.h"

#define SOLINA_MAX_VOICES 12

typedef struct {
    bristol_osc_t osc;
    float phase2;  /* Second oscillator for ensemble */
    int note;
    float velocity;
    float freq;
    int active;
    float level;
} solina_voice_t;

typedef struct {
    float sample_rate;
    solina_voice_t voices[SOLINA_MAX_VOICES];

    /* String sections */
    float violin_level;
    float viola_level;
    float cello_level;
    float bass_level;

    /* Ensemble effect */
    int ensemble_on;
    float ensemble_depth;
    float ensemble_rate;
    float ensemble_phase;

    /* Attack/Release */
    float attack;
    float release;

    float master_volume;
    float pitch_bend;
    float bend_range;
} solina_engine_t;

void solina_engine_init(solina_engine_t *e);
void solina_engine_reset(solina_engine_t *e);
void solina_engine_note_on(solina_engine_t *e, int note, float velocity);
void solina_engine_note_off(solina_engine_t *e, int note);
void solina_engine_pitch_bend(solina_engine_t *e, float bend);
void solina_engine_all_notes_off(solina_engine_t *e);
void solina_engine_render(solina_engine_t *e, float *output, int frames);

#endif
