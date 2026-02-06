/*
 * axxe_engine.c - ARP Axxe style synthesizer engine
 */

#include "axxe_engine.h"
#include <string.h>

void axxe_engine_init(axxe_engine_t *e) {
    memset(e, 0, sizeof(axxe_engine_t));
    e->sample_rate = BRISTOL_SAMPLE_RATE;

    osc_init(&e->voice.osc, 0.0f);
    filter_init(&e->voice.filter);
    env_init(&e->voice.filter_env);
    env_init(&e->voice.amp_env);

    e->osc_octave = 1;
    e->osc_saw = 1;
    e->osc_pw = 0.5f;
    e->filter_cutoff = 0.7f;
    e->filter_env_amt = 0.5f;
    e->fenv_a = 0.01f; e->fenv_d = 0.3f; e->fenv_s = 0.5f; e->fenv_r = 0.3f;
    e->aenv_a = 0.01f; e->aenv_d = 0.3f; e->aenv_s = 0.7f; e->aenv_r = 0.3f;
    lfo_init(&e->lfo);
    noise_init(&e->noise);
    e->master_volume = 0.8f;
    e->bend_range = 2.0f;
}

void axxe_engine_reset(axxe_engine_t *e) {
    e->voice.active = 0;
    env_init(&e->voice.filter_env);
    env_init(&e->voice.amp_env);
    filter_init(&e->voice.filter);
}

void axxe_engine_note_on(axxe_engine_t *e, int note, float velocity) {
    axxe_voice_t *v = &e->voice;
    v->note = note;
    v->velocity = velocity;
    v->freq = midi_to_freq(note);
    v->active = 1;
    v->gate = 1;

    v->filter_env.attack = e->fenv_a;
    v->filter_env.decay = e->fenv_d;
    v->filter_env.sustain = e->fenv_s;
    v->filter_env.release = e->fenv_r;
    v->amp_env.attack = e->aenv_a;
    v->amp_env.decay = e->aenv_d;
    v->amp_env.sustain = e->aenv_s;
    v->amp_env.release = e->aenv_r;
    v->filter.cutoff = e->filter_cutoff;
    v->filter.resonance = e->filter_resonance;
    v->filter.env_amount = e->filter_env_amt;
    v->filter.key_track = e->filter_kbd_track;

    env_gate_on(&v->filter_env);
    env_gate_on(&v->amp_env);
}

void axxe_engine_note_off(axxe_engine_t *e, int note) {
    if (e->voice.note == note) {
        e->voice.gate = 0;
        env_gate_off(&e->voice.filter_env);
        env_gate_off(&e->voice.amp_env);
    }
}

void axxe_engine_pitch_bend(axxe_engine_t *e, float bend) {
    e->pitch_bend = bend;
}

void axxe_engine_all_notes_off(axxe_engine_t *e) {
    e->voice.active = 0;
    e->voice.gate = 0;
    env_gate_off(&e->voice.filter_env);
    env_gate_off(&e->voice.amp_env);
}

static const float octave_mult[] = { 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 0.25f };

void axxe_engine_render(axxe_engine_t *e, float *output, int frames) {
    float sr = e->sample_rate;
    memset(output, 0, frames * sizeof(float));

    axxe_voice_t *v = &e->voice;
    if (!v->active && v->amp_env.state == ENV_IDLE) return;

    float lfo_val = lfo_process(&e->lfo, sr);
    float bend_mult = powf(2.0f, e->pitch_bend * e->bend_range / 12.0f);

    for (int i = 0; i < frames; i++) {
        float base_freq = v->freq * bend_mult;
        float lfo_mod = lfo_val * e->lfo_to_osc * 0.1f;
        float freq = base_freq * octave_mult[e->osc_octave % 6] * (1.0f + lfo_mod);

        float osc_out = 0.0f;
        if (e->osc_saw) {
            v->osc.wave = WAVE_SAW;
            osc_out += osc_generate(&v->osc, freq, sr);
        }
        if (e->osc_pulse) {
            float pw = e->osc_pw + lfo_val * e->lfo_to_pw * 0.4f;
            pw = clampf(pw, 0.1f, 0.9f);
            osc_out += ((float)v->osc.phase < pw ? 1.0f : -1.0f) * 0.8f;
        }
        if (e->osc_square) {
            osc_out += ((float)v->osc.phase < 0.5f ? 1.0f : -1.0f) * 0.8f;
        }
        if (!e->osc_saw && !e->osc_pulse && !e->osc_square) {
            v->osc.phase += freq / sr;
            if (v->osc.phase >= 1.0) v->osc.phase -= 1.0;
        }

        osc_out += noise_white(&e->noise) * e->noise_level;

        float fenv = env_process(&v->filter_env, v->gate, sr);
        float aenv = env_process(&v->amp_env, v->gate, sr);
        if (!v->gate && v->amp_env.state == ENV_IDLE) {
            v->active = 0;
            continue;
        }

        float filt_lfo = lfo_val * e->lfo_to_filter * 0.2f;
        float filtered = filter_process(&v->filter, osc_out, fenv, filt_lfo, base_freq, sr);
        output[i] = filtered * aenv * v->velocity * e->master_volume;
    }

    for (int i = 0; i < frames; i++) output[i] = fast_tanh(output[i]);
}
