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

#include <juce_gui_basics/juce_gui_basics.h>

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
        constexpr int kSecTrig = 0;  // TRIG
        constexpr int kSecSrc = 1;   // SRC
        constexpr int kSecAmp = 3;   // AMP
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

    // Find the first descendant of `root` of type T satisfying `pred`. Used to
    // reach the pool overlay's own list and toolbar buttons by what they are,
    // rather than by private member access, so the click goes through the
    // component tree exactly as the mouse does.
        template <typename T, typename Pred>
        T* findDescendant(juce::Component& root, Pred&& pred)
        {
            for (auto* c : root.getChildren())
            {
                if (auto* t = dynamic_cast<T*>(c); t != nullptr && pred(*t))
                    return t;
                if (auto* deeper = findDescendant<T>(*c, pred))
                    return deeper;
            }
            return nullptr;
        }

    // Click a point inside a component nested in one of the editor's top-level
    // children, through the real mouse path.
    //
    // A top-level child's own bounds are design space -- the rail button's go
    // straight to clickDesign -- but mapping a NESTED component through
    // editor.getLocalPoint() also applies that top-level child's scale
    // transform, so the result comes back already in physical pixels, and
    // clickDesign then scales it a second time. The first version of this did
    // exactly that and clicked off the edge of the pool overlay.
    //
    // So map into the top-level child (whose coordinates are design units) and
    // add its design-space position.
        void clickIn(UiDriver& d, juce::Component& topLevel, juce::Component& c,
                     juce::Point<int> local)
        {
            const auto inTop = topLevel.getLocalPoint(&c, local);
            d.clickDesign((topLevel.getPosition() + inTop).toFloat());
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
            return activeModal(ui) == Modal::None && ui.overlay == Overlay::None && activeFuncReskin(ui) == FuncReskin::None && !ui.generatorHubHeld && !ui.euclidHeld && !ui.melodicHeld && !ui.harmonyHeld;
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
                case Modal::TrackFxPicker:  out = SurfaceLayer::TrackFxPicker; return true;
                case Modal::MachinePicker:  out = SurfaceLayer::MachinePicker; return true;
                case Modal::GeneratorHub:   out = SurfaceLayer::GeneratorHub; return true;
                case Modal::Identity:       out = SurfaceLayer::Identity; return true;
                case Modal::Browser:        out = SurfaceLayer::Browser; return true;
                case Modal::NoteEdit:       out = SurfaceLayer::NoteEdit; return true;
                case Modal::PLockClear:
                    out = SurfaceLayer::PLockClear;
                    return true;

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
            SurfaceLayer owned{};
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

            // --- Wave two: gestures derived from the binding table and the
            //     editor's own entry sites, rather than guessed ------------------
                { Modal::Time, "Time",
                  [](UiDriver& d) { d.press(CB::SongScope); d.tap(CB::Section, kSecTrig);
                                d.release(CB::SongScope); },
                  nullptr },

            // The step inspector arms on a long hold of a single step and fires
            // from the editor's timer mid-hold, so the step stays down.
                { Modal::PLockClear, "PLockClear",
                  [](UiDriver& d) { holdLong(d, CB::Step, 4); },
                  nullptr },

            // Entered from the inspector: SRC tapped while the step is still held.
                { Modal::NoteEdit, "NoteEdit",
                  [](UiDriver& d) { holdLong(d, CB::Step, 4); d.tap(CB::Section, kSecSrc); },
                  nullptr },

            // Cue scope + AMP arms a long press; the console opens on the key-up.
                { Modal::Cue, "Cue",
                  [](UiDriver& d) { d.press(CB::CueScope); d.longPress(CB::Section, kSecAmp); },
                  nullptr },

            // Song + hold(MOD) names the song. Scene + hold(MOD) is the same
            // modal on a different operand and is covered by the Song variant.
                { Modal::Identity, "Identity",
                  [](UiDriver& d) { d.press(CB::SongScope); holdLong(d, CB::Section, kSecMod); },
                  nullptr },

            // Opened from the pool overlay: load a sample, open the pool from the
            // project rail, select the sample's row, press Props... -- the whole
            // path a person takes, through the component tree, not a callback.
            //
            // The browser groups pool rows under non-selectable headers, so which
            // display row the sample lands on is a layout fact this test should
            // not hard-code. It tries rows top-down the way someone scanning the
            // list would; Props on a header is a no-op that leaves the overlay
            // open, so a miss simply moves on to the next row.
                { Modal::SampleProps, "SampleProps",
                  [](UiDriver& d) {
                      const auto wav = juce::File(LOCKSTEP_TEST_DIR)
                                           .getChildFile("assets/drum_loop_110bpm.wav");
                      d.proc().samplePool().load(wav.getFullPathName());

                      auto& ed = d.editor();
                      d.clickDesign(DispatchProbe::railPoolBtn(ed).getCentre().toFloat());

                      auto& ov = DispatchProbe::poolOverlay(ed);
                      auto* list = findDescendant<juce::ListBox>(ov, [](auto&) { return true; });
                      auto* props = findDescendant<juce::TextButton>(
                          ov, [](juce::TextButton& b) { return b.getButtonText() == "Props..."; });
                      if (list == nullptr || props == nullptr)
                          return;   // entry assertion reports the miss

                      const int rowH = list->getRowHeight();
                      for (int row = 0; row < 4 && activeModal(d.ui()) != Modal::SampleProps; ++row)
                      {
                          clickIn(d, ov, *list, { 12, row * rowH + rowH / 2 });
                          clickIn(d, ov, *props, props->getLocalBounds().getCentre());
                      }
                  },
                  nullptr },
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
                                 "sweep table (want exactly 1)\n",
                                 m, seen);
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
