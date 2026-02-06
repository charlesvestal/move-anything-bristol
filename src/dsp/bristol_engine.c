/*
 * bristol_engine.c - Minimoog-style synthesizer engine
 *
 * Inspired by Bristol Synthesizer by Nick Copeland
 * https://bristol.sourceforge.net/
 * Original Bristol is GPL-3.0 licensed
 *
 * Ported/rewritten for Move Anything plugin API.
 */

#include "bristol_engine.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ========================================================================
 * Utility functions
 * ======================================================================== */

static inline float midi_to_freq(int note) {
    return 440.0f * powf(2.0f, (note - 69) / 12.0f);
}

static inline float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

/* Fast tanh approximation for filter saturation */
static inline float fast_tanh(float x) {
    if (x < -3.0f) return -1.0f;
    if (x > 3.0f) return 1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* ========================================================================
 * Noise generator
 * ======================================================================== */

static inline float noise_white(bristol_noise_t *n) {
    n->seed = n->seed * 1103515245 + 12345;
    return ((float)(n->seed >> 16) / 32768.0f) - 1.0f;
}

static inline float noise_pink(bristol_noise_t *n) {
    float white = noise_white(n);
    /* Paul Kellet's pink noise filter */
    n->pink_b0 = 0.99886f * n->pink_b0 + white * 0.0555179f;
    n->pink_b1 = 0.99332f * n->pink_b1 + white * 0.0750759f;
    n->pink_b2 = 0.96900f * n->pink_b2 + white * 0.1538520f;
    n->pink_b3 = 0.86650f * n->pink_b3 + white * 0.3104856f;
    n->pink_b4 = 0.55000f * n->pink_b4 + white * 0.5329522f;
    n->pink_b5 = -0.7616f * n->pink_b5 - white * 0.0168980f;
    float pink = n->pink_b0 + n->pink_b1 + n->pink_b2 + n->pink_b3
               + n->pink_b4 + n->pink_b5 + n->pink_b6 + white * 0.5362f;
    n->pink_b6 = white * 0.115926f;
    return pink * 0.11f;
}

static void noise_init(bristol_noise_t *n) {
    n->seed = 22222;
    n->level = 0.0f;
    n->type = 0;
    n->pink_b0 = n->pink_b1 = n->pink_b2 = 0.0f;
    n->pink_b3 = n->pink_b4 = n->pink_b5 = n->pink_b6 = 0.0f;
}

/* ========================================================================
 * Oscillator (band-limited using PolyBLEP)
 * ======================================================================== */

static inline float poly_blep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    } else if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

static float osc_generate(bristol_osc_t *osc, float freq, float sample_rate) {
    if (!osc->enabled) return 0.0f;

    float dt = freq / sample_rate;
    if (dt > 0.5f) dt = 0.5f;
    if (dt < 0.0f) dt = 0.0001f;

    float t = (float)osc->phase;
    float sample = 0.0f;

    switch (osc->wave) {
        case WAVE_SINE:
            sample = sinf(2.0f * M_PI * t);
            break;

        case WAVE_TRI: {
            /* Triangle from integrated square */
            sample = 4.0f * fabsf(t - 0.5f) - 1.0f;
            break;
        }

        case WAVE_SAW:
        case WAVE_RAMP: {
            /* Sawtooth with PolyBLEP */
            sample = 2.0f * t - 1.0f;
            sample -= poly_blep(t, dt);
            if (osc->wave == WAVE_SAW) sample = -sample;
            break;
        }

        case WAVE_SQUARE: {
            /* Square with PolyBLEP */
            sample = t < 0.5f ? 1.0f : -1.0f;
            sample += poly_blep(t, dt);
            sample -= poly_blep(fmodf(t + 0.5f, 1.0f), dt);
            break;
        }

        case WAVE_PULSE: {
            /* Pulse wave (25% duty) */
            float pw = 0.25f;
            sample = t < pw ? 1.0f : -1.0f;
            sample += poly_blep(t, dt);
            sample -= poly_blep(fmodf(t + (1.0f - pw), 1.0f), dt);
            break;
        }

        default:
            sample = 0.0f;
    }

    /* Advance phase */
    osc->phase += dt;
    if (osc->phase >= 1.0) osc->phase -= 1.0;

    osc->last_output = sample;
    return sample * osc->level;
}

static void osc_init(bristol_osc_t *osc, int index) {
    osc->phase = 0.0;
    osc->last_output = 0.0f;
    osc->enabled = 1;
    osc->wave = WAVE_SAW;
    osc->octave = 0;
    osc->detune = 0.0f;
    osc->level = (index == 0) ? 1.0f : 0.0f; /* Only OSC1 on by default */
}

/* ========================================================================
 * ADSR Envelope
 * ======================================================================== */

