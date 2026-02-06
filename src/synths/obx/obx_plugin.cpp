/*
 * obx_plugin.cpp - Move Anything plugin wrapper for OB-X
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "obx_engine.h"
#include "../../shared/bristol_mem_loader.h"
}

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
}

static const host_api_v1_t *g_host = nullptr;
static void plugin_log(const char *msg) { if (g_host && g_host->log) g_host->log(msg); }

/* Bristol OBX parameter indices from brightonOBX.c */
#define B_GLIDE         0
#define B_UNISON        1
#define B_OSC2_DETUNE   2
#define B_LFO_FREQ      3
#define B_LFO_SINE      4
#define B_LFO_SQUARE    5
#define B_LFO_SH        6
#define B_LFO_FM_DEPTH  7
#define B_LFO_FM_OSC1   8
#define B_LFO_FM_OSC2   9
#define B_LFO_FM_FILT   10
#define B_LFO_PWM_DEPTH 11
#define B_LFO_PWM_OSC1  12
#define B_LFO_PWM_OSC2  13
#define B_OSC1_OCT      14
#define B_OSC_PW        15
#define B_OSC2_TUNE     16
#define B_OSC1_SAW      17
#define B_OSC1_PULSE    18
#define B_OSC_XMOD      19
#define B_OSC_SYNC      20
#define B_OSC2_SAW      21
#define B_OSC2_PULSE    22
#define B_VCF_CUTOFF    23
#define B_VCF_RES       24
#define B_VCF_MOD       25
#define B_MIX_OSC1      26
#define B_MIX_OSC2      27
#define B_MIX_NOISE     28
#define B_VCF_4POLE     29
#define B_VCF_KBD       30
#define B_VCF_ENV       31
#define B_FENV_A        32
#define B_FENV_D        33
#define B_FENV_S        34
#define B_FENV_R        35
#define B_AENV_A        36
#define B_AENV_D        37
#define B_AENV_S        38
#define B_AENV_R        39
#define B_MASTER_VOL    40

#define MAX_PRESETS 128

struct OBXInstance {
    obx_engine_t engine;
    int current_preset;
    int preset_count;
    int octave_transpose;
    float gain;
    char module_dir[256];
    bristol_patch_t presets[MAX_PRESETS];
};

static float get_p(const bristol_patch_t *p, int idx, float def = 0.0f) {
    return (idx >= 0 && idx < p->param_count) ? p->params[idx] : def;
}

