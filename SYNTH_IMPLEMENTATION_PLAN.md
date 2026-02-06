# Bristol Synth Implementation Plan

## Overview

Port Bristol synthesizer emulations to Move Anything using the shared DSP components in `src/shared/bristol_common.h` and runtime .mem preset loading via `bristol_mem_loader.c`.

## Completed (13 Synths)

| Synth | Type | Presets | Status |
|-------|------|---------|--------|
| Mini (Minimoog) | Mono analog | 62 | ✅ Done |
| Juno (Juno-60) | DCO + Chorus | 9 | ✅ Done |
| Prophet-5 | 5-voice poly | 66 | ✅ Done |
| OB-X | 8-voice poly | 21 | ✅ Done |
| OB-Xa | 8-voice poly | 13 | ✅ Done |
| Jupiter-8 | 8-voice poly | 8 | ✅ Done |
| Odyssey | ARP duophonic | 8 | ✅ Done |
| Pro-1 | Mono synth | 15 | ✅ Done |
| Axxe | ARP mono | 2 | ✅ Done |
| Poly-6 | Korg 6-voice | 14 | ✅ Done |
| Solina | String machine | 10 | ✅ Done |
| Rhodes | Electric piano | 8 | ✅ Done |
| Roadrunner | Electric piano | 6 | ✅ Done |

## To Implement

### Priority 2: More Synths

| Synth | Brighton Source | Presets | Complexity | Notes |
|-------|-----------------|---------|------------|-------|
| 2600 | brightonArp2600.c | 17 | Hard | Semi-modular, many params |
| Poly-800 | brightonPoly800.c | 4 | Medium | Digital-ish Korg |
| Mono/Poly | brightonPoly.c | 1 | Medium | Korg mono/poly |
| Sonic-6 | brightonSID.c? | 62 | Medium | Moog Sonic Six |
| Memory Moog | brightonMemoryMoog.c | 9 | Hard | 6-voice, complex |

### Priority 3: Keys & Organs

| Synth | Brighton Source | Presets | Complexity | Notes |
|-------|-----------------|---------|------------|-------|
| Hammond B3 | brightonHammondB3.c | 23 | Hard | Drawbars, Leslie |
| Vox Continental | brightonVox.c? | 4 | Medium | Combo organ |

### Priority 4: Other

| Synth | Brighton Source | Presets | Complexity | Notes |
|-------|-----------------|---------|------------|-------|
| Prophet-52 | brightonProphet52.c | 64 | Medium | Prophet variant |
| Bit-1 | brightonBitOne.c | 12 | Medium | Crumar digital |
| Trilogy | brightonTrilogy.c? | 8 | Medium | Crumar |
| Stratus | brightonStratus.c? | 4 | Medium | Crumar |
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

## Key Implementation Notes

- **Mono output to stereo**: Always use `float mono_buf[256]` and duplicate to stereo
- **Complete get_param coverage**: Every parameter in `ui_hierarchy` must have a handler
- **MAX_PRESETS = 128**: Allows loading all presets from .mem files
- **Runtime preset loading**: Uses `bristol_scan_presets()` from `bristol_mem_loader.c`

## Directory Structure

```
src/synths/
  mini/       ✅
  juno/       ✅
  prophet/    ✅
  obx/        ✅
  obxa/       ✅
  odyssey/    ✅
  jupiter/    ✅
  pro1/       ✅
  axxe/       ✅
  poly6/      ✅
  solina/     ✅
  rhodes/     ✅
  roadrunner/ ✅

presets/
  mini/       # 62 presets
  juno/       # 9 presets
  prophet/    # 66 presets
  obx/        # 21 presets
  obxa/       # 13 presets
  odyssey/    # 8 presets
  jupiter/    # 8 presets
  pro1/       # 15 presets
  axxe/       # 2 presets
  poly6/      # 14 presets
  solina/     # 10 presets
  rhodes/     # 8 presets
  roadrunner/ # 6 presets
```
