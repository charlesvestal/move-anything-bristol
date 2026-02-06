/*
 * odyssey_engine.c - ARP Odyssey style synthesizer engine
 */

#include "odyssey_engine.h"
#include <string.h>

void odyssey_engine_init(odyssey_engine_t *e) {
    memset(e, 0, sizeof(odyssey_engine_t));
    e->sample_rate = BRISTOL_SAMPLE_RATE;

    osc_init(&e->osc1, 0.0f);
    osc_init(&e->osc2, 0.1f);
    filter_init(&e->filter);
    env_init(&e->filter_env);
    env_init(&e->amp_env);
    lfo_init(&e->lfo);
    noise_init(&e->noise);

    e->osc1_octave = 1;
    e->osc2_octave = 1;
    e->osc1_saw = 1;
    e->osc2_saw = 1;
    e->pulse_width = 0.5f;

    e->mix_osc1 = 0.5f;
    e->mix_osc2 = 0.5f;

    e->filter.cutoff = 0.7f;
    e->filter.env_amount = 0.5f;

    e->filter_env.attack = 0.01f;
    e->filter_env.decay = 0.3f;
    e->filter_env.sustain = 0.5f;
    e->filter_env.release = 0.3f;

    e->amp_env.attack = 0.01f;
    e->amp_env.decay = 0.3f;
    e->amp_env.sustain = 0.7f;
    e->amp_env.release = 0.3f;

    e->master_volume = 0.8f;
    e->bend_range = 2.0f;
    e->current_freq = midi_to_freq(60);
    e->target_freq = e->current_freq;
    e->glide_freq = e->current_freq;
}

void odyssey_engine_reset(odyssey_engine_t *e) {
    e->gate = 0;
    e->key_stack_count = 0;
    env_init(&e->filter_env);
    env_init(&e->amp_env);
    e->osc1.phase = 0.0;
    e->osc2.phase = 0.0;
    filter_init(&e->filter);
}

void odyssey_engine_note_on(odyssey_engine_t *e, int note, float velocity) {
    if (e->key_stack_count < 16) e->key_stack[e->key_stack_count++] = note;
    e->current_note = note;
    e->target_freq = midi_to_freq(note);
    e->velocity = velocity;
    if (e->glide <= 0.001f || !e->gate) e->glide_freq = e->target_freq;
    if (!e->gate) {
        env_gate_on(&e->filter_env);
        env_gate_on(&e->amp_env);
    }
    e->gate = 1;
}

void odyssey_engine_note_off(odyssey_engine_t *e, int note) {
    for (int i = 0; i < e->key_stack_count; i++) {
        if (e->key_stack[i] == note) {
            for (int j = i; j < e->key_stack_count - 1; j++) e->key_stack[j] = e->key_stack[j + 1];
            e->key_stack_count--;
            break;
        }
    }
    if (e->key_stack_count == 0) {
        e->gate = 0;
        env_gate_off(&e->filter_env);
        env_gate_off(&e->amp_env);
    } else {
        int new_note = e->key_stack[e->key_stack_count - 1];
        e->current_note = new_note;
        e->target_freq = midi_to_freq(new_note);
    }
}

void odyssey_engine_pitch_bend(odyssey_engine_t *e, float bend) { e->pitch_bend = bend; }
void odyssey_engine_all_notes_off(odyssey_engine_t *e) {
    e->gate = 0;
    e->key_stack_count = 0;
    env_gate_off(&e->filter_env);
    env_gate_off(&e->amp_env);
}

static const float octave_mult[] = { 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 0.25f };

void odyssey_engine_render(odyssey_engine_t *e, float *output, int frames) {
    float sr = e->sample_rate;

    for (int i = 0; i < frames; i++) {
        /* Glide */
        if (e->glide > 0.001f) {
            float rate = 1.0f - expf(-1.0f / (e->glide * sr * 0.5f));
            e->glide_freq += (e->target_freq - e->glide_freq) * rate;
        } else {
            e->glide_freq = e->target_freq;
        }

        float bend_mult = powf(2.0f, e->pitch_bend * e->bend_range / 12.0f);
        float base_freq = e->glide_freq * bend_mult;
        float lfo_out = lfo_process(&e->lfo, sr);

        /* Osc 1 */
        float freq1 = base_freq * octave_mult[e->osc1_octave % 6];
        freq1 *= 1.0f + lfo_out * e->lfo_to_osc * 0.1f;
        float osc1_out = 0.0f;
        if (e->osc1_saw) {
            e->osc1.wave = WAVE_SAW;
            osc1_out += osc_generate(&e->osc1, freq1, sr);
        }
        if (e->osc1_pulse) {
            float pw = e->pulse_width + lfo_out * e->lfo_to_pw * 0.4f;
            pw = clampf(pw, 0.1f, 0.9f);
            float t = (float)e->osc1.phase;
            osc1_out += (t < pw ? 1.0f : -1.0f) * 0.8f;
        }

        /* Osc 2 */
        float freq2 = base_freq * octave_mult[e->osc2_octave % 6] * (1.0f + e->osc2_detune * 0.05f);
        freq2 *= 1.0f + lfo_out * e->lfo_to_osc * 0.1f;
        float osc2_out = 0.0f;
        if (e->osc2_saw) {
            e->osc2.wave = WAVE_SAW;
            osc2_out += osc_generate(&e->osc2, freq2, sr);
        }
        if (e->osc2_pulse) {
            float pw = e->pulse_width + lfo_out * e->lfo_to_pw * 0.4f;
            pw = clampf(pw, 0.1f, 0.9f);
            float t = (float)e->osc2.phase;
            osc2_out += (t < pw ? 1.0f : -1.0f) * 0.8f;
        }

        /* Mixer */
        float mix = osc1_out * e->mix_osc1 + osc2_out * e->mix_osc2;
        if (e->mix_noise > 0.0f) mix += noise_white(&e->noise) * e->mix_noise;

        /* Envelopes */
        float fenv = env_process(&e->filter_env, e->gate, sr);
        float aenv = env_process(&e->amp_env, e->gate, sr);

        /* Filter */
        float filt_lfo = lfo_out * e->lfo_to_filter;
        float filtered = filter_process(&e->filter, mix, fenv, filt_lfo, base_freq, sr);

        /* VCA */
        float sample = filtered * aenv * e->velocity * e->master_volume;
        output[i] = fast_tanh(sample);
    }
}
