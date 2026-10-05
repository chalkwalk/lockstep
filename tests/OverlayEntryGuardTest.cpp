// OverlayEntryGuardTest -- the sticky-overlay field has ONE way in (6.9 M1).
//
// UiState::overlay is the single field holding the active sticky overlay, and
// making illegal co-existence unrepresentable was the whole point of collapsing
// three booleans into it. But that only holds if every transition goes through
// the functions that know what a transition costs.
//
// Exit always did: escapeOverlay() is the one way out, and it does not merely
// clear the field -- it resets the overlay's PARAMETERS, which live beside it
// (densityBank, densitySubPage, velBank, velSubPage, sigPage, cueParamPage,
// samplePropsPoolIndex, and the identity and browser blocks).
//
// Entry did not. Six sites in PluginEditor.cpp assigned the field directly, and
// a direct assignment over a live overlay skips the outgoing one's reset, so the
// next time that overlay opened it inherited state from its last visit. The
// field stayed legal; its parameters did not.
//
// enterOverlay() closes that: it escapes whatever is active, then sets the
// field. This test is what stops the seventh raw assignment appearing, because
// a rule enforced by remembering is not enforced (PRINCIPLES §20) -- and this
// is the second time this codebase has needed exactly this shape of guard, the
// first being the surface-invalidation channel next door.
//
// If this test failed for you: call enterOverlay(ui, Overlay::X) instead of
// assigning the field. If you genuinely need the raw write -- you are writing
// the reducer itself, or a test deliberately constructing a state -- the
// sanctioned files are listed below.

#include "TestHarness.h"

#include <juce_core/juce_core.h>

namespace lockstep
{
    namespace
    {
    // The only places allowed to assign UiState::overlay directly.
    //
    //   ModeReducer.cpp -- owns both directions (enterOverlay, escapeOverlay).
    //   UiState.h       -- the member's own declaration and initialiser.
        const char* const kSanctioned[] = {
            "ui/mode/ModeReducer.cpp",
            "state/UiState.h",
        };

        bool isSanctioned(const juce::String& relPath)
        {
            for (const auto* s : kSanctioned)
                if (relPath.endsWith(s))
                    return true;
            return false;
        }
    }   // namespace

    void runOverlayEntryGuardTests()
    {
        const juce::File srcDir{ juce::String(LOCKSTEP_SRC_DIR) };
        CHECK(srcDir.isDirectory(), "LOCKSTEP_SRC_DIR points at the source tree");

        juce::Array<juce::File> sources;
        srcDir.findChildFiles(sources, juce::File::findFiles, true, "*.cpp");
        srcDir.findChildFiles(sources, juce::File::findFiles, true, "*.h");
        CHECK(sources.size() > 50, "the scan found a plausible number of sources");

        juce::StringArray offenders;
        int sanctionedHits = 0;

        for (const auto& f : sources)
        {
            const juce::String rel = f.getRelativePathFrom(srcDir).replaceCharacter('\\', '/');

            juce::StringArray lines;
            lines.addLines(f.loadFileAsString());

            for (int i = 0; i < lines.size(); ++i)
            {
                const juce::String trimmed = lines[i].trim();

            // Prose about the rule is not a violation of it. This file's own
            // explanation, and the comments in ModeReducer, say the words.
                if (trimmed.startsWith("//") || trimmed.startsWith("*") || trimmed.startsWith("/*"))
                    continue;

            // Any ASSIGNMENT to the field, however the right-hand side is
            // spelled. The first version of this matched the literal
            // `.overlay = Overlay::` and therefore missed
            //
            //     ui.overlay = entering ? Overlay::Time : Overlay::None;
            //
            // in MetaBand.cpp -- a hand-rolled toggle doing exactly what this
            // guard exists to stop, sitting in the tree while the guard
            // reported clean. A guard that only catches the shape you thought
            // of is worth less than it appears.
            //
            // Comparisons are excluded explicitly; `!=` cannot match because
            // of the `!` between the name and the `=`.
                if (!trimmed.contains("overlay =") || trimmed.contains("overlay =="))
                    continue;

            // ...but only UiState's field. `overlay` is not a unique name --
            // InspectorModel has one of its own, and `m.overlay = buildOverlayRegion(...)`
            // is nothing to do with modal state. Narrowing by the right-hand
            // side (an Overlay:: value, which covers the ternary form too) or
            // by a receiver that is recognisably a UiState keeps the guard on
            // its own subject.
            //
            // Residual gap, stated rather than papered over: an assignment from
            // a variable of type Overlay through an unfamiliar receiver --
            // `cfg.overlay = saved;` -- would not be caught. Everything in the
            // tree today names either the type or a known receiver.
                const bool namesOverlayValue = trimmed.contains("Overlay::");
                const bool knownReceiver = trimmed.contains("ui.overlay =") || trimmed.contains("uiState_.overlay =") || trimmed.contains("state.overlay =") || trimmed.contains("u.overlay =");
                if (!namesOverlayValue && !knownReceiver)
                    continue;

                if (isSanctioned(rel))
                {
                    ++sanctionedHits;
                    continue;
                }

                offenders.add(rel + ":" + juce::String(i + 1) + "  " + trimmed);
            }
        }

    // The sanctioned files must actually contain writes. Without this the test
    // passes just as well when enterOverlay/escapeOverlay have been renamed,
    // moved or deleted -- a guard that cannot tell "nobody broke the rule" from
    // "the thing being guarded is gone" is not guarding anything.
        CHECK(sanctionedHits > 0,
              "the reducer still assigns UiState::overlay (the guard has something to guard)");

        if (!offenders.isEmpty())
        {
            for (const auto& o : offenders)
                std::fprintf(stderr, "  raw overlay write: %s\n", o.toRawUTF8());
        }

        CHECK(offenders.isEmpty(),
              "UiState::overlay is assigned only through enterOverlay/escapeOverlay");
    }
}   // namespace lockstep
