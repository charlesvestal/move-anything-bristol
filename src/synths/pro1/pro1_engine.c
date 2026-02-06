/*
 * pro1_engine.c - Sequential Pro-One style synthesizer engine
 */

#include "pro1_engine.h"
#include <string.h>

void pro1_engine_init(pro1_engine_t *e) {
    memset(e, 0, sizeof(pro1_engine_t));
    e->sample_rate = BRISTOL_SAMPLE_RATE;

    osc_init(&e->voice.osc_a, 0.0f);
    osc_init(&e->voice.osc_b, 0.05f);
    filter_init(&e->voice.filter);
    env_init(&e->voice.filter_env);
    env_init(&e->voice.amp_env);

    e->osc_a_octave = 1;
    e->osc_a_saw = 1;
    e->osc_a_pw = 0.5f;
    e->osc_b_octave = 1;
    e->osc_b_saw = 1;
    e->osc_b_pw = 0.5f;
    e->mix_osc_a = 0.5f;
    e->mix_osc_b = 0.5f;
    e->filter_cutoff = 0.7f;
    e->filter_env_amt = 0.5f;
    e->fenv_a = 0.01f; e->fenv_d = 0.3f; e->fenv_s = 0.5f; e->fenv_r = 0.3f;
    e->aenv_a = 0.01f; e->aenv_d = 0.3f; e->aenv_s = 0.7f; e->aenv_r = 0.3f;
    lfo_init(&e->lfo);
    noise_init(&e->noise);
    e->master_volume = 0.8f;
    e->bend_range = 2.0f;
}

void pro1_engine_reset(pro1_engine_t *e) {
    e->voice.active = 0;
    env_init(&e->voice.filter_env);
    env_init(&e->voice.amp_env);
    filter_init(&e->voice.filter);
}

void pro1_engine_note_on(pro1_engine_t *e, int note, float velocity) {
    pro1_voice_t *v = &e->voice;
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

void pro1_engine_note_off(pro1_engine_t *e, int note) {
    if (e->voice.note == note) {
        e->voice.gate = 0;
        env_gate_off(&e->voice.filter_env);
        env_gate_off(&e->voice.amp_env);
    }
}

void pro1_engine_pitch_bend(pro1_engine_t *e, float bend) {
    e->pitch_bend = bend;
}

void pro1_engine_mod_wheel(pro1_engine_t *e, float mod) {
    e->mod_wheel = mod;
}

void pro1_engine_all_notes_off(pro1_engine_t *e) {
    e->voice.active = 0;
    e->voice.gate = 0;
    env_gate_off(&e->voice.filter_env);
    env_gate_off(&e->voice.amp_env);
}

static const float octave_mult[] = { 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 0.25f };

void pro1_engine_render(pro1_engine_t *e, float *output, int frames) {
    float sr = e->sample_rate;
    memset(output, 0, frames * sizeof(float));

    pro1_voice_t *v = &e->voice;
    if (!v->active && v->amp_env.state == ENV_IDLE) return;

    float lfo_val = lfo_process(&e->lfo, sr);
    float mod_lfo = lfo_val * e->mod_wheel;
    float bend_mult = powf(2.0f, e->pitch_bend * e->bend_range / 12.0f);

    for (int i = 0; i < frames; i++) {
        float base_freq = v->freq * bend_mult;
        float lfo_mod = mod_lfo * e->lfo_to_osc * 0.1f;

        /* Osc A */
        float freq_a = base_freq * octave_mult[e->osc_a_octave % 6] * (1.0f + lfo_mod);
        float osc_a_out = 0.0f;
        if (e->osc_a_saw) {
            v->osc_a.wave = WAVE_SAW;
            osc_a_out += osc_generate(&v->osc_a, freq_a, sr);
        }
        if (e->osc_a_pulse) {
            float pw = e->osc_a_pw + mod_lfo * e->lfo_to_pw * 0.4f;
            pw = clampf(pw, 0.1f, 0.9f);
            osc_a_out += ((float)v->osc_a.phase < pw ? 1.0f : -1.0f) * 0.8f;
        }
        if (!e->osc_a_saw && !e->osc_a_pulse) {
            v->osc_a.phase += freq_a / sr;
            if (v->osc_a.phase >= 1.0) v->osc_a.phase -= 1.0;
        }

        /* Osc B */
        float freq_b = base_freq * octave_mult[e->osc_b_octave % 6] * (1.0f + e->osc_b_fine * 0.05f + lfo_mod);
        if (e->osc_sync && v->osc_a.phase < (freq_a / sr)) v->osc_b.phase = 0.0;
        float osc_b_out = 0.0f;
        if (e->osc_b_saw) {
            v->osc_b.wave = WAVE_SAW;
            osc_b_out += osc_generate(&v->osc_b, freq_b, sr);
        }
        if (e->osc_b_tri) {
            v->osc_b.wave = WAVE_TRI;
            osc_b_out += osc_generate(&v->osc_b, freq_b, sr) * 0.8f;
        }
        if (e->osc_b_pulse) {
            float pw = e->osc_b_pw + mod_lfo * e->lfo_to_pw * 0.4f;
            pw = clampf(pw, 0.1f, 0.9f);
            osc_b_out += ((float)v->osc_b.phase < pw ? 1.0f : -1.0f) * 0.8f;
        }
        if (!e->osc_b_saw && !e->osc_b_tri && !e->osc_b_pulse) {
            v->osc_b.phase += freq_b / sr;
            if (v->osc_b.phase >= 1.0) v->osc_b.phase -= 1.0;
        }

        /* Mix */
        float mix = osc_a_out * e->mix_osc_a + osc_b_out * e->mix_osc_b;
        mix += noise_white(&e->noise) * e->mix_noise;

        /* Envelopes */
        float fenv = env_process(&v->filter_env, v->gate, sr);
        float aenv = env_process(&v->amp_env, v->gate, sr);
        if (!v->gate && v->amp_env.state == ENV_IDLE) {
            v->active = 0;
            continue;
        }

        /* Filter */
        float filt_lfo = mod_lfo * e->lfo_to_filter * 0.2f;
        float filtered = filter_process(&v->filter, mix, fenv, filt_lfo, base_freq, sr);
        output[i] = filtered * aenv * v->velocity * e->master_volume;
    }

    for (int i = 0; i < frames; i++) output[i] = fast_tanh(output[i]);
}
