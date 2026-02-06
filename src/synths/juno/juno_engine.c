/*
 * juno_engine.c - Juno-style synthesizer engine
 *
 * DCO-based synth with BBD-style chorus.
 */

#include "juno_engine.h"

/* ========================================================================
 * Chorus effect (BBD-style)
 * ======================================================================== */

static void chorus_init(juno_chorus_t *c) {
    memset(c->buffer, 0, sizeof(c->buffer));
    c->write_pos = 0;
    c->lfo_phase = 0.0f;
    c->rate = 0.5f;
    c->depth = 0.5f;
    c->mode = 1;  /* Mode I by default */
}

static float chorus_process(juno_chorus_t *c, float input, float sample_rate) {
    if (c->mode == 0) return input;

    /* Write to delay buffer */
    c->buffer[c->write_pos] = input;
    c->write_pos = (c->write_pos + 1) & 4095;

    /* LFO for modulating delay time */
    float lfo_rate = 0.5f + c->rate * 2.0f;  /* 0.5 to 2.5 Hz */
    c->lfo_phase += lfo_rate / sample_rate;
    if (c->lfo_phase >= 1.0f) c->lfo_phase -= 1.0f;
    float lfo = sinf(2.0f * M_PI * c->lfo_phase);

    /* Calculate delay times based on mode */
    float delay1_ms, delay2_ms;
    float depth = c->depth * 2.0f;  /* 0-2 ms modulation depth */

    switch (c->mode) {
        case 1:  /* Mode I - subtle */
            delay1_ms = 3.0f + lfo * depth;
            delay2_ms = 0.0f;
            break;
        case 2:  /* Mode II - deeper */
            delay1_ms = 5.0f + lfo * depth * 1.5f;
            delay2_ms = 0.0f;
            break;
        case 3:  /* Mode I+II - both voices */
            delay1_ms = 3.0f + lfo * depth;
            delay2_ms = 5.0f - lfo * depth * 1.5f;  /* Opposite phase */
            break;
        default:
            return input;
    }

    /* Convert to samples */
    float delay1_samples = delay1_ms * sample_rate / 1000.0f;
    float delay2_samples = delay2_ms * sample_rate / 1000.0f;

    /* Read from delay buffer with linear interpolation */
    float read_pos1 = (float)c->write_pos - delay1_samples;
    if (read_pos1 < 0) read_pos1 += 4096.0f;
    int idx1 = (int)read_pos1;
    float frac1 = read_pos1 - (float)idx1;
    float wet1 = c->buffer[idx1 & 4095] * (1.0f - frac1) +
                 c->buffer[(idx1 + 1) & 4095] * frac1;

    float wet2 = 0.0f;
    if (c->mode == 3 && delay2_samples > 0) {
        float read_pos2 = (float)c->write_pos - delay2_samples;
        if (read_pos2 < 0) read_pos2 += 4096.0f;
        int idx2 = (int)read_pos2;
        float frac2 = read_pos2 - (float)idx2;
        wet2 = c->buffer[idx2 & 4095] * (1.0f - frac2) +
               c->buffer[(idx2 + 1) & 4095] * frac2;
    }

    /* Mix dry and wet (stereo spread would go here for stereo output) */
    return input * 0.7f + wet1 * 0.3f + wet2 * 0.3f;
}

/* ========================================================================
 * High-pass filter (simple 1-pole)
 * ======================================================================== */

static float hpf_process(float input, float *state, float cutoff, float sample_rate) {
    float fc = cutoff / sample_rate;
    if (fc > 0.49f) fc = 0.49f;
    float rc = 1.0f / (2.0f * M_PI * fc * sample_rate);
    float alpha = rc / (rc + 1.0f / sample_rate);
    float output = alpha * (*state + input - *state);
    *state = input;
    return output - input + *state;
}

/* ========================================================================
 * Engine functions
 * ======================================================================== */

void juno_engine_init(juno_engine_t *engine) {
    memset(engine, 0, sizeof(juno_engine_t));

    engine->sample_rate = BRISTOL_SAMPLE_RATE;

    /* DCO */
    osc_init(&engine->dco, 0);
    engine->dco.wave = WAVE_SAW;
    engine->pulse_width = 0.5f;
    engine->pw_lfo_amount = 0.0f;
    engine->saw_enabled = 1;
    engine->pulse_enabled = 0;
    engine->sub_enabled = 0;
    engine->sub_level = 0.5f;

    noise_init(&engine->noise);
    filter_init(&engine->filter);
    env_init(&engine->filter_env);
    env_init(&engine->amp_env);
    lfo_init(&engine->lfo);
    chorus_init(&engine->chorus);

    engine->hpf_cutoff = 20.0f;
    engine->hpf_state = 0.0f;

    engine->lfo_to_dco = 0.0f;
    engine->lfo_to_filter = 0.0f;

    engine->master_volume = 0.8f;
    engine->glide = 0.0f;

    engine->gate = 0;
    engine->current_note = 60;
    engine->current_freq = midi_to_freq(60);
    engine->target_freq = engine->current_freq;
    engine->glide_freq = engine->current_freq;
    engine->velocity = 0.0f;
    engine->key_stack_count = 0;

    engine->pitch_bend = 0.0f;
    engine->bend_range = 2.0f;
}

