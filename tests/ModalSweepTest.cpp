// ModalSweepTest -- every modal, entered for real, then interrupted (6.9 M2).
//
// WHAT THIS IS FOR. Lockstep has sixteen mutually-exclusive modal states and a
// grammar of eight modifiers and five verbs. ModalStateTest next door checks
// activeModal() against a UiState built by hand: it sets a flag and asks what
// the accessor says. That is worth having and it is not this.
//
// It never asks whether a real gesture can REACH a modal, or whether any
// gesture can LEAVE one. Those are the questions a person hits, and every one
// of the ten defects the CUJ arc found was found end to end by UiDriver rather
// than by a unit test over the same state.
//
// So: for each modal, drive the documented entry gesture, assert it arrived,
// then apply a fixed battery of interruptions and assert that afterwards
//
//   * the surface layer on display agrees with activeModal(), and
//   * a bounded escape sequence gets back to Modal::None with every modal flag
//     clear -- not merely activeModal() == None.
//
// That last distinction is the point. activeModal() returns one value by
// priority, so it can never report two modals; "exactly one is active" is true
// by construction and asserting it proves nothing. What can go wrong is a modal
// left half-shut: the field cleared but its PARAMETERS still set, which is
// precisely the bug M1 found when entry bypassed escapeOverlay(). So the
// at-rest assertion looks at the flags, not at the accessor.
//
// ON COVERAGE. The table below is exhaustive over Modal and a missing value
// fails the test, so a new modal cannot be added without someone deciding how
// it is reached. Rows that are not yet driven say so and say why; an honest
// hole that fails loudly beats a sweep that silently covers twelve of sixteen
// and reports success.

#include "UiDriver.h"

#include "../src/ui/mode/ModalState.h"
#include "../src/ui/mode/FuncReskin.h"
#include "../src/command/SurfaceLayer.h"

#include <cstdio>
#include <functional>
#include <vector>

namespace lockstep
{
namespace
{
    using test::CB;
    using test::UiDriver;

    // Section indices, as the binding table spells them.
    constexpr int kSecSrc = 1;   // SRC
    constexpr int kSecMod = 4;   // MOD
    constexpr int kSecFx = 5;    // FX

    // Hold a key past the long-press threshold and KEEP IT DOWN.
    //
    // UiDriver::longPress releases at the end, which is right for a gesture
    // that fires on the hold and then ends. It is wrong for a held chord like
    // the generator hub, which is only active while the key is physically
    // down. The timer tick is the part that is easy to miss: the editor decides
    // a long press has elapsed on its own timer, so advancing the clock without
    // ticking it leaves the hold invisible to everything but the release.
    void holdLong(UiDriver& d, CB b, int idx = -1)
    {
        d.press(b, idx);
        d.advanceMs(GestureRecognizer::kLongPressMs + 60.0);
        d.editor().timerCallback();
    }

    struct Row
    {
        Modal modal;
        const char* name;
        // Null when the entry gesture is not yet driven; `why` then explains.
        std::function<void(UiDriver&)> enter;
        const char* why;
    };

    // Is every modal flag genuinely clear? Stronger than activeModal() == None,
    // which only reports the highest-priority flag and says nothing about a
    // lower one left set beneath it.
    bool fullyAtRest(const UiState& ui)
    {
        return activeModal(ui) == Modal::None
               && ui.overlay == Overlay::None
               && activeFuncReskin(ui) == FuncReskin::None
               && !ui.generatorHubHeld
               && !ui.euclidHeld && !ui.melodicHeld && !ui.harmonyHeld;
    }


    // The layer a modal owns, if any.
    //
    // Not every modal has one: the MZ-band overlays (Density, Vel, Cue,
    // SampleProps, Time away from its Key page) and the armed generators change
    // the band and deliberately leave the step grid alone, so the grid showing
    // Base under them is correct rather than a lie.
    //
    // SurfaceLayer is declared in PRIORITY order, highest first, so a modal
    // that does own a layer can still legitimately be covered by one that
    // outranks it -- a pending confirm prompt is allowed to sit on top of the
    // machine picker. What must never happen is the surface showing something
    // that ranks BELOW the active modal, because then the modal is active and
    // invisible. That is the comparison below.
    bool ownsLayer(Modal m, SurfaceLayer& out)
    {
        switch (m)
        {
            case Modal::MasterFxPicker: out = SurfaceLayer::MasterFxPicker; return true;
            case Modal::TrackFxPicker:  out = SurfaceLayer::TrackFxPicker;  return true;
            case Modal::MachinePicker:  out = SurfaceLayer::MachinePicker;  return true;
            case Modal::GeneratorHub:   out = SurfaceLayer::GeneratorHub;   return true;
            case Modal::Identity:       out = SurfaceLayer::Identity;       return true;
            case Modal::Browser:        out = SurfaceLayer::Browser;        return true;
            case Modal::NoteEdit:       out = SurfaceLayer::NoteEdit;       return true;
            case Modal::PLockClear:     out = SurfaceLayer::PLockClear;     return true;

            // Band overlays and armed generators: no grid layer of their own.
            case Modal::None:
            case Modal::Euclid:
            case Modal::Melodic:
            case Modal::Harmony:
            case Modal::Time:
            case Modal::Density:
            case Modal::Vel:
            case Modal::SampleProps:
            case Modal::Cue:
                return false;
        }
        return false;
    }

