/*
 * bristol_plugin.cpp - Move Anything plugin wrapper for Bristol Mini
 *
 * Plugin API v2 wrapper for the Bristol Minimoog emulation.
 * Loads Bristol's .mem preset files at runtime.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

extern "C" {
#include "mini_engine.h"
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
 * Bristol Mini Parameter Indices (from brightonMini.c locations array)
 *
 * These are the exact parameter indices used in Bristol's .mem files.
 * ======================================================================== */

/* Control section */
#define BRISTOL_MINI_TUNE           0   /* Master tune (0-1, center notched) */
#define BRISTOL_MINI_GLIDE          1   /* Glide time (0-1) */
#define BRISTOL_MINI_MOD            2   /* Mod amount (0-1) */

/* Oscillator section */
#define BRISTOL_MINI_OSC1_OCT       3   /* Osc1 transpose (0-5: 16',8',4',2',1',Lo) */
#define BRISTOL_MINI_OSC2_OCT       4   /* Osc2 transpose */
#define BRISTOL_MINI_OSC3_OCT       5   /* Osc3 transpose */
#define BRISTOL_MINI_OSC2_DETUNE    6   /* Osc2 fine tune (0-1, center notched) */
#define BRISTOL_MINI_OSC3_DETUNE    7   /* Osc3 fine tune */
#define BRISTOL_MINI_OSC1_WAVE      8   /* Osc1 waveform (0-5) */
#define BRISTOL_MINI_OSC2_WAVE      9   /* Osc2 waveform */
#define BRISTOL_MINI_OSC3_WAVE      10  /* Osc3 waveform */

/* Mixer section */
#define BRISTOL_MINI_OSC1_LEVEL     11  /* Osc1 mixer level (0-1) */
#define BRISTOL_MINI_EXT_LEVEL      12  /* External input level */
#define BRISTOL_MINI_OSC2_LEVEL     13  /* Osc2 mixer level */
#define BRISTOL_MINI_NOISE_LEVEL    14  /* Noise level */
#define BRISTOL_MINI_OSC3_LEVEL     15  /* Osc3 mixer level */

/* Filter section */
#define BRISTOL_MINI_FILTER_CUTOFF  16  /* Filter frequency (0-1) */
#define BRISTOL_MINI_FILTER_RES     17  /* Filter emphasis/resonance */
#define BRISTOL_MINI_FILTER_ENV     18  /* Filter contour amount */

/* Filter envelope (ADR - no sustain on original Mini) */
#define BRISTOL_MINI_FENV_A         19  /* Filter attack */
#define BRISTOL_MINI_FENV_D         20  /* Filter decay */
#define BRISTOL_MINI_FENV_R         21  /* Filter release */

/* Amp envelope (ADR - no sustain on original Mini) */
#define BRISTOL_MINI_AENV_A         22  /* Amp attack */
#define BRISTOL_MINI_AENV_D         23  /* Amp decay */
#define BRISTOL_MINI_AENV_R         24  /* Amp release */

/* Output */
#define BRISTOL_MINI_MASTER_VOL     25  /* Master volume */

/* Switches (buttons) - indices 26-41 */
#define BRISTOL_MINI_OSC3_LFO       36  /* Osc3 as LFO switch */
#define BRISTOL_MINI_FILTER_KBD     41  /* Filter keyboard tracking */

/* ========================================================================
 * Preset management - runtime loaded from .mem files
 * ======================================================================== */

#define MAX_PRESETS 128

struct MiniInstance {
    mini_engine_t engine;
    int current_preset;
    int preset_count;
    int octave_transpose;
    float gain;
    char module_dir[256];
    bristol_patch_t presets[MAX_PRESETS];
};

/* Map Bristol waveform index to our wave enum
 * Bristol: 0=tri, 1=ramp, 2=saw, 3=square, 4=pulse, 5=noise
 * Ours: WAVE_SINE=0, WAVE_TRI=1, WAVE_SAW=2, WAVE_SQUARE=3, WAVE_PULSE=4 */
static bristol_wave_t map_bristol_wave(float w) {
    int wave = (int)(w + 0.5f);
    switch (wave) {
        case 0: return WAVE_TRI;      /* tri */
        case 1: return WAVE_SAW;      /* ramp (same as saw for us) */
        case 2: return WAVE_SAW;      /* saw */
        case 3: return WAVE_SQUARE;   /* square */
        case 4: return WAVE_PULSE;    /* pulse */
        default: return WAVE_SAW;
    }
}

