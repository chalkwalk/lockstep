/* tone_preset_swap — the ONE place Lockstep touches FluidLite's internals.
 *
 * Everything else in tonecore/ speaks only FluidLite's public API. This file
 * exists so that the private-header coupling is a single small C translation
 * unit with a written contract, rather than a set of internal includes spread
 * through C++ headers where nobody would notice them drifting.
 *
 * ── Why it has to exist ──────────────────────────────────────────────────
 *
 * `fluid_synth_program_change` is not real-time safe. Under it,
 * `fluid_defsfont_sfont_get_preset` calls FLUID_NEW on EVERY lookup, and
 * `fluid_channel_set_preset` frees the previous preset — so one program change
 * is a malloc plus a free. In Lockstep that lands on the audio thread twice
 * over: a P-Locked program change, and (far more commonly) turning the program
 * knob while the pattern rolls, since writeParam -> EngineCmd drains inside
 * processBlock. Browsing instruments live is an ordinary gesture, not a rare
 * one, so it cannot simply be waved through.
 *
 * ToneEngine therefore resolves every preset ONCE at load, on the message
 * thread, and thereafter swaps `fluid_channel_t::preset` directly. Nothing is
 * allocated and nothing is freed while audio runs.
 *
 * ── The four rules that make it safe (DESIGN §29.3) ───────────────────────
 *
 * A preset is freed in exactly THREE places, all in fluid_chan.c:
 *   fluid_channel_reset (:59), delete_fluid_channel (:162),
 *   fluid_channel_set_preset (:176).
 * Keeping cache entries out of all three is the whole of the contract:
 *
 *   1. Never call fluid_channel_set_preset. Use set_channel_preset below.
 *   2. Never call fluid_synth_system_reset — it is the only caller of
 *      fluid_channel_reset — nor fluid_synth_program_reset.
 *   3. Pass reset_presets = 0 to fluid_synth_sfload.
 *   4. Call detach_all_presets BEFORE delete_fluid_synth, so
 *      delete_fluid_channel cannot free a cache entry.
 *
 * A submodule bump must re-check those three free sites. If FluidLite ever
 * gains a public "set preset without taking ownership", this file goes away.
 */

#include "fluid_synth.h"
#include "fluid_chan.h"

#include "tone_preset_swap.h"

void lockstep_tone_set_channel_preset(fluid_synth_t* synth, int chan,
                                      fluid_preset_t* preset)
{
    if (synth == NULL || chan < 0 || chan >= synth->midi_channels)
        return;
    /* Assign, never fluid_channel_set_preset: that would free the cache entry
     * the previous call installed. The cache owns every preset for the whole
     * life of the engine. */
    synth->channel[chan]->preset = preset;
}

fluid_preset_t* lockstep_tone_get_channel_preset(fluid_synth_t* synth, int chan)
{
    if (synth == NULL || chan < 0 || chan >= synth->midi_channels)
        return NULL;
    return synth->channel[chan]->preset;
}

void lockstep_tone_detach_all_presets(fluid_synth_t* synth)
{
    int i;
    if (synth == NULL)
        return;
    /* Rule 4. After this, every channel is holding NULL, so delete_fluid_synth
     * -> delete_fluid_channel finds nothing to free and the cache stays ours
     * to release. */
    for (i = 0; i < synth->midi_channels; i++)
        if (synth->channel[i] != NULL)
            synth->channel[i]->preset = NULL;
}

int lockstep_tone_active_voice_count(fluid_synth_t* synth)
{
    return synth != NULL ? synth->active_voice_count : 0;
}