static void apply_preset(OBXInstance *inst, int idx) {
    if (idx < 0 || idx >= inst->preset_count) return;
    const bristol_patch_t *p = &inst->presets[idx];
    obx_engine_t *e = &inst->engine;
    obx_engine_reset(e);

    e->glide = get_p(p, B_GLIDE);
    e->unison = get_p(p, B_UNISON) > 0.5f ? 1 : 0;
    e->osc2_detune = get_p(p, B_OSC2_DETUNE);

    e->lfo.rate = get_p(p, B_LFO_FREQ);
    e->lfo_sine = get_p(p, B_LFO_SINE) > 0.5f ? 1 : 0;
    e->lfo_square = get_p(p, B_LFO_SQUARE) > 0.5f ? 1 : 0;
    e->lfo_sh = get_p(p, B_LFO_SH) > 0.5f ? 1 : 0;
    e->lfo_fm_depth = get_p(p, B_LFO_FM_DEPTH);
    e->lfo_fm_osc1 = get_p(p, B_LFO_FM_OSC1) > 0.5f ? 1 : 0;
    e->lfo_fm_osc2 = get_p(p, B_LFO_FM_OSC2) > 0.5f ? 1 : 0;
    e->lfo_fm_filter = get_p(p, B_LFO_FM_FILT) > 0.5f ? 1 : 0;
    e->lfo_pwm_depth = get_p(p, B_LFO_PWM_DEPTH);
    e->lfo_pwm_osc1 = get_p(p, B_LFO_PWM_OSC1) > 0.5f ? 1 : 0;
    e->lfo_pwm_osc2 = get_p(p, B_LFO_PWM_OSC2) > 0.5f ? 1 : 0;

    e->osc1_octave = (int)(get_p(p, B_OSC1_OCT) * 5.99f);
    e->osc_pw = get_p(p, B_OSC_PW, 0.5f);
    e->osc2_tune = get_p(p, B_OSC2_TUNE);
    e->osc1_saw = get_p(p, B_OSC1_SAW) > 0.5f ? 1 : 0;
    e->osc1_pulse = get_p(p, B_OSC1_PULSE) > 0.5f ? 1 : 0;
    e->osc_crossmod = get_p(p, B_OSC_XMOD) > 0.5f ? 1 : 0;
    e->osc_sync = get_p(p, B_OSC_SYNC) > 0.5f ? 1 : 0;
    e->osc2_saw = get_p(p, B_OSC2_SAW) > 0.5f ? 1 : 0;
    e->osc2_pulse = get_p(p, B_OSC2_PULSE) > 0.5f ? 1 : 0;

    e->filter_cutoff = get_p(p, B_VCF_CUTOFF, 0.7f);
    e->filter_resonance = get_p(p, B_VCF_RES);
    e->filter_mod = get_p(p, B_VCF_MOD);
    e->mix_osc1 = get_p(p, B_MIX_OSC1, 1.0f) > 0.5f ? 1 : 0;
    e->mix_osc2 = get_p(p, B_MIX_OSC2, 1.0f) > 0.5f ? 1 : 0;
    e->mix_noise = get_p(p, B_MIX_NOISE) > 0.5f ? 1 : 0;
    e->filter_4pole = get_p(p, B_VCF_4POLE) > 0.5f ? 1 : 0;
    e->filter_kbd = get_p(p, B_VCF_KBD) > 0.5f ? 1 : 0;
    e->filter_env_amt = get_p(p, B_VCF_ENV, 0.5f);

    e->fenv_attack = get_p(p, B_FENV_A, 0.01f);
    e->fenv_decay = get_p(p, B_FENV_D, 0.3f);
    e->fenv_sustain = get_p(p, B_FENV_S, 0.5f);
    e->fenv_release = get_p(p, B_FENV_R, 0.3f);

    e->aenv_attack = get_p(p, B_AENV_A, 0.01f);
    e->aenv_decay = get_p(p, B_AENV_D, 0.3f);
    e->aenv_sustain = get_p(p, B_AENV_S, 0.7f);
    e->aenv_release = get_p(p, B_AENV_R, 0.3f);

    e->master_volume = get_p(p, B_MASTER_VOL, 0.8f);
    inst->current_preset = idx;
}

static int load_presets(OBXInstance *inst) {
    char path[512];
    snprintf(path, sizeof(path), "%s/presets/obx", inst->module_dir);
    int count = bristol_scan_presets(path, inst->presets, MAX_PRESETS);
    if (count <= 0) {
        snprintf(path, sizeof(path), "%s/presets", inst->module_dir);
        count = bristol_scan_presets(path, inst->presets, MAX_PRESETS);
    }
    if (count > 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "[bristol-obx] Loaded %d presets", count);
        plugin_log(msg);
    } else {
        count = 1;
        strcpy(inst->presets[0].name, "Init");
        inst->presets[0].param_count = 41;
        for (int i = 0; i < 41; i++) inst->presets[0].params[i] = 0.5f;
        inst->presets[0].params[B_OSC1_SAW] = 1.0f;
        inst->presets[0].params[B_OSC2_SAW] = 1.0f;
        inst->presets[0].params[B_MIX_OSC1] = 1.0f;
        inst->presets[0].params[B_MIX_OSC2] = 1.0f;
    }
    return count;
}

static void* obx_create(const char *module_dir, const char *json_defaults) {
    (void)json_defaults;
    OBXInstance *inst = (OBXInstance*)calloc(1, sizeof(OBXInstance));
    if (!inst) return NULL;
    strncpy(inst->module_dir, module_dir ? module_dir : ".", sizeof(inst->module_dir) - 1);
    obx_engine_init(&inst->engine);
    inst->gain = 1.0f;
    inst->preset_count = load_presets(inst);
    if (inst->preset_count > 0) apply_preset(inst, 0);
    plugin_log("[bristol-obx] Instance created");
    return inst;
}

