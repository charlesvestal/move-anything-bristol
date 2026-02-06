/*
 * poly6_engine.h - Korg Poly-6 style synthesizer engine
 */

#ifndef POLY6_ENGINE_H
#define POLY6_ENGINE_H

#include "../../shared/bristol_common.h"

#define POLY6_MAX_VOICES 6

typedef struct {
    bristol_osc_t osc;
    bristol_filter_t filter;
    bristol_env_t filter_env;
    bristol_env_t amp_env;
    int note;
    float velocity;
    float freq;
    int active;
} poly6_voice_t;

typedef struct {
    float sample_rate;
    poly6_voice_t voices[POLY6_MAX_VOICES];

    /* Oscillator */
    int osc_octave;
    int osc_saw;
    int osc_pulse;
    int osc_sub;
    float osc_pw;
    float sub_level;

    /* Filter */
    float filter_cutoff;
    float filter_resonance;
    float filter_env_amt;
    float filter_kbd_track;

    /* Filter Envelope */
    float fenv_a, fenv_d, fenv_s, fenv_r;

    /* Amp Envelope */
    float aenv_a, aenv_d, aenv_s, aenv_r;

    /* LFO / Modulation Generator */
    bristol_lfo_t lfo;
    float lfo_to_osc;
    float lfo_to_filter;
    float lfo_to_pw;

    /* Chorus */
    int chorus_on;
    float chorus_depth;

    float master_volume;
    float pitch_bend;
    float bend_range;
} poly6_engine_t;

void poly6_engine_init(poly6_engine_t *e);
void poly6_engine_reset(poly6_engine_t *e);
void poly6_engine_note_on(poly6_engine_t *e, int note, float velocity);
void poly6_engine_note_off(poly6_engine_t *e, int note);
void poly6_engine_pitch_bend(poly6_engine_t *e, float bend);
void poly6_engine_all_notes_off(poly6_engine_t *e);
void poly6_engine_render(poly6_engine_t *e, float *output, int frames);

#endif