/* Convert 0-1 parameter to time in samples */
static inline float env_time_to_samples(float param, float sample_rate) {
    /* Exponential curve: 1ms to 10s */
    float time_ms = 1.0f + expf(param * 6.9f) * 1.0f; /* ~1ms to ~1000ms for most range */
    if (param > 0.9f) time_ms = 1.0f + expf(param * 9.2f); /* Extended range at top */
    return time_ms * sample_rate / 1000.0f;
}

static float env_process(bristol_env_t *env, int gate, float sample_rate) {
    float attack_samples = env_time_to_samples(env->attack, sample_rate);
    float decay_samples = env_time_to_samples(env->decay, sample_rate);
    float release_samples = env_time_to_samples(env->release, sample_rate);

    switch (env->state) {
        case ENV_IDLE:
            env->level = 0.0f;
            break;

        case ENV_ATTACK:
            if (attack_samples < 1.0f) attack_samples = 1.0f;
            env->level = env->attack_level + (1.0f - env->attack_level) * (float)(env->phase / attack_samples);
            env->phase += 1.0;
            if (env->phase >= attack_samples) {
                env->level = 1.0f;
                env->state = ENV_DECAY;
                env->phase = 0.0;
            }
            break;

        case ENV_DECAY:
            if (decay_samples < 1.0f) decay_samples = 1.0f;
            env->level = env->sustain + (1.0f - env->sustain) * (1.0f - (float)(env->phase / decay_samples));
            env->phase += 1.0;
            if (env->phase >= decay_samples) {
                env->level = env->sustain;
                env->state = ENV_SUSTAIN;
            }
            break;

        case ENV_SUSTAIN:
            env->level = env->sustain;
            break;

        case ENV_RELEASE:
            if (release_samples < 1.0f) release_samples = 1.0f;
            env->level = env->release_level * (1.0f - (float)(env->phase / release_samples));
            env->phase += 1.0;
            if (env->phase >= release_samples || env->level <= 0.0001f) {
                env->level = 0.0f;
                env->state = ENV_IDLE;
            }
            break;
    }

    return env->level;
}

static void env_gate_on(bristol_env_t *env) {
    env->attack_level = env->level; /* For smooth retrigger */
    env->state = ENV_ATTACK;
    env->phase = 0.0;
}

static void env_gate_off(bristol_env_t *env) {
    if (env->state != ENV_IDLE) {
        env->release_level = env->level;
        env->state = ENV_RELEASE;
        env->phase = 0.0;
    }
}

static void env_init(bristol_env_t *env) {
    env->state = ENV_IDLE;
    env->level = 0.0f;
    env->attack_level = 0.0f;
    env->release_level = 0.0f;
    env->phase = 0.0;
    env->attack = 0.01f;
    env->decay = 0.3f;
    env->sustain = 0.7f;
    env->release = 0.3f;
}

/* ========================================================================
 * Moog Ladder Filter (Huovilainen algorithm)
 * ======================================================================== */

static void filter_init(bristol_filter_t *f) {
    memset(f->stage, 0, sizeof(f->stage));
    memset(f->stage_z1, 0, sizeof(f->stage_z1));
    f->delay = 0.0f;
    f->cutoff = 0.5f;
    f->resonance = 0.0f;
    f->env_amount = 0.5f;
    f->key_track = 0.0f;
}

static float filter_process(bristol_filter_t *f, float input, float env_mod,
                           float lfo_mod, float key_freq, float sample_rate) {
    /* Calculate cutoff frequency */
    float cutoff_hz = 20.0f + f->cutoff * f->cutoff * 20000.0f;

    /* Apply envelope modulation */
    cutoff_hz += env_mod * f->env_amount * 10000.0f;

    /* Apply LFO modulation */
    cutoff_hz += lfo_mod * 2000.0f;

    /* Apply key tracking */
    if (f->key_track > 0.0f) {
        cutoff_hz += (key_freq - 261.63f) * f->key_track; /* Track from middle C */
    }

    /* Clamp cutoff */
    cutoff_hz = clampf(cutoff_hz, 20.0f, sample_rate * 0.45f);

    /* Normalized cutoff frequency */
    float fc = cutoff_hz / sample_rate;
    if (fc > 0.45f) fc = 0.45f;

    /* Huovilainen frequency and amplitude correction */
    float fc2 = fc * fc;
    float fc3 = fc2 * fc;
    float fcr = 1.8730f * fc3 + 0.4955f * fc2 - 0.6490f * fc + 0.9988f;
    float acr = -3.9364f * fc2 + 1.8409f * fc + 0.9968f;

    /* Tuned coefficient */
    float g = 1.0f - expf(-2.0f * M_PI * fcr * fc);

    /* Resonance (0-1 maps to 0-4 for self-oscillation) */
    float res = f->resonance * 4.0f;

    /* Thermal voltage scaling */
    float thermal = 0.000025f;

    /* Process with 2x oversampling */
    float out = 0.0f;
    for (int os = 0; os < 2; os++) {
        /* Input with feedback */
        float x = input * thermal - res * f->delay * acr;
        x = fast_tanh(x);

        /* 4 cascaded 1-pole filters */
        for (int i = 0; i < 4; i++) {
            float y = f->stage[i] + g * (fast_tanh(x) - fast_tanh(f->stage[i]));
            f->stage[i] = y;
            x = y;
        }

        /* Half-sample delay for phase correction */
        out = (f->stage[3] + f->delay) * 0.5f;
        f->delay = f->stage[3];
    }

    /* Scale output back up */
    return out / thermal * 0.5f;
}

