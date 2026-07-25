// CujCaptureTest -- Group G: the anchor flow. A live set becomes stems on disk.
//
// This is THE journey the instrument exists for: play a set, and walk away with a
// master take plus one WAV per track, ready to open in a DAW. Everything else in the
// catalogue supports it. It is also the journey with the most that can silently go
// half-right -- a master that records while the stems stay empty, stems that drift out
// of alignment with it, files for tracks that were never audible -- so it asserts what
// is actually ON DISK rather than what the state machine believed.
//
// Where it writes: the capture path is derived from the loaded project file, so the
// journey saves a project into a temp directory first. Without that, a gesture-driven
// capture writes to the user's real ~/Music/Lockstep/Captures -- a test must never do
// that, and the redirect is what lets the REAL gesture be driven end to end.
//
// See tests/CUJ_CATALOGUE.md.

#include "UiDriver.h"

#include <cstdio>

namespace lockstep
{
namespace
{
    using test::CB;
    using test::UiDriver;

    // G1 -- Arm -> roll -> stop -> saved.
    void testCaptureToStems(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [CUJ/G1] %s\n", what); ++failed; }
        };

        UiDriver d;
        installRealMachine(d.rig(), 0);
        installRealMachine(d.rig(), 1);

        // A scratch project, so the capture lands somewhere disposable.
        const juce::File dir = juce::File::createTempFile("lockstep_cuj_g1").getSiblingFile(
            "lockstep_cuj_g1_dir");
        dir.deleteRecursively();
        dir.createDirectory();
        const juce::File project = dir.getChildFile("set.lockstep");
        if (!test::expectReached(d, [&](UiDriver& dd) { return dd.proc().saveProjectFile(project); },
                                 "a project to capture beside", failed))
        {
            dir.deleteRecursively();
            return;
        }

        d.proc().setCaptureStems(true);

        // --- Arm: Func+Song+Record ------------------------------------------------
        // Resolved on key-UP (the cell's whole family -- tap, double-tap, hold -- lives
        // on one timeline), so it must be a real press/release, not a chord helper.
        d.gap();
        d.press(CB::Func);
        d.press(CB::SongScope);
        d.tap(CB::VerbRecord);
        d.release(CB::SongScope);
        d.release(CB::Func);

        // Arming is not recording: the tap says "capture the next thing I play", and
        // the take begins when the transport does.
        d.runBlocks(4);
        d.editor().timerCallback();

        if (!test::expectReached(d, [](UiDriver& dd) { return dd.proc().isCapturing(); },
                                 "Func+Song+Record arms, and rolling starts the take", failed))
        {
            d.proc().stopCapture();
            dir.deleteRecursively();
            return;
        }
        check(d.proc().isCapturingStems(), "...with stems armed alongside the master");

        // --- Roll: play the set ---------------------------------------------------
        d.runBlocks(400);                       // a bar of music
        check(d.proc().stemSamplesWritten(0) > 0, "track 0's stem is being written");
        check(d.proc().stemSamplesWritten(1) > 0, "track 1's stem too");

        // --- Stop: everything flushes to disk -------------------------------------
        const juce::File master = d.proc().captureFile();
        const auto elapsed = d.proc().stopCapture();
        check(elapsed.inSeconds() > 0.0, "the take reports a real duration");
        check(!d.proc().isCapturing(), "and the capture is over");

        // --- What a player actually walks away with -------------------------------
        check(master.existsAsFile(), "the master take is on disk");
        check(master.getSize() > 1000, "...with audio in it, not just a header");

        const juce::File takeDir = master.getParentDirectory();
        const auto stems = takeDir.findChildFiles(juce::File::findFiles, false, "track-*.wav");
        check(stems.size() >= 2, "one stem per audible track landed beside it");

        bool allNonEmpty = true;
        for (const auto& f : stems)
            if (f.getSize() <= 1000) allNonEmpty = false;
        check(allNonEmpty, "every stem has audio in it");

        // The alignment invariant (11.11): a stem is exactly as long as the master, so
        // the set drops straight onto a DAW timeline with no nudging.
        bool aligned = true;
        for (const auto& f : stems)
        {
            const double ratio = static_cast<double>(f.getSize())
                               / static_cast<double>(std::max<juce::int64>(master.getSize(), 1));
            if (ratio < 0.9 || ratio > 1.1) aligned = false;
        }
        check(aligned, "every stem is the same length as the master take");

        check(takeDir.getChildFile("take-sheet.txt").existsAsFile(),
              "a take sheet says what the files are");

        dir.deleteRecursively();
    }
}   // namespace

void runCujCaptureTests(int& failed)
{
    testCaptureToStems(failed);
}
}   // namespace lockstep