/* Map Bristol octave (0-5 = 16',8',4',2',1',Lo) to semitone offset */
static int map_bristol_octave(float oct) {
    int o = (int)(oct + 0.5f);
    /* 16'=0 is -2 oct, 8'=1 is -1 oct, 4'=2 is 0, 2'=3 is +1, 1'=4 is +2, Lo=5 is -2 (LFO range) */
    switch (o) {
        case 0: return -2;  /* 16' */
        case 1: return -1;  /* 8' */
        case 2: return 0;   /* 4' */
        case 3: return 1;   /* 2' */
        case 4: return 2;   /* 1' */
        case 5: return -2;  /* Lo (LFO range) */
        default: return 0;
    }
}

/* Get parameter with bounds checking */
static float get_bristol_param(const bristol_patch_t *patch, int idx, float def = 0.0f) {
    if (idx >= 0 && idx < patch->param_count) {
        return patch->params[idx];
    }
    return def;
}

static void apply_bristol_preset(MiniInstance *inst, int idx) {
    if (idx < 0 || idx >= inst->preset_count) return;

    const bristol_patch_t *p = &inst->presets[idx];
    mini_engine_t *e = &inst->engine;

    /* Reset engine state to clear filter and other artifacts */
    mini_engine_reset(e);

    /* Oscillators */
    e->osc[0].wave = map_bristol_wave(get_bristol_param(p, BRISTOL_MINI_OSC1_WAVE));
    e->osc[1].wave = map_bristol_wave(get_bristol_param(p, BRISTOL_MINI_OSC2_WAVE));
    e->osc[2].wave = map_bristol_wave(get_bristol_param(p, BRISTOL_MINI_OSC3_WAVE));

    e->osc[0].octave = map_bristol_octave(get_bristol_param(p, BRISTOL_MINI_OSC1_OCT));
    e->osc[1].octave = map_bristol_octave(get_bristol_param(p, BRISTOL_MINI_OSC2_OCT));
    e->osc[2].octave = map_bristol_octave(get_bristol_param(p, BRISTOL_MINI_OSC3_OCT));

    float osc1_level = get_bristol_param(p, BRISTOL_MINI_OSC1_LEVEL);
    float osc2_level = get_bristol_param(p, BRISTOL_MINI_OSC2_LEVEL);
    float osc3_level = get_bristol_param(p, BRISTOL_MINI_OSC3_LEVEL);

    e->osc[0].level = osc1_level;
    e->osc[1].level = osc2_level;
    e->osc[2].level = osc3_level;
    e->osc[0].enabled = osc1_level > 0.01f ? 1 : 0;
    e->osc[1].enabled = osc2_level > 0.01f ? 1 : 0;
    e->osc[2].enabled = osc3_level > 0.01f ? 1 : 0;

    /* Detune: Bristol stores 0-1 centered at 0.5, we want semitones */
    e->osc[1].detune = (get_bristol_param(p, BRISTOL_MINI_OSC2_DETUNE, 0.5f) - 0.5f) * 12.0f;
    e->osc[2].detune = (get_bristol_param(p, BRISTOL_MINI_OSC3_DETUNE, 0.5f) - 0.5f) * 12.0f;

    /* Filter */
    e->filter.cutoff = get_bristol_param(p, BRISTOL_MINI_FILTER_CUTOFF);
    e->filter.resonance = get_bristol_param(p, BRISTOL_MINI_FILTER_RES);
    e->filter.env_amount = get_bristol_param(p, BRISTOL_MINI_FILTER_ENV);
    e->filter.key_track = get_bristol_param(p, BRISTOL_MINI_FILTER_KBD) > 0.5f ? 1.0f : 0.0f;

    /* Filter envelope (ADR - Mini has no sustain, we set sustain to 0) */
    e->filter_env.attack = get_bristol_param(p, BRISTOL_MINI_FENV_A);
    e->filter_env.decay = get_bristol_param(p, BRISTOL_MINI_FENV_D);
    e->filter_env.sustain = 0.0f;  /* Mini has no filter sustain */
    e->filter_env.release = get_bristol_param(p, BRISTOL_MINI_FENV_R);

    /* Amp envelope (ADR - Mini has no sustain, we set sustain to decay level) */
    e->amp_env.attack = get_bristol_param(p, BRISTOL_MINI_AENV_A);
    e->amp_env.decay = get_bristol_param(p, BRISTOL_MINI_AENV_D);
    e->amp_env.sustain = 1.0f;  /* Hold at full after decay */
    e->amp_env.release = get_bristol_param(p, BRISTOL_MINI_AENV_R);

    /* LFO - Mini uses Osc3 as LFO when switch is on */
    e->osc3_as_lfo = get_bristol_param(p, BRISTOL_MINI_OSC3_LFO) > 0.5f ? 1 : 0;
    e->lfo.rate = 0.3f;  /* Default LFO rate */
    e->lfo_to_osc = get_bristol_param(p, BRISTOL_MINI_MOD) * 0.1f;
    e->lfo_to_filter = 0.0f;

    /* Other */
    e->noise.level = get_bristol_param(p, BRISTOL_MINI_NOISE_LEVEL);
    e->glide = get_bristol_param(p, BRISTOL_MINI_GLIDE);
    e->master_volume = get_bristol_param(p, BRISTOL_MINI_MASTER_VOL, 0.8f);

    inst->current_preset = idx;
}

