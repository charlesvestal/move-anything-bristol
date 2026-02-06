/*
 * bristol_common.h - Shared DSP components for Bristol synths
 *
 * Inspired by Bristol Synthesizer by Nick Copeland
 * https://bristol.sourceforge.net/
 * Original Bristol is GPL-3.0 licensed
 */

#ifndef BRISTOL_COMMON_H
#define BRISTOL_COMMON_H

#include <stdint.h>
#include <math.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define BRISTOL_SAMPLE_RATE 44100

/* ========================================================================
 * Common types
 * ======================================================================== */

/* Oscillator waveforms */
typedef enum {
    WAVE_TRI = 0,
    WAVE_RAMP,      /* Sawtooth up */
    WAVE_SAW,       /* Sawtooth down */
    WAVE_SQUARE,
    WAVE_PULSE,
    WAVE_SINE,
    WAVE_COUNT
} bristol_wave_t;

/* Envelope states */
typedef enum {
    ENV_IDLE = 0,
    ENV_ATTACK,
    ENV_DECAY,
    ENV_SUSTAIN,
    ENV_RELEASE
} bristol_env_state_t;

/* ========================================================================
 * Utility functions (inline)
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

typedef struct {
    uint32_t seed;
    float level;
    int type;           /* 0=white, 1=pink */
    /* Pink noise filter state */
    float pink_b0, pink_b1, pink_b2, pink_b3, pink_b4, pink_b5, pink_b6;
} bristol_noise_t;

static inline void noise_init(bristol_noise_t *n) {
    n->seed = 22222;
    n->level = 0.0f;
    n->type = 0;
    n->pink_b0 = n->pink_b1 = n->pink_b2 = 0.0f;
    n->pink_b3 = n->pink_b4 = n->pink_b5 = n->pink_b6 = 0.0f;
}

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

/* ========================================================================
 * Oscillator (band-limited using PolyBLEP)
 * ======================================================================== */

typedef struct {
    double phase;
    float last_output;
    int enabled;
    bristol_wave_t wave;
    int octave;         /* -2 to +2 octaves */
    float detune;       /* Fine tune in cents */
    float level;        /* Mix level 0-1 */
} bristol_osc_t;

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

static inline void osc_init(bristol_osc_t *osc, int index) {
    osc->phase = 0.0;
    osc->last_output = 0.0f;
    osc->enabled = 1;
    osc->wave = WAVE_SAW;
    osc->octave = 0;
    osc->detune = 0.0f;
    osc->level = (index == 0) ? 1.0f : 0.0f;
}

static inline float osc_generate(bristol_osc_t *osc, float freq, float sample_rate) {
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

        case WAVE_TRI:
            sample = 4.0f * fabsf(t - 0.5f) - 1.0f;
            break;

        case WAVE_SAW:
        case WAVE_RAMP: {
            sample = 2.0f * t - 1.0f;
            sample -= poly_blep(t, dt);
            if (osc->wave == WAVE_SAW) sample = -sample;
            break;
        }

        case WAVE_SQUARE: {
            sample = t < 0.5f ? 1.0f : -1.0f;
            sample += poly_blep(t, dt);
            sample -= poly_blep(fmodf(t + 0.5f, 1.0f), dt);
            break;
        }

        case WAVE_PULSE: {
            float pw = 0.25f;
            sample = t < pw ? 1.0f : -1.0f;
            sample += poly_blep(t, dt);
            sample -= poly_blep(fmodf(t + (1.0f - pw), 1.0f), dt);
            break;
        }

        default:
            sample = 0.0f;
    }

    osc->phase += dt;
    if (osc->phase >= 1.0) osc->phase -= 1.0;

    osc->last_output = sample;
    return sample * osc->level;
}

/* ========================================================================
 * ADSR Envelope (Bristol-style multiplier-based)
 *
 * Bristol uses a multiplier-based approach:
 * - Attack: linear ramp with rate = 12.0 / (sr * 0.0005 + duration * sr * param³)
 * - Decay/Release: multiplier = 1/pow(13.0, 1.0/(2.0 + param³ * sr * duration))
 * - Sustain: direct 0-1 value
 *
 * The 'duration' parameter sets overall time range (default ~10 seconds max)
 * ======================================================================== */

#define BRISTOL_ENV_DURATION 1.0f   /* Envelope time scale factor */

typedef struct {
    bristol_env_state_t state;
    float cgain;            /* Current gain (Bristol uses 1.0 to BRISTOL_VPO range) */

    /* Parameters (0-1 normalized from preset) */
    float attack;
    float decay;
    float sustain;
    float release;
} bristol_env_t;

static inline void env_init(bristol_env_t *env) {
    env->state = ENV_IDLE;
    env->cgain = 0.0f;
    env->attack = 0.01f;
    env->decay = 0.3f;
    env->sustain = 0.7f;
    env->release = 0.3f;
}

static inline void env_gate_on(bristol_env_t *env) {
    env->state = ENV_ATTACK;
    /* Don't reset cgain - allows retriggering from current level */
}

static inline void env_gate_off(bristol_env_t *env) {
    if (env->state != ENV_IDLE) {
        env->state = ENV_RELEASE;
    }
}