/* ========================================================================
 * LFO
 * ======================================================================== */

static void lfo_init(bristol_lfo_t *lfo) {
    lfo->phase = 0.0;
    lfo->rate = 0.3f;
    lfo->depth = 0.0f;
    lfo->wave = WAVE_TRI;
}

static float lfo_process(bristol_lfo_t *lfo, float sample_rate) {
    /* Rate: 0.1 Hz to 20 Hz */
    float freq = 0.1f + lfo->rate * lfo->rate * 19.9f;
    float dt = freq / sample_rate;

    float t = (float)lfo->phase;
    float sample = 0.0f;

    switch (lfo->wave) {
        case WAVE_SINE:
            sample = sinf(2.0f * M_PI * t);
            break;
        case WAVE_TRI:
            sample = 4.0f * fabsf(t - 0.5f) - 1.0f;
            break;
        case WAVE_SAW:
            sample = 2.0f * t - 1.0f;
            break;
        case WAVE_SQUARE:
            sample = t < 0.5f ? 1.0f : -1.0f;
            break;
        default:
            sample = sinf(2.0f * M_PI * t);
    }

    lfo->phase += dt;
    if (lfo->phase >= 1.0) lfo->phase -= 1.0;

    return sample * lfo->depth;
}

/* ========================================================================
 * Engine main functions
 * ======================================================================== */

void bristol_engine_init(bristol_engine_t *engine) {
    memset(engine, 0, sizeof(bristol_engine_t));

    engine->sample_rate = BRISTOL_SAMPLE_RATE;

    /* Initialize oscillators */
    for (int i = 0; i < 3; i++) {
        osc_init(&engine->osc[i], i);
    }
    /* Default octave offsets like Mini */
    engine->osc[0].octave = 0;
    engine->osc[1].octave = 0;
    engine->osc[2].octave = -1; /* OSC3 one octave down (often used as LFO) */

    /* Initialize noise */
    noise_init(&engine->noise);

    /* Initialize filter */
    filter_init(&engine->filter);

    /* Initialize envelopes */
    env_init(&engine->filter_env);
    env_init(&engine->amp_env);

    /* Initialize LFO */
    lfo_init(&engine->lfo);

    /* Default modulation */
    engine->lfo_to_osc = 0.0f;
    engine->lfo_to_filter = 0.0f;
    engine->osc3_as_lfo = 0;

    /* Master controls */
    engine->master_volume = 0.8f;
    engine->glide = 0.0f;
    engine->multi_trigger = 0;

    /* Voice state */
    engine->gate = 0;
    engine->current_note = 60;
    engine->current_freq = midi_to_freq(60);
    engine->target_freq = engine->current_freq;
    engine->velocity = 0.0f;
    engine->glide_freq = engine->current_freq;
    engine->key_stack_count = 0;

    /* Pitch bend */
    engine->pitch_bend = 0.0f;
    engine->bend_range = 2.0f;

    /* Mod wheel */
    engine->mod_wheel = 0.0f;

    /* Tuning */
    engine->master_tune = 0.0f;
    engine->octave_transpose = 0;
}

void bristol_engine_reset(bristol_engine_t *engine) {
    engine->gate = 0;
    engine->key_stack_count = 0;

    env_init(&engine->filter_env);
    env_init(&engine->amp_env);

    for (int i = 0; i < 3; i++) {
        engine->osc[i].phase = 0.0;
    }

    filter_init(&engine->filter);
}

void bristol_engine_note_on(bristol_engine_t *engine, int note, float velocity) {
    /* Add to key stack */
    if (engine->key_stack_count < 16) {
        engine->key_stack[engine->key_stack_count++] = note;
    }

    /* Set target frequency */
    engine->current_note = note;
    engine->target_freq = midi_to_freq(note + engine->octave_transpose * 12);
    engine->velocity = velocity;

    /* Handle glide */
    if (engine->glide <= 0.001f || !engine->gate) {
        engine->glide_freq = engine->target_freq;
    }

    /* Trigger envelopes */
    if (!engine->gate || engine->multi_trigger) {
        env_gate_on(&engine->filter_env);
        env_gate_on(&engine->amp_env);
    }

    engine->gate = 1;
}

