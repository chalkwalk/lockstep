// LayerRemapReachabilityTest — a layer remap must not swallow a compound.
//
// `kLayerRemaps` rewrites a button's identity from three bools (funcHeld, trackHeld,
// muteHeld). The rewrite is LOSSY: the new button records that Track or Mute was down
// and nothing about which OTHER modifier was. Every modifier outside that trio becomes
// invisible to whatever the remapped button dispatches to — and because
// `resolveBinding` matches a SUBSET of held mods, the bare row wins by default rather
// than failing loudly. A documented gesture disappears and a different one fires in
// its place, silently.
//
// This has now bitten four times:
//   Func+VerbClear → VerbDelete   killed Trig+Func+Clear      (retired, 9.29)
//   Step → ToggleMute             killed Mute+Song+step       (9.38 — muted a track)
//   Step → ToggleMute             killed Mute+Play+step       (9.38 — did nothing)
//   Step → SelectTrack            killed the sticky Track delete picker (9.38)
//
// So the rule is in DESIGN §37.1, and this is its keeper. It cannot prove a gesture
// works end to end — the journeys in `lockstep_dispatch_tests` do that. What it CAN do
// is refuse to let a compound be silently absent from BOTH delivery mechanisms:
//
//   Row         — a binding row requires the extra modifier, so the table delivers it.
//   Imperative  — dispatch intercepts the compound before the table is consulted,
//                 which is the only option when the qualifier is not one of the eight
//                 modifier bits (Play-held) or when the branch predates the table.
//
// The table below is the artifact. Adding a remap, or a compound on a remapped key,
// forces an entry and therefore a decision — which is the whole point, because the
// failure mode is nobody deciding.
//
// Sharpness, measured by breaking it rather than assumed:
//   - Delete the Scene+Mute row  → this test fails AND GestureTest:369 fails. The Row
//     half genuinely overlaps the gesture scenarios; it is not the reason to keep it.
//   - Add a new remap            → this test fails with "no compound listed", and
//     LayerResolveTest fails too — but only to say the OUTPUT changed. Nothing else
//     asks whether anyone decided what the other seven modifiers now mean on that key.
//     That question is this file's only unique job, and it is the one that was never
//     asked the four times this bit.

#include "TestHarness.h"

#include "../src/command/ButtonLayers.h"
#include "../src/command/KeyBindings.h"

namespace lockstep
{
namespace
{
    using B = ControllerButton;

    enum class Delivery
    {
        Row,          // a binding row requires the extra modifier
        Imperative,   // dispatch intercepts ahead of the table (site named below)
        EffectReads,  // one scope-agnostic row fires, and its EFFECT reads the modifier
    };

    struct Compound
    {
        const char* name;
        B effective;          // the button AFTER the remap
        uint16_t layerMod;    // the modifier that caused the remap
        uint16_t extraMod;    // the modifier the remap made invisible (0 = not a bit)
        SurfaceLayer layer;   // the layer the gesture lives on (mute rows are not Base)
        Delivery delivery;
        const char* site;     // where it is delivered — a row's action, or a function
    };

