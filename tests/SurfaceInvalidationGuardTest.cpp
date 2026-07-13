// Surface-invalidation guard (PRINCIPLES §22, 9.15 Stage 5).
//
// §22 says the surface has exactly ONE invalidation channel: a change that is
// visible on a cell marks the surface dirty via refreshSurface(), which
// coalesces into a single frame that renders *every* sink (screen + every
// connected controller). A bare repaint() redraws the plugin window only — so a
// surface-changing edit that repaints instead of invalidating leaves a
// connected controller's LEDs stale until something unrelated happens to
// invalidate. That is not a bug you can find by looking at the screen, which is
// exactly why it kept coming back (the historical "fix" was a per-mode repaint
// timer, papering over the missing signal).
//
// A principle enforced by remembering is not enforced (PRINCIPLES §20), so this
// test enforces it: every repaint() in the surface-owning files must either
//   (a) live in one of the sanctioned frame producers, or
//   (b) be marked `// chrome only: <reason>` — an explicit claim that the thing
//       being redrawn has no cell state (a banner, the status line, a window
//       overlay), reviewable as a one-line assertion.
// Anything else fails the build with the offending file:line.
//
// If you are here because this test failed: you almost certainly want
// refreshSurface() (or, inside KeyboardArea, markSurfaceDirty()). Reach for
// `// chrome only:` only when the pixels you are redrawing genuinely cannot
// appear on a controller.

#include "TestHarness.h"

#include <juce_core/juce_core.h>

namespace lockstep
{
namespace
{
    // Files that own the performance surface. Overlays with their own private
    // visuals (SamplePoolOverlay, SoundBankOverlay, InspectorBar, TimelineStrip)
    // are not surface owners: they paint windows, not cells.
    const char* const kSurfaceFiles[] = {
        "PluginEditor.cpp",
        "ui/KeyboardArea.cpp",
        "ui/KeyboardArea.h",
    };

    // The only functions allowed to paint directly: the single frame producer and
    // the vblank playhead (DESIGN §35.9.1/§35.9.3), plus KeyboardArea's
    // markSurfaceDirty() fallback (used only when no editor is wired, so no
    // controller can be open to go stale).
    const char* const kFrameProducers[] = {
        "renderSurfaceFrame",
        "onPlayheadVBlank",
        "markSurfaceDirty",
    };

    bool isFrameProducer(const juce::String& fn)
    {
        for (const auto* p : kFrameProducers)
            if (fn == p)
                return true;
        return false;
    }

    // Track the enclosing function by scanning back to its definition line. Handles
    // both out-of-line definitions ("void LockstepEditor::renderSurfaceFrame()") and
    // inline header methods ("void markSurfaceDirty()"). Fails safe: an unrecognised
    // enclosing function is *not* a frame producer, so its repaint must carry the
    // marker rather than being waved through.
    // `if (x)` / `while (x)` also end in ')' with no ';', so they masquerade as
    // definition lines; a back-scan that stops on one reports "if" as the enclosing
    // function and waves the repaint through (or flags it wrongly).
    bool isControlKeyword(const juce::String& s)
    {
        return s == "if" || s == "else" || s == "for" || s == "while" || s == "switch"
               || s == "catch" || s == "return" || s == "do";
    }

    juce::String enclosingFunction(const juce::StringArray& lines, int idx)
    {
        for (int i = idx; i >= 0; --i)
        {
            const juce::String l = lines[i].trimEnd();
            const juce::String t = l.trim();
            if (t.startsWith("//") || t.startsWith("*") || t.startsWith("/*"))
                continue;
            if (!l.endsWithChar(')') || !l.containsChar('(') || l.containsChar(';'))
                continue;   // not a definition line (call, declaration, macro)

            // Take the identifier immediately before the argument list.
            const int paren = l.indexOfChar('(');
            juce::String head = l.substring(0, paren).trim();
            const int colons = head.lastIndexOf("::");
            if (colons >= 0)
                head = head.substring(colons + 2);
            else if (head.containsChar(' '))
                head = head.fromLastOccurrenceOf(" ", false, false);   // drop return type

            head = head.trim();
            if (head.isEmpty() || isControlKeyword(head))
                continue;
            return head;
        }
        return {};
    }
}   // namespace

void runSurfaceInvalidationGuardTests()
{
    const juce::File srcDir { juce::String(LOCKSTEP_SRC_DIR) };
    CHECK(srcDir.isDirectory(), "LOCKSTEP_SRC_DIR points at the source tree");

    juce::StringArray offenders;
    int checked = 0;

    for (const auto* rel : kSurfaceFiles)
    {
        const juce::File f = srcDir.getChildFile(rel);
        CHECK(f.existsAsFile(), juce::String("surface file exists: ") + rel);

        juce::StringArray lines;
        lines.addLines(f.loadFileAsString());

        for (int i = 0; i < lines.size(); ++i)
        {
            const juce::String& line = lines[i];

            // Only real calls: skip comments and prose that mentions repaint().
            const juce::String trimmed = line.trim();
            if (trimmed.startsWith("//") || trimmed.startsWith("*"))
                continue;
            if (!line.contains("repaint();"))
                continue;

            ++checked;

            if (line.contains("// chrome only"))
                continue;
            if (isFrameProducer(enclosingFunction(lines, i)))
                continue;

            offenders.add(juce::String(rel) + ":" + juce::String(i + 1) + "  " + trimmed);
        }
    }

    // Self-check: if the scan silently stops matching, it would "pass" forever.
    CHECK(checked >= 5, "the scan actually found repaint() calls to classify");

    // ASCII only: juce::String(const char*) asserts on a non-ASCII byte, so no
    // section sign or em-dash in this message (the house gotcha, and this test
    // tripped it once already).
    CHECK(offenders.isEmpty(),
          juce::String("unrouted repaint(): use refreshSurface() (PRINCIPLES 22), or mark it "
                       "`// chrome only: <reason>` when it has no cell state:\n    ")
              + offenders.joinIntoString("\n    "));
}
}   // namespace lockstep
