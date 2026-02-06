/*
 * odyssey_engine.h - ARP Odyssey style synthesizer engine
 *
 * Duophonic analog synth with two oscillators.
 */

#ifndef ODYSSEY_ENGINE_H
#define ODYSSEY_ENGINE_H

#include "../../shared/bristol_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float sample_rate;

    /* Two oscillators */
    bristol_osc_t osc1;
    bristol_osc_t osc2;
    int osc1_octave;
    int osc2_octave;
    float osc2_detune;
    int osc1_saw;
    int osc1_pulse;
    int osc2_saw;
    int osc2_pulse;
    float pulse_width;

    /* Mixer */
    float mix_osc1;
    float mix_osc2;
    float mix_noise;

    /* Filter */
    bristol_filter_t filter;

    /* Envelopes */
    bristol_env_t filter_env;
    bristol_env_t amp_env;

    /* LFO */
    bristol_lfo_t lfo;
    float lfo_to_osc;
    float lfo_to_filter;
    float lfo_to_pw;

    /* Other */
    float glide;
    float master_volume;
    bristol_noise_t noise;

    /* Voice state */
    int gate;
    int current_note;
    float current_freq;
    float target_freq;
    float glide_freq;
    float velocity;

    int key_stack[16];
    int key_stack_count;

    float pitch_bend;
    float bend_range;

} odyssey_engine_t;

void odyssey_engine_init(odyssey_engine_t *engine);
void odyssey_engine_reset(odyssey_engine_t *engine);
void odyssey_engine_note_on(odyssey_engine_t *engine, int note, float velocity);
void odyssey_engine_note_off(odyssey_engine_t *engine, int note);
void odyssey_engine_pitch_bend(odyssey_engine_t *engine, float bend);
void odyssey_engine_render(odyssey_engine_t *engine, float *output, int frames);
void odyssey_engine_all_notes_off(odyssey_engine_t *engine);

#ifdef __cplusplus
}
#endif

#endif
