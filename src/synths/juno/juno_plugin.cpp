/*
 * juno_plugin.cpp - Move Anything plugin wrapper for Bristol Juno
 *
 * Plugin API v2 wrapper for the Juno-style DCO synth with chorus.
 * Loads Bristol's .mem preset files at runtime.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "juno_engine.h"
#include "../../shared/bristol_mem_loader.h"
}

/* Move Anything Plugin API */
extern "C" {

typedef struct host_api_v1 {
    uint32_t api_version;
    int sample_rate;
    int frames_per_block;
    uint8_t *mapped_memory;
    int audio_out_offset;
    int audio_in_offset;
    void (*log)(const char *msg);
    int (*midi_send_internal)(const uint8_t *msg, int len);
    int (*midi_send_external)(const uint8_t *msg, int len);
} host_api_v1_t;

typedef struct plugin_api_v2 {
    uint32_t api_version;
    void* (*create_instance)(const char *module_dir, const char *json_defaults);
    void (*destroy_instance)(void *instance);
    void (*on_midi)(void *instance, const uint8_t *msg, int len, int source);
    void (*set_param)(void *instance, const char *key, const char *val);
    int (*get_param)(void *instance, const char *key, char *buf, int buf_len);
    int (*get_error)(void *instance, char *buf, int buf_len);
    void (*render_block)(void *instance, int16_t *out_lr, int frames);
} plugin_api_v2_t;

} /* extern "C" */

static const host_api_v1_t *g_host = nullptr;

static void plugin_log(const char *msg) {
    if (g_host && g_host->log) {
        g_host->log(msg);
    }
}

/* ========================================================================
 * Bristol Juno Parameter Indices (from brightonJuno.c locations array)
 *
 * These are the exact parameter indices used in Bristol's .mem files.
 * ======================================================================== */

#define BRISTOL_JUNO_POWER          0   /* Power button */
#define BRISTOL_JUNO_DCO_MOD        1   /* Mod wheel -> DCO amount */
#define BRISTOL_JUNO_VCF_MOD        2   /* Mod wheel -> VCF amount */
#define BRISTOL_JUNO_TUNING         3   /* Master tuning */
#define BRISTOL_JUNO_GLIDE          4   /* Glide time */
#define BRISTOL_JUNO_LFO_MANUAL     5   /* LFO manual trigger button */
#define BRISTOL_JUNO_TRANSPOSE      6   /* Transpose (3-way: 0=low, 1=mid, 2=high) */
#define BRISTOL_JUNO_HOLD           7   /* Hold button */
#define BRISTOL_JUNO_LFO_RATE       8   /* LFO rate */
#define BRISTOL_JUNO_LFO_DELAY      9   /* LFO delay */
#define BRISTOL_JUNO_MAN_AUTO       10  /* Manual/Auto switch */
#define BRISTOL_JUNO_DCO_LFO        11  /* DCO LFO modulation amount */
#define BRISTOL_JUNO_PWM            12  /* PWM amount */
#define BRISTOL_JUNO_PWM_SOURCE     13  /* PWM source (3-way: man/lfo/env) */
#define BRISTOL_JUNO_PULSE          14  /* Pulse waveform on/off */
#define BRISTOL_JUNO_SAW            15  /* Saw/Ramp waveform on/off */
#define BRISTOL_JUNO_SQUARE         16  /* Square/Sub oscillator on/off */
#define BRISTOL_JUNO_SUB_LEVEL      17  /* Sub oscillator level */
#define BRISTOL_JUNO_NOISE          18  /* Noise level */
#define BRISTOL_JUNO_HPF            19  /* High-pass filter */
#define BRISTOL_JUNO_VCF_FREQ       20  /* VCF cutoff frequency */
#define BRISTOL_JUNO_VCF_RES        21  /* VCF resonance */
#define BRISTOL_JUNO_ENV_POLARITY   22  /* Envelope +/- polarity */
#define BRISTOL_JUNO_VCF_ENV        23  /* VCF envelope amount */
#define BRISTOL_JUNO_VCF_LFO        24  /* VCF LFO amount */
#define BRISTOL_JUNO_VCF_KBD        25  /* VCF keyboard tracking */
#define BRISTOL_JUNO_VCA_MODE       26  /* VCA env/gate switch */
#define BRISTOL_JUNO_VCA_LEVEL      27  /* VCA level */
#define BRISTOL_JUNO_ENV_A          28  /* Attack */
#define BRISTOL_JUNO_ENV_D          29  /* Decay */
#define BRISTOL_JUNO_ENV_S          30  /* Sustain */
#define BRISTOL_JUNO_ENV_R          31  /* Release */
#define BRISTOL_JUNO_CHORUS_1       32  /* Chorus I button */
#define BRISTOL_JUNO_CHORUS_2       33  /* Chorus II button */
#define BRISTOL_JUNO_CHORUS_12      34  /* Chorus I+II button */