    bool layerRankOk(SurfaceLayer shown, Modal now)
    {
        SurfaceLayer owned {};
        if (!ownsLayer(now, owned))
            return true;
        return static_cast<int>(shown) <= static_cast<int>(owned);
    }

    std::vector<Row> rows()
    {
        return {
            { Modal::None, "None", nullptr, "the resting state; nothing to enter" },

            // --- Func-layer pickers, straight from the binding table ----------
            // NOTE: ModalState.h's own comments say Func+Song+FX, Func+FX and
            // Func+Track for these three. The binding table says otherwise, and
            // the table is the authority (9.12). The gestures moved in 9.29 when
            // Func+Track became the Machine scope and the picker went to
            // Track+hold(SRC); those comments were never updated.
            { Modal::MasterFxPicker, "MasterFxPicker",
              [](UiDriver& d) { d.press(CB::SongScope); holdLong(d, CB::Section, kSecFx); },
              nullptr },
            { Modal::TrackFxPicker, "TrackFxPicker",
              [](UiDriver& d) { d.press(CB::TrackScope); d.longPress(CB::Section, kSecFx); },
              nullptr },
            { Modal::MachinePicker, "MachinePicker",
              [](UiDriver& d) { d.press(CB::TrackScope); d.longPress(CB::Section, kSecSrc); },
              nullptr },

            // --- Held chord ---------------------------------------------------
            { Modal::GeneratorHub, "GeneratorHub",
              [](UiDriver& d) { holdLong(d, CB::TapTempo); },
              nullptr },

            // --- Generators, reached through the hub --------------------------
            { Modal::Euclid, "Euclid",
              [](UiDriver& d) { holdLong(d, CB::TapTempo);
                                d.step(0); d.release(CB::TapTempo); },
              nullptr },
            { Modal::Density, "Density",
              [](UiDriver& d) { holdLong(d, CB::TapTempo);
                                d.step(1); d.release(CB::TapTempo); },
              nullptr },
            { Modal::Vel, "Vel",
              [](UiDriver& d) { holdLong(d, CB::TapTempo);
                                d.step(2); d.release(CB::TapTempo); },
              nullptr },
            { Modal::Melodic, "Melodic",
              [](UiDriver& d) { holdLong(d, CB::TapTempo);
                                d.step(3); d.release(CB::TapTempo); },
              nullptr },
            { Modal::Harmony, "Harmony",
              [](UiDriver& d) { holdLong(d, CB::TapTempo);
                                d.step(4); d.release(CB::TapTempo); },
              nullptr },

            // --- Browser ------------------------------------------------------
            { Modal::Browser, "Browser",
              [](UiDriver& d) { d.press(CB::Func); d.press(CB::SongScope);
                                d.tap(CB::Section, kSecMod);
                                d.release(CB::SongScope); d.release(CB::Func); },
              nullptr },

            // --- Not yet driven -------------------------------------------------
            // Each of these is reachable by a person; none has a gesture derived
            // from a source this test can cite yet, and inventing one would make
            // the sweep assert against its own guess rather than against the
            // product. Wave two.
            { Modal::Time, "Time", nullptr,
              "entry is a scope+TRIG chord whose owning row is not in KeyBindings" },
            { Modal::NoteEdit, "NoteEdit", nullptr,
              "Func+SRC+step; needs a step carrying a trig override to be meaningful" },
            { Modal::PLockClear, "PLockClear", nullptr,
              "Func+step; needs a step carrying P-locks to be meaningful" },
            { Modal::SampleProps, "SampleProps", nullptr,
              "opened from a pool row's Props button; needs a populated sample pool" },
            { Modal::Cue, "Cue", nullptr,
              "Cue scope + long-hold(AMP), gated on cueConsoleArmed_" },
            { Modal::Identity, "Identity", nullptr,
              "hold(MOD) under a Song/Scene/Sound scope; three variants to cover" },
        };
    }

    // The interruption battery. Each is a thing a person can do next.
    struct Interrupt
    {
        const char* name;
        std::function<void(UiDriver&)> apply;
    };

