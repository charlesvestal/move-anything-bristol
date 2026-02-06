/*
 * prophet_engine.h - Prophet-5 style synthesizer engine
 *
 * 5-voice polyphonic analog synth with two oscillators per voice.
 */

#ifndef PROPHET_ENGINE_H
#define PROPHET_ENGINE_H

#include "../../shared/bristol_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PROPHET_MAX_VOICES 5
#define PROPHET_MAX_RENDER 256

/* Single Prophet voice */
typedef struct {
    bristol_osc_t oscA;
    bristol_osc_t oscB;
    bristol_filter_t filter;
    bristol_env_t filter_env;
    bristol_env_t amp_env;

    int active;
    int note;
    float velocity;
    float freq;
    float target_freq;
} prophet_voice_t;

/* Complete Prophet-5 engine */
typedef struct {
    float sample_rate;

    /* Voices */
    prophet_voice_t voices[PROPHET_MAX_VOICES];
    int voice_count;

    /* Oscillator A settings */
    int oscA_octave;        /* 0-5: 16',8',4',2',1',Lo */
    int oscA_saw;
    int oscA_pulse;
    float oscA_pw;
    int oscA_sync;

    /* Oscillator B settings */
    int oscB_octave;
    float oscB_detune;
    int oscB_saw;
    int oscB_tri;
    int oscB_pulse;
    float oscB_pw;
    int oscB_lfo_mod;
    int oscB_kbd_track;

    /* Mixer */
    float mix_oscA;
    float mix_oscB;
    float mix_noise;

    /* Filter */
    float filter_cutoff;
    float filter_resonance;
    float filter_env_amt;
    int filter_kbd_track;

    /* Filter envelope */
    float fenv_attack;
    float fenv_decay;
    float fenv_sustain;
    float fenv_release;

    /* Amp envelope */
    float aenv_attack;
    float aenv_decay;
    float aenv_sustain;
    float aenv_release;

    /* LFO */
    bristol_lfo_t lfo;
    int lfo_saw;
    int lfo_tri;
    int lfo_square;

    /* Poly mod */
    float polymod_filter;
    float polymod_oscB;
    int polymod_freq_A;
    int polymod_pw_A;
    int polymod_filter_en;

    /* Wheel mod */
    float wheel_mix;
    int wheel_freq_A;
    int wheel_freq_B;
    int wheel_pw_A;
    int wheel_pw_B;
    int wheel_filter;

    /* Other */
    float glide;
    int unison;
    float master_tune;
    float master_volume;
    int release_on;

    /* Noise */
    bristol_noise_t noise;

    /* Pitch bend */
    float pitch_bend;
    float bend_range;

} prophet_engine_t;

void prophet_engine_init(prophet_engine_t *engine);
void prophet_engine_reset(prophet_engine_t *engine);
void prophet_engine_note_on(prophet_engine_t *engine, int note, float velocity);
void prophet_engine_note_off(prophet_engine_t *engine, int note);
void prophet_engine_pitch_bend(prophet_engine_t *engine, float bend);
void prophet_engine_render(prophet_engine_t *engine, float *output, int frames);
void prophet_engine_all_notes_off(prophet_engine_t *engine);

#ifdef __cplusplus
}
#endif

#endif /* PROPHET_ENGINE_H */
