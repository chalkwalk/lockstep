#pragma once

// The narrow seam onto FluidLite's channel internals. See tone_preset_swap.c
// for why it exists and the four rules that make it safe (DESIGN §29.3).
//
// Declared with opaque pointers so C++ callers never see FluidLite's private
// headers -- the coupling lives entirely inside the .c file.

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _fluid_synth_t fluid_synth_t;
typedef struct _fluid_preset_t fluid_preset_t;

// Install a cache-owned preset on a channel WITHOUT transferring ownership.
// Real-time safe: no allocation, no free. The alternative
// (fluid_synth_program_change) mallocs a preset and frees the old one.
void lockstep_tone_set_channel_preset(fluid_synth_t* synth, int chan,
                                      fluid_preset_t* preset);

fluid_preset_t* lockstep_tone_get_channel_preset(fluid_synth_t* synth, int chan);

// Active voice count -- diagnostic only. Reaches into the synth struct, which is
// why it lives here with the rest of the private-header coupling rather than
// being a second place that includes fluid_synth.h.
int lockstep_tone_active_voice_count(fluid_synth_t* synth);

// Rule 4: call before delete_fluid_synth so delete_fluid_channel cannot free a
// cache entry. Leaves every channel holding NULL.
void lockstep_tone_detach_all_presets(fluid_synth_t* synth);

#ifdef __cplusplus
}
#endif