static int load_presets(MiniInstance *inst) {
    char preset_path[512];
    snprintf(preset_path, sizeof(preset_path), "%s/presets/mini", inst->module_dir);

    int count = bristol_scan_presets(preset_path, inst->presets, MAX_PRESETS);

    if (count <= 0) {
        /* Try alternate path */
        snprintf(preset_path, sizeof(preset_path), "%s/presets", inst->module_dir);
        count = bristol_scan_presets(preset_path, inst->presets, MAX_PRESETS);
    }

    if (count > 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "[bristol-mini] Loaded %d presets from %s", count, preset_path);
        plugin_log(msg);
    } else {
        plugin_log("[bristol-mini] No presets found, using defaults");
        /* Create a default preset */
        count = 1;
        strcpy(inst->presets[0].name, "Init");
        inst->presets[0].param_count = 42;
        for (int i = 0; i < 42; i++) {
            inst->presets[0].params[i] = 0.5f;
        }
        /* Set some reasonable defaults */
        inst->presets[0].params[BRISTOL_MINI_OSC1_OCT] = 2.0f;  /* 4' */
        inst->presets[0].params[BRISTOL_MINI_OSC1_LEVEL] = 0.8f;
        inst->presets[0].params[BRISTOL_MINI_FILTER_CUTOFF] = 0.7f;
        inst->presets[0].params[BRISTOL_MINI_AENV_A] = 0.01f;
        inst->presets[0].params[BRISTOL_MINI_AENV_D] = 0.3f;
        inst->presets[0].params[BRISTOL_MINI_AENV_R] = 0.3f;
        inst->presets[0].params[BRISTOL_MINI_MASTER_VOL] = 0.8f;
    }

    return count;
}

/* ========================================================================
 * Plugin API implementation
 * ======================================================================== */

static void* bristol_create(const char *module_dir, const char *json_defaults) {
    (void)json_defaults;

    MiniInstance *inst = (MiniInstance*)calloc(1, sizeof(MiniInstance));
    if (!inst) return NULL;

    strncpy(inst->module_dir, module_dir ? module_dir : ".", sizeof(inst->module_dir) - 1);

    mini_engine_init(&inst->engine);
    inst->current_preset = 0;
    inst->octave_transpose = 0;
    inst->gain = 1.0f;

    /* Load presets from .mem files */
    inst->preset_count = load_presets(inst);

    /* Apply first preset */
    if (inst->preset_count > 0) {
        apply_bristol_preset(inst, 0);
    }

    plugin_log("[bristol-mini] Instance created");
    return inst;
}

static void bristol_destroy(void *instance) {
    MiniInstance *inst = (MiniInstance *)instance;
    if (!inst) return;
    free(inst);
    plugin_log("[bristol-mini] Destroyed instance");
}

static void bristol_on_midi(void *instance, const uint8_t *msg, int len, int source) {
    MiniInstance *inst = (MiniInstance *)instance;
    if (len < 1) return;

    uint8_t status = msg[0] & 0xF0;

    switch (status) {
        case 0x90: /* Note On */
            if (len >= 3 && msg[2] > 0) {
                int note = msg[1] + inst->octave_transpose * 12;
                float vel = msg[2] / 127.0f;
                mini_engine_note_on(&inst->engine, note, vel);
            } else if (len >= 2) {
                int note = msg[1] + inst->octave_transpose * 12;
                mini_engine_note_off(&inst->engine, note);
            }
            break;

        case 0x80: /* Note Off */
            if (len >= 2) {
                int note = msg[1] + inst->octave_transpose * 12;
                mini_engine_note_off(&inst->engine, note);
            }
            break;

        case 0xE0: /* Pitch Bend */
            if (len >= 3) {
                int bend = ((msg[2] << 7) | msg[1]) - 8192;
                float normalized = bend / 8192.0f;
                mini_engine_pitch_bend(&inst->engine, normalized);
            }
            break;

        case 0xB0: /* Control Change */
            if (len >= 3) {
                int cc = msg[1];
                float val = msg[2] / 127.0f;

                switch (cc) {
                    case 1: /* Mod wheel */
                        mini_engine_mod_wheel(&inst->engine, val);
                        break;
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
                    case 123: /* All Notes Off */
                        mini_engine_all_notes_off(&inst->engine);
                        break;
                }
            }
            break;
    }
}

