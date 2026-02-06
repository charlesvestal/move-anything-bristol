/*
 * prophet_plugin.cpp - Move Anything plugin wrapper for Prophet-5
 *
 * Plugin API v2 wrapper for the Prophet-5 polyphonic synth.
 * Loads Bristol's .mem preset files at runtime.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

extern "C" {
#include "prophet_engine.h"
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

/* Bristol Prophet parameter indices from brightonProphet.c locations[] */
#define BP_POLYMOD_FILTER   0
#define BP_POLYMOD_OSCB     1
#define BP_POLYMOD_FREQ_A   2
#define BP_POLYMOD_PW_A     3
#define BP_POLYMOD_FILT     4
#define BP_LFO_FREQ         5
#define BP_LFO_SAW          6
#define BP_LFO_TRI          7
#define BP_LFO_SQUARE       8
#define BP_WHEEL_MIX        9
#define BP_WHEEL_FREQ_A     10
#define BP_WHEEL_FREQ_B     11
#define BP_WHEEL_PW_A       12
#define BP_WHEEL_PW_B       13
#define BP_WHEEL_FILT       14
#define BP_OSCA_OCT         15
#define BP_OSCA_SAW         16
#define BP_OSCA_PULSE       17
#define BP_OSCA_PW          18
#define BP_OSCA_SYNC        19
#define BP_OSCB_OCT         20
#define BP_OSCB_TUNE        21
#define BP_OSCB_SAW         22
#define BP_OSCB_TRI         23
#define BP_OSCB_PULSE       24
#define BP_OSCB_PW          25
#define BP_OSCB_LFO         26
#define BP_OSCB_KBD         27
#define BP_GLIDE            28
#define BP_UNISON           29
#define BP_MIX_OSCA         30
#define BP_MIX_OSCB         31
#define BP_MIX_NOISE        32
#define BP_VCF_CUTOFF       33
#define BP_VCF_RES          34
#define BP_VCF_ENV          35
#define BP_VCF_KBD          36
#define BP_VCF_ATTACK       37
#define BP_VCF_DECAY        38
#define BP_VCF_SUSTAIN      39
#define BP_VCF_RELEASE      40
#define BP_VCA_ATTACK       41
#define BP_VCA_DECAY        42
#define BP_VCA_SUSTAIN      43
#define BP_VCA_RELEASE      44
#define BP_MASTER_TUNE      45
#define BP_MASTER_VOL       46
#define BP_RELEASE_ON       47

#define MAX_PRESETS 128

struct ProphetInstance {
    prophet_engine_t engine;
    int current_preset;
    int preset_count;
    int octave_transpose;
    float gain;
    char module_dir[256];
    bristol_patch_t presets[MAX_PRESETS];
};

static float get_param(const bristol_patch_t *p, int idx, float def = 0.0f) {
    if (idx >= 0 && idx < p->param_count) {
        return p->params[idx];
    }
    return def;
}

