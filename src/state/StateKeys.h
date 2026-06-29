#pragma once
// All property names and node-type identifiers used by PluginState.cpp.
// Write and read sites must reference these constants — never pass raw string
// literals to setProperty / getProperty / getChildWithName.
//
// Dynamic names ("n" + String(ni), "track_N_swing") stay as-is in PluginState.cpp
// because they are constructed at runtime; the pattern is documented below.

namespace lockstep::keys
{
  // ── Root node ───────────────────────────────────────────────────────────────
    inline constexpr const char* kLockstepState = "LockstepState";
    inline constexpr const char* kVersion = "version";

  // ── APVTS subtree ───────────────────────────────────────────────────────────
    inline constexpr const char* kLockstep = "Lockstep";

  // ── NewHierarchy container ──────────────────────────────────────────────────
    inline constexpr const char* kNewHierarchy = "NewHierarchy";
    inline constexpr const char* kActivePiece = "activePiece";
    inline constexpr const char* kActiveSect = "activeSect";
    inline constexpr const char* kLaunchQuant = "launchQuant";

  // ── Song node ───────────────────────────────────────────────────────────────
    inline constexpr const char* kSong = "Song";
    inline constexpr const char* kIdx = "i";      // generic index field
    inline constexpr const char* kSwing = "swing";

  // ── SongTrack node ──────────────────────────────────────────────────────────
    inline constexpr const char* kSongTrack = "SongTrack";
    inline constexpr const char* kTrackIdx = "t";   // track index (Kit, SongTrack, TO, E)

  // ── Scene node ──────────────────────────────────────────────────────────────
    inline constexpr const char* kScene = "Scene";
    inline constexpr const char* kCtN = "ct_n";
    inline constexpr const char* kCtD = "ct_d";
    inline constexpr const char* kHasTs = "hasTs";         // v21: Scene/Song hasTimeSig flag
    inline constexpr const char* kSongTsN = "song_ct_n";  // v21: Song-level timeSig numerator
    inline constexpr const char* kSongTsD = "song_ct_d";  // v21: Song-level timeSig denominator
    inline constexpr const char* kSetTsN = "set_ct_n";    // v21: Set-level defaultTimeSig numerator
    inline constexpr const char* kSetTsD = "set_ct_d";    // v21: Set-level defaultTimeSig denominator
    inline constexpr const char* kHasTempo = "hasTp";     // v21: Song/Scene hasTempo flag
    inline constexpr const char* kTempoRatio = "tpRat";   // v21: Song/Scene tempoRatio (double)
    // v22: key signature (DESIGN §4.10). root(0-11)/brightness(int8)/mods(6-bit
    // mask)/symmetric(0-2). Set-level on the NewHierarchy root; Song/Scene as
    // optional overrides. kHasKs gates the Song/Scene override.
    inline constexpr const char* kHasKs = "hasKs";
    inline constexpr const char* kSetKsRoot = "set_ks_r";
    inline constexpr const char* kSetKsBri  = "set_ks_b";
    inline constexpr const char* kSetKsMod  = "set_ks_m";
    inline constexpr const char* kSetKsSym  = "set_ks_s";
    inline constexpr const char* kSongKsRoot = "song_ks_r";
    inline constexpr const char* kSongKsBri  = "song_ks_b";
    inline constexpr const char* kSongKsMod  = "song_ks_m";
    inline constexpr const char* kSongKsSym  = "song_ks_s";
    inline constexpr const char* kScKsRoot = "sc_ks_r";
    inline constexpr const char* kScKsBri  = "sc_ks_b";
    inline constexpr const char* kScKsMod  = "sc_ks_m";
    inline constexpr const char* kScKsSym  = "sc_ks_s";
    inline constexpr const char* kMutesMask = "mutesMask";