void juno_engine_reset(juno_engine_t *engine) {
    engine->gate = 0;
    engine->key_stack_count = 0;
    env_init(&engine->filter_env);
    env_init(&engine->amp_env);
    engine->dco.phase = 0.0;
    filter_init(&engine->filter);
    chorus_init(&engine->chorus);
}

void juno_engine_note_on(juno_engine_t *engine, int note, float velocity) {
    if (engine->key_stack_count < 16) {
        engine->key_stack[engine->key_stack_count++] = note;
    }

    engine->current_note = note;
    engine->target_freq = midi_to_freq(note);
    engine->velocity = velocity;

    if (engine->glide <= 0.001f || !engine->gate) {
        engine->glide_freq = engine->target_freq;
    }

    if (!engine->gate) {
        env_gate_on(&engine->filter_env);
        env_gate_on(&engine->amp_env);
    }

    engine->gate = 1;
}

void juno_engine_note_off(juno_engine_t *engine, int note) {
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
        engine->target_freq = midi_to_freq(new_note);
    }
}

void juno_engine_pitch_bend(juno_engine_t *engine, float bend) {
    engine->pitch_bend = bend;
}

void juno_engine_all_notes_off(juno_engine_t *engine) {
    engine->gate = 0;
    engine->key_stack_count = 0;
    env_gate_off(&engine->filter_env);
    env_gate_off(&engine->amp_env);
}

void juno_engine_render(juno_engine_t *engine, float *output, int frames) {
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

        /* LFO */
        float lfo_out = lfo_process(&engine->lfo, sr);

        /* Apply LFO to pitch */
        float dco_freq = base_freq * (1.0f + lfo_out * engine->lfo_to_dco * 0.1f);

        /* DCO - generate waveforms */
        float dco_mix = 0.0f;

        /* Sawtooth */
        if (engine->saw_enabled) {
            engine->dco.wave = WAVE_SAW;
            dco_mix += osc_generate(&engine->dco, dco_freq, sr);
        }

        /* Pulse with PWM */
        if (engine->pulse_enabled) {
            float pw = engine->pulse_width + lfo_out * engine->pw_lfo_amount * 0.4f;
            pw = clampf(pw, 0.1f, 0.9f);
            /* Generate pulse manually for variable width */
            float t = (float)engine->dco.phase;
            float pulse = t < pw ? 1.0f : -1.0f;
            dco_mix += pulse * 0.8f;
        }

        /* Sub oscillator (one octave down, square) */
        if (engine->sub_enabled) {
            float sub_t = fmodf((float)engine->dco.phase * 0.5f, 1.0f);
            float sub = sub_t < 0.5f ? 1.0f : -1.0f;
            dco_mix += sub * engine->sub_level;
        }

        /* Advance DCO phase if not already done by saw */
        if (!engine->saw_enabled) {
            float dt = dco_freq / sr;
            engine->dco.phase += dt;
            if (engine->dco.phase >= 1.0) engine->dco.phase -= 1.0;
        }

        /* Add noise */
        if (engine->noise.level > 0.0f) {
            dco_mix += noise_white(&engine->noise) * engine->noise.level;
        }

        /* High-pass filter */
        float hpf_out = dco_mix;  /* Simplified - full HPF implementation TODO */

        /* Envelopes */
        float filter_env_val = env_process(&engine->filter_env, engine->gate, sr);
        float amp_env = env_process(&engine->amp_env, engine->gate, sr);

        /* Low-pass filter with LFO mod */
        float filter_lfo = lfo_out * engine->lfo_to_filter;
        float filtered = filter_process(&engine->filter, hpf_out, filter_env_val,
                                        filter_lfo, base_freq, sr);

        /* VCA */
        float sample = filtered * amp_env * engine->master_volume * engine->velocity;

        /* Chorus */
        sample = chorus_process(&engine->chorus, sample, sr);

        /* Soft clip */
        sample = fast_tanh(sample);

        output[i] = sample;
    }
}
