/*
 * rhodes_engine.c - Rhodes Electric Piano synthesizer engine
 */

#include "rhodes_engine.h"
#include <string.h>

static int find_voice(rhodes_engine_t *e) {
    for (int i = 0; i < RHODES_MAX_VOICES; i++)
        if (!e->voices[i].active) return i;
    return 0;
}

static int find_voice_for_note(rhodes_engine_t *e, int note) {
    for (int i = 0; i < RHODES_MAX_VOICES; i++)
        if (e->voices[i].active && e->voices[i].note == note) return i;
    return -1;
}

void rhodes_engine_init(rhodes_engine_t *e) {
    memset(e, 0, sizeof(rhodes_engine_t));
    e->sample_rate = BRISTOL_SAMPLE_RATE;

    for (int i = 0; i < RHODES_MAX_VOICES; i++) {
        rhodes_voice_t *v = &e->voices[i];
        v->phase = (double)i * 0.1;
        v->phase2 = (double)i * 0.15;
        env_init(&v->amp_env);
    }

    e->tine_level = 0.6f;
    e->tone_level = 0.4f;
    e->brightness = 0.5f;
    e->decay = 0.5f;
    e->release = 0.3f;
    e->tremolo_depth = 0.0f;
    e->tremolo_rate = 0.3f;
    e->master_volume = 0.8f;
    e->bend_range = 2.0f;
}

void rhodes_engine_reset(rhodes_engine_t *e) {
    for (int i = 0; i < RHODES_MAX_VOICES; i++) {
        e->voices[i].active = 0;
        env_init(&e->voices[i].amp_env);
    }
}

void rhodes_engine_note_on(rhodes_engine_t *e, int note, float velocity) {
    int vi = find_voice(e);
    rhodes_voice_t *v = &e->voices[vi];
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

void rhodes_engine_note_off(rhodes_engine_t *e, int note) {
    int vi = find_voice_for_note(e, note);
    if (vi >= 0) {
        env_gate_off(&e->voices[vi].amp_env);
    }
}

void rhodes_engine_pitch_bend(rhodes_engine_t *e, float bend) { e->pitch_bend = bend; }

void rhodes_engine_all_notes_off(rhodes_engine_t *e) {
    for (int i = 0; i < RHODES_MAX_VOICES; i++) {
        e->voices[i].active = 0;
        env_gate_off(&e->voices[i].amp_env);
    }
}

void rhodes_engine_render(rhodes_engine_t *e, float *output, int frames) {
    float sr = e->sample_rate;
    memset(output, 0, frames * sizeof(float));
    float bend_mult = powf(2.0f, e->pitch_bend * e->bend_range / 12.0f);

    /* Tremolo LFO */
    float trem_inc = e->tremolo_rate * 8.0f / sr;

    for (int vi = 0; vi < RHODES_MAX_VOICES; vi++) {
        rhodes_voice_t *v = &e->voices[vi];
        if (!v->active && v->amp_env.state == ENV_IDLE) continue;

        for (int i = 0; i < frames; i++) {
            float freq = v->freq * bend_mult;

            /* Simple FM synthesis for Rhodes-like tone */
            /* Tine: high partial with fast decay */
            /* Tone: fundamental with slower decay */
            float t = (float)v->phase;

            /* Modulator for tine "ping" */
            float mod = sinf(t * 6.283185f * 7.0f) * e->tine_level * (1.0f - t * 0.5f);
            mod = clampf(mod, -1.0f, 1.0f);

            /* Carrier - fundamental with slight modulation */
            float carrier = sinf((t + mod * 0.3f * e->brightness) * 6.283185f);

            /* Blend tine and tone */
            float sample = carrier * e->tone_level + sinf(t * 6.283185f * 2.0f) * e->tine_level * 0.3f;

            /* Advance phase */
            v->phase += freq / sr;
            if (v->phase >= 1.0) v->phase -= 1.0;

            /* Envelope */
            float aenv = env_process(&v->amp_env, v->active, sr);
            if (!v->active && v->amp_env.state == ENV_IDLE) continue;

            /* Tremolo */
            float trem = 1.0f;
            if (e->tremolo_depth > 0.01f) {
                e->tremolo_phase += trem_inc;
                if (e->tremolo_phase >= 1.0f) e->tremolo_phase -= 1.0f;
                trem = 1.0f - e->tremolo_depth * 0.5f * (1.0f + sinf(e->tremolo_phase * 6.283185f));
            }

            output[i] += sample * aenv * v->velocity * trem * e->master_volume;
        }
    }

    for (int i = 0; i < frames; i++) output[i] = fast_tanh(output[i]);
}
