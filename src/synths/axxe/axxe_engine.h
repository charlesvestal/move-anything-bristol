/*
 * axxe_engine.h - ARP Axxe style synthesizer engine
 */

#ifndef AXXE_ENGINE_H
#define AXXE_ENGINE_H

#include "../../shared/bristol_common.h"

typedef struct {
    bristol_osc_t osc;
    bristol_filter_t filter;
    bristol_env_t filter_env;
    bristol_env_t amp_env;
    int note;
    float velocity;
    float freq;
    int active;
    int gate;
} axxe_voice_t;

typedef struct {
    float sample_rate;
    axxe_voice_t voice;

    /* Oscillator */
    int osc_octave;
    int osc_saw;
    int osc_pulse;
    int osc_square;
    float osc_pw;

    /* Filter */
    float filter_cutoff;
    float filter_resonance;
    float filter_env_amt;
    float filter_kbd_track;

    /* Filter Envelope */
    float fenv_a, fenv_d, fenv_s, fenv_r;

    /* Amp Envelope */
    float aenv_a, aenv_d, aenv_s, aenv_r;

    /* LFO */
    bristol_lfo_t lfo;
    float lfo_to_osc;
    float lfo_to_filter;
    float lfo_to_pw;

    /* Noise */
    bristol_noise_t noise;
    float noise_level;

    float master_volume;
    float pitch_bend;
    float bend_range;
} axxe_engine_t;

void axxe_engine_init(axxe_engine_t *e);
void axxe_engine_reset(axxe_engine_t *e);
void axxe_engine_note_on(axxe_engine_t *e, int note, float velocity);
void axxe_engine_note_off(axxe_engine_t *e, int note);
void axxe_engine_pitch_bend(axxe_engine_t *e, float bend);
void axxe_engine_all_notes_off(axxe_engine_t *e);
void axxe_engine_render(axxe_engine_t *e, float *output, int frames);

#endif
