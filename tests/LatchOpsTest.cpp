// LatchOpsTest — structural lock for LatchOps pure helpers.
//
// These tests pin the column membership and boolean-mapping invariants so that
// any future drift between LatchOps and setModifierLatch (which encodes the same
// grouping) produces a test failure rather than silent misbehaviour.

#include "TestHarness.h"
#include "../src/ui/mode/LatchOps.h"
#include "../src/state/UiState.h"

namespace lockstep
{
    using CB = ControllerButton;

    // ── latchColumn ──────────────────────────────────────────────────────────

    static void testLatchColumnCol1()
    {
        CHECK(latchColumn(CB::PhraseScope) == 0, "PhraseScope → col 0");
        CHECK(latchColumn(CB::MorphScope)  == 0, "MorphScope → col 0");
        CHECK(latchColumn(CB::MuteScope)   == 0, "MuteScope → col 0");
    }

    static void testLatchColumnCol2()
    {
        CHECK(latchColumn(CB::TrackScope) == 1, "TrackScope → col 1");
        CHECK(latchColumn(CB::SceneScope) == 1, "SceneScope → col 1");
        CHECK(latchColumn(CB::SongScope)  == 1, "SongScope → col 1");
        CHECK(latchColumn(CB::FillScope)  == 1, "FillScope → col 1");
    }

    static void testLatchColumnNotLatchable()
    {
        CHECK(latchColumn(CB::Func)        == -1, "Func not latchable");
        CHECK(latchColumn(CB::CueScope)    == -1, "CueScope not latchable");
        CHECK(latchColumn(CB::NavUp)       == -1, "NavUp not latchable");
        CHECK(latchColumn(CB::VerbConfirm) == -1, "VerbConfirm not latchable");
        CHECK(latchColumn(CB::None)        == -1, "None not latchable");
    }

    // ── latchBoolFor ─────────────────────────────────────────────────────────

    static void testLatchBoolForReturnsRightPointer()
    {
        LatchState s;
        *latchBoolFor(s, CB::PhraseScope) = true;  CHECK(s.phrase, "PhraseScope → phrase");
        *latchBoolFor(s, CB::MorphScope)  = true;  CHECK(s.morph,  "MorphScope → morph");
        *latchBoolFor(s, CB::MuteScope)   = true;  CHECK(s.mute,   "MuteScope → mute");
        *latchBoolFor(s, CB::TrackScope)  = true;  CHECK(s.track,  "TrackScope → track");
        *latchBoolFor(s, CB::SceneScope)  = true;  CHECK(s.scene,  "SceneScope → scene");
        *latchBoolFor(s, CB::SongScope)   = true;  CHECK(s.song,   "SongScope → song");
        *latchBoolFor(s, CB::FillScope)   = true;  CHECK(s.fill,   "FillScope → fill");
    }

    static void testLatchBoolForNotLatchableReturnsNull()
    {
        LatchState s;
        CHECK(latchBoolFor(s, CB::Func)     == nullptr, "Func → nullptr");
        CHECK(latchBoolFor(s, CB::NavDown)  == nullptr, "NavDown → nullptr");
        CHECK(latchBoolFor(s, CB::Step)     == nullptr, "Step → nullptr");
        CHECK(latchBoolFor(s, CB::None)     == nullptr, "None → nullptr");
    }

    static void testLatchBoolForConstOverload()
    {
        LatchState s;
        s.song = true;
        const LatchState& cs = s;
        const bool* p = latchBoolFor(cs, CB::SongScope);
        CHECK(p != nullptr, "const overload non-null for SongScope");
        CHECK(*p == true,   "const overload reads correct value");
    }

    // ── clearLatchColumnExcept ────────────────────────────────────────────────

    static void testClearCol1ExceptPhrase()
    {
        LatchState s;
        s.phrase = true; s.morph = true; s.mute = true;
        // col2 should be unaffected:
        s.track = true; s.scene = true;

        clearLatchColumnExcept(s, CB::PhraseScope);

        CHECK(s.phrase,  "phrase (self) preserved");
        CHECK(!s.morph,  "morph cleared");
        CHECK(!s.mute,   "mute cleared");
        CHECK(s.track,   "track (col2) untouched");
        CHECK(s.scene,   "scene (col2) untouched");
    }