/* ========================================================================
 * Preset management - runtime loaded from .mem files
 * ======================================================================== */

#define MAX_PRESETS 128

struct JunoInstance {
    juno_engine_t engine;
    int current_preset;
    int preset_count;
    int octave_transpose;
    float gain;
    char module_dir[256];
    bristol_patch_t presets[MAX_PRESETS];
};

/* Get parameter with bounds checking */
static float get_bristol_param(const bristol_patch_t *patch, int idx, float def = 0.0f) {
    if (idx >= 0 && idx < patch->param_count) {
        return patch->params[idx];
    }
    return def;
}

/* Determine chorus mode from the three chorus buttons */
static int get_chorus_mode(const bristol_patch_t *p) {
    int mode = 0;
    if (get_bristol_param(p, BRISTOL_JUNO_CHORUS_1) > 0.5f) mode = 1;
    if (get_bristol_param(p, BRISTOL_JUNO_CHORUS_2) > 0.5f) mode = (mode == 0) ? 2 : 3;
    if (get_bristol_param(p, BRISTOL_JUNO_CHORUS_12) > 0.5f) mode = 3;
    return mode;
}

static void apply_bristol_preset(JunoInstance *inst, int idx) {
    if (idx < 0 || idx >= inst->preset_count) return;

    const bristol_patch_t *p = &inst->presets[idx];
    juno_engine_t *e = &inst->engine;

    /* Reset engine state to clear chorus buffer and other artifacts */
    juno_engine_reset(e);

    /* DCO waveform enables */
    e->saw_enabled = get_bristol_param(p, BRISTOL_JUNO_SAW) > 0.5f ? 1 : 0;
    e->pulse_enabled = get_bristol_param(p, BRISTOL_JUNO_PULSE) > 0.5f ? 1 : 0;
    e->sub_enabled = get_bristol_param(p, BRISTOL_JUNO_SQUARE) > 0.5f ? 1 : 0;

    /* PWM */
    e->pulse_width = 0.5f;  /* Base pulse width */
    e->pw_lfo_amount = get_bristol_param(p, BRISTOL_JUNO_PWM);
    e->sub_level = get_bristol_param(p, BRISTOL_JUNO_SUB_LEVEL);

    /* Filter */
    e->filter.cutoff = get_bristol_param(p, BRISTOL_JUNO_VCF_FREQ);
    e->filter.resonance = get_bristol_param(p, BRISTOL_JUNO_VCF_RES);
    e->filter.env_amount = get_bristol_param(p, BRISTOL_JUNO_VCF_ENV);
    e->filter.key_track = get_bristol_param(p, BRISTOL_JUNO_VCF_KBD);

    /* HPF: Bristol stores 0-1, convert to Hz (roughly 20-220 Hz) */
    e->hpf_cutoff = 20.0f + get_bristol_param(p, BRISTOL_JUNO_HPF) * 200.0f;

    /* Envelope (ADSR - shared between filter and amp) */
    float attack = get_bristol_param(p, BRISTOL_JUNO_ENV_A);
    float decay = get_bristol_param(p, BRISTOL_JUNO_ENV_D);
    float sustain = get_bristol_param(p, BRISTOL_JUNO_ENV_S);
    float release = get_bristol_param(p, BRISTOL_JUNO_ENV_R);

    e->filter_env.attack = attack;
    e->filter_env.decay = decay;
    e->filter_env.sustain = sustain;
    e->filter_env.release = release;

    e->amp_env.attack = attack;
    e->amp_env.decay = decay;
    e->amp_env.sustain = sustain;
    e->amp_env.release = release;

    /* LFO */
    e->lfo.rate = get_bristol_param(p, BRISTOL_JUNO_LFO_RATE);
    e->lfo_to_dco = get_bristol_param(p, BRISTOL_JUNO_DCO_LFO);
    e->lfo_to_filter = get_bristol_param(p, BRISTOL_JUNO_VCF_LFO);

    /* Chorus */
    e->chorus.mode = get_chorus_mode(p);

    /* Other */
    e->noise.level = get_bristol_param(p, BRISTOL_JUNO_NOISE);
    e->glide = get_bristol_param(p, BRISTOL_JUNO_GLIDE);
    e->master_volume = get_bristol_param(p, BRISTOL_JUNO_VCA_LEVEL, 0.8f);

    inst->current_preset = idx;
}

