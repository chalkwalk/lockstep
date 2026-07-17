// Clock-funnel guard (§20 single owner; UI-harness Tier 1).
//
// The editor reads the wall clock for every gesture decision (double-tap
// spacing, long-press elapsed, tap-tempo, toast aging). When those reads are
// scattered inline, "was this a double-tap?" is a question only real elapsed
// time can answer -- so a test that wants to exercise a gesture has to
// Thread::sleep through it. That made the interaction suites slow AND flaky:
// they asserted against a clock nobody controlled.
//
// So the editor has exactly ONE clock: LockstepEditor::nowMs(). Production
// behaviour is unchanged (it returns the wall clock); a test drives time by
// setting testNowMs_ through DispatchProbe and advancing it deliberately. The
// value of that seam is entirely in it being total -- a single inline
// juce::Time read left behind is a gesture path that silently ignores virtual
// time, and the test that trips over it will look like a logic bug somewhere
// else entirely.
//
// A principle enforced by remembering is not enforced (PRINCIPLES §20), so this
// test enforces it: the editor's sources may contain exactly one read of JUCE's
// millisecond counter -- the one inside nowMs(), marked `// clock funnel owner`.
//
// If you are here because this test failed: call nowMs() instead. It is private
// and lives in PluginEditor.h. Cosmetic paint clocks in KeyboardArea /
// ManipulationZone / TimelineStrip are deliberately NOT covered -- they drive
// animation phase, never a gesture decision, and no test asserts on them.

#include "TestHarness.h"

#include <juce_core/juce_core.h>

namespace lockstep
{
namespace
{
    // The editor owns the gesture clock; both halves of it are scanned.
    const char* const kEditorFiles[] = {
        "PluginEditor.cpp",
        "PluginEditor.h",
    };

    const char* const kMarker = "// clock funnel owner";
}   // namespace

void runClockFunnelGuardTests()
{
    const juce::File srcDir { juce::String(LOCKSTEP_SRC_DIR) };
    CHECK(srcDir.isDirectory(), "LOCKSTEP_SRC_DIR points at the source tree");

    juce::StringArray offenders;
    int funnelOwners = 0;

    for (const auto* rel : kEditorFiles)
    {
        const juce::File f = srcDir.getChildFile(rel);
        CHECK(f.existsAsFile(), juce::String("editor file exists: ") + rel);

        juce::StringArray lines;
        lines.addLines(f.loadFileAsString());

        for (int i = 0; i < lines.size(); ++i)
        {
            const juce::String& line = lines[i];

            // Only real calls: prose about the clock is not a clock read.
            const juce::String trimmed = line.trim();
            if (trimmed.startsWith("//") || trimmed.startsWith("*"))
                continue;
            if (!line.contains("getMillisecondCounter"))
                continue;

            if (line.contains(kMarker))
            {
                ++funnelOwners;
                continue;
            }

            offenders.add(juce::String(rel) + ":" + juce::String(i + 1) + "  " + trimmed);
        }
    }

    // Self-check: if the scan stops matching (file renamed, marker reworded), it
    // would "pass" forever while the funnel quietly rots.
    CHECK(funnelOwners == 1,
          juce::String("expected exactly one clock funnel owner marked `") + kMarker
              + "`, found " + juce::String(funnelOwners));

    // ASCII only: juce::String(const char*) asserts on a non-ASCII byte.
    CHECK(offenders.isEmpty(),
          juce::String("inline clock read in the editor: call nowMs() instead, so tests can "
                       "drive gesture timing without sleeping:\n    ")
              + offenders.joinIntoString("\n    "));
}
}   // namespace lockstep
