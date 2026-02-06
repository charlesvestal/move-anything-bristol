/*
 * mini_engine.h - Minimoog-style synthesizer engine
 *
 * Uses shared Bristol DSP components.
 */

#ifndef MINI_ENGINE_H
#define MINI_ENGINE_H

#include "../../shared/bristol_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MINI_MAX_RENDER 256

/* Complete Mini synth engine */
typedef struct {
    float sample_rate;

    /* 3 Oscillators */
    bristol_osc_t osc[3];

    /* Noise */
    bristol_noise_t noise;

    /* Filter */
    bristol_filter_t filter;

    /* Envelopes */
    bristol_env_t filter_env;
    bristol_env_t amp_env;

    /* LFO */
    bristol_lfo_t lfo;

    /* Modulation routing */
    float lfo_to_osc;
    float lfo_to_filter;
    int osc3_as_lfo;

    /* Master controls */
    float master_volume;
    float glide;
    int multi_trigger;

    /* Voice state */
    int gate;
    int current_note;
    float current_freq;
    float target_freq;
    float velocity;
    float glide_freq;

    /* Key stack for note priority */
    int key_stack[16];
    int key_stack_count;

    /* Pitch bend */
    float pitch_bend;
    float bend_range;

    /* Mod wheel */
    float mod_wheel;

    /* Tuning */
    float master_tune;
    int octave_transpose;

} mini_engine_t;

void mini_engine_init(mini_engine_t *engine);
void mini_engine_reset(mini_engine_t *engine);
void mini_engine_note_on(mini_engine_t *engine, int note, float velocity);
void mini_engine_note_off(mini_engine_t *engine, int note);
void mini_engine_pitch_bend(mini_engine_t *engine, float bend);
void mini_engine_mod_wheel(mini_engine_t *engine, float amount);
void mini_engine_render(mini_engine_t *engine, float *output, int frames);
void mini_engine_all_notes_off(mini_engine_t *engine);

#ifdef __cplusplus
}
#endif

#endif /* MINI_ENGINE_H */
