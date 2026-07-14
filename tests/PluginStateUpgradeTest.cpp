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

            beginTest("v29: sample_id re-resolves to the pool entry's current position");
            {
                // Two pool entries. Their child ORDER is the live pool index after
                // load, so a reference must resolve to the child whose hash matches,
                // not to whatever now sits at the old flat index. Here BBBB is child 1
                // but the P node's stale v points at 999 — the hash must win.
                auto makeTree = [](int firstHash) {
                    juce::ValueTree t(keys::kLockstepState);
                    t.setProperty(keys::kVersion, 29, nullptr);
                    auto pool = juce::ValueTree(keys::kSamplePool);
                    auto e0 = juce::ValueTree(keys::kEntry);
                    e0.setProperty("i", 8, nullptr);
                    e0.setProperty(keys::kHash, firstHash == 0 ? "aaaaaaaa" : "bbbbbbbb", nullptr);
                    auto e1 = juce::ValueTree(keys::kEntry);
                    e1.setProperty("i", 9, nullptr);
                    e1.setProperty(keys::kHash, firstHash == 0 ? "bbbbbbbb" : "aaaaaaaa", nullptr);
                    pool.appendChild(e0, nullptr);
                    pool.appendChild(e1, nullptr);
                    t.appendChild(juce::ValueTree(keys::kLockstep), nullptr);
                    t.appendChild(pool, nullptr);
                    // A kit BP node holding a sample_id reference to BBBB (by hash),
                    // with a deliberately-wrong flat v that the hash must override.
                    auto nh = juce::ValueTree(keys::kNewHierarchy);
                    auto p = juce::ValueTree("P");
                    p.setProperty(keys::kParamId, "sample_id", nullptr);
                    p.setProperty(keys::kPLockVal, 999, nullptr);
                    p.setProperty(keys::kSampleHash, "bbbbbbbb", nullptr);
                    nh.appendChild(p, nullptr);
                    t.appendChild(nh, nullptr);
                    return t;
                };

                // BBBB is child 1 → sample_id resolves to index 1.
                auto r0 = lockstep::PluginState::applyUpgrades(makeTree(0));
                auto p0 = r0.getChildWithName(keys::kNewHierarchy).getChild(0);
                expectEquals(static_cast<int>(p0.getProperty(keys::kPLockVal, -1)), 1,
                             "v29: sample_id resolves to BBBB at position 1");

                // Reorder so BBBB is child 0 → sample_id now resolves to index 0,
                // proving identity follows the hash, not the array slot.
                auto r1 = lockstep::PluginState::applyUpgrades(makeTree(1));
                auto p1 = r1.getChildWithName(keys::kNewHierarchy).getChild(0);
                expectEquals(static_cast<int>(p1.getProperty(keys::kPLockVal, -1)), 0,
                             "v29: after reorder, sample_id follows BBBB to position 0");
            }

            beginTest("v28 -> v29: legacy flat sample_id is bridged via the pool 'i' index");
            {
                // A v28 tree: sample_id is a raw flat index (9) with NO hash. The pool
                // node's per-entry 'i' attribute (runtime index at save) bridges it:
                // the entry with i==9 is child 1, so the reference resolves to index 1
                // and a durable 'sh' is stamped for subsequent loads.
                juce::ValueTree v28(keys::kLockstepState);
                v28.setProperty(keys::kVersion, 28, nullptr);
                auto pool = juce::ValueTree(keys::kSamplePool);
                auto e0 = juce::ValueTree(keys::kEntry);
                e0.setProperty("i", 8, nullptr);
                e0.setProperty(keys::kHash, "aaaaaaaa", nullptr);
                auto e1 = juce::ValueTree(keys::kEntry);
                e1.setProperty("i", 9, nullptr);
                e1.setProperty(keys::kHash, "bbbbbbbb", nullptr);
                pool.appendChild(e0, nullptr);
                pool.appendChild(e1, nullptr);
                v28.appendChild(juce::ValueTree(keys::kLockstep), nullptr);
                v28.appendChild(pool, nullptr);
                auto nh = juce::ValueTree(keys::kNewHierarchy);
                auto p = juce::ValueTree("P");
                p.setProperty(keys::kParamId, "sample_id", nullptr);
                p.setProperty(keys::kPLockVal, 9, nullptr);  // flat index, no hash
                nh.appendChild(p, nullptr);
                v28.appendChild(nh, nullptr);

                auto r = lockstep::PluginState::applyUpgrades(v28);
                auto pr = r.getChildWithName(keys::kNewHierarchy).getChild(0);
                expectEquals(static_cast<int>(pr.getProperty(keys::kPLockVal, -1)), 1,
                             "v28->v29: flat index 9 bridges to child position 1");
                expectEquals(pr.getProperty(keys::kSampleHash).toString(),
                             juce::String("bbbbbbbb"),
                             "v28->v29: durable hash stamped from the bridged entry");
            }

            beginTest("v31 -> v32: HQ delay time index becomes the beat fraction");
            {
                // A v31 doc stored the delay time as an index into the division
                // table; v32 stores the beats themselves. Index 2 named 1/8.
                juce::ValueTree v31(keys::kLockstepState);
                v31.setProperty(keys::kVersion, 31, nullptr);
                auto hierarchy = juce::ValueTree(keys::kNewHierarchy);
                auto mIns = juce::ValueTree(keys::kMasterIns);
                mIns.setProperty("slot", 0, nullptr);
                mIns.setProperty(keys::kEid, "lockstep.delay.v1", nullptr);
                auto pTime = juce::ValueTree("P");
                pTime.setProperty("id", "lockstep.delayhq.time", nullptr);
                pTime.setProperty("v", 2.0f, nullptr);          // 1/8
                mIns.appendChild(pTime, nullptr);
                auto pFbk = juce::ValueTree("P");
                pFbk.setProperty("id", "lockstep.delayhq.feedback", nullptr);
                pFbk.setProperty("v", 0.6f, nullptr);
                mIns.appendChild(pFbk, nullptr);
                hierarchy.appendChild(mIns, nullptr);
                v31.appendChild(hierarchy, nullptr);
                v31.appendChild(juce::ValueTree(keys::kLockstep), nullptr);
                v31.appendChild(juce::ValueTree(keys::kMisc), nullptr);

                const auto result = lockstep::PluginState::applyUpgrades(v31);
                expectEquals(static_cast<int>(result.getProperty(keys::kVersion, 0)),
                             lockstep::PluginState::kCurrentVersion,
                             "v31->v32: version stamp bumped");

                const auto mInsR = result.getChildWithName(keys::kNewHierarchy)
                                       .getChildWithName(keys::kMasterIns);
                const auto timeR = mInsR.getChild(0);
                expectWithinAbsoluteError(
                    static_cast<float>(timeR.getProperty("v")), 0.5f, 1.0e-4f,
                    "v31->v32: division index 2 becomes 0.5 beats (1/8)");
                const auto fbkR = mInsR.getChild(1);
                expectWithinAbsoluteError(
                    static_cast<float>(fbkR.getProperty("v")), 0.6f, 1.0e-4f,
                    "v31->v32: other delay params are untouched");
            }

            beginTest("v32 -> v33: a legacy project keeps epoch 0 (its melodies do not move)");
            {
                // The epoch salts the generator seeds. A pre-v33 project has none, and
                // must NOT acquire one at upgrade: inventing an epoch would re-roll
                // every melody the user had already generated and saved. Epoch 0 is
                // the identity such a project has always implicitly had.
                juce::ValueTree v32(keys::kLockstepState);
                v32.setProperty(keys::kVersion, 32, nullptr);
                v32.appendChild(juce::ValueTree(keys::kLockstep), nullptr);
                auto misc = juce::ValueTree(keys::kMisc);
                misc.setProperty(keys::kLocalBpm, 96.0, nullptr);
                v32.appendChild(misc, nullptr);

                const auto result = lockstep::PluginState::applyUpgrades(v32);
                expectEquals(static_cast<int>(result.getProperty(keys::kVersion, 0)),
                             lockstep::PluginState::kCurrentVersion,
                             "v32->v33: version stamp bumped");

                const auto miscR = result.getChildWithName(keys::kMisc);
                expect(!miscR.hasProperty(keys::kProjectEpoch),
                       "v32->v33: no epoch is invented for a legacy project (reads as 0)");
                expectWithinAbsoluteError(
                    static_cast<double>(miscR.getProperty(keys::kLocalBpm)), 96.0, 1.0e-6,
                    "v32->v33: the rest of the Misc node is untouched");
            }

            beginTest("v33 -> v34: temporal params convert through the project's saved BPM");
            {
                // Delay time was seconds; mod rates were Hz (the chorus a normalised
                // knob scaled to 0.1..5 Hz inside process()). All become beats /
                // periods-in-beats, converted at the tempo the project was WRITTEN at,
                // so it loads sounding the same. The walker must reach P-Locks and
                // fill-P-Locks too, not just base params -- they are the same node type,
                // and a delay time automated per step would otherwise be left in
                // seconds and read as a 250-beat delay.
                juce::ValueTree v33(keys::kLockstepState);
                v33.setProperty(keys::kVersion, 33, nullptr);
                v33.appendChild(juce::ValueTree(keys::kLockstep), nullptr);

                auto misc = juce::ValueTree(keys::kMisc);
                misc.setProperty(keys::kLocalBpm, 90.0, nullptr);   // 1 beat = 2/3 s
                v33.appendChild(misc, nullptr);

                auto hierarchy = juce::ValueTree(keys::kNewHierarchy);

                const auto mkP = [](const char* id, float v) {
                    juce::ValueTree p("P");
                    p.setProperty("id", id, nullptr);
                    p.setProperty("v", v, nullptr);
                    return p;
                };

                // A base param (master insert), a P-Lock and a fill-P-Lock, each of a
                // different temporal id -- one node type, three homes.
                auto mIns = juce::ValueTree(keys::kMasterIns);
                mIns.setProperty("slot", 0, nullptr);
                mIns.setProperty(keys::kEid, "lockstep.delay.v1", nullptr);
                mIns.appendChild(mkP("lockstep.delay.time", 0.5f), nullptr);   // 0.5 s
                hierarchy.appendChild(mIns, nullptr);

                auto plock = juce::ValueTree("PL");
                plock.appendChild(mkP("lockstep.flanger.rate", 1.5f), nullptr);  // 1.5 Hz
                hierarchy.appendChild(plock, nullptr);

                auto fillLock = juce::ValueTree("FPL");
                fillLock.appendChild(mkP("va_lfo_rate", 3.0f), nullptr);         // 3 Hz
                hierarchy.appendChild(fillLock, nullptr);

                auto chorus = juce::ValueTree("CH");
                chorus.appendChild(mkP("lockstep.chorus.rate", 0.2f), nullptr);  // norm -> 1 Hz
                hierarchy.appendChild(chorus, nullptr);

                v33.appendChild(hierarchy, nullptr);

                const auto result = lockstep::PluginState::applyUpgrades(v33);
                const auto h = result.getChildWithName(keys::kNewHierarchy);

                // 0.5 s at 90 BPM (beat = 0.6667 s) = 0.75 beats.
                const float delayBeats = static_cast<float>(
                    h.getChildWithName(keys::kMasterIns).getChild(0).getProperty("v"));
                expectWithinAbsoluteError(delayBeats, 0.75f, 1.0e-3f,
                    "v33->v34: delay seconds become beats at the saved BPM");

                // 1.5 Hz at 90 BPM: period = 1/1.5 s = 0.667 s = 1.0 beat.
                const float flangerBeats = static_cast<float>(
                    h.getChild(1).getChild(0).getProperty("v"));
                expectWithinAbsoluteError(flangerBeats, 1.0f, 1.0e-3f,
                    "v33->v34: a P-LOCKED flanger rate migrates too (Hz -> period in beats)");

                // 3 Hz at 90 BPM: period = 0.333 s = 0.5 beats.
                const float lfoBeats = static_cast<float>(
                    h.getChild(2).getChild(0).getProperty("v"));
                expectWithinAbsoluteError(lfoBeats, 0.5f, 1.0e-3f,
                    "v33->v34: a FILL-P-LOCKED LFO rate migrates too");

                // Chorus: the stored 0.2 was normalised; process() scaled it x5 -> 1 Hz.
                // 1 Hz at 90 BPM = 1 s = 1.5 beats. Repeating the OLD mapping exactly is
                // the whole job -- guessing at it would move every chorus ever saved.
                const float chorusBeats = static_cast<float>(
                    h.getChild(3).getChild(0).getProperty("v"));
                expectWithinAbsoluteError(chorusBeats, 1.5f, 1.0e-3f,
                    "v33->v34: the chorus rate migrates through its old normalised->Hz mapping");
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
