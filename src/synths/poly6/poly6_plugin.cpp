/*
 * poly6_plugin.cpp - Move Anything plugin wrapper for Korg Poly-6
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "poly6_engine.h"
#include "../../shared/bristol_mem_loader.h"
}

extern "C" {
typedef struct host_api_v1 {
    uint32_t api_version; int sample_rate; int frames_per_block;
    uint8_t *mapped_memory; int audio_out_offset; int audio_in_offset;
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

#define MAX_PRESETS 128

struct Poly6Instance {
    poly6_engine_t engine;
    int current_preset, preset_count, octave_transpose;
    float gain;
    char module_dir[256];
    bristol_patch_t presets[MAX_PRESETS];
};

static float get_p(const bristol_patch_t *p, int idx, float def = 0.0f) {
    return (idx >= 0 && idx < p->param_count) ? p->params[idx] : def;
}

static void apply_preset(Poly6Instance *inst, int idx) {
    if (idx < 0 || idx >= inst->preset_count) return;
    const bristol_patch_t *p = &inst->presets[idx];
    poly6_engine_t *e = &inst->engine;
    poly6_engine_reset(e);

    e->lfo.rate = get_p(p, 0);
    e->lfo_to_osc = get_p(p, 1);
    e->lfo_to_filter = get_p(p, 2);
    e->lfo_to_pw = get_p(p, 3);
    e->osc_octave = (int)(get_p(p, 4) * 5.99f);
    e->osc_saw = get_p(p, 5) > 0.5f ? 1 : 0;
    e->osc_pulse = get_p(p, 6) > 0.5f ? 1 : 0;
    e->osc_pw = get_p(p, 7, 0.5f);
    e->osc_sub = get_p(p, 8) > 0.5f ? 1 : 0;
    e->sub_level = get_p(p, 9, 0.5f);
    e->filter_cutoff = get_p(p, 10, 0.7f);
    e->filter_resonance = get_p(p, 11);
    e->filter_env_amt = get_p(p, 12, 0.5f);
    e->filter_kbd_track = get_p(p, 13);
    e->fenv_a = get_p(p, 14, 0.01f);
    e->fenv_d = get_p(p, 15, 0.3f);
    e->fenv_s = get_p(p, 16, 0.5f);
    e->fenv_r = get_p(p, 17, 0.3f);
    e->aenv_a = get_p(p, 18, 0.01f);
    e->aenv_d = get_p(p, 19, 0.3f);
    e->aenv_s = get_p(p, 20, 0.7f);
    e->aenv_r = get_p(p, 21, 0.3f);
    e->chorus_on = get_p(p, 22) > 0.5f ? 1 : 0;
    e->chorus_depth = get_p(p, 23, 0.5f);
    e->master_volume = get_p(p, 24, 0.8f);

    inst->current_preset = idx;
}

static int load_presets(Poly6Instance *inst) {
    char path[512];
    snprintf(path, sizeof(path), "%s/presets/poly6", inst->module_dir);
    int count = bristol_scan_presets(path, inst->presets, MAX_PRESETS);
    if (count <= 0) {
        count = 1;
        strcpy(inst->presets[0].name, "Init");
        inst->presets[0].param_count = 25;
        for (int i = 0; i < 25; i++) inst->presets[0].params[i] = 0.5f;
        inst->presets[0].params[5] = 1.0f;
    } else {
        char msg[256];
        snprintf(msg, sizeof(msg), "[bristol-poly6] Loaded %d presets", count);
        plugin_log(msg);
    }
    return count;
}

static void* poly6_create(const char *module_dir, const char *json_defaults) {
    (void)json_defaults;
    Poly6Instance *inst = (Poly6Instance*)calloc(1, sizeof(Poly6Instance));
    if (!inst) return NULL;
    strncpy(inst->module_dir, module_dir ? module_dir : ".", sizeof(inst->module_dir) - 1);
    poly6_engine_init(&inst->engine);
    inst->gain = 1.0f;
    inst->preset_count = load_presets(inst);
    if (inst->preset_count > 0) apply_preset(inst, 0);
    plugin_log("[bristol-poly6] Instance created");
    return inst;
}

static void poly6_destroy(void *instance) { if (instance) free(instance); }

static void poly6_on_midi(void *instance, const uint8_t *msg, int len, int source) {
    Poly6Instance *inst = (Poly6Instance *)instance;
    if (len < 1) return;
    uint8_t status = msg[0] & 0xF0;
    switch (status) {
        case 0x90:
            if (len >= 3 && msg[2] > 0)
                poly6_engine_note_on(&inst->engine, msg[1] + inst->octave_transpose * 12, msg[2] / 127.0f);
            else if (len >= 2)
                poly6_engine_note_off(&inst->engine, msg[1] + inst->octave_transpose * 12);
            break;
        case 0x80:
            if (len >= 2) poly6_engine_note_off(&inst->engine, msg[1] + inst->octave_transpose * 12);
            break;
        case 0xE0:
            if (len >= 3) poly6_engine_pitch_bend(&inst->engine, (((msg[2] << 7) | msg[1]) - 8192) / 8192.0f);
            break;
        case 0xB0:
            if (len >= 3) {
                float val = msg[2] / 127.0f;
                switch (msg[1]) {
                    case 74: inst->engine.filter_cutoff = val; break;
                    case 71: inst->engine.filter_resonance = val; break;
                    case 123: poly6_engine_all_notes_off(&inst->engine); break;
                }
            }
            break;
    }
}

static void poly6_set_param(void *instance, const char *key, const char *val) {
    Poly6Instance *inst = (Poly6Instance *)instance;
    float fval = (float)atof(val); int ival = atoi(val);
    if (strcmp(key, "preset") == 0) apply_preset(inst, ival);
    else if (strcmp(key, "octave_transpose") == 0) inst->octave_transpose = ival;
    else if (strcmp(key, "gain") == 0) inst->gain = fval;
    else if (strcmp(key, "filter_cutoff") == 0) inst->engine.filter_cutoff = fval;
    else if (strcmp(key, "filter_resonance") == 0) inst->engine.filter_resonance = fval;
    else if (strcmp(key, "filter_env_amount") == 0) inst->engine.filter_env_amt = fval;
    else if (strcmp(key, "amp_attack") == 0) inst->engine.aenv_a = fval;
    else if (strcmp(key, "amp_decay") == 0) inst->engine.aenv_d = fval;
    else if (strcmp(key, "amp_sustain") == 0) inst->engine.aenv_s = fval;
    else if (strcmp(key, "amp_release") == 0) inst->engine.aenv_r = fval;
    else if (strcmp(key, "lfo_rate") == 0) inst->engine.lfo.rate = fval;
}

static int poly6_get_param(void *instance, const char *key, char *buf, int buf_len) {
    Poly6Instance *inst = (Poly6Instance *)instance;
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
    if (strcmp(key, "amp_attack") == 0) return snprintf(buf, buf_len, "%.3f", inst->engine.aenv_a);
    if (strcmp(key, "amp_decay") == 0) return snprintf(buf, buf_len, "%.3f", inst->engine.aenv_d);
    if (strcmp(key, "amp_sustain") == 0) return snprintf(buf, buf_len, "%.3f", inst->engine.aenv_s);
    if (strcmp(key, "amp_release") == 0) return snprintf(buf, buf_len, "%.3f", inst->engine.aenv_r);
    if (strcmp(key, "lfo_rate") == 0) return snprintf(buf, buf_len, "%.3f", inst->engine.lfo.rate);
    if (strcmp(key, "ui_hierarchy") == 0) {
        return snprintf(buf, buf_len, R"({"modes":null,"levels":{"root":{"label":"Poly-6","list_param":"preset","count_param":"preset_count","name_param":"preset_name","knobs":["filter_cutoff","filter_resonance","filter_env_amount","lfo_rate","amp_attack","amp_decay","amp_sustain","amp_release"],"params":[{"key":"filter_cutoff","label":"Cutoff"},{"key":"filter_resonance","label":"Resonance"},{"key":"filter_env_amount","label":"Env Amt"},{"key":"lfo_rate","label":"LFO Rate"},{"key":"octave_transpose","label":"Octave"},{"key":"gain","label":"Gain"}]}}})");
    }
    if (strcmp(key, "chain_params") == 0) {
        return snprintf(buf, buf_len, R"([{"label":"Cutoff","cc":74,"value":%d,"type":"float","min":0,"max":1,"key":"filter_cutoff"},{"label":"Reso","cc":71,"value":%d,"type":"float","min":0,"max":1,"key":"filter_resonance"},{"label":"Env","cc":70,"value":%d,"type":"float","min":0,"max":1,"key":"filter_env_amount"},{"label":"LFO","cc":76,"value":%d,"type":"float","min":0,"max":1,"key":"lfo_rate"},{"label":"Atk","cc":73,"value":%d,"type":"float","min":0,"max":1,"key":"amp_attack"},{"label":"Dec","cc":75,"value":%d,"type":"float","min":0,"max":1,"key":"amp_decay"},{"label":"Sus","cc":79,"value":%d,"type":"float","min":0,"max":1,"key":"amp_sustain"},{"label":"Rel","cc":72,"value":%d,"type":"float","min":0,"max":1,"key":"amp_release"}])",
            (int)(inst->engine.filter_cutoff*127),(int)(inst->engine.filter_resonance*127),
            (int)(inst->engine.filter_env_amt*127),(int)(inst->engine.lfo.rate*127),
            (int)(inst->engine.aenv_a*127),(int)(inst->engine.aenv_d*127),
            (int)(inst->engine.aenv_s*127),(int)(inst->engine.aenv_r*127));
    }
    return 0;
}

static int poly6_get_error(void *inst, char *buf, int len) { (void)inst;(void)buf;(void)len; return 0; }

static void poly6_render(void *instance, int16_t *out_lr, int frames) {
    Poly6Instance *inst = (Poly6Instance *)instance;
    if (!inst) { memset(out_lr, 0, frames * 4); return; }
    float mono_buf[256];
    if (frames > 256) frames = 256;
    poly6_engine_render(&inst->engine, mono_buf, frames);
    float gain = inst->gain;
    for (int i = 0; i < frames; i++) {
        int32_t s = (int32_t)(mono_buf[i] * gain * 32767.0f);
        if (s > 32767) s = 32767; if (s < -32768) s = -32768;
        out_lr[i * 2] = out_lr[i * 2 + 1] = (int16_t)s;
    }
}

static plugin_api_v2_t g_api = {
    .api_version = 2, .create_instance = poly6_create, .destroy_instance = poly6_destroy,
    .on_midi = poly6_on_midi, .set_param = poly6_set_param, .get_param = poly6_get_param,
    .get_error = poly6_get_error, .render_block = poly6_render
};

extern "C" plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t *host) {
    g_host = host;
    plugin_log("[bristol-poly6] Plugin v2 initialized");
    return &g_api;
}