static int load_presets(JunoInstance *inst) {
    char preset_path[512];
    snprintf(preset_path, sizeof(preset_path), "%s/presets/juno", inst->module_dir);

    int count = bristol_scan_presets(preset_path, inst->presets, MAX_PRESETS);

    if (count <= 0) {
        /* Try alternate path */
        snprintf(preset_path, sizeof(preset_path), "%s/presets", inst->module_dir);
        count = bristol_scan_presets(preset_path, inst->presets, MAX_PRESETS);
    }

    if (count > 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "[bristol-juno] Loaded %d presets from %s", count, preset_path);
        plugin_log(msg);
    } else {
        plugin_log("[bristol-juno] No presets found, using defaults");
        /* Create a default preset */
        count = 1;
        strcpy(inst->presets[0].name, "Init");
        inst->presets[0].param_count = 35;
        for (int i = 0; i < 35; i++) {
            inst->presets[0].params[i] = 0.5f;
        }
        /* Set some reasonable defaults */
        inst->presets[0].params[BRISTOL_JUNO_SAW] = 1.0f;  /* Saw on */
        inst->presets[0].params[BRISTOL_JUNO_VCF_FREQ] = 0.7f;
        inst->presets[0].params[BRISTOL_JUNO_VCA_LEVEL] = 0.8f;
        inst->presets[0].params[BRISTOL_JUNO_ENV_A] = 0.01f;
        inst->presets[0].params[BRISTOL_JUNO_ENV_D] = 0.3f;
        inst->presets[0].params[BRISTOL_JUNO_ENV_S] = 0.7f;
        inst->presets[0].params[BRISTOL_JUNO_ENV_R] = 0.3f;
    }

    return count;
}

/* ========================================================================
 * Plugin API implementation
 * ======================================================================== */

static void* juno_create(const char *module_dir, const char *json_defaults) {
    (void)json_defaults;

    JunoInstance *inst = (JunoInstance*)calloc(1, sizeof(JunoInstance));
    if (!inst) return NULL;

    strncpy(inst->module_dir, module_dir ? module_dir : ".", sizeof(inst->module_dir) - 1);

    juno_engine_init(&inst->engine);
    inst->current_preset = 0;
    inst->octave_transpose = 0;
    inst->gain = 1.0f;

    /* Load presets from .mem files */
    inst->preset_count = load_presets(inst);

    /* Apply first preset */
    if (inst->preset_count > 0) {
        apply_bristol_preset(inst, 0);
    }

    plugin_log("[bristol-juno] Instance created");
    return inst;
}

static void juno_destroy(void *instance) {
    JunoInstance *inst = (JunoInstance *)instance;
    if (!inst) return;
    free(inst);
    plugin_log("[bristol-juno] Destroyed instance");
}