static void apply_preset(ProphetInstance *inst, int idx) {
    if (idx < 0 || idx >= inst->preset_count) return;

    const bristol_patch_t *p = &inst->presets[idx];
    prophet_engine_t *e = &inst->engine;

    prophet_engine_reset(e);

    /* Poly mod */
    e->polymod_filter = get_param(p, BP_POLYMOD_FILTER);
    e->polymod_oscB = get_param(p, BP_POLYMOD_OSCB);
    e->polymod_freq_A = get_param(p, BP_POLYMOD_FREQ_A) > 0.5f ? 1 : 0;
    e->polymod_pw_A = get_param(p, BP_POLYMOD_PW_A) > 0.5f ? 1 : 0;
    e->polymod_filter_en = get_param(p, BP_POLYMOD_FILT) > 0.5f ? 1 : 0;

    /* LFO */
    e->lfo.rate = get_param(p, BP_LFO_FREQ);
    e->lfo_saw = get_param(p, BP_LFO_SAW) > 0.5f ? 1 : 0;
    e->lfo_tri = get_param(p, BP_LFO_TRI) > 0.5f ? 1 : 0;
    e->lfo_square = get_param(p, BP_LFO_SQUARE) > 0.5f ? 1 : 0;

    /* Wheel mod */
    e->wheel_mix = get_param(p, BP_WHEEL_MIX);
    e->wheel_freq_A = get_param(p, BP_WHEEL_FREQ_A) > 0.5f ? 1 : 0;
    e->wheel_freq_B = get_param(p, BP_WHEEL_FREQ_B) > 0.5f ? 1 : 0;
    e->wheel_pw_A = get_param(p, BP_WHEEL_PW_A) > 0.5f ? 1 : 0;
    e->wheel_pw_B = get_param(p, BP_WHEEL_PW_B) > 0.5f ? 1 : 0;
    e->wheel_filter = get_param(p, BP_WHEEL_FILT) > 0.5f ? 1 : 0;

    /* Oscillator A */
    e->oscA_octave = (int)(get_param(p, BP_OSCA_OCT) * 5.99f);
    e->oscA_saw = get_param(p, BP_OSCA_SAW) > 0.5f ? 1 : 0;
    e->oscA_pulse = get_param(p, BP_OSCA_PULSE) > 0.5f ? 1 : 0;
    e->oscA_pw = get_param(p, BP_OSCA_PW, 0.5f);
    e->oscA_sync = get_param(p, BP_OSCA_SYNC) > 0.5f ? 1 : 0;

    /* Oscillator B */
    e->oscB_octave = (int)(get_param(p, BP_OSCB_OCT) * 5.99f);
    e->oscB_detune = get_param(p, BP_OSCB_TUNE, 0.5f) - 0.5f;  /* Center at 0.5 */
    e->oscB_saw = get_param(p, BP_OSCB_SAW) > 0.5f ? 1 : 0;
    e->oscB_tri = get_param(p, BP_OSCB_TRI) > 0.5f ? 1 : 0;
    e->oscB_pulse = get_param(p, BP_OSCB_PULSE) > 0.5f ? 1 : 0;
    e->oscB_pw = get_param(p, BP_OSCB_PW, 0.5f);
    e->oscB_lfo_mod = get_param(p, BP_OSCB_LFO) > 0.5f ? 1 : 0;
    e->oscB_kbd_track = get_param(p, BP_OSCB_KBD) > 0.5f ? 1 : 0;

    /* Other controls */
    e->glide = get_param(p, BP_GLIDE);
    e->unison = get_param(p, BP_UNISON) > 0.5f ? 1 : 0;

    /* Mixer */
    e->mix_oscA = get_param(p, BP_MIX_OSCA, 0.5f);
    e->mix_oscB = get_param(p, BP_MIX_OSCB, 0.5f);
    e->mix_noise = get_param(p, BP_MIX_NOISE);

    /* Filter */
    e->filter_cutoff = get_param(p, BP_VCF_CUTOFF, 0.7f);
    e->filter_resonance = get_param(p, BP_VCF_RES);
    e->filter_env_amt = get_param(p, BP_VCF_ENV, 0.5f);
    e->filter_kbd_track = get_param(p, BP_VCF_KBD) > 0.5f ? 1 : 0;

    /* Filter envelope */
    e->fenv_attack = get_param(p, BP_VCF_ATTACK, 0.01f);
    e->fenv_decay = get_param(p, BP_VCF_DECAY, 0.3f);
    e->fenv_sustain = get_param(p, BP_VCF_SUSTAIN, 0.5f);
    e->fenv_release = get_param(p, BP_VCF_RELEASE, 0.3f);

    /* Amp envelope */
    e->aenv_attack = get_param(p, BP_VCA_ATTACK, 0.01f);
    e->aenv_decay = get_param(p, BP_VCA_DECAY, 0.3f);
    e->aenv_sustain = get_param(p, BP_VCA_SUSTAIN, 0.7f);
    e->aenv_release = get_param(p, BP_VCA_RELEASE, 0.3f);

    /* Master */
    e->master_tune = get_param(p, BP_MASTER_TUNE, 0.5f) - 0.5f;
    e->master_volume = get_param(p, BP_MASTER_VOL, 0.8f);
    e->release_on = get_param(p, BP_RELEASE_ON, 1.0f) > 0.5f ? 1 : 0;

    inst->current_preset = idx;
}

static int load_presets(ProphetInstance *inst) {
    char preset_path[512];
    snprintf(preset_path, sizeof(preset_path), "%s/presets/prophet", inst->module_dir);

    int count = bristol_scan_presets(preset_path, inst->presets, MAX_PRESETS);

    if (count <= 0) {
        snprintf(preset_path, sizeof(preset_path), "%s/presets", inst->module_dir);
        count = bristol_scan_presets(preset_path, inst->presets, MAX_PRESETS);
    }

    if (count > 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "[bristol-prophet] Loaded %d presets", count);
        plugin_log(msg);
    } else {
        plugin_log("[bristol-prophet] No presets found, using defaults");
        count = 1;
        strcpy(inst->presets[0].name, "Init");
        inst->presets[0].param_count = 48;
        for (int i = 0; i < 48; i++) inst->presets[0].params[i] = 0.5f;
        inst->presets[0].params[BP_OSCA_SAW] = 1.0f;
        inst->presets[0].params[BP_VCF_CUTOFF] = 0.7f;
        inst->presets[0].params[BP_MASTER_VOL] = 0.8f;
    }

    return count;
}

