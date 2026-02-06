/*
 * bristol_plugin.cpp - Move Anything plugin wrapper for Bristol Mini
 *
 * Plugin API v2 wrapper for the Bristol Minimoog emulation.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

extern "C" {
#include "bristol_engine.h"
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
 * Preset definitions
 * ======================================================================== */

struct BristolPreset {
    const char *name;
    /* Oscillator settings */
    int osc1_wave, osc2_wave, osc3_wave;
    int osc1_oct, osc2_oct, osc3_oct;
    float osc1_level, osc2_level, osc3_level;
    float osc2_detune, osc3_detune;
    /* Filter */
    float filter_cutoff, filter_res, filter_env, filter_keytrack;
    /* Filter envelope */
    float fenv_a, fenv_d, fenv_s, fenv_r;
    /* Amp envelope */
    float aenv_a, aenv_d, aenv_s, aenv_r;
    /* LFO */
    float lfo_rate, lfo_to_osc, lfo_to_filter;
    /* Other */
    float noise_level;
    float glide;
    int osc3_as_lfo;
};

static const BristolPreset g_presets[] = {
    /* 0: Init - Clean Sawtooth */
    { "Init",
      WAVE_SAW, WAVE_SAW, WAVE_SAW, 0, 0, -1,
      1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
      0.4f, 0.0f, 0.6f, 0.0f,
      0.01f, 0.2f, 0.0f, 0.2f,
      0.01f, 0.1f, 1.0f, 0.3f,
      0.3f, 0.0f, 0.0f,
      0.0f, 0.0f, 0 },

    /* 1: Classic Mini Lead */
    { "Classic Lead",
      WAVE_SAW, WAVE_SAW, WAVE_SAW, 0, 0, 0,
      1.0f, 0.8f, 0.0f, 7.0f, 0.0f,
      0.35f, 0.4f, 0.7f, 0.3f,
      0.01f, 0.3f, 0.2f, 0.3f,
      0.01f, 0.1f, 1.0f, 0.25f,
      0.4f, 0.02f, 0.0f,
      0.0f, 0.05f, 0 },

    /* 2: Fat Bass */
    { "Fat Bass",
      WAVE_SAW, WAVE_SQUARE, WAVE_SAW, -1, -1, -2,
      1.0f, 0.7f, 0.5f, 5.0f, 0.0f,
      0.2f, 0.5f, 0.5f, 0.0f,
      0.01f, 0.4f, 0.0f, 0.15f,
      0.01f, 0.2f, 1.0f, 0.15f,
      0.3f, 0.0f, 0.0f,
      0.0f, 0.0f, 0 },

    /* 3: Resonant Sweep */
    { "Resonant Sweep",
      WAVE_SAW, WAVE_SAW, WAVE_TRI, 0, 0, -2,
      1.0f, 0.6f, 0.0f, 3.0f, 0.0f,
      0.15f, 0.85f, 0.8f, 0.0f,
      0.3f, 1.0f, 0.1f, 0.5f,
      0.01f, 0.05f, 1.0f, 0.4f,
      0.2f, 0.0f, 0.3f,
      0.0f, 0.1f, 1 },

    /* 4: PWM Pad */
    { "PWM Strings",
      WAVE_PULSE, WAVE_PULSE, WAVE_SAW, 0, 0, 0,
      0.8f, 0.8f, 0.3f, 8.0f, -8.0f,
      0.5f, 0.3f, 0.4f, 0.2f,
      0.01f, 0.3f, 0.6f, 0.4f,
      0.5f, 0.3f, 0.9f, 0.8f,
      0.25f, 0.0f, 0.15f,
      0.0f, 0.0f, 0 },

    /* 5: Acid Squelch */
    { "Acid Squelch",
      WAVE_SAW, WAVE_SQUARE, WAVE_SAW, 0, 0, -1,
      1.0f, 0.5f, 0.0f, 0.0f, 0.0f,
      0.1f, 0.9f, 0.85f, 0.0f,
      0.01f, 0.25f, 0.0f, 0.1f,
      0.01f, 0.15f, 1.0f, 0.1f,
      0.3f, 0.0f, 0.0f,
      0.0f, 0.0f, 0 },

    /* 6: Soft Sine Lead */
    { "Soft Sine",
      WAVE_SINE, WAVE_SINE, WAVE_TRI, 0, 1, -1,
      1.0f, 0.3f, 0.0f, 0.0f, 0.0f,
      0.6f, 0.0f, 0.3f, 0.0f,
      0.1f, 0.2f, 0.5f, 0.3f,
      0.15f, 0.2f, 1.0f, 0.5f,
      0.3f, 0.015f, 0.05f,
      0.0f, 0.08f, 0 },

    /* 7: Sync Lead */
    { "Detuned Sync",
      WAVE_SAW, WAVE_SAW, WAVE_SAW, 0, 0, 0,
      1.0f, 1.0f, 0.6f, 12.0f, 7.0f,
      0.4f, 0.5f, 0.6f, 0.4f,
      0.01f, 0.35f, 0.3f, 0.25f,
      0.01f, 0.1f, 1.0f, 0.3f,
      0.35f, 0.025f, 0.0f,
      0.0f, 0.03f, 0 },

    /* 8: Brass */
    { "Mini Brass",
      WAVE_SAW, WAVE_SAW, WAVE_SQUARE, 0, 0, 0,
      1.0f, 0.9f, 0.4f, 3.0f, 5.0f,
      0.25f, 0.4f, 0.65f, 0.3f,
      0.08f, 0.3f, 0.4f, 0.2f,
      0.06f, 0.15f, 0.95f, 0.2f,
      0.3f, 0.0f, 0.0f,
      0.0f, 0.0f, 0 },

    /* 9: Sub Bass */
    { "Sub Bass",
      WAVE_SINE, WAVE_SAW, WAVE_SINE, -1, -2, -2,
      0.7f, 0.5f, 0.8f, 0.0f, 0.0f,
      0.15f, 0.4f, 0.4f, 0.0f,
      0.01f, 0.3f, 0.0f, 0.15f,
      0.01f, 0.15f, 1.0f, 0.2f,
      0.3f, 0.0f, 0.0f,
      0.0f, 0.0f, 0 },

    /* 10: Pluck */
    { "Pluck",
      WAVE_SAW, WAVE_SQUARE, WAVE_SAW, 0, 0, 1,
      1.0f, 0.4f, 0.2f, 5.0f, 0.0f,
      0.25f, 0.3f, 0.7f, 0.5f,
      0.01f, 0.15f, 0.0f, 0.1f,
      0.01f, 0.4f, 0.0f, 0.15f,
      0.3f, 0.0f, 0.0f,
      0.0f, 0.0f, 0 },

    /* 11: Noise Lead */
    { "Noise Lead",
      WAVE_SAW, WAVE_SAW, WAVE_SAW, 0, 0, 0,
      0.8f, 0.6f, 0.0f, 5.0f, 0.0f,
      0.35f, 0.5f, 0.6f, 0.0f,
      0.01f, 0.3f, 0.3f, 0.2f,
      0.01f, 0.15f, 1.0f, 0.3f,
      0.3f, 0.0f, 0.0f,
      0.15f, 0.0f, 0 },

    /* 12: Growl Bass */
    { "Growl Bass",
      WAVE_SAW, WAVE_SQUARE, WAVE_TRI, -1, -1, -2,
      1.0f, 0.8f, 0.0f, 2.0f, 0.0f,
      0.2f, 0.75f, 0.7f, 0.0f,
      0.01f, 0.35f, 0.15f, 0.15f,
      0.01f, 0.1f, 1.0f, 0.15f,
      0.6f, 0.0f, 0.4f,
      0.0f, 0.0f, 1 },

    /* 13: Portamento Lead */
    { "Glide Lead",
      WAVE_SAW, WAVE_SAW, WAVE_SAW, 0, 0, 0,
      1.0f, 0.9f, 0.0f, 7.0f, 0.0f,
      0.4f, 0.35f, 0.5f, 0.3f,
      0.01f, 0.25f, 0.35f, 0.25f,
      0.01f, 0.1f, 1.0f, 0.3f,
      0.35f, 0.02f, 0.0f,
      0.0f, 0.25f, 0 },
};

