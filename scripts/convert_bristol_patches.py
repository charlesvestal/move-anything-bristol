#!/usr/bin/env python3
"""
Convert Bristol .mem patch files to C preset structs.
Bristol memory format:
  - Bytes 0-31: synth name
  - Bytes 32-63: patch name
  - Bytes 64+: float parameters (little-endian IEEE 754)
"""

import struct
import os
import sys
from pathlib import Path

# Bristol Mini parameter indices (from brightonMini.c)
MINI_PARAMS = {
    0: 'tune',           # Master tune
    1: 'glide',          # Glide time
    2: 'mod',            # Mod amount
    3: 'osc1_oct',       # Osc1 transpose (0-5 = 16',8',4',2',1',Lo)
    4: 'osc2_oct',       # Osc2 transpose
    5: 'osc3_oct',       # Osc3 transpose
    6: 'osc2_detune',    # Osc2 fine tune
    7: 'osc3_detune',    # Osc3 fine tune
    8: 'osc1_wave',      # Osc1 waveform (0-5)
    9: 'osc2_wave',      # Osc2 waveform
    10: 'osc3_wave',     # Osc3 waveform
    11: 'osc1_level',    # Osc1 mixer level
    12: 'ext_level',     # External input level
    13: 'osc2_level',    # Osc2 mixer level
    14: 'noise_level',   # Noise level
    15: 'osc3_level',    # Osc3 mixer level
    16: 'filter_cutoff', # Filter frequency
    17: 'filter_res',    # Filter emphasis (resonance)
    18: 'filter_env',    # Filter contour amount
    19: 'fenv_a',        # Filter attack
    20: 'fenv_d',        # Filter decay
    21: 'fenv_r',        # Filter release (no sustain on Mini)
    22: 'aenv_a',        # Amp attack
    23: 'aenv_d',        # Amp decay
    24: 'aenv_r',        # Amp release
    25: 'master_vol',    # Master volume
    # 26-27: on/off, A-440
    # 28-32: mixer on/off buttons
    # 33: white/pink noise
    # 34-38: control switches
    # 39-41: filter switches
}

# Bristol Juno parameter indices (from brightonJuno.c locations array)
# After 72-byte header (64 name + 8 int header)
JUNO_PARAMS = {
    0: 'power',          # On/Off button
    1: 'dco_mod',        # Mod wheel -> DCO
    2: 'vcf_mod',        # Mod wheel -> VCF
    3: 'tuning',         # Master tune
    4: 'glide',          # Glide time
    5: 'lfo_manual',     # LFO manual trigger button
    6: 'transpose',      # Transpose switch (3-way)
    7: 'hold',           # Hold button
    8: 'lfo_rate',       # LFO Rate
    9: 'lfo_delay',      # LFO Delay
    10: 'man_auto',      # Manual/Auto switch
    11: 'dco_lfo',       # DCO-LFO mod amount
    12: 'pwm',           # PWM amount
    13: 'pwm_source',    # PWM source (3-way: manual/lfo/env)
    14: 'pulse',         # Pulse on/off
    15: 'saw',           # Saw (ramp) on/off
    16: 'square',        # Square (sub) on/off
    17: 'sub_level',     # Sub oscillator level
    18: 'noise',         # Noise level
    19: 'hpf',           # HPF frequency
    20: 'vcf_freq',      # VCF frequency (cutoff)
    21: 'vcf_res',       # VCF resonance
    22: 'env_polarity',  # Env +/- switch
    23: 'vcf_env',       # VCF env mod amount
    24: 'vcf_lfo',       # VCF LFO mod amount
    25: 'vcf_kbd',       # VCF keyboard tracking
    26: 'vca_mode',      # VCA env/gate switch
    27: 'vca_level',     # VCA level
    28: 'env_a',         # Attack
    29: 'env_d',         # Decay
    30: 'env_s',         # Sustain
    31: 'env_r',         # Release
    32: 'chorus_1',      # Chorus I button
    33: 'chorus_2',      # Chorus II button
    34: 'chorus_3',      # Chorus I+II button
}

def read_mem_file(filepath):
    """Read a Bristol .mem file and extract parameters."""
    with open(filepath, 'rb') as f:
        data = f.read()

    # Extract names
    synth_name = data[0:32].rstrip(b'\x00').decode('ascii', errors='replace')
    patch_name = data[32:64].rstrip(b'\x00').decode('ascii', errors='replace')

    # Extract float parameters starting at offset 72 (64 + 8 bytes for two uint32 header fields)
    params = []
    offset = 72
    while offset + 4 <= len(data):
        val = struct.unpack('<f', data[offset:offset+4])[0]
        params.append(val)
        offset += 4

    return synth_name, patch_name, params

