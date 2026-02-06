/*
 * pro1_engine.h - Sequential Pro-One style synthesizer engine
 */

#ifndef PRO1_ENGINE_H
#define PRO1_ENGINE_H

#include "../../shared/bristol_common.h"

typedef struct {
    bristol_osc_t osc_a;
    bristol_osc_t osc_b;
    bristol_filter_t filter;
    bristol_env_t filter_env;
    bristol_env_t amp_env;
    int note;
    float velocity;
    float freq;
    int active;
    int gate;
} pro1_voice_t;

typedef struct {
    float sample_rate;
    pro1_voice_t voice;

    /* Oscillator A */
    int osc_a_octave;
    float osc_a_freq;
    int osc_a_saw;
    int osc_a_pulse;
    float osc_a_pw;

    /* Oscillator B */
    int osc_b_octave;
    float osc_b_freq;
    float osc_b_fine;
    int osc_b_saw;
    int osc_b_tri;
    int osc_b_pulse;
    float osc_b_pw;

    /* Mixer */
    float mix_osc_a;
    float mix_osc_b;
    float mix_noise;

    /* Sync/Mod */
    int osc_sync;
    float osc_b_to_a_fm;

    /* Filter */
    float filter_cutoff;
    float filter_resonance;
    float filter_env_amt;
    float filter_kbd_track;

    /* Filter Envelope */
    float fenv_a, fenv_d, fenv_s, fenv_r;

    /* Amp Envelope */
    float aenv_a, aenv_d, aenv_s, aenv_r;

    /* Glide */
    float glide;
    float glide_target;

    /* LFO */
    bristol_lfo_t lfo;
    float lfo_to_osc;
    float lfo_to_filter;
    float lfo_to_pw;

    /* Noise */
    bristol_noise_t noise;

    float master_volume;
    float pitch_bend;
    float bend_range;
    float mod_wheel;
} pro1_engine_t;

void pro1_engine_init(pro1_engine_t *e);
void pro1_engine_reset(pro1_engine_t *e);
void pro1_engine_note_on(pro1_engine_t *e, int note, float velocity);
void pro1_engine_note_off(pro1_engine_t *e, int note);
void pro1_engine_pitch_bend(pro1_engine_t *e, float bend);
void pro1_engine_mod_wheel(pro1_engine_t *e, float mod);
void pro1_engine_all_notes_off(pro1_engine_t *e);
void pro1_engine_render(pro1_engine_t *e, float *output, int frames);

#endif