static const int g_preset_count = sizeof(g_presets) / sizeof(g_presets[0]);

/* ========================================================================
 * Plugin instance
 * ======================================================================== */

struct BristolInstance {
    bristol_engine_t engine;
    int current_preset;
    int octave_transpose;
    float gain;
};

static void apply_preset(BristolInstance *inst, int idx) {
    if (idx < 0 || idx >= g_preset_count) return;

    const BristolPreset *p = &g_presets[idx];
    bristol_engine_t *e = &inst->engine;

    /* Oscillators */
    e->osc[0].wave = (bristol_wave_t)p->osc1_wave;
    e->osc[1].wave = (bristol_wave_t)p->osc2_wave;
    e->osc[2].wave = (bristol_wave_t)p->osc3_wave;
    e->osc[0].octave = p->osc1_oct;
    e->osc[1].octave = p->osc2_oct;
    e->osc[2].octave = p->osc3_oct;
    e->osc[0].level = p->osc1_level;
    e->osc[1].level = p->osc2_level;
    e->osc[2].level = p->osc3_level;
    e->osc[0].enabled = p->osc1_level > 0.0f ? 1 : 0;
    e->osc[1].enabled = p->osc2_level > 0.0f ? 1 : 0;
    e->osc[2].enabled = p->osc3_level > 0.0f || p->osc3_as_lfo ? 1 : 0;
    e->osc[1].detune = p->osc2_detune;
    e->osc[2].detune = p->osc3_detune;

    /* Filter */
    e->filter.cutoff = p->filter_cutoff;
    e->filter.resonance = p->filter_res;
    e->filter.env_amount = p->filter_env;
    e->filter.key_track = p->filter_keytrack;

    /* Filter envelope */
    e->filter_env.attack = p->fenv_a;
    e->filter_env.decay = p->fenv_d;
    e->filter_env.sustain = p->fenv_s;
    e->filter_env.release = p->fenv_r;

    /* Amp envelope */
    e->amp_env.attack = p->aenv_a;
    e->amp_env.decay = p->aenv_d;
    e->amp_env.sustain = p->aenv_s;
    e->amp_env.release = p->aenv_r;

    /* LFO */
    e->lfo.rate = p->lfo_rate;
    e->lfo_to_osc = p->lfo_to_osc;
    e->lfo_to_filter = p->lfo_to_filter;

    /* Other */
    e->noise.level = p->noise_level;
    e->glide = p->glide;
    e->osc3_as_lfo = p->osc3_as_lfo;

    inst->current_preset = idx;
}

