/* tools/lxr_ref: what the LXR's voice code needs of the rest of its firmware, on a host. GPL-3.0-only (driver.c).
 * The ARM intrinsics are exact; the RNG is a host one (the LXR's is hardware); the rest is inert. */
#include <stdint.h>
typedef struct { int Instance; } RNG_HandleTypeDef;
RNG_HandleTypeDef hrng;
static uint32_t s = 0x12345678;
uint32_t HAL_RNG_GetRandomNumber(RNG_HandleTypeDef *h) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
float adc_getFaderVolume(uint8_t v) { return 1.0f; }
/* ARM intrinsics, exact */
int32_t __SSAT(int32_t v, uint32_t b) { int32_t mx = (1 << (b - 1)) - 1, mn = -(1 << (b - 1)); return v > mx ? mx : v < mn ? mn : v; }
static int16_t sat16(int32_t v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }
uint32_t __QADD16(uint32_t a, uint32_t b) { return (uint16_t)sat16((int16_t)a + (int16_t)b) | ((uint32_t)(uint16_t)sat16((int16_t)(a >> 16) + (int16_t)(b >> 16)) << 16); }
uint32_t __QSUB16(uint32_t a, uint32_t b) { return (uint16_t)sat16((int16_t)a - (int16_t)b) | ((uint32_t)(uint16_t)sat16((int16_t)(a >> 16) - (int16_t)(b >> 16)) << 16); }
uint8_t __CLZ(uint32_t v) { return v ? __builtin_clz(v) : 32; }
/* the rest of the LXR: data, and no-ops */
char fxBus[65536]; uint8_t activeParameters[1024]; volatile uint32_t systick_ticks; uint8_t frontParser_activeTrack, menu_activePage;
float mixer_decimation_rate[7] = {1,1,1,1,1,1,1}; uint8_t mixer_audioRouting[6];
#define NOP(n) int n() { return 0; }
NOP(uart_sendFrontpanelByte) NOP(seq_getExtSync) NOP(FxBus_setParameter) NOP(sync_midiStartStop) NOP(uart_sendMidi)
NOP(seq_setRunning) NOP(seq_setMute) NOP(seq_recordAutomation) NOP(seq_isRunning) NOP(voiceControl_noteOn) NOP(usb_sendMidi)
NOP(sync_midiResetPosition) NOP(seq_sync) NOP(seq_setNextPattern) NOP(seq_sendMidiNoteOn) NOP(seq_patternSet) NOP(seq_midiNoteOff)
NOP(seq_isTrackMuted) NOP(seq_getBpm) NOP(seq_addNote) NOP(menu_switchSubPage) NOP(menu_repaint) NOP(menu_getViewedPattern)
NOP(menu_getSubPage) NOP(menu_getActiveVoice) NOP(Ringmod_setWave) NOP(FxBus_updateParameter) NOP(FxBus_prepareFxChange) NOP(Compressor_setRatio)
/* the modulation routing (velocity and LFO targets): off - the reference is the dry voice */
#include <string.h>
char velocityModulators[6 * 256];
NOP(modNode_init) NOP(modNode_setDestination) NOP(modNode_updateValue) NOP(modNode_originalValueChanged)
NOP(modNode_resetTargets) NOP(modNode_reassignVeloMod) NOP(modNode_setOriginalValueChanged)
