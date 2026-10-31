#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only (part of LXR: src/lxr/NOTICE.md - it carries the LXR-02 firmware's formulas or reads its sources)
"""Imports, once, the data LXR (machine 13, src/lxr/, GPL-3.0-only) takes from the Erica Synths LXR-02 firmware
(https://github.com/erica-synths/lxr-desktop-app, GPL-3.0-only) into src/lxr/:

  lxr_wave.h     its band-limited triangle, saw and square wavetables (Core/Src/Soundengine/DSPAudio/wavetable.c:
                 triTable, sawTable, recTable, 11 octaves of 1024 samples), as they are
  lxr_presets.h  the six voices of its init kit (Core/Src/Preset/initKit.c, "from hrtlkits_batch3"), as the raw
                 0..127 knob values of each voice's parameters, in lxr.c's order (P_*); CLAP and the open hat are
                 the snare and the hat of that kit with the changes listed below

Both files are committed; this is only rerun to re-import them. It reads the LXR's own sources (the parameter
numbers from Parameters.h, the kit from initKit.c) - nothing is typed in by hand.

    python3 tools/lxr_import.py path/to/lxr-desktop-app
"""
import os, re, sys

REPO = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
D = os.path.join(REPO, 'src', 'lxr')
HEAD = ('/* Imported from the Erica Synths LXR-02 firmware (lxr-desktop-app, GPL-3.0-only, Copyright (C) 2012-2026\n'
        ' * Erica Synths) by tools/lxr_import.py - do not edit. Part of LXR, GPL-3.0-only (src/lxr/LICENSE). */\n')

# lxr.c's parameter order (P_*), and where each comes from in the LXR, per voice kind: drum voice n (1..3),
# snare (4), cymbal (5), hat (6). None: the LXR has no such parameter for that voice (the default applies).
PARAMS = ['WAVE', 'WAVE2', 'WAVE3', 'COARSE', 'FINE', 'MODF1', 'MODF2', 'GAIN1', 'GAIN2', 'MIXMODE',
          'PDEC', 'PAMT', 'PSLOPE', 'ATT', 'DEC', 'DECO', 'VSLOPE', 'REPEAT', 'NOISEF', 'MIX',
          'FTYPE', 'FFREQ', 'FRESO', 'FDRIVE', 'DIST', 'VOL', 'DECIM', 'TWAVE', 'TVOL', 'TFREQ', 'PWM']


