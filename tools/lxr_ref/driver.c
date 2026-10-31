/* tools/lxr_ref: the Erica Synths LXR-02's own voice code, built on a host, to render the reference the LXR machine
 * (src/lxr/) is checked against. Never part of the firmware. Links the LXR firmware's sources (GPL-3.0-only,
 * Copyright (C) 2012-2026 Erica Synths), so this file is GPL-3.0-only too (src/lxr/LICENSE).
 *
 * The init kit (initKit.c) is applied through the LXR's own midiParser_ccHandler (the parameters it sends on a kit
 * load), the smoothing filters settle, then one voice is triggered at full velocity on the default note and rendered
 * as its mixer does: calc...Async once a 64-sample block, then the block. The modulation routing (LFOs, velocity
 * targets) is left out (stubs.c): the reference is the dry voice. Build and use: tools/lxr_reference.py.
 *
 *   lxref VOICE OUT.wav SECONDS [PAR=VALUE ...]   VOICE: 0..2 drum voices, 3 snare, 4 cymbal, 5 closed hat, 6 open */
#include <stdio.h>
#include <stdlib.h>
#include "Soundengine/MIDI/MidiMessages.h"
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/DSPAudio/HiHat.h"
#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/DSPAudio/random.h"
#include "Parameters.h"
#include "Preset/InitKit.h"
#include "Soundengine/DSPAudio/modulationNode.h"
void parameterArray_init(void);
void midiParser_ccHandler(MidiMsg msg, uint8_t updateOriginalValue, uint8_t isFloatModulator, float v);
void Snare_calcAsync(void); void Cymbal_calcAsync(void); void HiHat_calcAsync(void);
static void set(int par, int val) {
    /* the LXR's numbering: CC = parameter + 1 up to 126, then the second page (CC2) = parameter - 128 */
    if (par == 127) return;
    MidiMsg m; m.status = par < 127 ? MIDI_CC : MIDI_CC2; m.data1 = par < 127 ? par + 1 : par - 128; m.data2 = val;
    midiParser_ccHandler(m, 1, 0, 0.f);
}
static void wav(const char *fn, const int16_t *x, int n) {
    FILE *f = fopen(fn, "wb"); int sr = 44140, br = sr * 2, ds = n * 2, rs = 36 + ds; short ch = 1, ba = 2, bps = 16, fmt = 1; int fl = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&rs, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fl, 4, 1, f); fwrite(&fmt, 2, 1, f);
    fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bps, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&ds, 4, 1, f); fwrite(x, 2, n, f); fclose(f);
}
int main(int argc, char **argv) {
    /* argv: voice (0..2 drums, 3 snare, 4 cymbal, 5 closed hat, 6 open hat), out.wav, seconds, then par=val overrides */
    int v = atoi(argv[1]); float secs = atof(argv[3]);
    parameterArray_init();

    initRng(); initDrumVoice(); Snare_init(); HiHat_init(); Cymbal_init();
    for (int i = 1; i < END_OF_SOUND_PARAMETERS; i++) set(i, initKit[i + 8]);
    for (int a = 4; a < argc; a++) { int p, val; sscanf(argv[a], "%d=%d", &p, &val); set(p, val); }
    /* the smoothing filters (fader, volume, drive) settle first, as on an LXR long before a hit */
    for (int b = 0; b < 200; b++) {
        int16_t w[OUTPUT_DMA_SIZE] = {0};
        if (v < 3) { calcDrumVoiceAsync(v); calcDrumVoiceSyncBlock(v, w, OUTPUT_DMA_SIZE); }
        else if (v == 3) { Snare_calcAsync(); Snare_calcSyncBlock(w, OUTPUT_DMA_SIZE); }
        else if (v == 4) { Cymbal_calcAsync(); Cymbal_calcSyncBlock(w, OUTPUT_DMA_SIZE); }
        else { HiHat_calcAsync(); HiHat_calcSyncBlock(w, OUTPUT_DMA_SIZE); }
    }
    const int note = SEQ_DEFAULT_NOTE;
    if (v < 3) Drum_trigger(v, 127, note); else if (v == 3) Snare_trigger(127, note);
    else if (v == 4) Cymbal_trigger(127, note); else HiHat_trigger(127, v == 6, note);
    int nb = (int)(secs * 44140 / OUTPUT_DMA_SIZE); int16_t *out = calloc(nb * OUTPUT_DMA_SIZE, 2);
    for (int b = 0; b < nb; b++) {
        int16_t buf[OUTPUT_DMA_SIZE] = {0};
        if (v < 3) { calcDrumVoiceAsync(v); calcDrumVoiceSyncBlock(v, buf, OUTPUT_DMA_SIZE); }
        else if (v == 3) { Snare_calcAsync(); Snare_calcSyncBlock(buf, OUTPUT_DMA_SIZE); }
        else if (v == 4) { Cymbal_calcAsync(); Cymbal_calcSyncBlock(buf, OUTPUT_DMA_SIZE); }
        else { HiHat_calcAsync(); HiHat_calcSyncBlock(buf, OUTPUT_DMA_SIZE); }
        for (int i = 0; i < OUTPUT_DMA_SIZE; i++) out[b * OUTPUT_DMA_SIZE + i] = buf[i];
    }
    wav(argv[2], out, nb * OUTPUT_DMA_SIZE);
    return 0;
}
