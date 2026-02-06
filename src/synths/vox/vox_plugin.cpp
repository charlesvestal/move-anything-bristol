/*
 * vox_plugin.cpp - Move Anything plugin wrapper for Vox Continental
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "vox_engine.h"
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

struct VoxInstance {
    vox_engine_t engine;
    int current_preset, preset_count, octave_transpose;
    float gain;
    char module_dir[256];
    bristol_patch_t presets[MAX_PRESETS];
};

static float get_p(const bristol_patch_t *p, int idx, float def = 0.0f) {
    return (idx >= 0 && idx < p->param_count) ? p->params[idx] : def;
}

static void apply_preset(VoxInstance *inst, int idx) {
    if (idx < 0 || idx >= inst->preset_count) return;
    const bristol_patch_t *p = &inst->presets[idx];
    vox_engine_t *e = &inst->engine;
    vox_engine_reset(e);

    e->drawbar_16 = get_p(p, 0, 0.5f);
    e->drawbar_8 = get_p(p, 1, 0.8f);
    e->drawbar_4 = get_p(p, 2, 0.5f);
    e->drawbar_2 = get_p(p, 3, 0.3f);
    e->drawbar_mix = get_p(p, 4, 0.2f);
    e->vibrato_depth = get_p(p, 5);
    e->vibrato_rate = get_p(p, 6, 0.3f);
    e->percussion_on = get_p(p, 7) > 0.5f ? 1 : 0;
    e->percussion_decay = get_p(p, 8, 0.3f);
    e->master_volume = get_p(p, 9, 0.8f);

    inst->current_preset = idx;
}

static int load_presets(VoxInstance *inst) {
    char path[512];
    snprintf(path, sizeof(path), "%s/presets/vox", inst->module_dir);
    int count = bristol_scan_presets(path, inst->presets, MAX_PRESETS);
    if (count <= 0) {
        count = 1;
        strcpy(inst->presets[0].name, "Continental");
        inst->presets[0].param_count = 10;
        inst->presets[0].params[0] = 0.5f;
        inst->presets[0].params[1] = 0.8f;
        inst->presets[0].params[2] = 0.5f;
        inst->presets[0].params[3] = 0.3f;
        inst->presets[0].params[4] = 0.2f;
        inst->presets[0].params[5] = 0.0f;
        inst->presets[0].params[6] = 0.3f;
        inst->presets[0].params[7] = 0.0f;
        inst->presets[0].params[8] = 0.3f;
        inst->presets[0].params[9] = 0.8f;
    } else {
        char msg[256];
        snprintf(msg, sizeof(msg), "[bristol-vox] Loaded %d presets", count);
        plugin_log(msg);
    }
    return count;
}

static void* vox_create(const char *module_dir, const char *json_defaults) {
    (void)json_defaults;
    VoxInstance *inst = (VoxInstance*)calloc(1, sizeof(VoxInstance));
    if (!inst) return NULL;
    strncpy(inst->module_dir, module_dir ? module_dir : ".", sizeof(inst->module_dir) - 1);
    vox_engine_init(&inst->engine);
    inst->gain = 1.0f;
    inst->preset_count = load_presets(inst);
    if (inst->preset_count > 0) apply_preset(inst, 0);
    plugin_log("[bristol-vox] Instance created");
    return inst;
}

static void vox_destroy(void *instance) { if (instance) free(instance); }

static void vox_on_midi(void *instance, const uint8_t *msg, int len, int source) {
    VoxInstance *inst = (VoxInstance *)instance;
    if (len < 1) return;
    uint8_t status = msg[0] & 0xF0;
    switch (status) {
        case 0x90:
            if (len >= 3 && msg[2] > 0)
                vox_engine_note_on(&inst->engine, msg[1] + inst->octave_transpose * 12, msg[2] / 127.0f);
            else if (len >= 2)
                vox_engine_note_off(&inst->engine, msg[1] + inst->octave_transpose * 12);
            break;
        case 0x80:
            if (len >= 2) vox_engine_note_off(&inst->engine, msg[1] + inst->octave_transpose * 12);
            break;
        case 0xE0:
            if (len >= 3) vox_engine_pitch_bend(&inst->engine, (((msg[2] << 7) | msg[1]) - 8192) / 8192.0f);
            break;
        case 0xB0:
            if (len >= 3 && msg[1] == 123) vox_engine_all_notes_off(&inst->engine);
            break;
    }
}

static void vox_set_param(void *instance, const char *key, const char *val) {
    VoxInstance *inst = (VoxInstance *)instance;
    float fval = (float)atof(val); int ival = atoi(val);
    if (strcmp(key, "preset") == 0) apply_preset(inst, ival);
    else if (strcmp(key, "octave_transpose") == 0) inst->octave_transpose = ival;
    else if (strcmp(key, "gain") == 0) inst->gain = fval;
    else if (strcmp(key, "drawbar_16") == 0) inst->engine.drawbar_16 = fval;
    else if (strcmp(key, "drawbar_8") == 0) inst->engine.drawbar_8 = fval;
    else if (strcmp(key, "drawbar_4") == 0) inst->engine.drawbar_4 = fval;
    else if (strcmp(key, "drawbar_2") == 0) inst->engine.drawbar_2 = fval;
    else if (strcmp(key, "drawbar_mix") == 0) inst->engine.drawbar_mix = fval;
    else if (strcmp(key, "vibrato_depth") == 0) inst->engine.vibrato_depth = fval;
    else if (strcmp(key, "vibrato_rate") == 0) inst->engine.vibrato_rate = fval;
}

static int vox_get_param(void *instance, const char *key, char *buf, int buf_len) {
    VoxInstance *inst = (VoxInstance *)instance;
    if (strcmp(key, "preset") == 0) return snprintf(buf, buf_len, "%d", inst->current_preset);
    if (strcmp(key, "preset_count") == 0) return snprintf(buf, buf_len, "%d", inst->preset_count);
    if (strcmp(key, "preset_name") == 0) {
        if (inst->current_preset >= 0 && inst->current_preset < inst->preset_count)
            return snprintf(buf, buf_len, "%s", inst->presets[inst->current_preset].name);
        return snprintf(buf, buf_len, "Continental");
    }
    if (strcmp(key, "octave_transpose") == 0) return snprintf(buf, buf_len, "%d", inst->octave_transpose);
    if (strcmp(key, "gain") == 0) return snprintf(buf, buf_len, "%.3f", inst->gain);
    if (strcmp(key, "drawbar_16") == 0) return snprintf(buf, buf_len, "%.3f", inst->engine.drawbar_16);
    if (strcmp(key, "drawbar_8") == 0) return snprintf(buf, buf_len, "%.3f", inst->engine.drawbar_8);
    if (strcmp(key, "drawbar_4") == 0) return snprintf(buf, buf_len, "%.3f", inst->engine.drawbar_4);
    if (strcmp(key, "drawbar_2") == 0) return snprintf(buf, buf_len, "%.3f", inst->engine.drawbar_2);
    if (strcmp(key, "drawbar_mix") == 0) return snprintf(buf, buf_len, "%.3f", inst->engine.drawbar_mix);
    if (strcmp(key, "vibrato_depth") == 0) return snprintf(buf, buf_len, "%.3f", inst->engine.vibrato_depth);
    if (strcmp(key, "vibrato_rate") == 0) return snprintf(buf, buf_len, "%.3f", inst->engine.vibrato_rate);
    if (strcmp(key, "ui_hierarchy") == 0) {
        return snprintf(buf, buf_len, R"({"modes":null,"levels":{"root":{"label":"Vox","list_param":"preset","count_param":"preset_count","name_param":"preset_name","knobs":["drawbar_16","drawbar_8","drawbar_4","drawbar_2","drawbar_mix","vibrato_depth","vibrato_rate","gain"],"params":[{"key":"drawbar_16","label":"16'"},{"key":"drawbar_8","label":"8'"},{"key":"drawbar_4","label":"4'"},{"key":"drawbar_2","label":"2'"},{"key":"drawbar_mix","label":"Mix"},{"key":"vibrato_depth","label":"Vib Dep"},{"key":"vibrato_rate","label":"Vib Rate"},{"key":"octave_transpose","label":"Octave"},{"key":"gain","label":"Gain"}]}}})");
    }
    if (strcmp(key, "chain_params") == 0) {
        return snprintf(buf, buf_len, R"([{"label":"16'","cc":74,"value":%d,"type":"float","min":0,"max":1,"key":"drawbar_16"},{"label":"8'","cc":71,"value":%d,"type":"float","min":0,"max":1,"key":"drawbar_8"},{"label":"4'","cc":70,"value":%d,"type":"float","min":0,"max":1,"key":"drawbar_4"},{"label":"2'","cc":76,"value":%d,"type":"float","min":0,"max":1,"key":"drawbar_2"},{"label":"Mix","cc":73,"value":%d,"type":"float","min":0,"max":1,"key":"drawbar_mix"},{"label":"VibD","cc":75,"value":%d,"type":"float","min":0,"max":1,"key":"vibrato_depth"},{"label":"VibR","cc":77,"value":%d,"type":"float","min":0,"max":1,"key":"vibrato_rate"},{"label":"Gain","cc":7,"value":%d,"type":"float","min":0,"max":1,"key":"gain"}])",
            (int)(inst->engine.drawbar_16*127),(int)(inst->engine.drawbar_8*127),
            (int)(inst->engine.drawbar_4*127),(int)(inst->engine.drawbar_2*127),
            (int)(inst->engine.drawbar_mix*127),(int)(inst->engine.vibrato_depth*127),
            (int)(inst->engine.vibrato_rate*127),(int)(inst->gain*127));
    }
    return 0;
}

static int vox_get_error(void *inst, char *buf, int len) { (void)inst;(void)buf;(void)len; return 0; }

static void vox_render(void *instance, int16_t *out_lr, int frames) {
    VoxInstance *inst = (VoxInstance *)instance;
    if (!inst) { memset(out_lr, 0, frames * 4); return; }
    float mono_buf[256];
    if (frames > 256) frames = 256;
    vox_engine_render(&inst->engine, mono_buf, frames);
    float gain = inst->gain;
    for (int i = 0; i < frames; i++) {
        int32_t s = (int32_t)(mono_buf[i] * gain * 32767.0f);
        if (s > 32767) s = 32767; if (s < -32768) s = -32768;
        out_lr[i * 2] = out_lr[i * 2 + 1] = (int16_t)s;
    }
}

static plugin_api_v2_t g_api = {
    .api_version = 2, .create_instance = vox_create, .destroy_instance = vox_destroy,
    .on_midi = vox_on_midi, .set_param = vox_set_param, .get_param = vox_get_param,
    .get_error = vox_get_error, .render_block = vox_render
};

extern "C" plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t *host) {
    g_host = host;
    plugin_log("[bristol-vox] Plugin v2 initialized");
    return &g_api;
}