def source(kind, n):
    if kind == 'drum':
        return {'WAVE': f'OSC_WAVE_DRUM{n}', 'WAVE2': f'MOD_WAVE_DRUM{n}', 'COARSE': f'COARSE{n}', 'FINE': f'FINE{n}',
                'MODF1': f'FM_FREQ{n}', 'GAIN1': f'FMAMNT{n}', 'MIXMODE': f'MIX_MOD_{n}', 'PDEC': f'MOD_EG{n}',
                'PAMT': f'MODAMNT{n}', 'PSLOPE': f'PITCH_SLOPE{n}', 'ATT': f'VELOA{n}', 'DEC': f'VELOD{n}',
                'VSLOPE': f'VOL_SLOPE{n}', 'FTYPE': f'FILTER_TYPE_{n}', 'FFREQ': f'FILTER_FREQ_{n}',
                'FRESO': f'RESO_{n}', 'FDRIVE': f'FILTER_DRIVE_{n}', 'DIST': f'DRIVE{n}', 'VOL': f'VOL{n}',
                'DECIM': f'VOICE_DECIMATION{n}', 'TWAVE': f'TRANS{n}_WAVE', 'TVOL': f'TRANS{n}_VOL',
                'TFREQ': f'TRANS{n}_FREQ', 'PWM': f'PWM_{n}'}
    if kind == 'snare':
        return {'WAVE': 'OSC_WAVE_SNARE', 'COARSE': 'COARSE4', 'FINE': 'FINE4', 'PDEC': 'MOD_EG4', 'PAMT': 'MODAMNT4',
                'PSLOPE': 'PITCH_SLOPE4', 'ATT': 'VELOA4', 'DEC': 'VELOD4', 'VSLOPE': 'VOL_SLOPE4', 'REPEAT': 'REPEAT4',
                'NOISEF': 'NOISE_FREQ1', 'MIX': 'MIX1', 'FTYPE': 'FILTER_TYPE_4', 'FFREQ': 'FILTER_FREQ_4',
                'FRESO': 'RESO_4', 'FDRIVE': 'FILTER_DRIVE_4', 'DIST': 'SNARE_DISTORTION', 'VOL': 'VOL4',
                'DECIM': 'VOICE_DECIMATION4', 'TWAVE': 'TRANS4_WAVE', 'TVOL': 'TRANS4_VOL', 'TFREQ': 'TRANS4_FREQ',
                'PWM': 'PWM_4'}
    if kind == 'cym':
        return {'WAVE': 'WAVE1_CYM', 'WAVE2': 'WAVE2_CYM', 'WAVE3': 'WAVE3_CYM', 'COARSE': 'COARSE5', 'FINE': 'FINE5',
                'MODF1': 'MOD_OSC_F1_CYM', 'MODF2': 'MOD_OSC_F2_CYM', 'GAIN1': 'MOD_OSC_GAIN1_CYM',
                'GAIN2': 'MOD_OSC_GAIN2_CYM', 'ATT': 'VELOA5', 'DEC': 'VELOD5', 'VSLOPE': 'VOL_SLOPE5',
                'REPEAT': 'REPEAT5', 'FTYPE': 'FILTER_TYPE_5', 'FFREQ': 'FILTER_FREQ_5', 'FRESO': 'RESO_5',
                'FDRIVE': 'FILTER_DRIVE_5', 'DIST': 'CYMBAL_DISTORTION', 'VOL': 'VOL5', 'DECIM': 'VOICE_DECIMATION5',
                'TWAVE': 'TRANS5_WAVE', 'TVOL': 'TRANS5_VOL', 'TFREQ': 'TRANS5_FREQ', 'PWM': 'PWM_5'}
    return {'WAVE': 'WAVE1_HH', 'WAVE2': 'WAVE2_HH', 'WAVE3': 'WAVE3_HH', 'COARSE': 'COARSE6', 'FINE': 'FINE6',
            'MODF1': 'MOD_OSC_F1', 'MODF2': 'MOD_OSC_F2', 'GAIN1': 'MOD_OSC_GAIN1', 'GAIN2': 'MOD_OSC_GAIN2',
            'ATT': 'VELOA6', 'DEC': 'VELOD6_CLOSED', 'DECO': 'VELOD6_OPEN', 'VSLOPE': 'VOL_SLOPE6',
            'FTYPE': 'FILTER_TYPE_6', 'FFREQ': 'FILTER_FREQ_6', 'FRESO': 'RESO_6', 'FDRIVE': 'FILTER_DRIVE_6',
            'DIST': 'HAT_DISTORTION', 'VOL': 'VOL6', 'DECIM': 'VOICE_DECIMATION6', 'TWAVE': 'TRANS6_WAVE',
            'TVOL': 'TRANS6_VOL', 'TFREQ': 'TRANS6_FREQ', 'PWM': 'PWM_6'}


# the LXR's own defaults where a voice has no such parameter (its init functions): the mod oscillators' fine 64
DEFAULT = {'FINE': 64, 'MODF1': 70, 'MODF2': 70, 'GAIN1': 64, 'GAIN2': 64, 'DECIM': 127, 'VOL': 102}

# the eight presets: (name, kind, LXR voice, changes to the kit's values)
PRESETS = [
    ('KICK', 'drum', 1, {}),
    ('TOM', 'drum', 2, {}),
    ('PERC', 'drum', 3, {}),
    ('SNAR', 'snare', 4, {}),
    # CLAP: the snare voice in its repeat mode (the LXR's clap): noise only, band-passed, four quick repeats
    # (the attack knob is their spacing: 11, about 15 ms by the LXR's curve), then a tail (decay 25, about 0.4 s)
    ('CLAP', 'snare', 4, {'MIX': 127, 'REPEAT': 4, 'ATT': 11, 'DEC': 25, 'VSLOPE': 30, 'FTYPE': 2, 'FFREQ': 70,
                          'FRESO': 40, 'PAMT': 0, 'NOISEF': 127}),
    ('CHAT', 'hat', 6, {}),
    ('OHAT', 'hat', 6, {}),                    # the same hat, triggered open (its open decay)
    ('CYMB', 'cym', 5, {}),
]