    // Every documented compound that survives a layer remap today.
    constexpr Compound kCompounds[] = {
        // --- Step → ToggleMute (Mute layer) -----------------------------------
        { "Scene+Mute+step = per-scene mute", B::ToggleMute, kModMute, kModScene,
          SurfaceLayer::MuteView, Delivery::Row, "AId::SceneMuteToggle" },
        { "Func+Mute+step = solo", B::ToggleMute, kModMute, kModFunc,
          SurfaceLayer::MuteView, Delivery::Row, "AId::SoloToggle" },
        { "Morph+Mute+step = fluid mute", B::ToggleMute, kModMute, kModMorph,
          SurfaceLayer::MorphMuteView, Delivery::Row, "AId::FluidMuteToggle" },
        { "Mute+Song+step = blank-create a song", B::ToggleMute, kModMute, kModSong,
          SurfaceLayer::MuteView, Delivery::Imperative,
          "PluginEditor::dispatchDown ToggleMute -> handleSongSlotPress" },
        // Play-held is not one of the eight modifier bits, so no row can name it.
        // That is exactly when Imperative is the only honest answer.
        { "Mute+Play+step = relaunch/retrigger", B::ToggleMute, kModMute, 0,
          SurfaceLayer::MuteView, Delivery::Imperative,
          "PluginEditor::dispatchDown ToggleMute -> queueRelaunch" },

        // --- Step → SelectTrack (Track layer) ---------------------------------
        { "Track+Phrase+step = per-track deviation", B::SelectTrack, kModTrack, kModPhrase,
          SurfaceLayer::Base, Delivery::Imperative,
          "PluginEditor::dispatchDown SelectTrack (phraseScopeHeld)" },
        // The case with no modifier at all: the arming chord is already RELEASED, so
        // the slot tap arrives as a bare Step. Listed because it is the same disease —
        // code matching on the identity a remap produced.
        { "delete picker slot tap, chord released", B::SelectTrack, kModTrack, 0,
          SurfaceLayer::DeletePicker, Delivery::Imperative,
          "CommandCore::handleDown accepts Step OR SelectTrack" },

        // --- Section → MetaSection (Func layer) -------------------------------
        // There are no CB::MetaSection rows at all: the whole family routes through
        // routeSection -> SectionResolve, which reads the held scopes itself. So every
        // Func+scope+section compound is Imperative by construction, not by omission.
        { "Func+scope+section = the meta/secondary pages", B::MetaSection, kModFunc, kModSong,
          SurfaceLayer::Base, Delivery::Imperative,
          "PluginEditor::routeSection -> SectionResolve (SecAction::MetaSection)" },

        // --- VerbSnapshot → Restore (Func layer) ------------------------------
        // The third shape, and the tidiest: ONE scope-agnostic row fires, and the
        // effect asks which scope is held (9.37 item A). The remap costs nothing here
        // because nothing downstream matches on the modifier -- it is read, not
        // matched. Worth having in the table as the example of how to survive a remap
        // without an interception.
        { "Track+Func+Y = restore THAT scope's stack", B::Restore, kModFunc, kModTrack,
          SurfaceLayer::Base, Delivery::EffectReads,
          "LockstepEditor::ckScope -> firstHeldSectionSuiteScope" },
    };

    // A compound delivered by a ROW must actually have one: resolving with the extra
    // modifier held has to beat the bare row. If it does not, the modifier is invisible
    // and the gesture is dead — the exact shape of all four historical failures.
    void testRowDeliveredCompoundsResolveDistinctly()
    {
        for (const auto& c : kCompounds)
        {
            if (c.delivery != Delivery::Row) continue;

            const auto& with = resolveBinding(c.effective, 0,
                                              static_cast<uint16_t>(c.layerMod | c.extraMod),
                                              c.layer);

            CHECK(with.action != ActionId::None,
                  juce::String(c.name) + ": resolves to something (" + c.site + ")");
            // The load-bearing one. A winning row that REQUIRES the extra modifier
            // cannot be the bare row, which is the failure every historical case had:
            // the bare row matched on a subset and quietly won.
            CHECK((with.requiredMods & c.extraMod) != 0,
                  juce::String(c.name) + ": the winning row actually REQUIRES that modifier");
        }
    }

    // A compound delivered IMPERATIVELY must be intercepted before the table, because
    // the table cannot help it: with the extra modifier held the bare row still wins on
    // popcount. Asserting that here is not a tautology — it is the standing reason the
    // interception may never be removed, recorded where someone deleting it will look.
    void testImperativeCompoundsAreShadowedByTheTable()
    {
        for (const auto& c : kCompounds)
        {
            if ((c.delivery != Delivery::Imperative && c.delivery != Delivery::EffectReads)
                || c.extraMod == 0)
                continue;

            const auto& with = resolveBinding(c.effective, 0,
                                              static_cast<uint16_t>(c.layerMod | c.extraMod),
                                              c.layer);
            CHECK((with.requiredMods & c.extraMod) == 0,
                  juce::String(c.name)
                      + ": no row claims this modifier, so dispatch MUST intercept it — "
                      + c.site);
        }
    }

    // Every remap in the table is accounted for here. A new remap with no compound
    // listed is not necessarily a bug, but it IS an undecided question: nobody has
    // asked what the other seven modifiers mean on that key. Fail until someone does.
    void testEveryRemapIsAccountedFor()
    {
        for (const auto& r : kLayerRemaps)
        {
            bool found = false;
            for (const auto& c : kCompounds)
                if (c.effective == r.effective) { found = true; break; }

            CHECK(found,
                  juce::String("kLayerRemaps entry → button ")
                      + juce::String(static_cast<int>(r.effective))
                      + " has no compound listed in LayerRemapReachabilityTest: decide what"
                        " the other modifiers mean on that key, then record it (DESIGN 37.1)");
        }
    }
}   // namespace

void runLayerRemapReachabilityTests()
{
    testRowDeliveredCompoundsResolveDistinctly();
    testImperativeCompoundsAreShadowedByTheTable();
    testEveryRemapIsAccountedFor();
}
}   // namespace lockstep