static void juno_on_midi(void *instance, const uint8_t *msg, int len, int source) {
    JunoInstance *inst = (JunoInstance *)instance;
    if (len < 1) return;

    uint8_t status = msg[0] & 0xF0;

    switch (status) {
        case 0x90: /* Note On */
            if (len >= 3 && msg[2] > 0) {
                int note = msg[1] + inst->octave_transpose * 12;
                float vel = msg[2] / 127.0f;
                juno_engine_note_on(&inst->engine, note, vel);
            } else if (len >= 2) {
                int note = msg[1] + inst->octave_transpose * 12;
                juno_engine_note_off(&inst->engine, note);
            }
            break;

        case 0x80: /* Note Off */
            if (len >= 2) {
                int note = msg[1] + inst->octave_transpose * 12;
                juno_engine_note_off(&inst->engine, note);
            }
            break;

        case 0xE0: /* Pitch Bend */
            if (len >= 3) {
                int bend = ((msg[2] << 7) | msg[1]) - 8192;
                float normalized = bend / 8192.0f;
                juno_engine_pitch_bend(&inst->engine, normalized);
            }
            break;

        case 0xB0: /* Control Change */
            if (len >= 3) {
                int cc = msg[1];
                float val = msg[2] / 127.0f;

                switch (cc) {
                    case 74: /* Filter cutoff */
                        inst->engine.filter.cutoff = val;
                        break;
                    case 71: /* Resonance */
                        inst->engine.filter.resonance = val;
                        break;
                    case 73: /* Attack */
                        inst->engine.amp_env.attack = val;
                        inst->engine.filter_env.attack = val;
                        break;
                    case 75: /* Decay */
                        inst->engine.amp_env.decay = val;
                        inst->engine.filter_env.decay = val;
                        break;
                    case 79: /* Sustain */
                        inst->engine.amp_env.sustain = val;
                        break;
                    case 72: /* Release */
                        inst->engine.amp_env.release = val;
                        inst->engine.filter_env.release = val;
                        break;
                    case 5: /* Glide/Portamento */
                        inst->engine.glide = val;
                        break;
                    case 93: /* Chorus mode (0-3) */
                        inst->engine.chorus.mode = (int)(val * 3.99f);
                        break;
                    case 123: /* All Notes Off */
                        juno_engine_all_notes_off(&inst->engine);
                        break;
                }
            }
            break;
    }
}

static void juno_set_param(void *instance, const char *key, const char *val) {
    JunoInstance *inst = (JunoInstance *)instance;
    float fval = (float)atof(val);
    int ival = atoi(val);

    if (strcmp(key, "preset") == 0) {
        apply_bristol_preset(inst, ival);
    } else if (strcmp(key, "octave_transpose") == 0) {
        inst->octave_transpose = ival;
    } else if (strcmp(key, "gain") == 0) {
        inst->gain = fval;
    } else if (strcmp(key, "filter_cutoff") == 0) {
        inst->engine.filter.cutoff = fval;
    } else if (strcmp(key, "filter_resonance") == 0) {
        inst->engine.filter.resonance = fval;
    } else if (strcmp(key, "filter_env_amount") == 0) {
        inst->engine.filter.env_amount = fval;
    } else if (strcmp(key, "filter_keytrack") == 0) {
        inst->engine.filter.key_track = fval;
    } else if (strcmp(key, "amp_attack") == 0) {
        inst->engine.amp_env.attack = fval;
    } else if (strcmp(key, "amp_decay") == 0) {
        inst->engine.amp_env.decay = fval;
    } else if (strcmp(key, "amp_sustain") == 0) {
        inst->engine.amp_env.sustain = fval;
    } else if (strcmp(key, "amp_release") == 0) {
        inst->engine.amp_env.release = fval;
    } else if (strcmp(key, "filter_attack") == 0) {
        inst->engine.filter_env.attack = fval;
    } else if (strcmp(key, "filter_decay") == 0) {
        inst->engine.filter_env.decay = fval;
    } else if (strcmp(key, "filter_sustain") == 0) {
        inst->engine.filter_env.sustain = fval;
    } else if (strcmp(key, "filter_release") == 0) {
        inst->engine.filter_env.release = fval;
    } else if (strcmp(key, "lfo_rate") == 0) {
        inst->engine.lfo.rate = fval;
    } else if (strcmp(key, "lfo_to_filter") == 0) {
        inst->engine.lfo_to_filter = fval;
    } else if (strcmp(key, "lfo_to_dco") == 0) {
        inst->engine.lfo_to_dco = fval;
    } else if (strcmp(key, "glide") == 0) {
        inst->engine.glide = fval;
    } else if (strcmp(key, "saw_enabled") == 0) {
        inst->engine.saw_enabled = ival;
    } else if (strcmp(key, "pulse_enabled") == 0) {
        inst->engine.pulse_enabled = ival;
    } else if (strcmp(key, "sub_enabled") == 0) {
        inst->engine.sub_enabled = ival;
    } else if (strcmp(key, "pulse_width") == 0) {
        inst->engine.pulse_width = fval;
    } else if (strcmp(key, "pw_lfo_amount") == 0) {
        inst->engine.pw_lfo_amount = fval;
    } else if (strcmp(key, "sub_level") == 0) {
        inst->engine.sub_level = fval;
    } else if (strcmp(key, "chorus_mode") == 0) {
        inst->engine.chorus.mode = ival;
    } else if (strcmp(key, "noise_level") == 0) {
        inst->engine.noise.level = fval;
    } else if (strcmp(key, "master_volume") == 0) {
        inst->engine.master_volume = fval;
    }
}