static void* prophet_create(const char *module_dir, const char *json_defaults) {
    (void)json_defaults;

    ProphetInstance *inst = (ProphetInstance*)calloc(1, sizeof(ProphetInstance));
    if (!inst) return NULL;

    strncpy(inst->module_dir, module_dir ? module_dir : ".", sizeof(inst->module_dir) - 1);

    prophet_engine_init(&inst->engine);
    inst->current_preset = 0;
    inst->octave_transpose = 0;
    inst->gain = 1.0f;

    inst->preset_count = load_presets(inst);

    if (inst->preset_count > 0) {
        apply_preset(inst, 0);
    }

    plugin_log("[bristol-prophet] Instance created");
    return inst;
}

static void prophet_destroy(void *instance) {
    ProphetInstance *inst = (ProphetInstance *)instance;
    if (!inst) return;
    free(inst);
    plugin_log("[bristol-prophet] Destroyed instance");
}

static void prophet_on_midi(void *instance, const uint8_t *msg, int len, int source) {
    ProphetInstance *inst = (ProphetInstance *)instance;
    if (len < 1) return;

    uint8_t status = msg[0] & 0xF0;

    switch (status) {
        case 0x90:
            if (len >= 3 && msg[2] > 0) {
                int note = msg[1] + inst->octave_transpose * 12;
                float vel = msg[2] / 127.0f;
                prophet_engine_note_on(&inst->engine, note, vel);
            } else if (len >= 2) {
                int note = msg[1] + inst->octave_transpose * 12;
                prophet_engine_note_off(&inst->engine, note);
            }
            break;

        case 0x80:
            if (len >= 2) {
                int note = msg[1] + inst->octave_transpose * 12;
                prophet_engine_note_off(&inst->engine, note);
            }
            break;

        case 0xE0:
            if (len >= 3) {
                int bend = ((msg[2] << 7) | msg[1]) - 8192;
                float normalized = bend / 8192.0f;
                prophet_engine_pitch_bend(&inst->engine, normalized);
            }
            break;

        case 0xB0:
            if (len >= 3) {
                int cc = msg[1];
                float val = msg[2] / 127.0f;
                switch (cc) {
                    case 74: inst->engine.filter_cutoff = val; break;
                    case 71: inst->engine.filter_resonance = val; break;
                    case 73: inst->engine.aenv_attack = val; break;
                    case 75: inst->engine.aenv_decay = val; break;
                    case 79: inst->engine.aenv_sustain = val; break;
                    case 72: inst->engine.aenv_release = val; break;
                    case 123: prophet_engine_all_notes_off(&inst->engine); break;
                }
            }
            break;
    }
}

static void prophet_set_param(void *instance, const char *key, const char *val) {
    ProphetInstance *inst = (ProphetInstance *)instance;
    float fval = (float)atof(val);
    int ival = atoi(val);

    if (strcmp(key, "preset") == 0) {
        apply_preset(inst, ival);
    } else if (strcmp(key, "octave_transpose") == 0) {
        inst->octave_transpose = ival;
    } else if (strcmp(key, "gain") == 0) {
        inst->gain = fval;
    } else if (strcmp(key, "filter_cutoff") == 0) {
        inst->engine.filter_cutoff = fval;
    } else if (strcmp(key, "filter_resonance") == 0) {
        inst->engine.filter_resonance = fval;
    } else if (strcmp(key, "filter_env_amount") == 0) {
        inst->engine.filter_env_amt = fval;
    } else if (strcmp(key, "amp_attack") == 0) {
        inst->engine.aenv_attack = fval;
    } else if (strcmp(key, "amp_decay") == 0) {
        inst->engine.aenv_decay = fval;
    } else if (strcmp(key, "amp_sustain") == 0) {
        inst->engine.aenv_sustain = fval;
    } else if (strcmp(key, "amp_release") == 0) {
        inst->engine.aenv_release = fval;
    } else if (strcmp(key, "filter_attack") == 0) {
        inst->engine.fenv_attack = fval;
    } else if (strcmp(key, "filter_decay") == 0) {
        inst->engine.fenv_decay = fval;
    } else if (strcmp(key, "filter_sustain") == 0) {
        inst->engine.fenv_sustain = fval;
    } else if (strcmp(key, "filter_release") == 0) {
        inst->engine.fenv_release = fval;
    } else if (strcmp(key, "lfo_rate") == 0) {
        inst->engine.lfo.rate = fval;
    } else if (strcmp(key, "mix_oscA") == 0) {
        inst->engine.mix_oscA = fval;
    } else if (strcmp(key, "mix_oscB") == 0) {
        inst->engine.mix_oscB = fval;
    } else if (strcmp(key, "mix_noise") == 0) {
        inst->engine.mix_noise = fval;
    } else if (strcmp(key, "glide") == 0) {
        inst->engine.glide = fval;
    } else if (strcmp(key, "master_volume") == 0) {
        inst->engine.master_volume = fval;
    }
}

