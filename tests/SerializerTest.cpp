// SerializerTest — pure, headless regression tests for serializer-adjacent
// predicates that gate what gets persisted. The full save/load round-trip is
// LockstepProcessor-coupled (not headless), but the *decisions* that caused data
// loss are extracted as pure helpers and pinned here.

#include "TestHarness.h"
#include "../src/core/Scene.h"

namespace lockstep
{
    // Regression for the dropped-scene-state bug: the serializer used to gate the
    // Scene write on Scene::initialised, which was never set true, so every
    // per-scene assignment was silently discarded on save. The fix gates on
    // sceneHasContent() (non-default content) instead.
    static void testSceneHasContent()
    {
        // A freshly-constructed scene is default → not worth persisting.
        Scene fresh;
        CHECK(!sceneHasContent(fresh), "fresh scene is default (not persisted)");

        // A non-zero globalPhrase makes it non-default — must persist.
        Scene assigned;
        assigned.globalPhrase = 1;
        CHECK(sceneHasContent(assigned), "scene with a non-zero globalPhrase has content");

        // A muted track (activeMask false) is non-default content.
        Scene masked;
        masked.activeMask[0] = false;
        CHECK(sceneHasContent(masked), "scene with an active-mask change has content");

        // A non-4/4 core time is non-default content.
        Scene meter;
        meter.coreTime.numerator = 7;
        meter.coreTime.denominator = 8;
        CHECK(sceneHasContent(meter), "scene with a non-default coreTime has content");

        // A morph snapshot entry is non-default content.
        Scene morph;
        morph.morphA[{0, 1}] = 0.5f;
        CHECK(sceneHasContent(morph), "scene with a morph snapshot has content");

        // The initialised flag must NOT, by itself, mark content (it was the bug).
        Scene flagged;
        flagged.initialised = true;
        CHECK(!sceneHasContent(flagged),
              "initialised flag alone does not imply content (regression guard)");
    }

    void runSerializerTests()
    {
        testSceneHasContent();
    }
}
