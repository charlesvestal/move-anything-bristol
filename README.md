# Bristol Mini for Move Everything

Minimoog-style monophonic synthesizer for Ableton Move, inspired by the [Bristol Synthesizer](https://bristol.sourceforge.net/) by Nick Copeland.

## Features

- **3 Oscillators** with 6 waveforms each (sine, triangle, saw up, saw down, square, pulse)
- **Band-limited oscillators** using PolyBLEP for clean, alias-free sound
- **Moog ladder filter** using Huovilainen's nonlinear algorithm with tanh saturation
- **Dual ADSR envelopes** for filter and amplitude
- **LFO** with multiple waveforms for modulation
- **Noise generator** (white and pink)
- **Glide/portamento**
- **14 factory presets**

## Presets

1. Init - Clean sawtooth
2. Classic Lead - Detuned dual-saw lead
3. Fat Bass - Multi-oscillator bass
4. Resonant Sweep - Filter sweep with OSC3 modulation
5. PWM Strings - Pulse wave pad
6. Acid Squelch - TB-303 style bass
7. Soft Sine - Gentle sine lead
8. Detuned Sync - Wide detuned lead
9. Mini Brass - Classic brass sound
10. Sub Bass - Deep sub frequencies
11. Pluck - Percussive pluck
12. Noise Lead - Lead with noise texture
13. Growl Bass - Aggressive modulated bass
14. Glide Lead - Portamento lead

## Building

```bash
./scripts/build.sh
```

Requires Docker for cross-compilation to ARM64.

## Installation

```bash
./scripts/install.sh
```

Or download from GitHub Releases and install via Module Store.

## License

GPL-3.0 - Based on Bristol Synthesizer algorithms.

## Credits

- Bristol Synthesizer by Nick Copeland
- Huovilainen Moog ladder filter algorithm
- PolyBLEP anti-aliasing technique

## AI Assistance Disclaimer

This module is part of Move Everything and was developed with AI assistance, including Claude, Codex, and other AI assistants.

All architecture, implementation, and release decisions are reviewed by human maintainers.  
AI-assisted content may still contain errors, so please validate functionality, security, and license compatibility before production use.
