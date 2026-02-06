/*
 * mini_engine.c - Minimoog-style synthesizer engine
 */

#include "mini_engine.h"

void mini_engine_init(mini_engine_t *engine) {
    memset(engine, 0, sizeof(mini_engine_t));

    engine->sample_rate = BRISTOL_SAMPLE_RATE;

    /* Initialize oscillators */
    for (int i = 0; i < 3; i++) {
        osc_init(&engine->osc[i], i);
    }
    engine->osc[0].octave = 0;
    engine->osc[1].octave = 0;
    engine->osc[2].octave = -1;

    noise_init(&engine->noise);
    filter_init(&engine->filter);
    env_init(&engine->filter_env);
    env_init(&engine->amp_env);
    lfo_init(&engine->lfo);

    engine->lfo_to_osc = 0.0f;
    engine->lfo_to_filter = 0.0f;
    engine->osc3_as_lfo = 0;

    engine->master_volume = 0.8f;
    engine->glide = 0.0f;
    engine->multi_trigger = 0;

    engine->gate = 0;
    engine->current_note = 60;
    engine->current_freq = midi_to_freq(60);
    engine->target_freq = engine->current_freq;
    engine->velocity = 0.0f;
    engine->glide_freq = engine->current_freq;
    engine->key_stack_count = 0;

    engine->pitch_bend = 0.0f;
    engine->bend_range = 2.0f;
    engine->mod_wheel = 0.0f;
    engine->master_tune = 0.0f;
    engine->octave_transpose = 0;
}

void mini_engine_reset(mini_engine_t *engine) {
    engine->gate = 0;
    engine->key_stack_count = 0;

    env_init(&engine->filter_env);
    env_init(&engine->amp_env);

    for (int i = 0; i < 3; i++) {
        engine->osc[i].phase = 0.0;
    }

    filter_init(&engine->filter);
}

void mini_engine_note_on(mini_engine_t *engine, int note, float velocity) {
    if (engine->key_stack_count < 16) {
        engine->key_stack[engine->key_stack_count++] = note;
    }

    engine->current_note = note;
    engine->target_freq = midi_to_freq(note + engine->octave_transpose * 12);
    engine->velocity = velocity;

    if (engine->glide <= 0.001f || !engine->gate) {
        engine->glide_freq = engine->target_freq;
    }

    if (!engine->gate || engine->multi_trigger) {
        env_gate_on(&engine->filter_env);
        env_gate_on(&engine->amp_env);
    }

    engine->gate = 1;
}

void mini_engine_note_off(mini_engine_t *engine, int note) {
    for (int i = 0; i < engine->key_stack_count; i++) {
        if (engine->key_stack[i] == note) {
            for (int j = i; j < engine->key_stack_count - 1; j++) {
                engine->key_stack[j] = engine->key_stack[j + 1];
            }
            engine->key_stack_count--;
            break;
        }
    }

    if (engine->key_stack_count == 0) {
        engine->gate = 0;
        env_gate_off(&engine->filter_env);
        env_gate_off(&engine->amp_env);
    } else {
        int new_note = engine->key_stack[engine->key_stack_count - 1];
        engine->current_note = new_note;
        engine->target_freq = midi_to_freq(new_note + engine->octave_transpose * 12);
    }
}

void mini_engine_pitch_bend(mini_engine_t *engine, float bend) {
    engine->pitch_bend = bend;
}

void mini_engine_mod_wheel(mini_engine_t *engine, float amount) {
    engine->mod_wheel = amount;
}

void mini_engine_all_notes_off(mini_engine_t *engine) {
    engine->gate = 0;
    engine->key_stack_count = 0;
    env_gate_off(&engine->filter_env);
    env_gate_off(&engine->amp_env);
}

void mini_engine_render(mini_engine_t *engine, float *output, int frames) {
    float sr = engine->sample_rate;

    for (int i = 0; i < frames; i++) {
        /* Glide */
        if (engine->glide > 0.001f) {
            float glide_rate = 1.0f - expf(-1.0f / (engine->glide * sr * 0.5f));
            engine->glide_freq += (engine->target_freq - engine->glide_freq) * glide_rate;
        } else {
            engine->glide_freq = engine->target_freq;
        }

        /* Pitch bend */
        float bend_mult = powf(2.0f, engine->pitch_bend * engine->bend_range / 12.0f);
        float base_freq = engine->glide_freq * bend_mult;
        base_freq *= powf(2.0f, engine->master_tune / 1200.0f);

        /* LFO */
        float lfo_out = lfo_process(&engine->lfo, sr);
        float mod_depth = engine->lfo_to_osc + engine->mod_wheel * 0.5f;

        /* Oscillators */
        float osc_mix = 0.0f;
        for (int o = 0; o < 3; o++) {
            if (!engine->osc[o].enabled) continue;
            if (o == 2 && engine->osc3_as_lfo) continue;

            float osc_freq = base_freq * powf(2.0f, engine->osc[o].octave);
            osc_freq *= powf(2.0f, engine->osc[o].detune / 1200.0f);

            if (mod_depth > 0.0f) {
                osc_freq *= 1.0f + lfo_out * mod_depth * 0.1f;
            }

            osc_mix += osc_generate(&engine->osc[o], osc_freq, sr);
        }

        /* OSC3 as modulation */
        float osc3_mod = 0.0f;
        if (engine->osc3_as_lfo && engine->osc[2].enabled) {
            float osc3_freq = base_freq * powf(2.0f, engine->osc[2].octave);
            osc3_mod = osc_generate(&engine->osc[2], osc3_freq, sr);
        }

        /* Noise */
        if (engine->noise.level > 0.0f) {
            float noise_sample = engine->noise.type == 0
                ? noise_white(&engine->noise)
                : noise_pink(&engine->noise);
            osc_mix += noise_sample * engine->noise.level;
        }

        /* Envelopes */
        float filter_env_val = env_process(&engine->filter_env, engine->gate, sr);
        float amp_env = env_process(&engine->amp_env, engine->gate, sr);

        /* Filter */
        float filter_lfo = lfo_out * engine->lfo_to_filter + osc3_mod * 0.5f;
        float filtered = filter_process(&engine->filter, osc_mix, filter_env_val,
                                        filter_lfo, base_freq, sr);

        /* Output */
        float sample = filtered * amp_env * engine->master_volume * engine->velocity;
        sample = fast_tanh(sample);

        output[i] = sample;
    }
}
