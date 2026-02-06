/*
 * obx_engine.c - Oberheim OB-X style synthesizer engine
 *
 * 8-voice polyphonic analog synth.
 */

#include "obx_engine.h"
#include <string.h>

static int find_voice(obx_engine_t *engine) {
    for (int i = 0; i < OBX_MAX_VOICES; i++) {
        if (!engine->voices[i].active) return i;
    }
    return 0;  /* Steal voice 0 */
}

static int find_voice_for_note(obx_engine_t *engine, int note) {
    for (int i = 0; i < OBX_MAX_VOICES; i++) {
        if (engine->voices[i].active && engine->voices[i].note == note) return i;
    }
    return -1;
}

void obx_engine_init(obx_engine_t *engine) {
    memset(engine, 0, sizeof(obx_engine_t));
    engine->sample_rate = BRISTOL_SAMPLE_RATE;

    for (int i = 0; i < OBX_MAX_VOICES; i++) {
        obx_voice_t *v = &engine->voices[i];
        osc_init(&v->osc1, i * 0.1f);
        osc_init(&v->osc2, i * 0.1f + 0.05f);
        filter_init(&v->filter);
        env_init(&v->filter_env);
        env_init(&v->amp_env);
    }

    engine->osc1_octave = 1;
    engine->osc1_saw = 1;
    engine->osc_pw = 0.5f;
    engine->osc2_saw = 1;

    engine->filter_cutoff = 0.7f;
    engine->mix_osc1 = 1;
    engine->mix_osc2 = 1;

    engine->fenv_attack = 0.01f;
    engine->fenv_decay = 0.3f;
    engine->fenv_sustain = 0.5f;
    engine->fenv_release = 0.3f;

    engine->aenv_attack = 0.01f;
    engine->aenv_decay = 0.3f;
    engine->aenv_sustain = 0.7f;
    engine->aenv_release = 0.3f;

    lfo_init(&engine->lfo);
    noise_init(&engine->noise);

    engine->master_volume = 0.8f;
    engine->bend_range = 2.0f;
}

void obx_engine_reset(obx_engine_t *engine) {
    for (int i = 0; i < OBX_MAX_VOICES; i++) {
        obx_voice_t *v = &engine->voices[i];
        v->active = 0;
        env_init(&v->filter_env);
        env_init(&v->amp_env);
        filter_init(&v->filter);
    }
}

void obx_engine_note_on(obx_engine_t *engine, int note, float velocity) {
    int vi = find_voice(engine);
    obx_voice_t *v = &engine->voices[vi];

    v->note = note;
    v->velocity = velocity;
    v->freq = midi_to_freq(note);
    v->active = 1;

    v->filter_env.attack = engine->fenv_attack;
    v->filter_env.decay = engine->fenv_decay;
    v->filter_env.sustain = engine->fenv_sustain;
    v->filter_env.release = engine->fenv_release;

    v->amp_env.attack = engine->aenv_attack;
    v->amp_env.decay = engine->aenv_decay;
    v->amp_env.sustain = engine->aenv_sustain;
    v->amp_env.release = engine->aenv_release;

    v->filter.cutoff = engine->filter_cutoff;
    v->filter.resonance = engine->filter_resonance;
    v->filter.env_amount = engine->filter_env_amt;
    v->filter.key_track = engine->filter_kbd ? 0.5f : 0.0f;

    env_gate_on(&v->filter_env);
    env_gate_on(&v->amp_env);
}

void obx_engine_note_off(obx_engine_t *engine, int note) {
    int vi = find_voice_for_note(engine, note);
    if (vi >= 0) {
        obx_voice_t *v = &engine->voices[vi];
        env_gate_off(&v->filter_env);
        env_gate_off(&v->amp_env);
    }
}

void obx_engine_pitch_bend(obx_engine_t *engine, float bend) {
    engine->pitch_bend = bend;
}

void obx_engine_all_notes_off(obx_engine_t *engine) {
    for (int i = 0; i < OBX_MAX_VOICES; i++) {
        engine->voices[i].active = 0;
        env_gate_off(&engine->voices[i].filter_env);
        env_gate_off(&engine->voices[i].amp_env);
    }
}