/* ========================================================================
 * Plugin API implementation
 * ======================================================================== */

static void* bristol_create(const char *module_dir, const char *json_defaults) {
    (void)module_dir;
    (void)json_defaults;

    BristolInstance *inst = (BristolInstance*)calloc(1, sizeof(BristolInstance));
    if (!inst) return NULL;

    bristol_engine_init(&inst->engine);
    inst->current_preset = 0;
    inst->octave_transpose = 0;
    inst->gain = 1.0f;

    apply_preset(inst, 0);

    plugin_log("[bristol] Instance created");
    return inst;
}

static void bristol_destroy(void *instance) {
    BristolInstance *inst = (BristolInstance *)instance;
    if (!inst) return;
    free(inst);
    plugin_log("[bristol] Destroyed instance");
}

static void bristol_on_midi(void *instance, const uint8_t *msg, int len, int source) {
    BristolInstance *inst = (BristolInstance *)instance;
    if (len < 1) return;

    uint8_t status = msg[0] & 0xF0;

    switch (status) {
        case 0x90: /* Note On */
            if (len >= 3 && msg[2] > 0) {
                int note = msg[1] + inst->octave_transpose * 12;
                float vel = msg[2] / 127.0f;
                bristol_engine_note_on(&inst->engine, note, vel);
            } else if (len >= 2) {
                int note = msg[1] + inst->octave_transpose * 12;
                bristol_engine_note_off(&inst->engine, note);
            }
            break;

        case 0x80: /* Note Off */
            if (len >= 2) {
                int note = msg[1] + inst->octave_transpose * 12;
                bristol_engine_note_off(&inst->engine, note);
            }
            break;

        case 0xE0: /* Pitch Bend */
            if (len >= 3) {
                int bend = ((msg[2] << 7) | msg[1]) - 8192;
                float normalized = bend / 8192.0f;
                bristol_engine_pitch_bend(&inst->engine, normalized);
            }
            break;

        case 0xB0: /* Control Change */
            if (len >= 3) {
                int cc = msg[1];
                float val = msg[2] / 127.0f;

                switch (cc) {
                    case 1: /* Mod wheel */
                        bristol_engine_mod_wheel(&inst->engine, val);
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
                        bristol_engine_all_notes_off(&inst->engine);
                        break;
                }
            }
            break;
    }
}

static void bristol_set_param(void *instance, const char *key, const char *val) {
    BristolInstance *inst = (BristolInstance *)instance;
    float fval = (float)atof(val);
    int ival = atoi(val);

    if (strcmp(key, "preset") == 0) {
        apply_preset(inst, ival);
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
    BristolInstance *inst = (BristolInstance *)instance;

    if (strcmp(key, "preset") == 0) {
        return snprintf(buf, buf_len, "%d", inst->current_preset);
    } else if (strcmp(key, "preset_count") == 0) {
        return snprintf(buf, buf_len, "%d", g_preset_count);
    } else if (strcmp(key, "preset_name") == 0) {
        return snprintf(buf, buf_len, "%s", g_presets[inst->current_preset].name);
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
    BristolInstance *inst = (BristolInstance *)instance;
    if (!inst) {
        memset(out_lr, 0, frames * 4);
        return;
    }

    float mono_buf[256];
    if (frames > 256) frames = 256;

    bristol_engine_render(&inst->engine, mono_buf, frames);

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
    plugin_log("[bristol] Plugin v2 initialized");
    return &g_api;
}
