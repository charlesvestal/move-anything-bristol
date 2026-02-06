/*
 * obxa_engine.h - Oberheim OB-Xa style synthesizer engine
 */

#ifndef OBXA_ENGINE_H
#define OBXA_ENGINE_H

#include "../../shared/bristol_common.h"

#define OBXA_MAX_VOICES 8

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
} obxa_voice_t;

typedef struct {
    float sample_rate;
    obxa_voice_t voices[OBXA_MAX_VOICES];

    /* Oscillator 1 */
    int osc1_octave;
    float osc1_freq;
    int osc1_saw;
    int osc1_pulse;
    float osc1_pw;

    /* Oscillator 2 */
    int osc2_octave;
    float osc2_freq;
    float osc2_detune;
    int osc2_saw;
    int osc2_pulse;
    float osc2_pw;

    /* Sync/Mix */
    int osc_sync;
    float mix_osc1;
    float mix_osc2;
    float mix_noise;

    /* Filter */
    float filter_cutoff;
    float filter_resonance;
    float filter_env_amt;
    int filter_kbd_track;
    int filter_4pole;

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
} obxa_engine_t;

void obxa_engine_init(obxa_engine_t *e);
void obxa_engine_reset(obxa_engine_t *e);
void obxa_engine_note_on(obxa_engine_t *e, int note, float velocity);
void obxa_engine_note_off(obxa_engine_t *e, int note);
void obxa_engine_pitch_bend(obxa_engine_t *e, float bend);
void obxa_engine_all_notes_off(obxa_engine_t *e);
void obxa_engine_render(obxa_engine_t *e, float *output, int frames);

#endif