    std::vector<Interrupt> interrupts()
    {
        return {
            { "Func double-tap (universal escape)",
              [](UiDriver& d) { d.doubleTap(CB::Func); } },
            { "Func tap", [](UiDriver& d) { d.tap(CB::Func); } },
            { "Track tap", [](UiDriver& d) { d.tap(CB::TrackScope); } },
            { "Phrase tap", [](UiDriver& d) { d.tap(CB::PhraseScope); } },
            { "Scene tap", [](UiDriver& d) { d.tap(CB::SceneScope); } },
            { "Morph tap", [](UiDriver& d) { d.tap(CB::MorphScope); } },
            { "Song tap", [](UiDriver& d) { d.tap(CB::SongScope); } },
            { "Mute tap", [](UiDriver& d) { d.tap(CB::MuteScope); } },
            { "Fill tap", [](UiDriver& d) { d.tap(CB::FillScope); } },
            { "transport start/stop",
              [](UiDriver& d) { d.tap(CB::VerbPlay); d.runBlocks(2);
                                d.gap(); d.tap(CB::VerbPlay); d.runBlocks(2); } },
        };
    }

    // Everything the surface offers for getting out, applied in order. Bounded
    // on purpose: "you can always escape" means a SHORT fixed sequence works,
    // not that some sequence exists.
    void escapeHatch(UiDriver& d)
    {
        d.gap();
        d.doubleTap(CB::Func);
        d.gap();
        d.release(CB::TapTempo);
        d.release(CB::TrackScope);
        d.release(CB::SongScope);
        d.release(CB::Func);
        d.gap();
        d.doubleTap(CB::Func);
        d.gap();
        d.runBlocks(2);
    }

    void sweep(int& failed)
    {
        auto fail = [&failed](const char* what) {
            std::fprintf(stderr, "FAIL [ModalSweep] %s\n", what);
            ++failed;
        };

        const auto table = rows();

        // Exhaustiveness: every Modal value must appear exactly once, so adding
        // one forces a decision about how it is reached rather than silently
        // widening the gap.
        for (int m = 0; m <= static_cast<int>(Modal::Browser); ++m)
        {
            int seen = 0;
            for (const auto& r : table)
                if (static_cast<int>(r.modal) == m)
                    ++seen;
            if (seen != 1)
            {
                std::fprintf(stderr,
                             "FAIL [ModalSweep] Modal value %d appears %d times in the "
                             "sweep table (want exactly 1)\n", m, seen);
                ++failed;
            }
        }

        int driven = 0, skipped = 0;

        for (const auto& r : table)
        {
            if (r.enter == nullptr)
            {
                if (r.modal != Modal::None)
                {
                    ++skipped;
                    std::fprintf(stderr, "  [ModalSweep] not yet driven: %-16s (%s)\n",
                                 r.name, r.why ? r.why : "no reason given");
                }
                continue;
            }
            ++driven;

            // ---- entry ---------------------------------------------------
            {
                UiDriver d;
                r.enter(d);
                d.runBlocks(1);
                const Modal got = activeModal(d.ui());
                if (got != r.modal)
                {
                    std::fprintf(stderr,
                                 "FAIL [ModalSweep] %s: entry gesture reached %s instead\n",
                                 r.name, modalName(got));
                    ++failed;
                    continue;   // the interrupts below would test the wrong modal
                }
            }

            // ---- interrupt battery --------------------------------------
            for (const auto& iv : interrupts())
            {
                UiDriver d;
                r.enter(d);
                d.runBlocks(1);
                if (activeModal(d.ui()) != r.modal)
                    continue;   // entry already reported above

                iv.apply(d);
                d.runBlocks(1);

                // The banner must not lie about where we are.
                const auto& ui = d.ui();
                const Modal now = activeModal(ui);
                const SurfaceLayer shown = d.surface().activeLayer;
                if (!layerRankOk(shown, now))
                {
                    std::fprintf(stderr,
                                 "FAIL [ModalSweep] %s + \"%s\": activeModal() says %s "
                                 "but the surface shows a lower-ranked layer (%d) -- the "
                                 "modal is active and invisible\n",
                                 r.name, iv.name, modalName(now), static_cast<int>(shown));
                    ++failed;
                }

                // And it must still be possible to get out.
                escapeHatch(d);
                if (!fullyAtRest(d.ui()))
                {
                    std::fprintf(stderr,
                                 "FAIL [ModalSweep] %s + \"%s\": stuck -- escape hatch "
                                 "left %s active\n",
                                 r.name, iv.name,
                                 modalName(activeModal(d.ui())));
                    ++failed;
                }
            }
        }

        std::fprintf(stderr, "[ModalSweep] %d modal(s) driven, %d not yet driven, "
                             "%d interrupt(s) each\n",
                     driven, skipped, static_cast<int>(interrupts().size()));

        if (driven == 0)
            fail("no modal was driven at all -- the sweep is not sweeping");
    }
}   // namespace

void runModalSweepTests(int& failed)
{
    sweep(failed);
}
}   // namespace lockstep