    static void testClearCol1ExceptMorph()
    {
        LatchState s;
        s.phrase = true; s.morph = true; s.mute = true;

        clearLatchColumnExcept(s, CB::MorphScope);

        CHECK(!s.phrase, "phrase cleared");
        CHECK(s.morph,   "morph (self) preserved");
        CHECK(!s.mute,   "mute cleared");
    }

    static void testClearCol1ExceptMute()
    {
        LatchState s;
        s.phrase = true; s.morph = true; s.mute = true;

        clearLatchColumnExcept(s, CB::MuteScope);

        CHECK(!s.phrase, "phrase cleared");
        CHECK(!s.morph,  "morph cleared");
        CHECK(s.mute,    "mute (self) preserved");
    }

    static void testClearCol2ExceptTrack()
    {
        LatchState s;
        s.track = true; s.scene = true; s.song = true; s.fill = true;
        // col1 should be unaffected:
        s.phrase = true;

        clearLatchColumnExcept(s, CB::TrackScope);

        CHECK(s.track,   "track (self) preserved");
        CHECK(!s.scene,  "scene cleared");
        CHECK(!s.song,   "song cleared");
        CHECK(!s.fill,   "fill cleared");
        CHECK(s.phrase,  "phrase (col1) untouched");
    }

    static void testClearCol2ExceptScene()
    {
        LatchState s;
        s.track = true; s.scene = true; s.song = true; s.fill = true;

        clearLatchColumnExcept(s, CB::SceneScope);

        CHECK(!s.track,  "track cleared");
        CHECK(s.scene,   "scene (self) preserved");
        CHECK(!s.song,   "song cleared");
        CHECK(!s.fill,   "fill cleared");
    }

    static void testClearCol2ExceptSong()
    {
        LatchState s;
        s.track = true; s.scene = true; s.song = true; s.fill = true;

        clearLatchColumnExcept(s, CB::SongScope);

        CHECK(!s.track,  "track cleared");
        CHECK(!s.scene,  "scene cleared");
        CHECK(s.song,    "song (self) preserved");
        CHECK(!s.fill,   "fill cleared");
    }

    static void testClearCol2ExceptFill()
    {
        LatchState s;
        s.track = true; s.scene = true; s.song = true; s.fill = true;

        clearLatchColumnExcept(s, CB::FillScope);

        CHECK(!s.track,  "track cleared");
        CHECK(!s.scene,  "scene cleared");
        CHECK(!s.song,   "song cleared");
        CHECK(s.fill,    "fill (self) preserved");
    }

    static void testClearNotLatchableIsNoop()
    {
        LatchState s;
        s.phrase = true; s.track = true;

        clearLatchColumnExcept(s, CB::Func);    // not latchable
        clearLatchColumnExcept(s, CB::NavUp);   // not latchable
        clearLatchColumnExcept(s, CB::None);    // not latchable

        CHECK(s.phrase, "phrase still set after noop");
        CHECK(s.track,  "track still set after noop");
    }

    // ── LatchState::any() consistency ────────────────────────────────────────

    static void testAnyViaLatchBoolFor()
    {
        LatchState s;
        CHECK(!s.any(), "empty → any() == false");

        *latchBoolFor(s, CB::SongScope) = true;
        CHECK(s.any(), "after setting song → any() == true");

        *latchBoolFor(s, CB::SongScope) = false;
        CHECK(!s.any(), "after clearing song → any() == false");
    }

    static void testAnyAfterClearColumnExcept()
    {
        LatchState s;
        s.track = true; s.scene = true;
        clearLatchColumnExcept(s, CB::TrackScope);
        // only track remains
        CHECK(s.any(), "track still set → any() still true");
        *latchBoolFor(s, CB::TrackScope) = false;
        CHECK(!s.any(), "all cleared → any() == false");
    }

    // ─────────────────────────────────────────────────────────────────────────

    void runLatchOpsTests()
    {
        testLatchColumnCol1();
        testLatchColumnCol2();
        testLatchColumnNotLatchable();
        testLatchBoolForReturnsRightPointer();
        testLatchBoolForNotLatchableReturnsNull();
        testLatchBoolForConstOverload();
        testClearCol1ExceptPhrase();
        testClearCol1ExceptMorph();
        testClearCol1ExceptMute();
        testClearCol2ExceptTrack();
        testClearCol2ExceptScene();
        testClearCol2ExceptSong();
        testClearCol2ExceptFill();
        testClearNotLatchableIsNoop();
        testAnyViaLatchBoolFor();
        testAnyAfterClearColumnExcept();
    }

} // namespace lockstep
