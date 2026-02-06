/*
 * jupiter_engine.h - Roland Jupiter-8 style synthesizer engine
 *
 * 8-voice polyphonic analog synth with two oscillators per voice.
 */

#ifndef JUPITER_ENGINE_H
#define JUPITER_ENGINE_H

#include "../../shared/bristol_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JUPITER_MAX_VOICES 8

typedef struct {
    bristol_osc_t osc1;
    bristol_osc_t osc2;
    bristol_filter_t filter;
    bristol_env_t filter_env;
    bristol_env_t amp_env;
    int active;
    int note;
    float velocity;
    float freq;
} jupiter_voice_t;

typedef struct {
    float sample_rate;
    jupiter_voice_t voices[JUPITER_MAX_VOICES];

    /* Oscillators */
    int osc1_octave;
    int osc1_saw, osc1_pulse, osc1_tri;
    float osc1_pw;
    int osc2_octave;
    int osc2_saw, osc2_pulse, osc2_noise;
    float osc2_pw;
    float osc2_detune;
    int osc_sync;
    int osc_xmod;

    /* Mixer */
    float mix_osc1, mix_osc2;

    /* Filter */
    float filter_cutoff, filter_resonance, filter_env_amt;
    int filter_kbd_track;

    /* Envelopes */
    float fenv_a, fenv_d, fenv_s, fenv_r;
    float aenv_a, aenv_d, aenv_s, aenv_r;

    /* LFO */
    bristol_lfo_t lfo;
    float lfo_to_osc, lfo_to_pw, lfo_to_filter;

    /* Other */
    float glide;
    float master_volume;
    bristol_noise_t noise;
    float pitch_bend, bend_range;

} jupiter_engine_t;

void jupiter_engine_init(jupiter_engine_t *engine);
void jupiter_engine_reset(jupiter_engine_t *engine);
void jupiter_engine_note_on(jupiter_engine_t *engine, int note, float velocity);
void jupiter_engine_note_off(jupiter_engine_t *engine, int note);
void jupiter_engine_pitch_bend(jupiter_engine_t *engine, float bend);
void jupiter_engine_render(jupiter_engine_t *engine, float *output, int frames);
void jupiter_engine_all_notes_off(jupiter_engine_t *engine);

#ifdef __cplusplus
}
#endif

#endif