def par_numbers(lxr):
    s = open(os.path.join(lxr, 'Core', 'Inc', 'Parameters.h')).read()
    s = re.sub(r'/\*.*?\*/', '', s, flags=re.S)
    s = re.sub(r'//[^\n]*', '', s)
    body = s[s.index('enum ParamEnums'):]
    body = body[body.index('{') + 1:body.index('}')]
    num, v = {}, -1
    for item in body.split(','):
        item = item.strip()
        if not item:
            continue
        if '=' in item:
            n, e = item.split('=')
            n, e = n.strip(), e.strip()
            v = num[e] if e in num else int(e, 0)
        else:
            n = item
            v += 1
        num[n] = v
    return num


def init_kit(lxr):
    s = open(os.path.join(lxr, 'Core', 'Src', 'Preset', 'initKit.c')).read()
    s = re.sub(r'/\*.*?\*/', '', s, flags=re.S)
    s = re.sub(r'//[^\n]*', '', s)
    body = s[s.index('initKit[INIT_KIT_SIZE]'):]
    body = body[body.index('{') + 1:body.index('}')]
    return [int(x, 0) for x in re.findall(r'0x[0-9a-fA-F]+|\d+', body)]


def tables(lxr):
    s = open(os.path.join(lxr, 'Core', 'Src', 'Soundengine', 'DSPAudio', 'wavetable.c')).read()
    s = re.sub(r'/\*.*?\*/', '', s, flags=re.S)
    s = re.sub(r'//[^\n]*', '', s)
    out = {}
    for name in ('triTable', 'sawTable', 'recTable'):
        i = s.index(f'{name}[11][1024]')
        j = s.index('};', i)
        vals = [int(x) for x in re.findall(r'-?\d+', s[s.index('{', i):j])]
        assert len(vals) == 11 * 1024, (name, len(vals))
        out[name] = vals
    return out


def main():
    lxr = sys.argv[1]
    num, kit = par_numbers(lxr), init_kit(lxr)
    para = lambda name: kit[num['PAR_' + name] + 8]          # presetManager: para[i - 8] = initKit[i]
    t = tables(lxr)
    w = [HEAD, '/* triTable, sawTable, recTable: 11 octaves (band-limited from MIDI notes 0, 12, .. 120) x 1024 */\n',
         'static const short lx_wav[3][11][1024] __attribute__((section(".lxr_ro"))) = {\n']
    for name in ('triTable', 'sawTable', 'recTable'):
        w.append(' {\n')
        for b in range(11):
            w.append('  {' + ','.join(str(x) for x in t[name][b * 1024:(b + 1) * 1024]) + '},\n')
        w.append(' },\n')
    w.append('};\n')
    open(os.path.join(D, 'lxr_wave.h'), 'w').write(''.join(w))
    p = [HEAD, '/* the presets: the init kit\'s voices ("from hrtlkits_batch3"), knob values 0..127, in P_* order */\n',
         '#define N_PRESETS %d\n' % len(PRESETS),
         'static const u8 lx_presets[N_PRESETS][N_P] __attribute__((section(".lxr_ro"))) = {\n']
    kinds = {'drum': 'K_DRUM', 'snare': 'K_SNARE', 'cym': 'K_CYM', 'hat': 'K_HAT'}
    for name, kind, n, changes in PRESETS:
        src = source(kind, n)
        vals = []
        for q in PARAMS:
            v = changes.get(q, para(src[q]) if q in src else DEFAULT.get(q, 0))
            vals.append(v)
        p.append('  /* %-4s */ {%s},\n' % (name, ', '.join(str(v) for v in vals)))
    p.append('};\n')
    p.append('static const u8 lx_kind[N_PRESETS] __attribute__((section(".lxr_ro"))) = {%s};\n'
             % ', '.join(kinds[k] for _, k, _, _ in PRESETS))
    p.append('static const u8 lx_open[N_PRESETS] __attribute__((section(".lxr_ro"))) = {%s};\n'
             % ', '.join('1' if name == 'OHAT' else '0' for name, _, _, _ in PRESETS))
    open(os.path.join(D, 'lxr_presets.h'), 'w').write(''.join(p))
    print('wrote src/lxr/lxr_wave.h, src/lxr/lxr_presets.h')


if __name__ == '__main__':
    main()
