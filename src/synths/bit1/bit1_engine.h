/*
 * bit1_engine.h - Crumar Bit-1 style synthesizer engine
 */

#ifndef BIT1_ENGINE_H
#define BIT1_ENGINE_H

#include "../../shared/bristol_common.h"

#define BIT1_MAX_VOICES 6

typedef struct {
    bristol_osc_t osc1;
    bristol_osc_t osc2;
    bristol_filter_t filter;
    bristol_env_t filter_env;
    bristol_env_t amp_env;
    int note;
    float velocity;
    float freq;
    int active;
} bit1_voice_t;

typedef struct {
    float sample_rate;
    bit1_voice_t voices[BIT1_MAX_VOICES];

    /* Oscillator 1 */
    int osc1_octave;
    int osc1_wave;  /* DCO waveform */
    float osc1_pw;

    /* Oscillator 2 */
    int osc2_octave;
    int osc2_wave;
    float osc2_pw;
    float osc2_detune;

    /* Mixer */
    float mix_osc1;
    float mix_osc2;
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
} bit1_engine_t;

void bit1_engine_init(bit1_engine_t *e);
void bit1_engine_reset(bit1_engine_t *e);
void bit1_engine_note_on(bit1_engine_t *e, int note, float velocity);
void bit1_engine_note_off(bit1_engine_t *e, int note);
void bit1_engine_pitch_bend(bit1_engine_t *e, float bend);
void bit1_engine_all_notes_off(bit1_engine_t *e);
void bit1_engine_render(bit1_engine_t *e, float *output, int frames);

#endif