static int juno_get_param(void *instance, const char *key, char *buf, int buf_len) {
    JunoInstance *inst = (JunoInstance *)instance;

    if (strcmp(key, "preset") == 0) {
        return snprintf(buf, buf_len, "%d", inst->current_preset);
    } else if (strcmp(key, "preset_count") == 0) {
        return snprintf(buf, buf_len, "%d", inst->preset_count);
    } else if (strcmp(key, "preset_name") == 0) {
        if (inst->current_preset >= 0 && inst->current_preset < inst->preset_count) {
            return snprintf(buf, buf_len, "%s", inst->presets[inst->current_preset].name);
        }
        return snprintf(buf, buf_len, "Init");
    } else if (strcmp(key, "octave_transpose") == 0) {
        return snprintf(buf, buf_len, "%d", inst->octave_transpose);
    } else if (strcmp(key, "gain") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->gain);
    /* Filter parameters */
    } else if (strcmp(key, "filter_cutoff") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.filter.cutoff);
    } else if (strcmp(key, "filter_resonance") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.filter.resonance);
    } else if (strcmp(key, "filter_env_amount") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.filter.env_amount);
    } else if (strcmp(key, "filter_keytrack") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.filter.key_track);
    /* Amp envelope */
    } else if (strcmp(key, "amp_attack") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.amp_env.attack);
    } else if (strcmp(key, "amp_decay") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.amp_env.decay);
    } else if (strcmp(key, "amp_sustain") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.amp_env.sustain);
    } else if (strcmp(key, "amp_release") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.amp_env.release);
    /* Filter envelope */
    } else if (strcmp(key, "filter_attack") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.filter_env.attack);
    } else if (strcmp(key, "filter_decay") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.filter_env.decay);
    } else if (strcmp(key, "filter_sustain") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.filter_env.sustain);
    } else if (strcmp(key, "filter_release") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.filter_env.release);
    /* LFO */
    } else if (strcmp(key, "lfo_rate") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.lfo.rate);
    } else if (strcmp(key, "lfo_to_filter") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.lfo_to_filter);
    } else if (strcmp(key, "lfo_to_dco") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.lfo_to_dco);
    /* DCO */
    } else if (strcmp(key, "saw_enabled") == 0) {
        return snprintf(buf, buf_len, "%d", inst->engine.saw_enabled);
    } else if (strcmp(key, "pulse_enabled") == 0) {
        return snprintf(buf, buf_len, "%d", inst->engine.pulse_enabled);
    } else if (strcmp(key, "sub_enabled") == 0) {
        return snprintf(buf, buf_len, "%d", inst->engine.sub_enabled);
    } else if (strcmp(key, "sub_level") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.sub_level);
    } else if (strcmp(key, "pulse_width") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.pulse_width);
    } else if (strcmp(key, "pw_lfo_amount") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.pw_lfo_amount);
    } else if (strcmp(key, "noise_level") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.noise.level);
    /* Other */
    } else if (strcmp(key, "glide") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.glide);
    } else if (strcmp(key, "chorus_mode") == 0) {
        return snprintf(buf, buf_len, "%d", inst->engine.chorus.mode);
    } else if (strcmp(key, "master_volume") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.master_volume);
    } else if (strcmp(key, "ui_hierarchy") == 0) {
        return snprintf(buf, buf_len, R"({
  "modes": null,
  "levels": {
    "root": {
      "label": "Juno",
      "list_param": "preset",
      "count_param": "preset_count",
      "name_param": "preset_name",
      "knobs": ["filter_cutoff", "filter_resonance", "filter_env_amount", "lfo_rate", "amp_attack", "amp_decay", "amp_sustain", "amp_release"],
      "params": [
        {"key": "filter_cutoff", "label": "Cutoff"},
        {"key": "filter_resonance", "label": "Resonance"},
        {"key": "filter_env_amount", "label": "Env Amt"},
        {"key": "chorus_mode", "label": "Chorus Mode"},
        {"key": "octave_transpose", "label": "Octave"},
        {"key": "gain", "label": "Gain"},
        {"level": "dco", "label": "DCO"},
        {"level": "envelopes", "label": "Envelopes"}
      ]
    },
    "dco": {
      "label": "DCO",
      "knobs": ["saw_enabled", "pulse_enabled", "sub_enabled", "sub_level", "pulse_width", "pw_lfo_amount", "noise_level", "glide"],
      "params": [
        {"key": "saw_enabled", "label": "Saw"},
        {"key": "pulse_enabled", "label": "Pulse"},
        {"key": "sub_enabled", "label": "Sub"},
        {"key": "sub_level", "label": "Sub Level"},
        {"key": "noise_level", "label": "Noise"},
        {"key": "glide", "label": "Glide"}
      ]
    },
    "envelopes": {
      "label": "Envelopes",
      "knobs": ["amp_attack", "amp_decay", "amp_sustain", "amp_release", "filter_attack", "filter_decay", "filter_sustain", "filter_release"],
      "params": [
        {"key": "amp_attack", "label": "Amp Attack"},
        {"key": "amp_decay", "label": "Amp Decay"},
        {"key": "amp_sustain", "label": "Amp Sustain"},
        {"key": "amp_release", "label": "Amp Release"},
        {"key": "filter_attack", "label": "Flt Attack"},
        {"key": "filter_decay", "label": "Flt Decay"},
        {"key": "filter_sustain", "label": "Flt Sustain"},
        {"key": "filter_release", "label": "Flt Release"}
      ]
    }
  }
})");
    } else if (strcmp(key, "chain_params") == 0) {
        return snprintf(buf, buf_len, R"([
  {"label": "Cutoff", "cc": 74, "value": %d, "type": "float", "min": 0, "max": 1, "key": "filter_cutoff"},
  {"label": "Reso", "cc": 71, "value": %d, "type": "float", "min": 0, "max": 1, "key": "filter_resonance"},
  {"label": "Env Amt", "cc": 70, "value": %d, "type": "float", "min": 0, "max": 1, "key": "filter_env_amount"},
  {"label": "LFO Rate", "cc": 76, "value": %d, "type": "float", "min": 0, "max": 1, "key": "lfo_rate"},
  {"label": "Attack", "cc": 73, "value": %d, "type": "float", "min": 0, "max": 1, "key": "amp_attack"},
  {"label": "Decay", "cc": 75, "value": %d, "type": "float", "min": 0, "max": 1, "key": "amp_decay"},
  {"label": "Sustain", "cc": 79, "value": %d, "type": "float", "min": 0, "max": 1, "key": "amp_sustain"},
  {"label": "Release", "cc": 72, "value": %d, "type": "float", "min": 0, "max": 1, "key": "amp_release"}
])",
            (int)(inst->engine.filter.cutoff * 127),
            (int)(inst->engine.filter.resonance * 127),
            (int)(inst->engine.filter.env_amount * 127),
            (int)(inst->engine.lfo.rate * 127),
            (int)(inst->engine.amp_env.attack * 127),
            (int)(inst->engine.amp_env.decay * 127),
            (int)(inst->engine.amp_env.sustain * 127),
            (int)(inst->engine.amp_env.release * 127)
        );
    }

    return 0;
}

