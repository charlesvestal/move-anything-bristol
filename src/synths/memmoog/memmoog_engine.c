/*
 * memmoog_engine.c - Memory Moog style synthesizer engine
 */

#include "memmoog_engine.h"
#include <string.h>

static int find_voice(memmoog_engine_t *e) {
    for (int i = 0; i < MEMMOOG_MAX_VOICES; i++)
        if (!e->voices[i].active) return i;
    return 0;
}

static int find_voice_for_note(memmoog_engine_t *e, int note) {
    for (int i = 0; i < MEMMOOG_MAX_VOICES; i++)
        if (e->voices[i].active && e->voices[i].note == note) return i;
    return -1;
}

void memmoog_engine_init(memmoog_engine_t *e) {
    memset(e, 0, sizeof(memmoog_engine_t));
    e->sample_rate = BRISTOL_SAMPLE_RATE;

    for (int i = 0; i < MEMMOOG_MAX_VOICES; i++) {
        memmoog_voice_t *v = &e->voices[i];
        osc_init(&v->osc1, i * 0.1f);
        osc_init(&v->osc2, i * 0.1f + 0.03f);
        osc_init(&v->osc3, i * 0.1f + 0.06f);
        filter_init(&v->filter);
        env_init(&v->filter_env);
        env_init(&v->amp_env);
    }

    e->osc1_octave = 1; e->osc1_wave = 1; e->osc1_pw = 0.5f;
    e->osc2_octave = 1; e->osc2_wave = 1; e->osc2_pw = 0.5f;
    e->osc3_octave = 1; e->osc3_wave = 1; e->osc3_pw = 0.5f;
    e->mix_osc1 = 0.5f; e->mix_osc2 = 0.5f; e->mix_osc3 = 0.3f;
    e->filter_cutoff = 0.7f; e->filter_env_amt = 0.5f;
    e->fenv_a = 0.01f; e->fenv_d = 0.3f; e->fenv_s = 0.5f; e->fenv_r = 0.3f;
    e->aenv_a = 0.01f; e->aenv_d = 0.3f; e->aenv_s = 0.7f; e->aenv_r = 0.3f;
    lfo_init(&e->lfo);
    noise_init(&e->noise);
    e->master_volume = 0.8f;
    e->bend_range = 2.0f;
}

void memmoog_engine_reset(memmoog_engine_t *e) {
    for (int i = 0; i < MEMMOOG_MAX_VOICES; i++) {
        memmoog_voice_t *v = &e->voices[i];
        v->active = 0;
        env_init(&v->filter_env);
        env_init(&v->amp_env);
        filter_init(&v->filter);
    }
}

void memmoog_engine_note_on(memmoog_engine_t *e, int note, float velocity) {
    int vi = find_voice(e);
    memmoog_voice_t *v = &e->voices[vi];
    v->note = note; v->velocity = velocity; v->freq = midi_to_freq(note); v->active = 1;

    v->filter_env.attack = e->fenv_a; v->filter_env.decay = e->fenv_d;
    v->filter_env.sustain = e->fenv_s; v->filter_env.release = e->fenv_r;
    v->amp_env.attack = e->aenv_a; v->amp_env.decay = e->aenv_d;
    v->amp_env.sustain = e->aenv_s; v->amp_env.release = e->aenv_r;
    v->filter.cutoff = e->filter_cutoff; v->filter.resonance = e->filter_resonance;
    v->filter.env_amount = e->filter_env_amt;
    v->filter.key_track = e->filter_kbd_track;

    env_gate_on(&v->filter_env);
    env_gate_on(&v->amp_env);
}

void memmoog_engine_note_off(memmoog_engine_t *e, int note) {
    int vi = find_voice_for_note(e, note);
    if (vi >= 0) {
        env_gate_off(&e->voices[vi].filter_env);
        env_gate_off(&e->voices[vi].amp_env);
    }
}

void memmoog_engine_pitch_bend(memmoog_engine_t *e, float bend) { e->pitch_bend = bend; }

void memmoog_engine_all_notes_off(memmoog_engine_t *e) {
    for (int i = 0; i < MEMMOOG_MAX_VOICES; i++) {
        e->voices[i].active = 0;
        env_gate_off(&e->voices[i].filter_env);
        env_gate_off(&e->voices[i].amp_env);
    }
}

static const float octave_mult[] = { 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 0.25f };

static float gen_osc_wave(bristol_osc_t *osc, int wave, float pw, float freq, float sr) {
    float out = 0.0f;
    switch (wave) {
        case 0: osc->wave = WAVE_TRI; out = osc_generate(osc, freq, sr); break;
        case 1: osc->wave = WAVE_SAW; out = osc_generate(osc, freq, sr); break;
        case 2: out = ((float)osc->phase < pw ? 1.0f : -1.0f) * 0.8f;
                osc->phase += freq / sr; if (osc->phase >= 1.0) osc->phase -= 1.0; break;
        default: osc->wave = WAVE_SAW; out = osc_generate(osc, freq, sr); break;
    }
    return out;
}

void memmoog_engine_render(memmoog_engine_t *e, float *output, int frames) {
    float sr = e->sample_rate;
    memset(output, 0, frames * sizeof(float));
    float lfo_val = lfo_process(&e->lfo, sr);
    float bend_mult = powf(2.0f, e->pitch_bend * e->bend_range / 12.0f);

    for (int vi = 0; vi < MEMMOOG_MAX_VOICES; vi++) {
        memmoog_voice_t *v = &e->voices[vi];
        if (!v->active && v->amp_env.state == ENV_IDLE) continue;

        for (int i = 0; i < frames; i++) {
            float base_freq = v->freq * bend_mult;
            float lfo_mod = lfo_val * e->lfo_to_osc * 0.1f;

            float freq1 = base_freq * octave_mult[e->osc1_octave % 6] * (1.0f + lfo_mod);
            float freq2 = base_freq * octave_mult[e->osc2_octave % 6] * (1.0f + e->osc2_detune * 0.05f + lfo_mod);
            float freq3 = base_freq * octave_mult[e->osc3_octave % 6] * (1.0f + e->osc3_detune * 0.05f + lfo_mod);

            float pw1 = e->osc1_pw + lfo_val * e->lfo_to_pw * 0.4f;
            float pw2 = e->osc2_pw + lfo_val * e->lfo_to_pw * 0.4f;
            float pw3 = e->osc3_pw + lfo_val * e->lfo_to_pw * 0.4f;

            float osc1_out = gen_osc_wave(&v->osc1, e->osc1_wave, clampf(pw1, 0.1f, 0.9f), freq1, sr);
            float osc2_out = gen_osc_wave(&v->osc2, e->osc2_wave, clampf(pw2, 0.1f, 0.9f), freq2, sr);
            float osc3_out = gen_osc_wave(&v->osc3, e->osc3_wave, clampf(pw3, 0.1f, 0.9f), freq3, sr);

            float mix = osc1_out * e->mix_osc1 + osc2_out * e->mix_osc2 + osc3_out * e->mix_osc3;
            mix += noise_white(&e->noise) * e->mix_noise;

            float fenv = env_process(&v->filter_env, v->active, sr);
            float aenv = env_process(&v->amp_env, v->active, sr);
            if (!v->active && v->amp_env.state == ENV_IDLE) continue;

            float filt_lfo = lfo_val * e->lfo_to_filter * 0.2f;
            float filtered = filter_process(&v->filter, mix, fenv, filt_lfo, base_freq, sr);
            output[i] += filtered * aenv * v->velocity * e->master_volume;
        }
    }

    for (int i = 0; i < frames; i++) output[i] = fast_tanh(output[i]);
}