def convert_mini_patch(patch_name, params):
    """Convert Mini params to our MiniPreset struct format."""
    # Map Bristol waveform index to our wave enum
    # Bristol: 0=tri, 1=ramp, 2=saw, 3=square, 4=pulse, 5=noise (stored as 0-5 float)
    # Ours: WAVE_SINE=0, WAVE_TRI=1, WAVE_SAW=2, WAVE_SQUARE=3, WAVE_PULSE=4
    def map_wave(w):
        w = int(round(w))  # Bristol stores as float 0-5
        if w == 0: return 1  # tri -> WAVE_TRI
        if w == 1: return 2  # ramp -> WAVE_SAW
        if w == 2: return 2  # saw -> WAVE_SAW
        if w == 3: return 3  # square -> WAVE_SQUARE
        if w == 4: return 4  # pulse -> WAVE_PULSE
        return 2  # default saw

    def map_oct(o):
        # Bristol: 0-5 = 16',8',4',2',1',Lo (stored as float 0-5)
        o = int(round(o))
        return o - 2 if o < 5 else -2

    # Get values with defaults
    def p(idx, default=0.0):
        return params[idx] if idx < len(params) else default

    return {
        'name': patch_name if patch_name != 'no name' else f'Patch',
        'osc1_wave': map_wave(p(8)),
        'osc2_wave': map_wave(p(9)),
        'osc3_wave': map_wave(p(10)),
        'osc1_oct': map_oct(p(3)),
        'osc2_oct': map_oct(p(4)),
        'osc3_oct': map_oct(p(5)),
        'osc1_level': p(11),
        'osc2_level': p(13),
        'osc3_level': p(15),
        'osc2_detune': (p(6) - 0.5) * 12,  # Convert 0-1 centered at 0.5 to semitones
        'osc3_detune': (p(7) - 0.5) * 12,
        'filter_cutoff': p(16),
        'filter_res': p(17),
        'filter_env': p(18),
        'filter_keytrack': 0.0,  # Not in original Mini
        'fenv_a': p(19),
        'fenv_d': p(20),
        'fenv_s': 0.0,  # Mini has no filter sustain
        'fenv_r': p(21),
        'aenv_a': p(22),
        'aenv_d': p(23),
        'aenv_s': 1.0,  # Mini has no amp sustain, assume full
        'aenv_r': p(24),
        'lfo_rate': 0.3,  # Not directly mapped
        'lfo_to_osc': p(2) * 0.1,  # Mod amount
        'lfo_to_filter': 0.0,
        'noise_level': p(14),
        'glide': p(1),
        'osc3_as_lfo': 0,
    }

def convert_juno_patch(patch_name, params):
    """Convert Juno params to our JunoPreset struct format."""
    def p(idx, default=0.0):
        return params[idx] if idx < len(params) else default

    # Determine chorus mode from the three chorus buttons
    chorus_mode = 0
    if p(32) > 0.5:  # Chorus I
        chorus_mode = 1
    if p(33) > 0.5:  # Chorus II
        chorus_mode = 2 if chorus_mode == 0 else 3  # II alone or I+II
    if p(34) > 0.5:  # Chorus I+II button
        chorus_mode = 3

    return {
        'name': patch_name if patch_name != 'no name' else f'Patch',
        'saw_enabled': 1 if p(15) > 0.5 else 0,      # Saw/ramp button
        'pulse_enabled': 1 if p(14) > 0.5 else 0,    # Pulse button
        'sub_enabled': 1 if p(16) > 0.5 else 0,      # Square/sub button
        'pulse_width': 0.5,  # Base PW (modulated by PWM)
        'pw_lfo_amount': p(12),                       # PWM amount
        'sub_level': p(17),                           # Sub oscillator level
        'filter_cutoff': p(20),                       # VCF frequency
        'filter_res': p(21),                          # VCF resonance
        'filter_env': p(23),                          # VCF env mod
        'filter_keytrack': p(25),                     # VCF keyboard tracking
        'hpf_cutoff': 20.0 + p(19) * 200,            # HPF (0-1 -> 20-220 Hz)
        'fenv_a': p(28),                              # Attack
        'fenv_d': p(29),                              # Decay
        'fenv_s': p(30),                              # Sustain
        'fenv_r': p(31),                              # Release
        'aenv_a': p(28),                              # Same ADSR for amp
        'aenv_d': p(29),
        'aenv_s': p(30),
        'aenv_r': p(31),
        'lfo_rate': p(8),                             # LFO rate
        'lfo_to_dco': p(11),                          # DCO-LFO mod
        'lfo_to_filter': p(24),                       # VCF LFO mod
        'chorus_mode': chorus_mode,
        'noise_level': p(18),                         # Noise level
        'glide': p(4),                                # Glide time
    }