static void bristol_set_param(void *instance, const char *key, const char *val) {
    MiniInstance *inst = (MiniInstance *)instance;
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
    } else if (strcmp(key, "lfo_to_osc") == 0) {
        inst->engine.lfo_to_osc = fval;
    } else if (strcmp(key, "glide") == 0) {
        inst->engine.glide = fval;
    } else if (strcmp(key, "osc1_level") == 0) {
        inst->engine.osc[0].level = fval;
        inst->engine.osc[0].enabled = fval > 0.0f ? 1 : 0;
    } else if (strcmp(key, "osc2_level") == 0) {
        inst->engine.osc[1].level = fval;
        inst->engine.osc[1].enabled = fval > 0.0f ? 1 : 0;
    } else if (strcmp(key, "osc3_level") == 0) {
        inst->engine.osc[2].level = fval;
        inst->engine.osc[2].enabled = fval > 0.0f ? 1 : 0;
    } else if (strcmp(key, "osc1_wave") == 0) {
        inst->engine.osc[0].wave = (bristol_wave_t)ival;
    } else if (strcmp(key, "osc2_wave") == 0) {
        inst->engine.osc[1].wave = (bristol_wave_t)ival;
    } else if (strcmp(key, "osc3_wave") == 0) {
        inst->engine.osc[2].wave = (bristol_wave_t)ival;
    } else if (strcmp(key, "noise_level") == 0) {
        inst->engine.noise.level = fval;
    } else if (strcmp(key, "master_volume") == 0) {
        inst->engine.master_volume = fval;
    }
}

static int bristol_get_param(void *instance, const char *key, char *buf, int buf_len) {
    MiniInstance *inst = (MiniInstance *)instance;

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
        return snprintf(buf, buf_len, "%.2f", inst->gain);
    } else if (strcmp(key, "filter_cutoff") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.filter.cutoff);
    } else if (strcmp(key, "filter_resonance") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.filter.resonance);
    } else if (strcmp(key, "ui_hierarchy") == 0) {
        return snprintf(buf, buf_len, R"({
  "modes": null,
  "levels": {
    "root": {
      "label": "Bristol",
      "list_param": "preset",
      "count_param": "preset_count",
      "name_param": "preset_name",
      "knobs": ["filter_cutoff", "filter_resonance", "filter_env_amount", "lfo_rate", "amp_attack", "amp_decay", "amp_sustain", "amp_release"],
      "params": [
        {"key": "filter_cutoff", "label": "Cutoff"},
        {"key": "filter_resonance", "label": "Resonance"},
        {"key": "filter_env_amount", "label": "Env Amt"},
        {"key": "lfo_rate", "label": "LFO Rate"},
        {"key": "octave_transpose", "label": "Octave"},
        {"key": "gain", "label": "Gain"},
        {"level": "oscillators", "label": "Oscillators"},
        {"level": "envelopes", "label": "Envelopes"}
      ]
    },
    "oscillators": {
      "label": "Oscillators",
      "knobs": ["osc1_level", "osc2_level", "osc3_level", "noise_level", "osc1_wave", "osc2_wave", "osc3_wave", "glide"],
      "params": [
        {"key": "osc1_level", "label": "OSC1 Level"},
        {"key": "osc2_level", "label": "OSC2 Level"},
        {"key": "osc3_level", "label": "OSC3 Level"},
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

static int bristol_get_error(void *instance, char *buf, int buf_len) {
    (void)instance;
    (void)buf;
    (void)buf_len;
    return 0;  /* No error */
}

static void bristol_render(void *instance, int16_t *out_lr, int frames) {
    MiniInstance *inst = (MiniInstance *)instance;
    if (!inst) {
        memset(out_lr, 0, frames * 4);
        return;
    }

    float mono_buf[256];
    if (frames > 256) frames = 256;

    mini_engine_render(&inst->engine, mono_buf, frames);

    /* Convert to stereo int16 */
    float gain = inst->gain;
    for (int i = 0; i < frames; i++) {
        float sample = mono_buf[i] * gain;

        /* Clamp */
        int32_t s = (int32_t)(sample * 32767.0f);
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;

        out_lr[i * 2] = (int16_t)s;
        out_lr[i * 2 + 1] = (int16_t)s;
    }
}

/* ========================================================================
 * Plugin entry point
 * ======================================================================== */

static plugin_api_v2_t g_api = {
    .api_version = 2,
    .create_instance = bristol_create,
    .destroy_instance = bristol_destroy,
    .on_midi = bristol_on_midi,
    .set_param = bristol_set_param,
    .get_param = bristol_get_param,
    .get_error = bristol_get_error,
    .render_block = bristol_render
};

extern "C" plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t *host) {
    g_host = host;
    plugin_log("[bristol-mini] Plugin v2 initialized");
    return &g_api;
}
