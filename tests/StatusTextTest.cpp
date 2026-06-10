// StatusTextTest — verify StatusText.h builders are non-empty and correct.

#include "TestHarness.h"
#include "../src/command/StatusText.h"

namespace lockstep
{
    static void testStatusText()
    {
        // Non-empty: every builder returns a non-empty string.
        CHECK(status::copiedTrack(0).isNotEmpty(),    "copiedTrack non-empty");
        CHECK(status::pastedTrack(0).isNotEmpty(),    "pastedTrack non-empty");
        CHECK(status::clearedTrack(0).isNotEmpty(),   "clearedTrack non-empty");
        CHECK(status::deletedTrack(0).isNotEmpty(),   "deletedTrack non-empty");
        CHECK(status::copiedPhrase().isNotEmpty(),    "copiedPhrase non-empty");
        CHECK(status::pastedPhrase().isNotEmpty(),    "pastedPhrase non-empty");
        CHECK(status::clearedPhrase().isNotEmpty(),   "clearedPhrase non-empty");
        CHECK(status::deletedPhrase().isNotEmpty(),   "deletedPhrase non-empty");
        CHECK(status::deletedPart().isNotEmpty(),     "deletedPart non-empty");
        CHECK(status::copiedScene().isNotEmpty(),     "copiedScene non-empty");
        CHECK(status::pastedScene().isNotEmpty(),     "pastedScene non-empty");
        CHECK(status::pastedSceneFloor().isNotEmpty(),"pastedSceneFloor non-empty");
        CHECK(status::sceneCreated(1).isNotEmpty(),   "sceneCreated non-empty");
        CHECK(status::sceneBaseline(1).isNotEmpty(),  "sceneBaseline non-empty");
        CHECK(status::sceneBlank(1).isNotEmpty(),     "sceneBlank non-empty");
        CHECK(status::length(16).isNotEmpty(),        "length non-empty");
        CHECK(status::lengthAllTracks(16).isNotEmpty(),"lengthAllTracks non-empty");
        CHECK(status::morphBaked().isNotEmpty(),      "morphBaked non-empty");
        CHECK(status::morphErased().isNotEmpty(),     "morphErased non-empty");
        CHECK(status::morphMuteSet().isNotEmpty(),    "morphMuteSet non-empty");
        CHECK(status::morphMuteCleared().isNotEmpty(),"morphMuteCleared non-empty");
        CHECK(status::baked().isNotEmpty(),           "baked non-empty");
        CHECK(status::noDeviationsToBake().isNotEmpty(),"noDeviationsToBake non-empty");
        CHECK(status::capturedAll().isNotEmpty(),     "capturedAll non-empty");
        CHECK(status::panic().isNotEmpty(),           "panic non-empty");
        CHECK(status::quantized().isNotEmpty(),       "quantized non-empty");
        CHECK(status::cancelled().isNotEmpty(),       "cancelled non-empty");
        CHECK(status::nothingCopied().isNotEmpty(),   "nothingCopied non-empty");
        CHECK(status::pastePickScope().isNotEmpty(),  "pastePickScope non-empty");
        CHECK(status::confirmDelete("Track 1").isNotEmpty(), "confirmDelete non-empty");

        // Track number embeds correctly (1-indexed).
        CHECK(status::copiedTrack(0)  == "Copied Track 1",   "copiedTrack 1-indexed");
        CHECK(status::pastedTrack(1)  == "Pasted -> Track 2","pastedTrack 1-indexed");
        CHECK(status::clearedTrack(7) == "Cleared Track 8",  "clearedTrack 1-indexed");
        CHECK(status::deletedTrack(3) == "Deleted Track 4",  "deletedTrack 1-indexed");

        // Scene numbers are passed in already 1-indexed.
        CHECK(status::sceneCreated(3)  == "Scene 3 created",    "sceneCreated");
        CHECK(status::sceneBaseline(2) == "Scene 2 (baseline)", "sceneBaseline");
        CHECK(status::sceneBlank(5)    == "Scene 5 (blank)",    "sceneBlank");

        // Length embeds correctly.
        CHECK(status::length(32)         == "Length 32",               "length");
        CHECK(status::lengthAllTracks(8) == "Length 8 (all tracks)",   "lengthAllTracks");

        // Confirm-delete includes entity name.
        CHECK(status::confirmDelete("Phrase") == "Delete Phrase?  P=Yes  Func+P=No",
              "confirmDelete content");
    }

    void runStatusTextTests()
    {
        testStatusText();
    }

} // namespace lockstep
