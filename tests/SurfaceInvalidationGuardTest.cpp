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

#include <cstring>

namespace lockstep
{
    void runLogicalRepaintGuard();

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

    runLogicalRepaintGuard();
}

// ---------------------------------------------------------------------------
// The COORDINATE half of the same discipline (5.3 Item E).
//
// The rule above is about *whether* the right channel is used. This one is about
// *where*: LockstepEditor lays out on a fixed logical design canvas and scales it to
// the window, so every region it caches is in design pixels -- but
// Component::repaint(Rectangle) takes physical ones. At the shipped 1.2x default the
// two do not overlap at all, so the region never redraws. No paint code is wrong, no
// state is wrong, and nothing renders: the VU meters simply froze, and every visual
// test stayed green because paintEntireComponent ignores invalid regions entirely.
//
// It went unnoticed at all seven call sites at once, which is the signature of a rule
// that lives in someone's head. So: in PluginEditor.cpp an unqualified repaint() with
// an argument must be repaintLogical(). A bare repaint() is fine (no coordinates to
// get wrong), and a repaint on a CHILD (`someChild_.repaint(r)`) is fine too -- the
// child's own coordinate space is already the scaled one.
// ---------------------------------------------------------------------------
void runLogicalRepaintGuard()
{
    const juce::File f = juce::File(juce::String(LOCKSTEP_SRC_DIR)).getChildFile("PluginEditor.cpp");
    CHECK(f.existsAsFile(), "PluginEditor.cpp exists");

    juce::StringArray lines;
    lines.addLines(f.loadFileAsString());

    juce::StringArray offenders;
    int routed = 0;

    for (int i = 0; i < lines.size(); ++i)
    {
        const juce::String& line = lines[i];
        const juce::String trimmed = line.trim();
        if (trimmed.startsWith("//") || trimmed.startsWith("*"))
            continue;

        if (line.contains("repaintLogical("))
            ++routed;

        for (int at = line.indexOf("repaint("); at >= 0; at = line.indexOf(at + 1, "repaint("))
        {
            // Qualified call on another component: its coords are its own.
            if (at > 0)
            {
                const auto prev = line[at - 1];
                if (prev == '.' || prev == '>')
                    continue;
            }
            // Bare repaint(): nothing to mis-scale.
            const int argAt = at + static_cast<int>(std::strlen("repaint("));
            if (argAt < line.length() && line[argAt] == ')')
                continue;

            offenders.add(juce::String(i + 1) + "  " + trimmed);
        }
    }

    // Self-check: if repaintLogical is ever renamed, this scan would find nothing to
    // approve of and would then be a test that cannot fail. The routed sites are the
    // meter/blink pair, the capture band, the two confirm pop-over edges and the two
    // master-gain drag paths.
    CHECK(routed >= 5,
          juce::String("the scan still recognises repaintLogical() call sites (found ")
              + juce::String(routed) + ")");

    CHECK(offenders.isEmpty(),
          juce::String("PluginEditor.cpp caches its regions in LOGICAL design-canvas "
                       "coords, but repaint(rect) takes PHYSICAL pixels -- at the 1.2x "
                       "default they do not overlap and the region never redraws (this "
                       "is what froze the VU meters). Use repaintLogical():\n    ")
              + offenders.joinIntoString("\n    "));
}
}   // namespace lockstep