void bristol_engine_note_off(bristol_engine_t *engine, int note) {
    /* Remove from key stack */
    for (int i = 0; i < engine->key_stack_count; i++) {
        if (engine->key_stack[i] == note) {
            for (int j = i; j < engine->key_stack_count - 1; j++) {
                engine->key_stack[j] = engine->key_stack[j + 1];
            }
            engine->key_stack_count--;
            break;
        }
    }

    /* If no keys held, release */
    if (engine->key_stack_count == 0) {
        engine->gate = 0;
        env_gate_off(&engine->filter_env);
        env_gate_off(&engine->amp_env);
    } else {
        /* Play the most recent note still held */
        int new_note = engine->key_stack[engine->key_stack_count - 1];
        engine->current_note = new_note;
        engine->target_freq = midi_to_freq(new_note + engine->octave_transpose * 12);
    }
}

void bristol_engine_pitch_bend(bristol_engine_t *engine, float bend) {
    engine->pitch_bend = bend;
}

void bristol_engine_mod_wheel(bristol_engine_t *engine, float amount) {
    engine->mod_wheel = amount;
}

void bristol_engine_all_notes_off(bristol_engine_t *engine) {
    engine->gate = 0;
    engine->key_stack_count = 0;
    env_gate_off(&engine->filter_env);
    env_gate_off(&engine->amp_env);
}

void bristol_engine_render(bristol_engine_t *engine, float *output, int frames) {
    float sr = engine->sample_rate;

    for (int i = 0; i < frames; i++) {
        /* Process glide */
        if (engine->glide > 0.001f) {
            float glide_rate = 1.0f - expf(-1.0f / (engine->glide * sr * 0.5f));
            engine->glide_freq += (engine->target_freq - engine->glide_freq) * glide_rate;
        } else {
            engine->glide_freq = engine->target_freq;
        }

        /* Apply pitch bend */
        float bend_mult = powf(2.0f, engine->pitch_bend * engine->bend_range / 12.0f);
        float base_freq = engine->glide_freq * bend_mult;

        /* Apply master tune */
        base_freq *= powf(2.0f, engine->master_tune / 1200.0f);

        /* Process LFO */
        float lfo_out = lfo_process(&engine->lfo, sr);

        /* Add mod wheel to LFO depth */
        float mod_depth = engine->lfo_to_osc + engine->mod_wheel * 0.5f;

        /* Process oscillators */
        float osc_mix = 0.0f;
        for (int o = 0; o < 3; o++) {
            if (!engine->osc[o].enabled) continue;

            /* Skip OSC3 if used as LFO */
            if (o == 2 && engine->osc3_as_lfo) continue;

            /* Calculate oscillator frequency */
            float osc_freq = base_freq * powf(2.0f, engine->osc[o].octave);
            osc_freq *= powf(2.0f, engine->osc[o].detune / 1200.0f);

            /* Apply LFO modulation to pitch */
            if (mod_depth > 0.0f) {
                osc_freq *= 1.0f + lfo_out * mod_depth * 0.1f;
            }

            osc_mix += osc_generate(&engine->osc[o], osc_freq, sr);
        }

        /* Add OSC3 as modulation source if enabled */
        float osc3_mod = 0.0f;
        if (engine->osc3_as_lfo && engine->osc[2].enabled) {
            float osc3_freq = base_freq * powf(2.0f, engine->osc[2].octave);
            osc3_mod = osc_generate(&engine->osc[2], osc3_freq, sr);
        }

        /* Add noise */
        if (engine->noise.level > 0.0f) {
            float noise_sample = engine->noise.type == 0
                ? noise_white(&engine->noise)
                : noise_pink(&engine->noise);
            osc_mix += noise_sample * engine->noise.level;
        }

        /* Process envelopes */
        float filter_env = env_process(&engine->filter_env, engine->gate, sr);
        float amp_env = env_process(&engine->amp_env, engine->gate, sr);

        /* Process filter */
        float filter_lfo = lfo_out * engine->lfo_to_filter + osc3_mod * 0.5f;
        float filtered = filter_process(&engine->filter, osc_mix, filter_env,
                                        filter_lfo, base_freq, sr);

        /* Apply amplitude envelope and master volume */
        float sample = filtered * amp_env * engine->master_volume * engine->velocity;

        /* Soft clip output */
        sample = fast_tanh(sample);

        output[i] = sample;
    }
}
