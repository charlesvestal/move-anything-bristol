/*
 * memmoog_engine.h - Memory Moog style synthesizer engine
 */

#ifndef MEMMOOG_ENGINE_H
#define MEMMOOG_ENGINE_H

#include "../../shared/bristol_common.h"

#define MEMMOOG_MAX_VOICES 6

typedef struct {
    bristol_osc_t osc1;
    bristol_osc_t osc2;
    bristol_osc_t osc3;
    bristol_filter_t filter;
    bristol_env_t filter_env;
    bristol_env_t amp_env;
    int note;
    float velocity;
    float freq;
    int active;
} memmoog_voice_t;

typedef struct {
    float sample_rate;
    memmoog_voice_t voices[MEMMOOG_MAX_VOICES];

    /* Oscillator 1 */
    int osc1_octave;
    int osc1_wave;  /* 0=tri, 1=saw, 2=pulse */
    float osc1_pw;

    /* Oscillator 2 */
    int osc2_octave;
    int osc2_wave;
    float osc2_pw;
    float osc2_detune;

    /* Oscillator 3 */
    int osc3_octave;
    int osc3_wave;
    float osc3_pw;
    float osc3_detune;

    /* Mixer */
    float mix_osc1;
    float mix_osc2;
    float mix_osc3;
    float mix_noise;

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

    float master_volume;
    float pitch_bend;
    float bend_range;
    float glide;
} memmoog_engine_t;

void memmoog_engine_init(memmoog_engine_t *e);
void memmoog_engine_reset(memmoog_engine_t *e);
void memmoog_engine_note_on(memmoog_engine_t *e, int note, float velocity);
void memmoog_engine_note_off(memmoog_engine_t *e, int note);
void memmoog_engine_pitch_bend(memmoog_engine_t *e, float bend);
void memmoog_engine_all_notes_off(memmoog_engine_t *e);
void memmoog_engine_render(memmoog_engine_t *e, float *output, int frames);

#endif
