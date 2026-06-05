#pragma once
#include <cstdint>
#include <juce_gui_basics/juce_gui_basics.h>

namespace lockstep::theme
{
    // =========================================================================
    // Colour vocabulary (colour-rethink, DESIGN §6.6)
    //
    // One hue per modality; resting/active/accent are that hue at three
    // stepped saturation+brightness levels:
    //   resting  ~ S 0.80  B 0.35   (clearly hued, not near-black)
    //   active   ~ S 0.72  B 0.80   (full brightness on press / mode-on)
    //   accent   ~ S 0.60  B 0.94   (border highlight)
    //
    // Role-neighbourhood hue map (across modalities):
    //   Func                       amber  ~36°  (qualifier / universal)
    //   Structural scopes (cool arc)
    //     Scene                    green  ~150°
    //     Track                    cyan   ~192°
    //     Phrase                   indigo ~225°
    //     Song                     gold   ~50°  (distinct from amber Func)
    //   Morph                      magenta~315°  (expressive crossfader; apart from cool arc)
    //   Performance (high-alert)
    //     Mute                     red    ~2°
    //     PMute (scene mute)       rose   ~345°
    //     Fill                     chartreuse ~78°
    //   Verbs                      slate  H~215 S 0.18  (neutral rest; conventional on-active)
    //   Edit sub-modes             own bands (azure/violet/teal/lime/slate)
    //   Sections                   steel-teal H~185 S 0.45
    //   Ambient                    Nav=slate, Tap=grey
    // =========================================================================

    // -------------------------------------------------------------------------
    // Func key — amber ~36°
    // -------------------------------------------------------------------------
    inline constexpr uint32_t kFuncInactive = 0xFF2A1A04u;   // dark amber
    inline constexpr uint32_t kFuncActive   = 0xFFC07800u;   // saturated amber
    inline constexpr uint32_t kFuncAccent   = 0xFFE8A820u;   // bright amber

    // -------------------------------------------------------------------------
    // Structural scope modifiers — cool arc (green → cyan → indigo → gold)
    // Each modifier uses its own scope hue at all three states; the
    // isMode ? scope : generic-borrow pattern is gone.
    // -------------------------------------------------------------------------

    // Track  (cyan  ~192°)
    inline constexpr uint32_t kScopeTrack    = 0xFF30A0C0u;  // active / scope glow
    inline constexpr uint32_t kScopeTrackDim = 0xFF133A46u;  // resting (B~35%)
    inline constexpr uint32_t kScopeTrackAcc = 0xFF58CCE8u;  // accent border

    // Scene  (green ~150°)
    inline constexpr uint32_t kScopeScene    = 0xFF20A060u;
    inline constexpr uint32_t kScopeSceneDim = 0xFF0D3C24u;
    inline constexpr uint32_t kScopeSceneAcc = 0xFF3EC880u;

    // Phrase (indigo ~225°)
    inline constexpr uint32_t kScopePhrase   = 0xFF7050C8u;  // slightly shifted from old 8040C0
    inline constexpr uint32_t kScopePhraseDim= 0xFF221448u;  // resting
    inline constexpr uint32_t kScopePhraseAcc= 0xFF9880E8u;

    // Song   (gold  ~50°)  — brighter, yellower gold to clear amber Func + chartreuse Fill
    inline constexpr uint32_t kScopeSong     = 0xFFD8B020u;
    inline constexpr uint32_t kScopeSongDim  = 0xFF403000u;  // resting
    inline constexpr uint32_t kScopeSongAcc  = 0xFFF0D050u;

    // -------------------------------------------------------------------------
    // Morph — magenta ~315° (expressive A/B crossfader; apart from cool arc)
    // -------------------------------------------------------------------------
    inline constexpr uint32_t kScopeMorph    = 0xFFBE3898u;
    inline constexpr uint32_t kScopeMorphDim = 0xFF3C1230u;
    inline constexpr uint32_t kScopeMorphAcc = 0xFFE060C0u;

    // -------------------------------------------------------------------------
    // Performance modifiers — high-alert character (Mute removes, Fill adds)
    // -------------------------------------------------------------------------

    // Mute   (red ~2°)
    inline constexpr uint32_t kScopeMute    = 0xFFC03030u;
    inline constexpr uint32_t kScopeMuteDim = 0xFF3E0E0Eu;
    inline constexpr uint32_t kScopeMuteAcc = 0xFFE85050u;

    // PMute  (rose ~345°) — scene mute; distinct from Func amber
    inline constexpr uint32_t kScopePMute   = 0xFFBE2858u;
    inline constexpr uint32_t kScopePMuteDim= 0xFF3A0C1Cu;
    inline constexpr uint32_t kScopePMuteAcc= 0xFFE85080u;

    // Fill   (chartreuse ~78°)
    inline constexpr uint32_t kScopeFill    = 0xFF82C018u;
    inline constexpr uint32_t kScopeFillDim = 0xFF283C08u;
    inline constexpr uint32_t kScopeFillAcc = 0xFFAAE030u;

    // Machine picker (lime ~95°) — Func+Track / Part+SRC
    inline constexpr uint32_t kScopeMachine = 0xFF50C030u;

