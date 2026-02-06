/*
 * prophet_engine.c - Prophet-5 style synthesizer engine
 *
 * 5-voice polyphonic analog synth.
 */

#include "prophet_engine.h"
#include <string.h>

/* Find a free voice or steal the oldest */
static int find_voice(prophet_engine_t *engine) {
    /* First, look for an inactive voice */
    for (int i = 0; i < PROPHET_MAX_VOICES; i++) {
        if (!engine->voices[i].active) {
            return i;
        }
    }
    /* Steal voice 0 (simple voice stealing) */
    return 0;
}

/* Find voice playing a specific note */
static int find_voice_for_note(prophet_engine_t *engine, int note) {
    for (int i = 0; i < PROPHET_MAX_VOICES; i++) {
        if (engine->voices[i].active && engine->voices[i].note == note) {
            return i;
        }
    }
    return -1;
}

void prophet_engine_init(prophet_engine_t *engine) {
    memset(engine, 0, sizeof(prophet_engine_t));

    engine->sample_rate = BRISTOL_SAMPLE_RATE;
    engine->voice_count = PROPHET_MAX_VOICES;

    /* Initialize voices */
    for (int i = 0; i < PROPHET_MAX_VOICES; i++) {
        prophet_voice_t *v = &engine->voices[i];
        osc_init(&v->oscA, i * 0.1f);
        osc_init(&v->oscB, i * 0.1f + 0.05f);
        filter_init(&v->filter);
        env_init(&v->filter_env);
        env_init(&v->amp_env);
        v->active = 0;
    }

    /* Default oscillator settings */
    engine->oscA_octave = 1;  /* 8' */
    engine->oscA_saw = 1;
    engine->oscA_pulse = 0;
    engine->oscA_pw = 0.5f;
    engine->oscA_sync = 0;

    engine->oscB_octave = 1;
    engine->oscB_detune = 0.0f;
    engine->oscB_saw = 1;
    engine->oscB_tri = 0;
    engine->oscB_pulse = 0;
    engine->oscB_pw = 0.5f;
    engine->oscB_lfo_mod = 0;
    engine->oscB_kbd_track = 1;

    /* Mixer */
    engine->mix_oscA = 0.5f;
    engine->mix_oscB = 0.5f;
    engine->mix_noise = 0.0f;

    /* Filter */
    engine->filter_cutoff = 0.7f;
    engine->filter_resonance = 0.0f;
    engine->filter_env_amt = 0.5f;
    engine->filter_kbd_track = 0;

    /* Envelopes */
    engine->fenv_attack = 0.01f;
    engine->fenv_decay = 0.3f;
    engine->fenv_sustain = 0.5f;
    engine->fenv_release = 0.3f;

    engine->aenv_attack = 0.01f;
    engine->aenv_decay = 0.3f;
    engine->aenv_sustain = 0.7f;
    engine->aenv_release = 0.3f;

    /* LFO */
    lfo_init(&engine->lfo);
    engine->lfo_tri = 1;

    /* Noise */
    noise_init(&engine->noise);

    /* Master */
    engine->master_volume = 0.8f;
    engine->bend_range = 2.0f;
    engine->release_on = 1;
}

void prophet_engine_reset(prophet_engine_t *engine) {
    for (int i = 0; i < PROPHET_MAX_VOICES; i++) {
        prophet_voice_t *v = &engine->voices[i];
        v->active = 0;
        env_init(&v->filter_env);
        env_init(&v->amp_env);
        v->oscA.phase = 0.0;
        v->oscB.phase = 0.0;
        filter_init(&v->filter);
    }
}

void prophet_engine_note_on(prophet_engine_t *engine, int note, float velocity) {
    int vi = find_voice(engine);
    prophet_voice_t *v = &engine->voices[vi];

    v->note = note;
    v->velocity = velocity;
    v->freq = midi_to_freq(note);
    v->target_freq = v->freq;
    v->active = 1;

    /* Setup envelopes for this voice */
    v->filter_env.attack = engine->fenv_attack;
    v->filter_env.decay = engine->fenv_decay;
    v->filter_env.sustain = engine->fenv_sustain;
    v->filter_env.release = engine->fenv_release;

    v->amp_env.attack = engine->aenv_attack;
    v->amp_env.decay = engine->aenv_decay;
    v->amp_env.sustain = engine->aenv_sustain;
    v->amp_env.release = engine->aenv_release;

    /* Setup filter */
    v->filter.cutoff = engine->filter_cutoff;
    v->filter.resonance = engine->filter_resonance;
    v->filter.env_amount = engine->filter_env_amt;
    v->filter.key_track = engine->filter_kbd_track ? 0.5f : 0.0f;

    env_gate_on(&v->filter_env);
    env_gate_on(&v->amp_env);
}