static const float octave_mult[] = { 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 0.25f };

void obx_engine_render(obx_engine_t *engine, float *output, int frames) {
    float sr = engine->sample_rate;
    memset(output, 0, frames * sizeof(float));

    float lfo_val = lfo_process(&engine->lfo, sr);
    float bend_mult = powf(2.0f, engine->pitch_bend * engine->bend_range / 12.0f);

    for (int vi = 0; vi < OBX_MAX_VOICES; vi++) {
        obx_voice_t *v = &engine->voices[vi];
        if (!v->active && v->amp_env.state == ENV_IDLE) continue;

        for (int i = 0; i < frames; i++) {
            float base_freq = v->freq * bend_mult;

            /* LFO modulation */
            float lfo_fm = engine->lfo_fm_depth * lfo_val * 0.1f;
            float lfo_pwm = engine->lfo_pwm_depth * lfo_val * 0.4f;

            /* Osc 1 */
            float freq1 = base_freq * octave_mult[engine->osc1_octave % 6];
            if (engine->lfo_fm_osc1) freq1 *= (1.0f + lfo_fm);

            float osc1_out = 0.0f;
            if (engine->osc1_saw) {
                v->osc1.wave = WAVE_SAW;
                osc1_out += osc_generate(&v->osc1, freq1, sr);
            }
            if (engine->osc1_pulse) {
                float pw = engine->osc_pw;
                if (engine->lfo_pwm_osc1) pw += lfo_pwm;
                pw = clampf(pw, 0.1f, 0.9f);
                float t = (float)v->osc1.phase;
                osc1_out += (t < pw ? 1.0f : -1.0f) * 0.8f;
            }
            if (!engine->osc1_saw && !engine->osc1_pulse) {
                v->osc1.phase += freq1 / sr;
                if (v->osc1.phase >= 1.0) v->osc1.phase -= 1.0;
            }

            /* Osc 2 */
            float freq2 = base_freq * (1.0f + engine->osc2_detune * 0.05f);
            freq2 *= 1.0f + engine->osc2_tune * 0.02f;
            if (engine->lfo_fm_osc2) freq2 *= (1.0f + lfo_fm);

            if (engine->osc_sync && v->osc1.phase < (freq1 / sr)) {
                v->osc2.phase = 0.0;
            }

            float osc2_out = 0.0f;
            if (engine->osc2_saw) {
                v->osc2.wave = WAVE_SAW;
                osc2_out += osc_generate(&v->osc2, freq2, sr);
            }
            if (engine->osc2_pulse) {
                float pw = engine->osc_pw;
                if (engine->lfo_pwm_osc2) pw += lfo_pwm;
                pw = clampf(pw, 0.1f, 0.9f);
                float t = (float)v->osc2.phase;
                osc2_out += (t < pw ? 1.0f : -1.0f) * 0.8f;
            }
            if (!engine->osc2_saw && !engine->osc2_pulse) {
                v->osc2.phase += freq2 / sr;
                if (v->osc2.phase >= 1.0) v->osc2.phase -= 1.0;
            }

            /* Mixer */
            float mix = 0.0f;
            if (engine->mix_osc1) mix += osc1_out;
            if (engine->mix_osc2) mix += osc2_out;
            if (engine->mix_noise) mix += noise_white(&engine->noise) * 0.5f;

            /* Envelopes */
            float fenv = env_process(&v->filter_env, v->active, sr);
            float aenv = env_process(&v->amp_env, v->active, sr);

            if (!v->active && v->amp_env.state == ENV_IDLE) continue;

            /* Filter with LFO mod */
            float filt_lfo = engine->lfo_fm_filter ? lfo_val * 0.2f : 0.0f;
            float filtered = filter_process(&v->filter, mix, fenv, filt_lfo, base_freq, sr);

            /* VCA */
            float sample = filtered * aenv * v->velocity * engine->master_volume;
            output[i] += sample;
        }
    }

    for (int i = 0; i < frames; i++) {
        output[i] = fast_tanh(output[i]);
    }
}