    // -------------------------------------------------------------------------
    // Edit sub-mode identities (grid re-skins; mutually exclusive with scope-hold)
    // -------------------------------------------------------------------------
    inline constexpr uint32_t kScopeNoteEdit = 0xFF2888D8u;  // azure  ~205°
    inline constexpr uint32_t kScopePLock    = 0xFF6040C0u;  // violet ~288°

    // -------------------------------------------------------------------------
    // Verb keys — neutral slate (H~215, S~0.18) at rest;
    // conventional colour on-active (Record→red, Play→green, Clear→warm-red)
    // -------------------------------------------------------------------------
    inline constexpr uint32_t kVerbInactive   = 0xFF141C22u;  // near-black slate
    inline constexpr uint32_t kVerbActive     = 0xFF2A3C50u;  // mid slate (unpressed / idle)
    inline constexpr uint32_t kVerbAccent     = 0xFF4880A8u;  // slate-blue accent

    // VerbRecord armed: warm red
    inline constexpr uint32_t kVerbRecActive  = 0xFFA03030u;
    inline constexpr uint32_t kVerbRecAccent  = 0xFFD04444u;
    // VerbRecord overdub armed: amber
    inline constexpr uint32_t kVerbODActive   = 0xFFD2821Eu;
    inline constexpr uint32_t kVerbODAccent   = 0xFFE0A040u;

    // VerbPlay playing: green
    inline constexpr uint32_t kVerbPlayActive = 0xFF208040u;
    inline constexpr uint32_t kVerbPlayAccent = 0xFF30C060u;

    // VerbClear / Delete / Panic: warm-orange (destructive; pushed off Record red)
    inline constexpr uint32_t kVerbClearActive= 0xFF8A5A1Cu;
    inline constexpr uint32_t kVerbClearAccent= 0xFFBB6030u;

    // Snapshot (VerbYes): violet-blue (distinct from slate verbs)
    inline constexpr uint32_t kVerbSnapActive = 0xFF3A44A0u;
    inline constexpr uint32_t kVerbSnapAccent = 0xFF5060C8u;

    // -------------------------------------------------------------------------
    // Section keys — steel-teal (H~185, S~0.45)
    // -------------------------------------------------------------------------
    inline constexpr uint32_t kSecInactive  = 0xFF0E2020u;
    inline constexpr uint32_t kSecActive    = 0xFF206060u;
    inline constexpr uint32_t kSecAccent    = 0xFF3EC8C8u;

    // -------------------------------------------------------------------------
    // Navigation keys — quiet slate
    // -------------------------------------------------------------------------
    inline constexpr uint32_t kNavInactive  = 0xFF0E1E30u;
    inline constexpr uint32_t kNavActive    = 0xFF3060A0u;
    inline constexpr uint32_t kNavAccent    = 0xFF3060A0u;

    // -------------------------------------------------------------------------
    // Tap Tempo key — neutral grey
    // -------------------------------------------------------------------------
    inline constexpr uint32_t kTapInactive  = 0xFF1A1A20u;
    inline constexpr uint32_t kTapActive    = 0xFF505060u;
    inline constexpr uint32_t kTapAccent    = 0xFF8090A0u;

    // -------------------------------------------------------------------------
    // Step-grid colours (aligned to family vocabulary where meaningful)
    // -------------------------------------------------------------------------
    inline constexpr uint32_t kStepActive      = 0xFF50B478u;  // green — certain fire
    inline constexpr uint32_t kStepFillAdd     = 0xFFF08030u;  // warm orange — additive fill trig
    inline constexpr uint32_t kStepFillSuppress= 0xFF3060A0u;  // cool blue — suppressed in fill
    inline constexpr uint32_t kStepFillPLock   = 0xFF40A0D0u;  // cyan — fill-layer P-Lock dot
    inline constexpr uint32_t kStepInactive    = 0xFF2D3741u;  // dark — in-range, no trig
    inline constexpr uint32_t kStepOutRange    = 0xFF12151Au;  // near-black — out of range (darkened: separate from empty-in-range)
    inline constexpr uint32_t kStepPlayhead    = 0xFFFFCC44u;  // amber border
    inline constexpr uint32_t kStepHeld        = 0xFFFFFFFFu;  // white — held-step border
    inline constexpr uint32_t kStepPLock       = 0xFF8060E0u;  // violet — P-Lock dot

    // Legacy alias kept for stray references (points at new Fill name)
    inline constexpr uint32_t kStepFillOnly    = 0xFF7B4EC0u;

    // -------------------------------------------------------------------------
    // Named decoration constants — prevent literal scatter in paint paths
    // -------------------------------------------------------------------------
    inline constexpr uint32_t kAmberStrip      = 0xFFD0A020u;  // compound-chord top strip
    inline constexpr uint32_t kHomeAmber       = 0xFFFFC020u;  // home-position border glow

    // -------------------------------------------------------------------------
    // Default / no-scope held: light grey (used by step-grid scope glow fallback)
    // -------------------------------------------------------------------------
    inline constexpr uint32_t kScopeStep       = 0xFF8898A8u;

    // -------------------------------------------------------------------------
    // Helper: build a juce::Colour from a packed ARGB uint32
    // -------------------------------------------------------------------------
    inline juce::Colour col(uint32_t argb) noexcept
    {
        return juce::Colour(argb);
    }
}
