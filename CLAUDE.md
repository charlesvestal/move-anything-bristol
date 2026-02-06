# Bristol Mini for Move Anything

## Overview

Minimoog-style monophonic synthesizer inspired by Bristol Synthesizer. Uses Huovilainen's nonlinear Moog ladder filter algorithm and PolyBLEP band-limited oscillators.

## Build Commands

```bash
./scripts/build.sh     # Build with Docker cross-compilation
./scripts/install.sh   # Deploy to Move device
```

## Architecture

```
src/
  module.json              # Module metadata
  dsp/
    bristol_engine.h       # Core synth engine header
    bristol_engine.c       # DSP implementation (oscillators, filter, envelopes)
    bristol_plugin.cpp     # Move Anything v2 plugin wrapper
```

## Key DSP Components

- **Oscillators**: PolyBLEP anti-aliased waveforms (sine, tri, saw, square, pulse)
- **Filter**: Huovilainen 4-pole Moog ladder with 2x oversampling and tanh saturation
- **Envelopes**: Exponential ADSR with smooth retrigger
- **LFO**: Multiple waveforms, routes to pitch and filter
- **Noise**: White and pink (Paul Kellet algorithm)

## Parameters

Main knobs (mapped to CCs):
- CC 74: Filter Cutoff
- CC 71: Filter Resonance
- CC 70: Filter Env Amount
- CC 76: LFO Rate
- CC 73: Attack
- CC 75: Decay
- CC 79: Sustain
- CC 72: Release

## Future Improvements

- Port additional Bristol synths (Juno, Prophet, OBX, etc.)
- Add synth selection parameter
- Implement oscillator sync
- Add ring modulation