  // ── Morph snapshot maps ─────────────────────────────────────────────────────
    inline constexpr const char* kMorphA = "MorphA";
    inline constexpr const char* kMorphB = "MorphB";
    inline constexpr const char* kMorphEntry = "E";
    inline constexpr const char* kMorphSlotIdx = "s";   // slot index within E, PL/P

  // ── Phrase node ─────────────────────────────────────────────────────────────
    inline constexpr const char* kPhrase = "Phrase";
    inline constexpr const char* kLen = "len";
    inline constexpr const char* kNSel = "nsel";

  // ── TrigDefaults node ───────────────────────────────────────────────────────
    inline constexpr const char* kTrigDefaults = "TrigDefaults";
    inline constexpr const char* kNote = "note";
    inline constexpr const char* kVel = "vel";
    inline constexpr const char* kGateV = "gateV";

  // ── BaseCond node ───────────────────────────────────────────────────────────
    inline constexpr const char* kBaseCond = "BaseCond";

  // ── Steps container + step node (S) ─────────────────────────────────────────
    inline constexpr const char* kSteps = "Steps";
    inline constexpr const char* kStep = "S";
    inline constexpr const char* kTrig = "t";   // same short key as kTrackIdx — use by context
    inline constexpr const char* kMo = "mo";

  // ── Condition node (C) ──────────────────────────────────────────────────────
    inline constexpr const char* kCond = "C";
    inline constexpr const char* kP = "p";    // probability percent
    inline constexpr const char* kN = "n";    // iter numerator  (also used as step "n<i>" prefix)
    inline constexpr const char* kD = "d";    // iter denominator
    inline constexpr const char* kPd = "pd";   // prev dependency

  // ── Trig override node (TO) ─────────────────────────────────────────────────
    inline constexpr const char* kTO = "TO";
    inline constexpr const char* kNc = "nc";    // note count
  // dynamic: "n" + juce::String(ni)  — use keys::kN + String(ni)
    inline constexpr const char* kHv = "hv";    // has velocity flag
    inline constexpr const char* kV = "v";     // generic value field
    inline constexpr const char* kHg = "hg";    // has gate flag
    inline constexpr const char* kGv = "gv";    // gate value
    inline constexpr const char* kHsi = "hsi";   // has sound id flag
    inline constexpr const char* kSi = "si";    // sound id
    inline constexpr const char* kHrt = "hrt";   // has retrig flag
    inline constexpr const char* kRt = "rt";    // retrig rate

  // ── P-Lock container (PL) + param entry (P) ─────────────────────────────────
    inline constexpr const char* kPLocks = "PL";
    inline constexpr const char* kParam = "P";    // child node type within PL/FPL
    inline constexpr const char* kPLockSlot = "s";    // legacy v14: integer slot index
    inline constexpr const char* kPLockVal = "v";    // float value (both v14 and v15)
  // kParamId = "id" (see Base Params section) is reused as the v15 P-Lock string id key.
  // Fill-specific overrides (parallel to TO/PL above):
    inline constexpr const char* kFillTS = "fts";   // FillTrigState enum value
    inline constexpr const char* kFillTO = "FTO";   // fill trig override node
    inline constexpr const char* kFillPLocks = "FPL";   // fill P-Lock container

  // ── Kit node ────────────────────────────────────────────────────────────────
    inline constexpr const char* kKit = "Kit";
    inline constexpr const char* kMId = "mId";
    inline constexpr const char* kDId = "dId";
    inline constexpr const char* kMPreset = "mPreset";
    inline constexpr const char* kStaticPath = "staticPath";  // StaticMachine streamed file (DESIGN §29.2)
    inline constexpr const char* kDiv = "div";
    inline constexpr const char* kDensMus = "dMus"; // Density::Musicality (uint8)
    inline constexpr const char* kDensSel = "dSel"; // Density::DensitySelection (uint8)
    inline constexpr const char* kVelMode  = "vMd";  // VelMode (uint8): Off/Bar
    inline constexpr const char* kVelBlend = "vBl";  // VelBlend (uint8): Replace/Mix
    inline constexpr const char* kVelDepth = "vDp";  // float [0,1]
    inline constexpr const char* kVelCenter = "vCt"; // int [1,127]
    inline constexpr const char* kScaleMode = "scMd"; // v23: ScaleMode (uint8): Off/Snap/Filter

