# Bristol Synth Implementation Plan

## Overview

Port Bristol synthesizer emulations to Move Anything using the shared DSP components in `src/shared/bristol_common.h` and runtime .mem preset loading via `bristol_mem_loader.c`.

## Completed

| Synth | Type | Presets | Status |
|-------|------|---------|--------|
| Mini (Minimoog) | Mono analog | 62 | ✅ Done |
| Juno (Juno-60) | DCO + Chorus | 9 | ✅ Done |

## To Implement

### Priority 1: Classic Analog Synths

| Synth | Brighton Source | Presets | Complexity | Notes |
|-------|-----------------|---------|------------|-------|
| Prophet-5 | brightonProphet.c | 66 | Medium | 5-voice poly, classic architecture |
| OB-X | brightonOBX.c | 21 | Medium | 8-voice poly, ladder filter |
| OB-Xa | brightonOBXa.c | 13 | Medium | Enhanced OB-X |
| Jupiter-8 | brightonJupiter.c | 8 | Medium | Roland flagship poly |
| Odyssey | brightonOdyssey.c | 8 | Medium | ARP duophonic |

### Priority 2: More Analog Synths

| Synth | Brighton Source | Presets | Complexity | Notes |
|-------|-----------------|---------|------------|-------|
| Pro-1 | brightonProOne.c | 15 | Easy | Mono, Pro-One style |
| Axxe | brightonAxxe.c | 2 | Easy | Simple ARP mono |
| 2600 | brightonArp2600.c | 17 | Hard | Semi-modular, many params |
| Poly-6 | brightonPoly6.c | 14 | Medium | Korg analog poly |
| Poly-800 | brightonPoly800.c | 4 | Medium | Digital-ish Korg |
| Mono/Poly | brightonPoly.c | 1 | Medium | Korg mono/poly |
| Sonic-6 | brightonSID.c? | 62 | Medium | Moog Sonic Six |
| Memory Moog | brightonMemoryMoog.c | 9 | Hard | 6-voice, complex |

### Priority 3: Keys & Organs

| Synth | Brighton Source | Presets | Complexity | Notes |
|-------|-----------------|---------|------------|-------|
| Rhodes | brightonRhodes.c | 8 | Medium | Electric piano |
| Hammond B3 | brightonHammondB3.c | 23 | Hard | Drawbars, Leslie |
| Vox Continental | brightonVox.c? | 4 | Medium | Combo organ |
| Solina | brightonSolina.c? | 10 | Easy | String machine |

### Priority 4: Other

| Synth | Brighton Source | Presets | Complexity | Notes |
|-------|-----------------|---------|------------|-------|
| Prophet-52 | brightonProphet52.c | 64 | Medium | Prophet variant |
| Bit-1 | brightonBitOne.c | 12 | Medium | Crumar digital |
| Trilogy | brightonTrilogy.c? | 8 | Medium | Crumar |
| Stratus | brightonStratus.c? | 4 | Medium | Crumar |
| Roadrunner | brightonRoadRunner.c | 6 | Easy | Crumar electric piano |
| MG-1 | brightonRealistic.c | 1 | Easy | Moog/Realistic budget |

## Skip (Not Working in Bristol)

| Synth | Reason |
|-------|--------|
| CS-80 | "Not operable" per Bristol docs |
| MS-20 | Only GUI finished |
| Synthi AKS | Filter/reverb need rework |
| DX-7 | We already have Dexed |

## Implementation Pattern

For each synth:

1. **Read brighton source** to get parameter indices from `locations[]` array
2. **Create engine**: `src/synths/<id>/<id>_engine.c` and `.h`
   - Use shared components from `bristol_common.h`
   - Map Bristol parameters to engine state
3. **Create plugin**: `src/synths/<id>/<id>_plugin.cpp`
   - Implement get_param for ALL parameters in ui_hierarchy
   - Map preset parameters to engine
4. **Create UI files**: `module.json`, `ui.js`
5. **Update build.sh** to compile new synth
6. **Test** with presets

## Parameter Extraction Pattern

Each Brighton file has a `locations[]` array defining parameter mappings:

```c
static brightonLocations locations[DEVICE_COUNT] = {
    {"Osc1-Tuning", ...}, // Index 0
    {"Osc2-Tuning", ...}, // Index 1
    // etc.
};
```

These indices match the .mem file parameter order.

## Build Script Structure

```bash
# For each synth:
${CROSS_PREFIX}g++ -g -O3 -shared -fPIC -std=c++14 \
    src/synths/<id>/<id>_engine.c \
    src/synths/<id>/<id>_plugin.cpp \
    build/bristol_mem_loader.o \
    -o build/<id>-dsp.so \
    -Isrc/synths/<id> -Isrc/shared -lm
```

## Directory Structure

```
src/synths/
  mini/       # ✅ Done
  juno/       # ✅ Done
  prophet/    # To implement
  obx/        # To implement
  ...

presets/
  mini/       # 62 presets
  juno/       # 9 presets
  prophet/    # 66 presets
  obx/        # 21 presets
  ...
```

## Notes

- All synths share `bristol_common.h` DSP components (oscillators, filters, envelopes, LFO)
- All use `bristol_mem_loader.c` for runtime preset loading
- Mono output duplicated to stereo (avoid the Juno render bug)
- Complete get_param coverage needed for Shadow UI editing