static inline float env_process(bristol_env_t *env, int gate, float sample_rate) {
    float duration = BRISTOL_ENV_DURATION;
    float param3;
    float multiplier;
    float attack_rate;

    switch (env->state) {
        case ENV_IDLE:
            env->cgain = 0.0f;
            break;

        case ENV_ATTACK:
            /* Bristol linear attack: rate = 12.0 / (sr * 0.0005 + duration * sr * param³) */
            param3 = env->attack * env->attack * env->attack;
            attack_rate = 1.0f / (sample_rate * 0.0005f + duration * sample_rate * param3);
            env->cgain += attack_rate;
            if (env->cgain >= 1.0f) {
                env->cgain = 1.0f;
                env->state = ENV_DECAY;
            }
            break;

        case ENV_DECAY:
            /* Bristol decay: multiplier = 1/pow(13.0, 1.0/(2.0 + param³ * sr * duration)) */
            param3 = env->decay * env->decay * env->decay;
            multiplier = 1.0f / powf(13.0f, 1.0f / (2.0f + param3 * sample_rate * duration));
            env->cgain *= multiplier;
            if (env->cgain <= env->sustain) {
                env->cgain = env->sustain;
                env->state = ENV_SUSTAIN;
            }
            break;

        case ENV_SUSTAIN:
            env->cgain = env->sustain;
            break;

        case ENV_RELEASE:
            /* Bristol release: same multiplier formula as decay */
            param3 = env->release * env->release * env->release;
            multiplier = 1.0f / powf(13.0f, 1.0f / (2.0f + param3 * sample_rate * duration));
            env->cgain *= multiplier;
            if (env->cgain <= 0.001f) {
                env->cgain = 0.0f;
                env->state = ENV_IDLE;
            }
            break;
    }

    return env->cgain;
}

/* ========================================================================
 * Moog Ladder Filter (Huovilainen algorithm)
 * ======================================================================== */

typedef struct {
    float stage[4];
    float stage_z1[4];
    float delay;

    /* Parameters */
    float cutoff;       /* 0-1 normalized (target) */
    float resonance;    /* 0-1 (self-oscillation at ~0.95+) */
    float env_amount;   /* Filter envelope depth */
    float key_track;    /* Keyboard tracking amount */

    /* Per-sample smoothed values */
    float cutoff_smooth;
    float resonance_smooth;
} bristol_filter_t;

static inline void filter_init(bristol_filter_t *f) {
    memset(f->stage, 0, sizeof(f->stage));
    memset(f->stage_z1, 0, sizeof(f->stage_z1));
    f->delay = 0.0f;
    f->cutoff = 0.5f;
    f->resonance = 0.0f;
    f->env_amount = 0.5f;
    f->key_track = 0.0f;
    f->cutoff_smooth = 0.5f;
    f->resonance_smooth = 0.0f;
}

static inline float filter_process(bristol_filter_t *f, float input, float env_mod,
                                   float lfo_mod, float key_freq, float sample_rate) {
    /* Per-sample parameter smoothing */
    const float smooth_coeff = 0.005f;
    f->cutoff_smooth += (f->cutoff - f->cutoff_smooth) * smooth_coeff;
    f->resonance_smooth += (f->resonance - f->resonance_smooth) * smooth_coeff;

    /* Calculate cutoff frequency */
    float cutoff_hz = 20.0f + f->cutoff_smooth * f->cutoff_smooth * 20000.0f;
    cutoff_hz += env_mod * f->env_amount * 10000.0f;
    cutoff_hz += lfo_mod * 2000.0f;

    if (f->key_track > 0.0f) {
        cutoff_hz += (key_freq - 261.63f) * f->key_track;
    }

    cutoff_hz = clampf(cutoff_hz, 20.0f, sample_rate * 0.45f);

    float fc = cutoff_hz / sample_rate;
    if (fc > 0.45f) fc = 0.45f;

    /* Huovilainen coefficients */
    float fc2 = fc * fc;
    float fc3 = fc2 * fc;
    float fcr = 1.8730f * fc3 + 0.4955f * fc2 - 0.6490f * fc + 0.9988f;
    float acr = -3.9364f * fc2 + 1.8409f * fc + 0.9968f;
    float g = 1.0f - expf(-2.0f * M_PI * fcr * fc);
    float res = f->resonance_smooth * 4.0f;
    float thermal = 0.000025f;

    /* 2x oversampling */
    float out = 0.0f;
    for (int os = 0; os < 2; os++) {
        float x = input * thermal - res * f->delay * acr;
        x = fast_tanh(x);

        for (int i = 0; i < 4; i++) {
            float y = f->stage[i] + g * (fast_tanh(x) - fast_tanh(f->stage[i]));
            f->stage[i] = y;
            x = y;
        }

        out = (f->stage[3] + f->delay) * 0.5f;
        f->delay = f->stage[3];
    }

    return out / thermal * 0.5f;
}

/* ========================================================================
 * LFO
 * ======================================================================== */

typedef struct {
    double phase;
    float rate;         /* 0-1 normalized */
    float depth;
    bristol_wave_t wave;
} bristol_lfo_t;

static inline void lfo_init(bristol_lfo_t *lfo) {
    lfo->phase = 0.0;
    lfo->rate = 0.3f;
    lfo->depth = 0.0f;
    lfo->wave = WAVE_TRI;
}

static inline float lfo_process(bristol_lfo_t *lfo, float sample_rate) {
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

#ifdef __cplusplus
}
#endif

#endif /* BRISTOL_COMMON_H */