  // ── Base params container (BP) ───────────────────────────────────────────────
    inline constexpr const char* kBaseParams = "BP";
    inline constexpr const char* kParamId = "id";

  // ── Insert / MasterIns nodes ─────────────────────────────────────────────────
    inline constexpr const char* kIns = "Ins";
    inline constexpr const char* kMasterIns = "MasterIns";
    inline constexpr const char* kMasterSnd = "MasterSnd";  // 8.26: send return slot
    inline constexpr const char* kSlot = "slot";
    inline constexpr const char* kEid = "eid";
    inline constexpr const char* kBypass = "bypass";

  // ── ProjectSoundPool + SoundEntry ────────────────────────────────────────────
    inline constexpr const char* kSoundPool = "SoundPool";   // project sound-bank node
    inline constexpr const char* kSoundEntry = "SE";          // child node per entry
    inline constexpr const char* kSeName = "nm";              // entry display name
    inline constexpr const char* kSeSampleIdx = "spi";        // samplePoolIndex (-1 = none)
  // kMId / kDId / kBaseParams / kParam / kParamId / kV reused from Kit section above.

  // ── SamplePool + Entry ───────────────────────────────────────────────────────
    inline constexpr const char* kSamplePool = "SamplePool";
    inline constexpr const char* kEntry = "Entry";
    inline constexpr const char* kPath = "path";
    inline constexpr const char* kHash = "hash";

  // ── CCMappings + mapping node (M) ────────────────────────────────────────────
    inline constexpr const char* kCCMappings = "CCMappings";
    inline constexpr const char* kMapping = "M";
    inline constexpr const char* kCc = "cc";
    inline constexpr const char* kScope = "scope";
    inline constexpr const char* kCcTrack = "track";
    inline constexpr const char* kMz = "mz";
    inline constexpr const char* kApvts = "apvts";
    inline constexpr const char* kRel = "rel";
    inline constexpr const char* kScale = "scale";
    inline constexpr const char* kEnc = "enc";
    inline constexpr const char* kSlotId = "slotId";

  // ── Misc node ────────────────────────────────────────────────────────────────
    inline constexpr const char* kMisc = "Misc";
    inline constexpr const char* kFocusTrack = "focusTrack";
    inline constexpr const char* kLocalBpm = "localBpm";

  // ── Legacy (upgrade paths only) ──────────────────────────────────────────────
    inline constexpr const char* kProject = "Project";   // v2 legacy container
    inline constexpr const char* kBank = "Bank";      // v2 legacy container
    inline constexpr const char* kPattern = "Pattern";   // v2 legacy pattern node
    inline constexpr const char* kPart = "Part";      // v2 legacy part node
    inline constexpr const char* kTrackNode = "Track";     // v2 legacy track node
    inline constexpr const char* kPartTrack = "PartTrack"; // v2 legacy part-track node
    inline constexpr const char* kBaseParamsLegacy = "BaseParams"; // v2 legacy name for BP
    inline constexpr const char* kPartRef = "partRef";   // v2 legacy
    inline constexpr const char* kMachineId = "machineId"; // v2 legacy machineId field
    inline constexpr const char* kInit = "init";      // v3/v4 initialised flag
    inline constexpr const char* kGateMs = "gateMs";    // v2 legacy gate in ms
    inline constexpr const char* kGLegacy = "g";         // v2 legacy TO gate float ms
    inline constexpr const char* kGp = "gp";        // v10 legacy global-phrase ref
    inline constexpr const char* kValue = "value";     // v1 legacy APVTS param value
  // dynamic: "track_" + String(t) + "_swing"  — legacy APVTS swing key, only in upgrade path
}
