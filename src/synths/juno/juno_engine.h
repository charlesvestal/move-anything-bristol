/*
 * juno_engine.h - Juno-style synthesizer engine
 *
 * DCO-based synth with chorus, inspired by Roland Juno-60/106.
 * Uses shared Bristol DSP components.
 */

#ifndef JUNO_ENGINE_H
#define JUNO_ENGINE_H

#include "../../shared/bristol_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JUNO_MAX_RENDER 256

/* Juno chorus effect */
typedef struct {
    float buffer[4096];
    int write_pos;
    float lfo_phase;
    float rate;
    float depth;
    int mode;  /* 0=off, 1=I, 2=II, 3=I+II */
} juno_chorus_t;

/* Complete Juno synth engine */
typedef struct {
    float sample_rate;

    /* Single DCO (digitally controlled oscillator) */
    bristol_osc_t dco;
    float pulse_width;
    float pw_lfo_amount;
    int saw_enabled;
    int pulse_enabled;
    int sub_enabled;
    float sub_level;

    /* Noise */
    bristol_noise_t noise;

    /* High-pass filter (simple 1-pole) */
    float hpf_cutoff;
    float hpf_state;

    /* Low-pass filter (4-pole) */
    bristol_filter_t filter;

    /* Envelopes */
    bristol_env_t filter_env;
    bristol_env_t amp_env;

    /* LFO */
    bristol_lfo_t lfo;
    float lfo_to_dco;
    float lfo_to_filter;

    /* Chorus */
    juno_chorus_t chorus;

    /* Master controls */
    float master_volume;
    float glide;

    /* Voice state */
    int gate;
    int current_note;
    float current_freq;
    float target_freq;
    float velocity;
    float glide_freq;

    /* Key stack */
    int key_stack[16];
    int key_stack_count;

    /* Pitch bend */
    float pitch_bend;
    float bend_range;

} juno_engine_t;

void juno_engine_init(juno_engine_t *engine);
void juno_engine_reset(juno_engine_t *engine);
void juno_engine_note_on(juno_engine_t *engine, int note, float velocity);
void juno_engine_note_off(juno_engine_t *engine, int note);
void juno_engine_pitch_bend(juno_engine_t *engine, float bend);
void juno_engine_render(juno_engine_t *engine, float *output, int frames);
void juno_engine_all_notes_off(juno_engine_t *engine);

#ifdef __cplusplus
}
#endif

#endif /* JUNO_ENGINE_H */
