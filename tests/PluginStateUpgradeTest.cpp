// PluginStateUpgradeTest -- serializer upgrade-chain guard (ported from the
// PluginProcessor constructor's JUCE_DEBUG startup run into the headless test
// harness, so upgrade regressions turn ctest red instead of being swallowed at
// plugin-load time). Kept as a juce::UnitTest (category "PluginState") to
// preserve every beginTest case verbatim; runPluginStateUpgradeTests() drives it
// and folds any failure into the harness CHECK counter.

#include "TestHarness.h"
#include "../src/state/PluginState.h"
#include "../src/state/StateKeys.h"
#include "../src/core/Subdivision.h"
#include "../src/core/Scale.h"
#include "../src/core/LaunchQuant.h"
#include "../src/machine/SamplePool.h"
#include "../src/machine/SampleMachine.h"
#include <juce_data_structures/juce_data_structures.h>

namespace
{
    using namespace lockstep;

    class PluginStateUpgradeTest : public juce::UnitTest
    {
    public:
        PluginStateUpgradeTest()
            : juce::UnitTest("Upgrade chain", "PluginState") {}

        void runTest() override
        {
            beginTest("v0 -> v1: bare APVTS tree is wrapped correctly");
            {
                juce::ValueTree v0(keys::kLockstep);
                v0.setProperty("someParam", 0.5, nullptr);

                const auto v1 = lockstep::PluginState::applyUpgrades(v0);

                expect(v1.getType() == juce::Identifier(keys::kLockstepState),
                       "root type must be LockstepState");
                expectEquals(static_cast<int>(v1.getProperty(keys::kVersion, -1)),
                             lockstep::PluginState::kCurrentVersion,
                             "version must be current");
                expect(v1.getChildWithName(keys::kLockstep).isValid(),
                       "upgraded tree must contain APVTS child");
                expect(!v1.getChildWithName(keys::kSamplePool).isValid(),
                       "v0 upgrades must not invent a SamplePool node");
                expect(!v1.getChildWithName("Sequence").isValid(),
                       "v0 upgrades must not invent a Sequence node");
            }

            beginTest("v1 (flat Sequence) clean-breaks to current");
            {
                // A pre-v2 tree carried a flat "Sequence" node. The Phase 7 clean
                // break discards all legacy hierarchy; APVTS/SamplePool/Misc stay.
                juce::ValueTree v1(keys::kLockstepState);
                v1.setProperty(keys::kVersion, 1, nullptr);
                v1.appendChild(juce::ValueTree(keys::kLockstep), nullptr);
                v1.appendChild(juce::ValueTree(keys::kSamplePool), nullptr);
                v1.appendChild(juce::ValueTree("Sequence"), nullptr);
                v1.appendChild(juce::ValueTree(keys::kMisc), nullptr);

                const auto result = lockstep::PluginState::applyUpgrades(v1);

                expectEquals(static_cast<int>(result.getProperty(keys::kVersion, -1)),
                             lockstep::PluginState::kCurrentVersion,
                             "version collapsed to current");
                expect(!result.getChildWithName("Sequence").isValid(),
                       "legacy flat Sequence discarded");
                expect(!result.getChildWithName(keys::kProject).isValid(),
                       "no legacy Project remains");
                expect(result.getChildWithName(keys::kLockstep).isValid(),
                       "APVTS preserved");
                expect(result.getChildWithName(keys::kSamplePool).isValid(),
                       "SamplePool preserved");
                expect(result.getChildWithName(keys::kMisc).isValid(),
                       "Misc preserved");
            }

            beginTest("legacy Project (v2-v4) clean-breaks to current");
            {
                // Any legacy Bank/Pattern/Part hierarchy is dropped wholesale by
                // the pre-release clean break; the processor re-seeds defaults.
                juce::ValueTree v2(keys::kLockstepState);
                v2.setProperty(keys::kVersion, 2, nullptr);
                v2.appendChild(juce::ValueTree(keys::kLockstep), nullptr);
                v2.appendChild(juce::ValueTree(keys::kSamplePool), nullptr);

                juce::ValueTree proj(keys::kProject);
                juce::ValueTree bank(keys::kBank);
                bank.appendChild(juce::ValueTree(keys::kPattern), nullptr);
                bank.appendChild(juce::ValueTree(keys::kPart), nullptr);
                proj.appendChild(bank, nullptr);
                v2.appendChild(proj, nullptr);
                v2.appendChild(juce::ValueTree(keys::kMisc), nullptr);

                const auto result = lockstep::PluginState::applyUpgrades(v2);

                expectEquals(static_cast<int>(result.getProperty(keys::kVersion, -1)),
                             lockstep::PluginState::kCurrentVersion,
                             "version collapsed to current");
                expect(!result.getChildWithName(keys::kProject).isValid(),
                       "legacy Project/Bank/Pattern/Part discarded");
                expect(result.getChildWithName(keys::kLockstep).isValid(),
                       "APVTS preserved");
                expect(result.getChildWithName(keys::kSamplePool).isValid(),
                       "SamplePool preserved");
                expect(result.getChildWithName(keys::kMisc).isValid(),
                       "Misc preserved");
            }

            beginTest("current version passes through unchanged");
            {
                juce::ValueTree cur(keys::kLockstepState);
                cur.setProperty(keys::kVersion, lockstep::PluginState::kCurrentVersion, nullptr);
                cur.appendChild(juce::ValueTree(keys::kLockstep), nullptr);
                cur.appendChild(juce::ValueTree(keys::kNewHierarchy), nullptr);

                const auto result = lockstep::PluginState::applyUpgrades(cur);

                expectEquals(static_cast<int>(result.getProperty(keys::kVersion, -1)),
                             lockstep::PluginState::kCurrentVersion,
                             "current version unchanged");
                expect(result.getChildWithName(keys::kNewHierarchy).isValid(),
                       "NewHierarchy node preserved at current version");
            }

            beginTest("v9 -> v10: APVTS swing migrated into Song[0] NewHierarchy");
            {
                // Build a v9 state with non-zero global swing and per-track swing.
                juce::ValueTree v9(keys::kLockstepState);
                v9.setProperty(keys::kVersion, 9, nullptr);

                juce::ValueTree apvts(keys::kLockstep);
                {
                    juce::ValueTree p1("PARAM");
                    p1.setProperty("id", keys::kSwing, nullptr);
                    p1.setProperty("value", 0.25, nullptr);
                    apvts.appendChild(p1, nullptr);

                    juce::ValueTree p2("PARAM");
                    p2.setProperty("id", "track_0_swing", nullptr);
                    p2.setProperty("value", -0.1, nullptr);
                    apvts.appendChild(p2, nullptr);
                }
                v9.appendChild(apvts, nullptr);

                const auto result = lockstep::PluginState::applyUpgrades(v9);

                expectEquals(static_cast<int>(result.getProperty(keys::kVersion, -1)),
                             lockstep::PluginState::kCurrentVersion,
                             "v9->v10+: version reaches current after full chain");

                const auto nh = result.getChildWithName(keys::kNewHierarchy);
                expect(nh.isValid(), "v9->v10: NewHierarchy present after migration");

                juce::ValueTree song0;
                for (auto c : nh)
                    if (c.getType() == juce::Identifier(keys::kSong) && static_cast<int>(c.getProperty("i", -1)) == 0)
                    {
                        song0 = c;
                        break;
                    }
                expect(song0.isValid(), "v9->v10: Song[0] node created");

                expectWithinAbsoluteError(
                    static_cast<float>(static_cast<double>(song0.getProperty(keys::kSwing, 0.0))),
                    0.25f, 0.001f, "v9->v10: song-all swing migrated");

                juce::ValueTree trk0;
                for (auto c : song0)
                    if (c.getType() == juce::Identifier(keys::kSongTrack) && static_cast<int>(c.getProperty("t", -1)) == 0)
                    {
                        trk0 = c;
                        break;
                    }
                expect(trk0.isValid(), "v9->v10: SongTrack[0] created for non-zero track swing");
                expectWithinAbsoluteError(
                    static_cast<float>(static_cast<double>(trk0.getProperty(keys::kSwing, 0.0))),
                    -0.1f, 0.001f, "v9->v10: track-0 swing migrated");
            }

            beginTest("v15 -> v16: missing SoundPool node loads as empty pool");
            {
                // A v15 tree has no SoundPool child; upgrade stamps to v16 but doesn't add the node.
                // readProjectSoundPool on that tree yields an empty pool (tested here at tree level).
                juce::ValueTree v15(keys::kLockstepState);
                v15.setProperty(keys::kVersion, 15, nullptr);
                v15.appendChild(juce::ValueTree(keys::kLockstep), nullptr);
                v15.appendChild(juce::ValueTree(keys::kNewHierarchy), nullptr);

                const auto result = lockstep::PluginState::applyUpgrades(v15);

                expectEquals(static_cast<int>(result.getProperty(keys::kVersion, -1)),
                             lockstep::PluginState::kCurrentVersion,
                             "v15->v16: version stamped to current");
                expect(!result.getChildWithName(keys::kSoundPool).isValid(),
                       "v15->v16: no SoundPool node invented (upgrade is trivial)");
                expect(result.getChildWithName(keys::kNewHierarchy).isValid(),
                       "v15->v16: NewHierarchy preserved");
            }

            beginTest("v18 -> v19: density fields default to Mixed/Scrub on upgrade");
            {
                // Minimal v18 tree with one Kit node that has no density properties.
                juce::ValueTree v18(keys::kLockstepState);
                v18.setProperty(keys::kVersion, 18, nullptr);
                auto nh = juce::ValueTree(keys::kNewHierarchy);
                auto song = juce::ValueTree(keys::kSong);
                auto songTrack = juce::ValueTree(keys::kSongTrack);
                auto kitNode = juce::ValueTree(keys::kKit);
                kitNode.setProperty(keys::kMId, juce::String(SampleMachine::kMachineId), nullptr);
                songTrack.appendChild(kitNode, nullptr);
                song.appendChild(songTrack, nullptr);
                nh.appendChild(song, nullptr);
                v18.appendChild(nh, nullptr);
                v18.appendChild(juce::ValueTree(keys::kLockstep), nullptr);
                v18.appendChild(juce::ValueTree(keys::kSamplePool), nullptr);
                v18.appendChild(juce::ValueTree(keys::kMisc), nullptr);

                const auto result = lockstep::PluginState::applyUpgrades(v18);

                expectEquals(static_cast<int>(result.getProperty(keys::kVersion, -1)),
                             lockstep::PluginState::kCurrentVersion,
                             "v18->v19: version stamped to current");

                // Density fields are absent from the v18 Kit node; readKitFromNode
                // will default them to Mixed/Scrub — no data migration needed.
                // Verify the Kit node was preserved and the upgrade didn't corrupt it.
                const auto nhResult = result.getChildWithName(keys::kNewHierarchy);
                expect(nhResult.isValid(), "v18->v19: NewHierarchy preserved");
                const auto kitResult = nhResult.getChildWithName(keys::kSong)
                                               .getChildWithName(keys::kSongTrack)
                                               .getChildWithName(keys::kKit);
                expect(kitResult.isValid(), "v18->v19: Kit node preserved");
                // dMus/dSel absent on disk; defaults applied at read time.
                expect(!kitResult.hasProperty(keys::kDensMus),
                       "v18->v19: dMus absent (default applied at read)");
                expect(!kitResult.hasProperty(keys::kDensSel),
                       "v18->v19: dSel absent (default applied at read)");
            }

            beginTest("v20 -> v21: tempo/timesig hierarchy: v20 song node has no hasTp/tpRat");
            {
                // v20 tree with a Song node; tempo/timesig fields absent → loaded with defaults.
                juce::ValueTree v20(keys::kLockstepState);
                v20.setProperty(keys::kVersion, 20, nullptr);
                auto nh = juce::ValueTree(keys::kNewHierarchy);
                auto song = juce::ValueTree(keys::kSong);
                nh.appendChild(song, nullptr);
                v20.appendChild(nh, nullptr);
                v20.appendChild(juce::ValueTree(keys::kLockstep), nullptr);
                v20.appendChild(juce::ValueTree(keys::kSamplePool), nullptr);
                v20.appendChild(juce::ValueTree(keys::kMisc), nullptr);

                const auto result = lockstep::PluginState::applyUpgrades(v20);

                expectEquals(static_cast<int>(result.getProperty(keys::kVersion, -1)),
                             lockstep::PluginState::kCurrentVersion,
                             "v20->v21: version stamped to current");

                const auto nhResult = result.getChildWithName(keys::kNewHierarchy);
                expect(nhResult.isValid(), "v20->v21: NewHierarchy preserved");
                const auto songResult = nhResult.getChildWithName(keys::kSong);
                expect(songResult.isValid(), "v20->v21: Song node preserved");
                expect(!songResult.hasProperty(keys::kHasTempo),
                       "v20->v21: kHasTempo absent (read path defaults to ratio=1.0)");
                expect(!songResult.hasProperty(keys::kTempoRatio),
                       "v20->v21: kTempoRatio absent (no override stored for v20 songs)");
                expect(!songResult.hasProperty(keys::kHasTs),
                       "v20->v21: kHasTs absent (timesig inherits project default)");
            }

            beginTest("v21: song tempo ratio round-trips through ValueTree properties");
            {
                // A v21 tree with explicit Song tempo and time-sig override.
                juce::ValueTree v21(keys::kLockstepState);
                v21.setProperty(keys::kVersion, 21, nullptr);
                auto nh = juce::ValueTree(keys::kNewHierarchy);
                auto song = juce::ValueTree(keys::kSong);
                song.setProperty(keys::kHasTempo, 1, nullptr);
                song.setProperty(keys::kTempoRatio, 0.75, nullptr);
                song.setProperty(keys::kHasTs, 1, nullptr);
                song.setProperty(keys::kSongTsN, 3, nullptr);
                song.setProperty(keys::kSongTsD, 4, nullptr);
                nh.appendChild(song, nullptr);
                v21.appendChild(nh, nullptr);
                v21.appendChild(juce::ValueTree(keys::kLockstep), nullptr);
                v21.appendChild(juce::ValueTree(keys::kSamplePool), nullptr);
                v21.appendChild(juce::ValueTree(keys::kMisc), nullptr);

                const auto result = lockstep::PluginState::applyUpgrades(v21);
                const auto songResult = result.getChildWithName(keys::kNewHierarchy)
                                              .getChildWithName(keys::kSong);

                expectEquals(static_cast<int>(songResult.getProperty(keys::kHasTempo, 0)),
                             1, "v21: kHasTempo round-trips as 1");
                expectWithinAbsoluteError(
                    static_cast<double>(songResult.getProperty(keys::kTempoRatio, 1.0)),
                    0.75, 1e-9, "v21: kTempoRatio round-trips as 0.75");
                expectEquals(static_cast<int>(songResult.getProperty(keys::kHasTs, 0)),
                             1, "v21: kHasTs round-trips as 1");
                expectEquals(static_cast<int>(songResult.getProperty(keys::kSongTsN, 4)),
                             3, "v21: kSongTsN round-trips as 3");
                expectEquals(static_cast<int>(songResult.getProperty(keys::kSongTsD, 4)),
                             4, "v21: kSongTsD round-trips as 4");
            }

            beginTest("v22: key signature round-trips through ValueTree properties");
            {
                // A v22 tree with a Set-level default key and a Song override.
                juce::ValueTree v22(keys::kLockstepState);
                v22.setProperty(keys::kVersion, 22, nullptr);
                auto nh = juce::ValueTree(keys::kNewHierarchy);
                nh.setProperty(keys::kSetKsRoot, 7, nullptr);   // G
                nh.setProperty(keys::kSetKsBri, -1, nullptr);   // Ionian
                nh.setProperty(keys::kSetKsMod, 0, nullptr);
                nh.setProperty(keys::kSetKsSym, 0, nullptr);
                auto song = juce::ValueTree(keys::kSong);
                song.setProperty(keys::kHasKs, 1, nullptr);
                song.setProperty(keys::kSongKsRoot, 9, nullptr);          // A
                song.setProperty(keys::kSongKsBri, -4, nullptr);          // Aeolian
                song.setProperty(keys::kSongKsMod, 1, nullptr);           // Harmonic (bit 0)
                song.setProperty(keys::kSongKsSym, 0, nullptr);
                nh.appendChild(song, nullptr);
                v22.appendChild(nh, nullptr);
                v22.appendChild(juce::ValueTree(keys::kLockstep), nullptr);
                v22.appendChild(juce::ValueTree(keys::kSamplePool), nullptr);
                v22.appendChild(juce::ValueTree(keys::kMisc), nullptr);

                const auto result = lockstep::PluginState::applyUpgrades(v22);
                const auto nhResult = result.getChildWithName(keys::kNewHierarchy);
                const auto songResult = nhResult.getChildWithName(keys::kSong);

                expectEquals(static_cast<int>(nhResult.getProperty(keys::kSetKsRoot, -1)),
                             7, "v22: Set key root round-trips as G(7)");
                expectEquals(static_cast<int>(songResult.getProperty(keys::kHasKs, 0)),
                             1, "v22: Song kHasKs round-trips as 1");
                expectEquals(static_cast<int>(songResult.getProperty(keys::kSongKsBri, 0)),
                             -4, "v22: Song key brightness round-trips as Aeolian(-4)");
                expectEquals(static_cast<int>(songResult.getProperty(keys::kSongKsMod, 0)),
                             1, "v22: Song key modifier bitmask round-trips as 1 (Harmonic)");
            }

            beginTest("v24 -> v25: legacy launchQuant bar count remapped to LaunchQuant enum");
            {
                // A v24 tree whose NewHierarchy node stores launchQuant as the
                // legacy bar count 4 (== 4-bar quantize). After upgrade it must
                // be the LaunchQuant::Bars4 enum value.
                juce::ValueTree v24(keys::kLockstepState);
                v24.setProperty(keys::kVersion, 24, nullptr);
                auto nh = juce::ValueTree(keys::kNewHierarchy);
                nh.setProperty(keys::kLaunchQuant, 4, nullptr);  // legacy: 4 bars
                v24.appendChild(nh, nullptr);
                v24.appendChild(juce::ValueTree(keys::kLockstep), nullptr);
                v24.appendChild(juce::ValueTree(keys::kSamplePool), nullptr);
                v24.appendChild(juce::ValueTree(keys::kMisc), nullptr);

                const auto result = lockstep::PluginState::applyUpgrades(v24);
                const auto nhResult = result.getChildWithName(keys::kNewHierarchy);
                expectEquals(static_cast<int>(nhResult.getProperty(keys::kLaunchQuant, -1)),
                             static_cast<int>(lockstep::LaunchQuant::Bars4),
                             "v24->v25: legacy launchQuant 4 remaps to Bars4 enum");

                // A missing launchQuant stays absent (read path defaults to Bar).
                juce::ValueTree v24b(keys::kLockstepState);
                v24b.setProperty(keys::kVersion, 24, nullptr);
                v24b.appendChild(juce::ValueTree(keys::kNewHierarchy), nullptr);
                v24b.appendChild(juce::ValueTree(keys::kLockstep), nullptr);
                v24b.appendChild(juce::ValueTree(keys::kSamplePool), nullptr);
                v24b.appendChild(juce::ValueTree(keys::kMisc), nullptr);
                const auto resultB = lockstep::PluginState::applyUpgrades(v24b);
                expect(!resultB.getChildWithName(keys::kNewHierarchy).hasProperty(keys::kLaunchQuant),
                       "v24->v25: absent launchQuant stays absent (read defaults to Bar)");
            }

            beginTest("v25 -> v26: sample-pool entry survives, no analysis props invented");
            {
                // A v25 tree with a single pool entry (path + hash, no analysis
                // props) upgrades to v26 with the entry intact and no cached
                // analysis fabricated — the entry will simply be re-analysed on
                // load (exact v25 behaviour).
                juce::ValueTree v25(keys::kLockstepState);
                v25.setProperty(keys::kVersion, 25, nullptr);
                auto pool = juce::ValueTree(keys::kSamplePool);
                auto entry = juce::ValueTree(keys::kEntry);
                entry.setProperty("i", 0, nullptr);
                entry.setProperty(keys::kPath, "/tmp/loop.wav", nullptr);
                entry.setProperty(keys::kHash, "deadbeef", nullptr);
                pool.appendChild(entry, nullptr);
                v25.appendChild(juce::ValueTree(keys::kNewHierarchy), nullptr);
                v25.appendChild(juce::ValueTree(keys::kLockstep), nullptr);
                v25.appendChild(pool, nullptr);
                v25.appendChild(juce::ValueTree(keys::kMisc), nullptr);

                const auto result = lockstep::PluginState::applyUpgrades(v25);
                expectEquals(static_cast<int>(result.getProperty(keys::kVersion, 0)),
                             lockstep::PluginState::kCurrentVersion,
                             "v25->v26: version stamp bumped through the chain");
                const auto poolR = result.getChildWithName(keys::kSamplePool);
                expect(poolR.isValid() && poolR.getNumChildren() == 1,
                       "v25->v26: pool entry survives");
                const auto entryR = poolR.getChild(0);
                expectEquals(entryR.getProperty(keys::kPath).toString(),
                             juce::String("/tmp/loop.wav"), "v25->v26: entry path intact");
                expect(!entryR.hasProperty(keys::kAnalysed),
                       "v25->v26: no analysis props invented for a legacy entry");
            }

            beginTest("future version: valid tree returned without crash");
            {
                juce::ValueTree future(keys::kLockstepState);
                future.setProperty(keys::kVersion, lockstep::PluginState::kCurrentVersion + 1, nullptr);
                future.appendChild(juce::ValueTree(keys::kLockstep), nullptr);

                const auto result = lockstep::PluginState::applyUpgrades(future);

                expect(result.isValid(), "future version must return a valid tree");
                expect(result.getChildWithName(keys::kLockstep).isValid(),
                       "APVTS child must be intact");
            }
        }
    } gUpgradeTest;
} // anonymous namespace

namespace lockstep
{
    void runPluginStateUpgradeTests()
    {
        // The anonymous-namespace static above self-registers in JUCE's global
        // unit-test list under category "PluginState"; run just that category and
        // fold any failures into the harness counter.
        juce::UnitTestRunner runner;
        runner.setAssertOnFailure(false);
        runner.setPassesAreLogged(false);
        runner.runTestsInCategory("PluginState");
        for (int i = 0; i < runner.getNumResults(); ++i)
        {
            const auto* r = runner.getResult(i);
            if (r == nullptr) continue;
            CHECK(r->failures == 0,
                  juce::String("PluginState upgrade '") + r->unitTestName + " / "
                      + r->subcategoryName + "' had " + juce::String(r->failures)
                      + " failure(s)");
        }
    }
}