static int juno_get_error(void *instance, char *buf, int buf_len) {
    (void)instance;
    (void)buf;
    (void)buf_len;
    return 0;  /* No error */
}

static void juno_render(void *instance, int16_t *out_lr, int frames) {
    JunoInstance *inst = (JunoInstance *)instance;
    if (!inst) {
        memset(out_lr, 0, frames * 4);
        return;
    }

    float mono_buf[256];
    if (frames > 256) frames = 256;

    juno_engine_render(&inst->engine, mono_buf, frames);

    /* Convert mono to stereo int16 */
    float gain = inst->gain;
    for (int i = 0; i < frames; i++) {
        float sample = mono_buf[i] * gain;

        /* Clamp */
        int32_t s = (int32_t)(sample * 32767.0f);
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;

        out_lr[i * 2] = (int16_t)s;      /* Left */
        out_lr[i * 2 + 1] = (int16_t)s;  /* Right */
    }
}

/* ========================================================================
 * Plugin entry point
 * ======================================================================== */

static plugin_api_v2_t g_api = {
    .api_version = 2,
    .create_instance = juno_create,
    .destroy_instance = juno_destroy,
    .on_midi = juno_on_midi,
    .set_param = juno_set_param,
    .get_param = juno_get_param,
    .get_error = juno_get_error,
    .render_block = juno_render
};

extern "C" plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t *host) {
    g_host = host;
    plugin_log("[bristol-juno] Plugin v2 initialized");
    return &g_api;
}
