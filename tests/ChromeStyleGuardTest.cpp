// Chrome-style guard (9.33).
//
// The three button groups at the top of the window had grown three different looks
// -- three fills, three fonts, three heights -- for one reason: each was styled where
// it happened to be CONSTRUCTED. The file bar hard-coded a fill; the rail buttons and
// combo boxes inherited raw JUCE defaults; the transport fetched its resting colours
// from the DEFAULT LookAndFeel, which is not the one the editor installs, so it could
// not inherit anything. Nobody decided the surface should look like that; it is what
// you get when a look has no owner.
//
// ChromeLookAndFeel is now that owner. A look enforced by remembering is not enforced
// (PRINCIPLES §20), so this test enforces it: a per-component TextButton / ComboBox
// colour override must be a deliberate SEMANTIC claim -- the colour is saying
// something (armed, recording, destructive, transparent-so-the-VU-shows-through) --
// and must say so with a `// semantic colour: <reason>` marker. Anything else is
// decoration, and decoration belongs to the look, not to the call site.
//
// If you are here because this test failed: you almost certainly want to delete the
// setColour() and let the button inherit. Reach for the marker only when the colour
// carries meaning that the base cannot know about.

#include "TestHarness.h"

#include <juce_core/juce_core.h>

namespace lockstep
{
namespace
{
    // Every file that builds plain chrome. ChromeLookAndFeel.h is the owner and is
    // exempt by definition -- it is where the look is allowed to be an opinion.
    const char* const kChromeFiles[] = {
        "PluginEditor.cpp",
        "ui/InPluginTransport.cpp",
        "ui/StandaloneFileBar.cpp",
        "ui/SoundBankOverlay.cpp",
        "ui/SamplePoolOverlay.cpp",
    };

    // The colour ids that decide how a plain control LOOKS. (Slider/label colours are
    // out of scope: the MZ's rotaries carry their own painted language.)
    const char* const kStyleIds[] = {
        "juce::TextButton::buttonColourId",
        "juce::TextButton::buttonOnColourId",
        "juce::TextButton::textColourOffId",
        "juce::TextButton::textColourOnId",
        "juce::ComboBox::backgroundColourId",
        "juce::ComboBox::outlineColourId",
        "juce::ComboBox::textColourId",
    };

    bool mentionsStyleId(const juce::String& line)
    {
        for (const auto* id : kStyleIds)
            if (line.contains(id))
                return true;
        return false;
    }

    // A colour override is almost never one line: it is a RUN of setColour calls (fill,
    // on-fill, text) that share one reason. So the marker covers the whole run -- walk
    // back over the run's own lines (and their continuations), then look at the two
    // lines above it for the claim. Requiring the marker on each individual call would
    // just teach people to paste it, which is the opposite of making them think.
    bool isRunLine(const juce::String& line)
    {
        const auto t = line.trim();
        if (t.isEmpty()) return true;
        if (t.contains("setColour") || mentionsStyleId(line)) return true;
        // A continuation of a wrapped call: an argument line ending in ',' or ');'.
        return t.endsWith(",") || t.endsWith(");");
    }

    bool hasMarker(const juce::StringArray& lines, int idx)
    {
        int i = idx;
        while (i > 0 && isRunLine(lines[i]))
            --i;
        for (int j = juce::jmax(0, i - 2); j <= juce::jmin(lines.size() - 1, i + 1); ++j)
            if (lines[j].contains("semantic colour:"))
                return true;
        return false;
    }
}   // namespace

void runChromeStyleGuardTests()
{
    const juce::File srcDir { juce::String(LOCKSTEP_SRC_DIR) };
    CHECK(srcDir.isDirectory(), "LOCKSTEP_SRC_DIR points at the source tree");

    juce::StringArray offenders;
    int scanned = 0;

    for (const auto* rel : kChromeFiles)
    {
        const juce::File f = srcDir.getChildFile(rel);
        if (!f.existsAsFile())
            continue;   // standalone-only files may be absent in some configs

        juce::StringArray lines;
        lines.addLines(f.loadFileAsString());
        ++scanned;

        for (int i = 0; i < lines.size(); ++i)
        {
            const juce::String t = lines[i].trim();
            if (t.startsWith("//") || t.startsWith("*"))
                continue;
            if (!t.contains("setColour") || !mentionsStyleId(lines[i]))
                continue;
            if (hasMarker(lines, i))
                continue;

            offenders.add(juce::String(rel) + ":" + juce::String(i + 1) + "  " + t);
        }
    }

    CHECK(scanned > 0, "chrome guard: scanned at least one chrome file");
    CHECK(offenders.isEmpty(),
          "chrome style: a control's look belongs to ChromeLookAndFeel, not to the file "
          "it is constructed in. Delete the setColour and inherit, or mark it "
          "`// semantic colour: <reason>` if the colour genuinely means something:\n  "
          + offenders.joinIntoString("\n  "));
}
}   // namespace lockstep