void prophet_engine_note_off(prophet_engine_t *engine, int note) {
    int vi = find_voice_for_note(engine, note);
    if (vi >= 0) {
        prophet_voice_t *v = &engine->voices[vi];
        if (engine->release_on) {
            env_gate_off(&v->filter_env);
            env_gate_off(&v->amp_env);
        } else {
            v->active = 0;
        }
    }
}

void prophet_engine_pitch_bend(prophet_engine_t *engine, float bend) {
    engine->pitch_bend = bend;
}

void prophet_engine_all_notes_off(prophet_engine_t *engine) {
    for (int i = 0; i < PROPHET_MAX_VOICES; i++) {
        prophet_voice_t *v = &engine->voices[i];
        v->active = 0;
        env_gate_off(&v->filter_env);
        env_gate_off(&v->amp_env);
    }
}

/* Octave multipliers: 16', 8', 4', 2', 1', Lo */
static const float octave_mult[] = { 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 0.25f };

void prophet_engine_render(prophet_engine_t *engine, float *output, int frames) {
    float sr = engine->sample_rate;

    /* Clear output buffer */
    memset(output, 0, frames * sizeof(float));

    /* LFO (global) */
    float lfo_val = lfo_process(&engine->lfo, sr);

    /* Pitch bend multiplier */
    float bend_mult = powf(2.0f, engine->pitch_bend * engine->bend_range / 12.0f);

    /* Render each voice */
    for (int vi = 0; vi < PROPHET_MAX_VOICES; vi++) {
        prophet_voice_t *v = &engine->voices[vi];

        if (!v->active && v->amp_env.state == ENV_IDLE) continue;

        for (int i = 0; i < frames; i++) {
            /* Calculate oscillator frequencies */
            float base_freq = v->freq * bend_mult;

            /* Oscillator A */
            float freqA = base_freq * octave_mult[engine->oscA_octave % 6];
            float oscA_out = 0.0f;
            if (engine->oscA_saw) {
                v->oscA.wave = WAVE_SAW;
                oscA_out += osc_generate(&v->oscA, freqA, sr);
            }
            if (engine->oscA_pulse) {
                /* Pulse wave with PW */
                float t = (float)v->oscA.phase;
                float pw = engine->oscA_pw;
                oscA_out += (t < pw ? 1.0f : -1.0f) * 0.8f;
            }
            if (!engine->oscA_saw && !engine->oscA_pulse) {
                /* Advance phase anyway */
                v->oscA.phase += freqA / sr;
                if (v->oscA.phase >= 1.0) v->oscA.phase -= 1.0;
            }

            /* Oscillator B */
            float freqB = base_freq * octave_mult[engine->oscB_octave % 6];
            freqB *= 1.0f + engine->oscB_detune * 0.05f;  /* Detune */

            /* LFO modulation to osc B */
            if (engine->oscB_lfo_mod) {
                freqB *= 1.0f + lfo_val * 0.1f;
            }

            /* Sync: reset oscB when oscA completes cycle */
            if (engine->oscA_sync && v->oscA.phase < (freqA / sr)) {
                v->oscB.phase = 0.0;
            }

            float oscB_out = 0.0f;
            if (engine->oscB_saw) {
                v->oscB.wave = WAVE_SAW;
                oscB_out += osc_generate(&v->oscB, freqB, sr);
            }
            if (engine->oscB_tri) {
                v->oscB.wave = WAVE_TRI;
                oscB_out += osc_generate(&v->oscB, freqB, sr);
            }
            if (engine->oscB_pulse) {
                float t = (float)v->oscB.phase;
                float pw = engine->oscB_pw;
                oscB_out += (t < pw ? 1.0f : -1.0f) * 0.8f;
            }
            if (!engine->oscB_saw && !engine->oscB_tri && !engine->oscB_pulse) {
                v->oscB.phase += freqB / sr;
                if (v->oscB.phase >= 1.0) v->oscB.phase -= 1.0;
            }

            /* Mixer */
            float mix = oscA_out * engine->mix_oscA +
                       oscB_out * engine->mix_oscB +
                       noise_white(&engine->noise) * engine->mix_noise;

            /* Envelopes */
            float fenv = env_process(&v->filter_env, v->active, sr);
            float aenv = env_process(&v->amp_env, v->active, sr);

            /* Check if voice is done */
            if (!v->active && v->amp_env.state == ENV_IDLE) {
                continue;
            }

            /* Filter */
            float filtered = filter_process(&v->filter, mix, fenv, lfo_val * 0.1f, base_freq, sr);

            /* VCA */
            float sample = filtered * aenv * v->velocity * engine->master_volume;

            /* Accumulate */
            output[i] += sample;
        }
    }

    /* Soft clip */
    for (int i = 0; i < frames; i++) {
        output[i] = fast_tanh(output[i]);
    }
}
