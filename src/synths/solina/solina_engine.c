/*
 * solina_engine.c - Solina String Machine synthesizer engine
 */

#include "solina_engine.h"
#include <string.h>

static int find_voice(solina_engine_t *e) {
    for (int i = 0; i < SOLINA_MAX_VOICES; i++)
        if (!e->voices[i].active) return i;
    return 0;
}

static int find_voice_for_note(solina_engine_t *e, int note) {
    for (int i = 0; i < SOLINA_MAX_VOICES; i++)
        if (e->voices[i].active && e->voices[i].note == note) return i;
    return -1;
}

void solina_engine_init(solina_engine_t *e) {
    memset(e, 0, sizeof(solina_engine_t));
    e->sample_rate = BRISTOL_SAMPLE_RATE;

    for (int i = 0; i < SOLINA_MAX_VOICES; i++) {
        osc_init(&e->voices[i].osc, i * 0.1f);
        e->voices[i].phase2 = (float)i * 0.15f;
    }

    e->violin_level = 0.7f;
    e->viola_level = 0.5f;
    e->cello_level = 0.3f;
    e->bass_level = 0.2f;
    e->ensemble_on = 1;
    e->ensemble_depth = 0.3f;
    e->ensemble_rate = 0.5f;
    e->attack = 0.1f;
    e->release = 0.3f;
    e->master_volume = 0.8f;
    e->bend_range = 2.0f;
}

void solina_engine_reset(solina_engine_t *e) {
    for (int i = 0; i < SOLINA_MAX_VOICES; i++) {
        e->voices[i].active = 0;
        e->voices[i].level = 0.0f;
    }
}

void solina_engine_note_on(solina_engine_t *e, int note, float velocity) {
    int vi = find_voice(e);
    solina_voice_t *v = &e->voices[vi];
    v->note = note;
    v->velocity = velocity;
    v->freq = midi_to_freq(note);
    v->active = 1;
}

void solina_engine_note_off(solina_engine_t *e, int note) {
    int vi = find_voice_for_note(e, note);
    if (vi >= 0) {
        e->voices[vi].active = 0;  /* Start release */
    }
}

void solina_engine_pitch_bend(solina_engine_t *e, float bend) { e->pitch_bend = bend; }

void solina_engine_all_notes_off(solina_engine_t *e) {
    for (int i = 0; i < SOLINA_MAX_VOICES; i++) {
        e->voices[i].active = 0;
    }
}

void solina_engine_render(solina_engine_t *e, float *output, int frames) {
    float sr = e->sample_rate;
    memset(output, 0, frames * sizeof(float));
    float bend_mult = powf(2.0f, e->pitch_bend * e->bend_range / 12.0f);

    /* Ensemble LFO */
    float ens_lfo_inc = e->ensemble_rate * 5.0f / sr;

    for (int vi = 0; vi < SOLINA_MAX_VOICES; vi++) {
        solina_voice_t *v = &e->voices[vi];

        /* Check if voice should produce sound */
        if (!v->active && v->level <= 0.001f) continue;

        for (int i = 0; i < frames; i++) {
            /* Simple AR envelope */
            float target = v->active ? 1.0f : 0.0f;
            float rate = v->active ? e->attack : e->release;
            rate = clampf(rate, 0.001f, 1.0f);
            float coef = 1.0f - expf(-1.0f / (rate * sr));
            v->level += (target - v->level) * coef;

            if (!v->active && v->level <= 0.001f) {
                v->level = 0.0f;
                continue;
            }

            float freq = v->freq * bend_mult;

            /* Ensemble modulation */
            float ens_mod = 0.0f;
            if (e->ensemble_on) {
                e->ensemble_phase += ens_lfo_inc;
                if (e->ensemble_phase >= 1.0f) e->ensemble_phase -= 1.0f;
                ens_mod = sinf(e->ensemble_phase * 6.283185f) * e->ensemble_depth * 0.01f;
            }

            /* Main oscillator - sawtooth for strings */
            float freq1 = freq * (1.0f + ens_mod);
            float freq2 = freq * (1.0f - ens_mod * 0.7f);

            v->osc.phase += freq1 / sr;
            if (v->osc.phase >= 1.0) v->osc.phase -= 1.0;
            v->phase2 += freq2 / sr;
            if (v->phase2 >= 1.0f) v->phase2 -= 1.0f;

            float osc1 = (float)v->osc.phase * 2.0f - 1.0f;
            float osc2 = v->phase2 * 2.0f - 1.0f;
            float osc_out = (osc1 + osc2) * 0.5f;

            /* Simple lowpass for warmth */
            static float lp = 0.0f;
            lp += (osc_out - lp) * 0.3f;

            output[i] += lp * v->level * v->velocity * e->master_volume;
        }
    }

    for (int i = 0; i < frames; i++) output[i] = fast_tanh(output[i]);
}
