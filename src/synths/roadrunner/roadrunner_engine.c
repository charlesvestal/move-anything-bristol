/*
 * roadrunner_engine.c - Crumar Roadrunner Electric Piano engine
 */

#include "roadrunner_engine.h"
#include <string.h>

static int find_voice(roadrunner_engine_t *e) {
    for (int i = 0; i < ROADRUNNER_MAX_VOICES; i++)
        if (!e->voices[i].active) return i;
    return 0;
}

static int find_voice_for_note(roadrunner_engine_t *e, int note) {
    for (int i = 0; i < ROADRUNNER_MAX_VOICES; i++)
        if (e->voices[i].active && e->voices[i].note == note) return i;
    return -1;
}

void roadrunner_engine_init(roadrunner_engine_t *e) {
    memset(e, 0, sizeof(roadrunner_engine_t));
    e->sample_rate = BRISTOL_SAMPLE_RATE;

    for (int i = 0; i < ROADRUNNER_MAX_VOICES; i++) {
        roadrunner_voice_t *v = &e->voices[i];
        v->phase = (double)i * 0.1;
        env_init(&v->amp_env);
    }

    e->brightness = 0.5f;
    e->decay = 0.5f;
    e->release = 0.3f;
    e->vibrato_depth = 0.0f;
    e->vibrato_rate = 0.3f;
    e->master_volume = 0.8f;
    e->bend_range = 2.0f;
}

void roadrunner_engine_reset(roadrunner_engine_t *e) {
    for (int i = 0; i < ROADRUNNER_MAX_VOICES; i++) {
        e->voices[i].active = 0;
        env_init(&e->voices[i].amp_env);
    }
}

void roadrunner_engine_note_on(roadrunner_engine_t *e, int note, float velocity) {
    int vi = find_voice(e);
    roadrunner_voice_t *v = &e->voices[vi];
    v->note = note;
    v->velocity = velocity;
    v->freq = midi_to_freq(note);
    v->active = 1;

    v->amp_env.attack = 0.001f;
    v->amp_env.decay = e->decay;
    v->amp_env.sustain = 0.0f;
    v->amp_env.release = e->release;

    env_gate_on(&v->amp_env);
}

void roadrunner_engine_note_off(roadrunner_engine_t *e, int note) {
    int vi = find_voice_for_note(e, note);
    if (vi >= 0) {
        env_gate_off(&e->voices[vi].amp_env);
    }
}

void roadrunner_engine_pitch_bend(roadrunner_engine_t *e, float bend) { e->pitch_bend = bend; }

void roadrunner_engine_all_notes_off(roadrunner_engine_t *e) {
    for (int i = 0; i < ROADRUNNER_MAX_VOICES; i++) {
        e->voices[i].active = 0;
        env_gate_off(&e->voices[i].amp_env);
    }
}

void roadrunner_engine_render(roadrunner_engine_t *e, float *output, int frames) {
    float sr = e->sample_rate;
    memset(output, 0, frames * sizeof(float));
    float bend_mult = powf(2.0f, e->pitch_bend * e->bend_range / 12.0f);

    /* Vibrato LFO */
    float vib_inc = e->vibrato_rate * 6.0f / sr;

    for (int vi = 0; vi < ROADRUNNER_MAX_VOICES; vi++) {
        roadrunner_voice_t *v = &e->voices[vi];
        if (!v->active && v->amp_env.state == ENV_IDLE) continue;

        for (int i = 0; i < frames; i++) {
            /* Vibrato */
            float vib = 0.0f;
            if (e->vibrato_depth > 0.01f) {
                e->vibrato_phase += vib_inc;
                if (e->vibrato_phase >= 1.0f) e->vibrato_phase -= 1.0f;
                vib = sinf(e->vibrato_phase * 6.283185f) * e->vibrato_depth * 0.02f;
            }

            float freq = v->freq * bend_mult * (1.0f + vib);

            /* Simple additive synthesis for EP tone */
            float t = (float)v->phase;
            float sample = sinf(t * 6.283185f);  /* Fundamental */
            sample += sinf(t * 6.283185f * 2.0f) * 0.5f * e->brightness;  /* 2nd harmonic */
            sample += sinf(t * 6.283185f * 3.0f) * 0.25f * e->brightness;  /* 3rd harmonic */
            sample *= 0.5f;

            /* Advance phase */
            v->phase += freq / sr;
            if (v->phase >= 1.0) v->phase -= 1.0;

            /* Envelope */
            float aenv = env_process(&v->amp_env, v->active, sr);
            if (v->amp_env.state == ENV_IDLE) {
                v->active = 0;
                continue;
            }

            output[i] += sample * aenv * v->velocity * e->master_volume;
        }
    }

    for (int i = 0; i < frames; i++) output[i] = fast_tanh(output[i]);
}