def generate_mini_c_struct(patches):
    """Generate C code for Mini presets."""
    lines = ["static const MiniPreset g_presets[] = {"]

    for i, patch in enumerate(patches):
        lines.append(f"    /* {i}: {patch['name']} */")
        lines.append(f"    {{ \"{patch['name']}\",")
        lines.append(f"      {patch['osc1_wave']}, {patch['osc2_wave']}, {patch['osc3_wave']}, "
                    f"{patch['osc1_oct']}, {patch['osc2_oct']}, {patch['osc3_oct']},")
        lines.append(f"      {patch['osc1_level']:.2f}f, {patch['osc2_level']:.2f}f, {patch['osc3_level']:.2f}f, "
                    f"{patch['osc2_detune']:.1f}f, {patch['osc3_detune']:.1f}f,")
        lines.append(f"      {patch['filter_cutoff']:.2f}f, {patch['filter_res']:.2f}f, "
                    f"{patch['filter_env']:.2f}f, {patch['filter_keytrack']:.2f}f,")
        lines.append(f"      {patch['fenv_a']:.2f}f, {patch['fenv_d']:.2f}f, "
                    f"{patch['fenv_s']:.2f}f, {patch['fenv_r']:.2f}f,")
        lines.append(f"      {patch['aenv_a']:.2f}f, {patch['aenv_d']:.2f}f, "
                    f"{patch['aenv_s']:.2f}f, {patch['aenv_r']:.2f}f,")
        lines.append(f"      {patch['lfo_rate']:.2f}f, {patch['lfo_to_osc']:.2f}f, "
                    f"{patch['lfo_to_filter']:.2f}f,")
        lines.append(f"      {patch['noise_level']:.2f}f, {patch['glide']:.2f}f, "
                    f"{patch['osc3_as_lfo']} }},")
        lines.append("")

    lines.append("};")
    return '\n'.join(lines)

def generate_juno_c_struct(patches):
    """Generate C code for Juno presets."""
    lines = ["static const JunoPreset g_presets[] = {"]

    for i, patch in enumerate(patches):
        lines.append(f"    /* {i}: {patch['name']} */")
        lines.append(f"    {{ \"{patch['name']}\",")
        lines.append(f"      {patch['saw_enabled']}, {patch['pulse_enabled']}, {patch['sub_enabled']},")
        lines.append(f"      {patch['pulse_width']:.2f}f, {patch['pw_lfo_amount']:.2f}f, {patch['sub_level']:.2f}f,")
        lines.append(f"      {patch['filter_cutoff']:.2f}f, {patch['filter_res']:.2f}f, "
                    f"{patch['filter_env']:.2f}f, {patch['filter_keytrack']:.2f}f,")
        lines.append(f"      {patch['hpf_cutoff']:.1f}f,")
        lines.append(f"      {patch['fenv_a']:.2f}f, {patch['fenv_d']:.2f}f, "
                    f"{patch['fenv_s']:.2f}f, {patch['fenv_r']:.2f}f,")
        lines.append(f"      {patch['aenv_a']:.2f}f, {patch['aenv_d']:.2f}f, "
                    f"{patch['aenv_s']:.2f}f, {patch['aenv_r']:.2f}f,")
        lines.append(f"      {patch['lfo_rate']:.2f}f, {patch['lfo_to_dco']:.3f}f, "
                    f"{patch['lfo_to_filter']:.2f}f,")
        lines.append(f"      {patch['chorus_mode']},")
        lines.append(f"      {patch['noise_level']:.2f}f, {patch['glide']:.2f}f }},")
        lines.append("")

    lines.append("};")
    return '\n'.join(lines)

def main():
    # Process Mini patches
    mini_dir = Path('/tmp/bristol-0.60.11/memory/mini')
    mini_patches = []

    for mem_file in sorted(mini_dir.glob('*.mem')):
        synth, name, params = read_mem_file(mem_file)
        patch = convert_mini_patch(name, params)
        patch['name'] = name if name != 'no name' else mem_file.stem
        mini_patches.append(patch)
        print(f"Mini: {mem_file.name} -> {patch['name']}", file=sys.stderr)

    # Limit to ~20 best patches (skip duplicates/empty)
    # Select patches with interesting settings
    selected_mini = []
    seen_names = set()
    for p in mini_patches:
        if p['name'] not in seen_names and len(selected_mini) < 20:
            # Skip if all oscillators are silent
            if p['osc1_level'] > 0.01 or p['osc2_level'] > 0.01 or p['osc3_level'] > 0.01:
                selected_mini.append(p)
                seen_names.add(p['name'])

    print("\n// ============ MINI PRESETS ============\n")
    print(generate_mini_c_struct(selected_mini))

    # Process Juno patches
    juno_dir = Path('/tmp/bristol-0.60.11/memory/juno')
    juno_patches = []

    for mem_file in sorted(juno_dir.glob('*.mem')):
        synth, name, params = read_mem_file(mem_file)
        patch = convert_juno_patch(name, params)
        patch['name'] = name if name != 'no name' else mem_file.stem
        juno_patches.append(patch)
        print(f"Juno: {mem_file.name} -> {patch['name']}", file=sys.stderr)

    print("\n// ============ JUNO PRESETS ============\n")
    print(generate_juno_c_struct(juno_patches))

if __name__ == '__main__':
    main()