static void obx_destroy(void *instance) {
    if (instance) free(instance);
}

static void obx_on_midi(void *instance, const uint8_t *msg, int len, int source) {
    OBXInstance *inst = (OBXInstance *)instance;
    if (len < 1) return;
    uint8_t status = msg[0] & 0xF0;
    switch (status) {
        case 0x90:
            if (len >= 3 && msg[2] > 0) {
                obx_engine_note_on(&inst->engine, msg[1] + inst->octave_transpose * 12, msg[2] / 127.0f);
            } else if (len >= 2) {
                obx_engine_note_off(&inst->engine, msg[1] + inst->octave_transpose * 12);
            }
            break;
        case 0x80:
            if (len >= 2) obx_engine_note_off(&inst->engine, msg[1] + inst->octave_transpose * 12);
            break;
        case 0xE0:
            if (len >= 3) {
                int bend = ((msg[2] << 7) | msg[1]) - 8192;
                obx_engine_pitch_bend(&inst->engine, bend / 8192.0f);
            }
            break;
        case 0xB0:
            if (len >= 3) {
                float val = msg[2] / 127.0f;
                switch (msg[1]) {
                    case 74: inst->engine.filter_cutoff = val; break;
                    case 71: inst->engine.filter_resonance = val; break;
                    case 123: obx_engine_all_notes_off(&inst->engine); break;
                }
            }
            break;
    }
}

static void obx_set_param(void *instance, const char *key, const char *val) {
    OBXInstance *inst = (OBXInstance *)instance;
    float fval = (float)atof(val);
    int ival = atoi(val);
    if (strcmp(key, "preset") == 0) apply_preset(inst, ival);
    else if (strcmp(key, "octave_transpose") == 0) inst->octave_transpose = ival;
    else if (strcmp(key, "gain") == 0) inst->gain = fval;
    else if (strcmp(key, "filter_cutoff") == 0) inst->engine.filter_cutoff = fval;
    else if (strcmp(key, "filter_resonance") == 0) inst->engine.filter_resonance = fval;
    else if (strcmp(key, "filter_env_amount") == 0) inst->engine.filter_env_amt = fval;
    else if (strcmp(key, "amp_attack") == 0) inst->engine.aenv_attack = fval;
    else if (strcmp(key, "amp_decay") == 0) inst->engine.aenv_decay = fval;
    else if (strcmp(key, "amp_sustain") == 0) inst->engine.aenv_sustain = fval;
    else if (strcmp(key, "amp_release") == 0) inst->engine.aenv_release = fval;
    else if (strcmp(key, "lfo_rate") == 0) inst->engine.lfo.rate = fval;
}

