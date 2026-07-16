#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace lockstep
{
    // Generative naming for the 5.3 identity overlay (DESIGN §23.1 / §23.4).
    // Pure, JUCE-free, deterministic: the grid IS the picker. Three modes, all
    // sharing one top-row x bottom-row compose structure. Curated word lists are
    // shown generatively (no Markov) — the shuffle picks which 8 of each list the
    // two step rows display, seeded so the same entity keeps suggesting the same
    // name until its content changes.
    enum class NameMode : std::uint8_t
    {
        AdjNoun,        // "Amber Fox"  — 8 adjectives  x 8 nouns
        Syllable,       // "Tavo"       — 8 prefixes    x 8 suffixes
        SectionLetter,  // "Bridge B"   — 8 fixed section words x A-H
    };
    inline constexpr int kNameModeCount = 3;
    inline constexpr int kNameRowCells  = 8;   // step keys per row (top / bottom)
    inline constexpr int kNameMaxChars  = 16;  // name length cap (DESIGN §23.1)

    // Cycle the mode by `delta` (+1 = next, -1 = prev), wrapping. Used by Nav </>.
    [[nodiscard]] NameMode cycleNameMode(NameMode mode, int delta) noexcept;

    // Short human label for the mode, shown in the overlay's nav banner so the
    // (sticky, per-scope) current mode is always visible.
    [[nodiscard]] const char* nameModeLabel(NameMode mode) noexcept;

    namespace namegen
    {
        // The 8 words shown in `row` (0 = top, 1 = bottom) for `mode`, chosen
        // deterministically from the master list by `seed`. For SectionLetter both
        // rows are FIXED (seed ignored): top = the 8 section types, bottom = A-H.
        std::array<std::string, kNameRowCells> rowWords(NameMode mode, int row,
                                                        std::uint32_t seed);

        // Compose the full candidate from a chosen top + bottom word. AdjNoun and
        // SectionLetter join with a space ("Amber Fox" / "Bridge B"); Syllable
        // concatenates ("Ta" + "vo" = "Tavo"). Result is capped at kNameMaxChars.
        std::string compose(NameMode mode, const std::string& top,
                            const std::string& bottom);

        // Convenience: compose directly from per-row seeds + selected cell [0,8).
        std::string composeAt(NameMode mode, std::uint32_t topSeed, int topSel,
                              std::uint32_t bottomSeed, int bottomSel);

        // Hash-seeded deterministic default for an entity whose content hashes to
        // `hash`: per-row seeds + the pre-selected cell in each row. The MODE is
        // chosen by the caller (per-scope memory), not here — so the same entity
        // suggests the same name (zero presses ⇒ just Confirm) until it changes.
        struct Seed
        {
            std::uint32_t topSeed    = 1;
            std::uint32_t bottomSeed = 1;
            int           topSel     = 0;
            int           bottomSel  = 0;
        };
        [[nodiscard]] Seed defaultSeed(std::uint32_t hash) noexcept;
    }
}