static int prophet_get_param(void *instance, const char *key, char *buf, int buf_len) {
    ProphetInstance *inst = (ProphetInstance *)instance;

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
    } else if (strcmp(key, "filter_cutoff") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.filter_cutoff);
    } else if (strcmp(key, "filter_resonance") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.filter_resonance);
    } else if (strcmp(key, "filter_env_amount") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.filter_env_amt);
    } else if (strcmp(key, "amp_attack") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.aenv_attack);
    } else if (strcmp(key, "amp_decay") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.aenv_decay);
    } else if (strcmp(key, "amp_sustain") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.aenv_sustain);
    } else if (strcmp(key, "amp_release") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.aenv_release);
    } else if (strcmp(key, "filter_attack") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.fenv_attack);
    } else if (strcmp(key, "filter_decay") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.fenv_decay);
    } else if (strcmp(key, "filter_sustain") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.fenv_sustain);
    } else if (strcmp(key, "filter_release") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.fenv_release);
    } else if (strcmp(key, "lfo_rate") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.lfo.rate);
    } else if (strcmp(key, "mix_oscA") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.mix_oscA);
    } else if (strcmp(key, "mix_oscB") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.mix_oscB);
    } else if (strcmp(key, "mix_noise") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.mix_noise);
    } else if (strcmp(key, "glide") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.glide);
    } else if (strcmp(key, "master_volume") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->engine.master_volume);
    } else if (strcmp(key, "ui_hierarchy") == 0) {
        return snprintf(buf, buf_len, R"({
  "modes": null,
  "levels": {
    "root": {
      "label": "Prophet",
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
        {"level": "mixer", "label": "Mixer"},
        {"level": "envelopes", "label": "Envelopes"}
      ]
    },
    "mixer": {
      "label": "Mixer",
      "knobs": ["mix_oscA", "mix_oscB", "mix_noise", "glide", "master_volume", "", "", ""],
      "params": [
        {"key": "mix_oscA", "label": "Osc A Level"},
        {"key": "mix_oscB", "label": "Osc B Level"},
        {"key": "mix_noise", "label": "Noise"},
        {"key": "glide", "label": "Glide"},
        {"key": "master_volume", "label": "Volume"}
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
            (int)(inst->engine.filter_cutoff * 127),
            (int)(inst->engine.filter_resonance * 127),
            (int)(inst->engine.filter_env_amt * 127),
            (int)(inst->engine.lfo.rate * 127),
            (int)(inst->engine.aenv_attack * 127),
            (int)(inst->engine.aenv_decay * 127),
            (int)(inst->engine.aenv_sustain * 127),
            (int)(inst->engine.aenv_release * 127)
        );
    }

    return 0;
}

static int prophet_get_error(void *instance, char *buf, int buf_len) {
    (void)instance; (void)buf; (void)buf_len;
    return 0;
}

static void prophet_render(void *instance, int16_t *out_lr, int frames) {
    ProphetInstance *inst = (ProphetInstance *)instance;
    if (!inst) {
        memset(out_lr, 0, frames * 4);
        return;
    }

    float mono_buf[256];
    if (frames > 256) frames = 256;

    prophet_engine_render(&inst->engine, mono_buf, frames);

    /* Convert mono to stereo int16 */
    float gain = inst->gain;
    for (int i = 0; i < frames; i++) {
        float sample = mono_buf[i] * gain;
        int32_t s = (int32_t)(sample * 32767.0f);
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        out_lr[i * 2] = (int16_t)s;
        out_lr[i * 2 + 1] = (int16_t)s;
    }
}

static plugin_api_v2_t g_api = {
    .api_version = 2,
    .create_instance = prophet_create,
    .destroy_instance = prophet_destroy,
    .on_midi = prophet_on_midi,
    .set_param = prophet_set_param,
    .get_param = prophet_get_param,
    .get_error = prophet_get_error,
    .render_block = prophet_render
};

extern "C" plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t *host) {
    g_host = host;
    plugin_log("[bristol-prophet] Plugin v2 initialized");
    return &g_api;
}
