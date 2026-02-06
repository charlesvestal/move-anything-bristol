/*
 * obx_engine.h - Oberheim OB-X style synthesizer engine
 *
 * 8-voice polyphonic analog synth with two oscillators per voice.
 */

#ifndef OBX_ENGINE_H
#define OBX_ENGINE_H

#include "../../shared/bristol_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OBX_MAX_VOICES 8
#define OBX_MAX_RENDER 256

/* Single OBX voice */
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
} obx_voice_t;

/* Complete OB-X engine */
typedef struct {
    float sample_rate;

    /* Voices */
    obx_voice_t voices[OBX_MAX_VOICES];

    /* Control */
    float glide;
    int unison;
    float osc2_detune;

    /* LFO */
    bristol_lfo_t lfo;
    int lfo_sine;
    int lfo_square;
    int lfo_sh;
    float lfo_fm_depth;
    int lfo_fm_osc1;
    int lfo_fm_osc2;
    int lfo_fm_filter;
    float lfo_pwm_depth;
    int lfo_pwm_osc1;
    int lfo_pwm_osc2;

    /* Oscillators */
    int osc1_octave;
    float osc_pw;
    float osc2_tune;
    int osc1_saw;
    int osc1_pulse;
    int osc_crossmod;
    int osc_sync;
    int osc2_saw;
    int osc2_pulse;

    /* Filter */
    float filter_cutoff;
    float filter_resonance;
    float filter_mod;
    int mix_osc1;
    int mix_osc2;
    int mix_noise;
    int filter_4pole;
    int filter_kbd;
    float filter_env_amt;

    /* Envelopes */
    float fenv_attack;
    float fenv_decay;
    float fenv_sustain;
    float fenv_release;

    float aenv_attack;
    float aenv_decay;
    float aenv_sustain;
    float aenv_release;

    /* Master */
    float master_volume;
    bristol_noise_t noise;

    /* Pitch bend */
    float pitch_bend;
    float bend_range;

} obx_engine_t;

void obx_engine_init(obx_engine_t *engine);
void obx_engine_reset(obx_engine_t *engine);
void obx_engine_note_on(obx_engine_t *engine, int note, float velocity);
void obx_engine_note_off(obx_engine_t *engine, int note);
void obx_engine_pitch_bend(obx_engine_t *engine, float bend);
void obx_engine_render(obx_engine_t *engine, float *output, int frames);
void obx_engine_all_notes_off(obx_engine_t *engine);

#ifdef __cplusplus
}
#endif

#endif /* OBX_ENGINE_H */
