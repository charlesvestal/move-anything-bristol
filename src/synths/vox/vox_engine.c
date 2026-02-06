/*
 * vox_engine.c - Vox Continental Combo Organ engine
 */

#include "vox_engine.h"
#include <string.h>

static int find_voice(vox_engine_t *e) {
    for (int i = 0; i < VOX_MAX_VOICES; i++)
        if (!e->voices[i].active) return i;
    return 0;
}

static int find_voice_for_note(vox_engine_t *e, int note) {
    for (int i = 0; i < VOX_MAX_VOICES; i++)
        if (e->voices[i].active && e->voices[i].note == note) return i;
    return -1;
}

void vox_engine_init(vox_engine_t *e) {
    memset(e, 0, sizeof(vox_engine_t));
    e->sample_rate = BRISTOL_SAMPLE_RATE;

    for (int i = 0; i < VOX_MAX_VOICES; i++) {
        e->voices[i].phase = (double)i * 0.1;
    }

    e->drawbar_16 = 0.5f;
    e->drawbar_8 = 0.8f;
    e->drawbar_4 = 0.5f;
    e->drawbar_2 = 0.3f;
    e->drawbar_mix = 0.2f;
    e->vibrato_depth = 0.0f;
    e->vibrato_rate = 0.3f;
    e->percussion_decay = 0.3f;
    e->master_volume = 0.8f;
    e->bend_range = 2.0f;
}

void vox_engine_reset(vox_engine_t *e) {
    for (int i = 0; i < VOX_MAX_VOICES; i++) {
        e->voices[i].active = 0;
        e->voices[i].level = 0.0f;
    }
}

void vox_engine_note_on(vox_engine_t *e, int note, float velocity) {
    int vi = find_voice(e);
    vox_voice_t *v = &e->voices[vi];
    v->note = note;
    v->velocity = velocity;
    v->freq = midi_to_freq(note);
    v->active = 1;
    v->level = 1.0f;  /* Organ has instant attack */
}

void vox_engine_note_off(vox_engine_t *e, int note) {
    int vi = find_voice_for_note(e, note);
    if (vi >= 0) {
        e->voices[vi].active = 0;  /* Organ has instant release */
    }
}

void vox_engine_pitch_bend(vox_engine_t *e, float bend) { e->pitch_bend = bend; }

void vox_engine_all_notes_off(vox_engine_t *e) {
    for (int i = 0; i < VOX_MAX_VOICES; i++) {
        e->voices[i].active = 0;
    }
}

void vox_engine_render(vox_engine_t *e, float *output, int frames) {
    float sr = e->sample_rate;
    memset(output, 0, frames * sizeof(float));
    float bend_mult = powf(2.0f, e->pitch_bend * e->bend_range / 12.0f);

    /* Vibrato LFO */
    float vib_inc = e->vibrato_rate * 6.0f / sr;

    for (int vi = 0; vi < VOX_MAX_VOICES; vi++) {
        vox_voice_t *v = &e->voices[vi];
        if (!v->active) continue;

        for (int i = 0; i < frames; i++) {
            /* Vibrato */
            float vib = 0.0f;
            if (e->vibrato_depth > 0.01f) {
                e->vibrato_phase += vib_inc;
                if (e->vibrato_phase >= 1.0f) e->vibrato_phase -= 1.0f;
                vib = sinf(e->vibrato_phase * 6.283185f) * e->vibrato_depth * 0.02f;
            }

            float freq = v->freq * bend_mult * (1.0f + vib);
            float t = (float)v->phase;

            /* Organ tone generation using harmonics */
            float sample = 0.0f;

            /* Square-ish waveforms typical of combo organs */
            /* 16' (sub-octave) */
            sample += e->drawbar_16 * (sinf(t * 3.14159f) > 0 ? 0.5f : -0.5f);

            /* 8' (fundamental) */
            sample += e->drawbar_8 * (sinf(t * 6.28318f) > 0 ? 0.6f : -0.6f);

            /* 4' (octave) */
            sample += e->drawbar_4 * (sinf(t * 12.56637f) > 0 ? 0.4f : -0.4f);

            /* 2' (second octave) */
            sample += e->drawbar_2 * (sinf(t * 25.13274f) > 0 ? 0.3f : -0.3f);

            /* Mixture (multiple high harmonics) */
            sample += e->drawbar_mix * sinf(t * 37.7f) * 0.2f;
            sample += e->drawbar_mix * sinf(t * 50.27f) * 0.15f;

            /* Normalize */
            sample *= 0.3f;

            /* Advance phase */
            v->phase += freq / sr;
            if (v->phase >= 1.0) v->phase -= 1.0;

            output[i] += sample * v->velocity * e->master_volume;
        }
    }

    /* Simple waveshaping for character */
    for (int i = 0; i < frames; i++) output[i] = fast_tanh(output[i] * 1.2f);
}
