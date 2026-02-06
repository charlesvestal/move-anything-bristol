/*
 * bristol_engine.h - Minimoog-style synthesizer engine
 *
 * Inspired by Bristol Synthesizer by Nick Copeland
 * https://bristol.sourceforge.net/
 * Original Bristol is GPL-3.0 licensed
 *
 * Ported/rewritten for Move Anything plugin API.
 */

#ifndef BRISTOL_ENGINE_H
#define BRISTOL_ENGINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BRISTOL_SAMPLE_RATE 44100
#define BRISTOL_MAX_RENDER 256

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

/* Single oscillator state */
typedef struct {
    double phase;
    float last_output;
    int enabled;
    bristol_wave_t wave;
    int octave;         /* -2 to +2 octaves */
    float detune;       /* Fine tune in cents */
    float level;        /* Mix level 0-1 */
} bristol_osc_t;

/* ADSR envelope */
typedef struct {
    bristol_env_state_t state;
    float level;
    float attack_level;     /* Level at attack start (for retrigger) */
    float release_level;    /* Level at release start */
    double phase;

    /* Parameters (0-1 normalized) */
    float attack;
    float decay;
    float sustain;
    float release;
} bristol_env_t;

/* Moog ladder filter (Huovilainen) */
typedef struct {
    /* Filter stages */
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

/* LFO */
typedef struct {
    double phase;
    float rate;         /* 0-1 normalized (maps to ~0.1-20 Hz) */
    float depth;        /* Modulation depth */
    bristol_wave_t wave;
} bristol_lfo_t;

/* Noise generator */
typedef struct {
    uint32_t seed;
    float level;
    int type;           /* 0=white, 1=pink */
    /* Pink noise filter state */
    float pink_b0, pink_b1, pink_b2, pink_b3, pink_b4, pink_b5, pink_b6;
} bristol_noise_t;

/* Complete Mini synth engine */
typedef struct {
    float sample_rate;

    /* 3 Oscillators */
    bristol_osc_t osc[3];

    /* Noise */
    bristol_noise_t noise;

    /* Filter */
    bristol_filter_t filter;

    /* Envelopes */
    bristol_env_t filter_env;
    bristol_env_t amp_env;

    /* LFO */
    bristol_lfo_t lfo;

    /* Modulation routing */
    float lfo_to_osc;       /* LFO to oscillator pitch */
    float lfo_to_filter;    /* LFO to filter cutoff */
    int osc3_as_lfo;        /* Use OSC3 as modulation source */

    /* Master controls */
    float master_volume;
    float glide;            /* Portamento time */
    int multi_trigger;      /* Retrigger envelopes on each note */

    /* Voice state */
    int gate;
    int current_note;
    float current_freq;
    float target_freq;
    float velocity;
    float glide_freq;       /* Current glide frequency */

    /* Key stack for note priority */
    int key_stack[16];
    int key_stack_count;

    /* Pitch bend */
    float pitch_bend;
    float bend_range;       /* In semitones */

    /* Mod wheel */
    float mod_wheel;

    /* Tuning */
    float master_tune;      /* Master tune offset in cents */
    int octave_transpose;

} bristol_engine_t;

/* Initialize engine with defaults */
void bristol_engine_init(bristol_engine_t *engine);

/* Reset engine state (all notes off) */
void bristol_engine_reset(bristol_engine_t *engine);

/* Process MIDI note on */
void bristol_engine_note_on(bristol_engine_t *engine, int note, float velocity);

/* Process MIDI note off */
void bristol_engine_note_off(bristol_engine_t *engine, int note);

/* Process pitch bend (-1 to +1) */
void bristol_engine_pitch_bend(bristol_engine_t *engine, float bend);

/* Process mod wheel (0 to 1) */
void bristol_engine_mod_wheel(bristol_engine_t *engine, float amount);

/* Render audio block */
void bristol_engine_render(bristol_engine_t *engine, float *output, int frames);

/* All notes off */
void bristol_engine_all_notes_off(bristol_engine_t *engine);

#ifdef __cplusplus
}
#endif

#endif /* BRISTOL_ENGINE_H */