static int obx_get_param(void *instance, const char *key, char *buf, int buf_len) {
    OBXInstance *inst = (OBXInstance *)instance;
    if (strcmp(key, "preset") == 0) return snprintf(buf, buf_len, "%d", inst->current_preset);
    if (strcmp(key, "preset_count") == 0) return snprintf(buf, buf_len, "%d", inst->preset_count);
    if (strcmp(key, "preset_name") == 0) {
        if (inst->current_preset >= 0 && inst->current_preset < inst->preset_count)
            return snprintf(buf, buf_len, "%s", inst->presets[inst->current_preset].name);
        return snprintf(buf, buf_len, "Init");
    }
    if (strcmp(key, "octave_transpose") == 0) return snprintf(buf, buf_len, "%d", inst->octave_transpose);
    if (strcmp(key, "gain") == 0) return snprintf(buf, buf_len, "%.3f", inst->gain);
    if (strcmp(key, "filter_cutoff") == 0) return snprintf(buf, buf_len, "%.3f", inst->engine.filter_cutoff);
    if (strcmp(key, "filter_resonance") == 0) return snprintf(buf, buf_len, "%.3f", inst->engine.filter_resonance);
    if (strcmp(key, "filter_env_amount") == 0) return snprintf(buf, buf_len, "%.3f", inst->engine.filter_env_amt);
    if (strcmp(key, "amp_attack") == 0) return snprintf(buf, buf_len, "%.3f", inst->engine.aenv_attack);
    if (strcmp(key, "amp_decay") == 0) return snprintf(buf, buf_len, "%.3f", inst->engine.aenv_decay);
    if (strcmp(key, "amp_sustain") == 0) return snprintf(buf, buf_len, "%.3f", inst->engine.aenv_sustain);
    if (strcmp(key, "amp_release") == 0) return snprintf(buf, buf_len, "%.3f", inst->engine.aenv_release);
    if (strcmp(key, "lfo_rate") == 0) return snprintf(buf, buf_len, "%.3f", inst->engine.lfo.rate);
    if (strcmp(key, "ui_hierarchy") == 0) {
        return snprintf(buf, buf_len, R"({"modes":null,"levels":{"root":{"label":"OB-X","list_param":"preset","count_param":"preset_count","name_param":"preset_name","knobs":["filter_cutoff","filter_resonance","filter_env_amount","lfo_rate","amp_attack","amp_decay","amp_sustain","amp_release"],"params":[{"key":"filter_cutoff","label":"Cutoff"},{"key":"filter_resonance","label":"Resonance"},{"key":"filter_env_amount","label":"Env Amt"},{"key":"lfo_rate","label":"LFO Rate"},{"key":"octave_transpose","label":"Octave"},{"key":"gain","label":"Gain"}]}}})");
    }
    if (strcmp(key, "chain_params") == 0) {
        return snprintf(buf, buf_len, R"([{"label":"Cutoff","cc":74,"value":%d,"type":"float","min":0,"max":1,"key":"filter_cutoff"},{"label":"Reso","cc":71,"value":%d,"type":"float","min":0,"max":1,"key":"filter_resonance"},{"label":"Env Amt","cc":70,"value":%d,"type":"float","min":0,"max":1,"key":"filter_env_amount"},{"label":"LFO","cc":76,"value":%d,"type":"float","min":0,"max":1,"key":"lfo_rate"},{"label":"Attack","cc":73,"value":%d,"type":"float","min":0,"max":1,"key":"amp_attack"},{"label":"Decay","cc":75,"value":%d,"type":"float","min":0,"max":1,"key":"amp_decay"},{"label":"Sustain","cc":79,"value":%d,"type":"float","min":0,"max":1,"key":"amp_sustain"},{"label":"Release","cc":72,"value":%d,"type":"float","min":0,"max":1,"key":"amp_release"}])",
            (int)(inst->engine.filter_cutoff*127),(int)(inst->engine.filter_resonance*127),
            (int)(inst->engine.filter_env_amt*127),(int)(inst->engine.lfo.rate*127),
            (int)(inst->engine.aenv_attack*127),(int)(inst->engine.aenv_decay*127),
            (int)(inst->engine.aenv_sustain*127),(int)(inst->engine.aenv_release*127));
    }
    return 0;
}

static int obx_get_error(void *instance, char *buf, int buf_len) { (void)instance;(void)buf;(void)buf_len; return 0; }

static void obx_render(void *instance, int16_t *out_lr, int frames) {
    OBXInstance *inst = (OBXInstance *)instance;
    if (!inst) { memset(out_lr, 0, frames * 4); return; }
    float mono_buf[256];
    if (frames > 256) frames = 256;
    obx_engine_render(&inst->engine, mono_buf, frames);
    float gain = inst->gain;
    for (int i = 0; i < frames; i++) {
        int32_t s = (int32_t)(mono_buf[i] * gain * 32767.0f);
        if (s > 32767) s = 32767; if (s < -32768) s = -32768;
        out_lr[i * 2] = out_lr[i * 2 + 1] = (int16_t)s;
    }
}

static plugin_api_v2_t g_api = {
    .api_version = 2, .create_instance = obx_create, .destroy_instance = obx_destroy,
    .on_midi = obx_on_midi, .set_param = obx_set_param, .get_param = obx_get_param,
    .get_error = obx_get_error, .render_block = obx_render
};

extern "C" plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t *host) {
    g_host = host;
    plugin_log("[bristol-obx] Plugin v2 initialized");
    return &g_api;
}
