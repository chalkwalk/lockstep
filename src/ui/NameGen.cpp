#include "NameGen.h"

#include <array>
#include <vector>

namespace lockstep
{
    NameMode cycleNameMode(NameMode mode, int delta) noexcept
    {
        int m = static_cast<int>(mode) + delta;
        m %= kNameModeCount;
        if (m < 0) m += kNameModeCount;
        return static_cast<NameMode>(m);
    }

    namespace namegen
    {
        // ── Curated word lists (shown generatively; the shuffle picks 8) ────────
        // Kept deliberately evocative but neutral — the headline use is grouping
        // scenes "by eye" (intro / chorus / drop), not literal description.

        static const std::vector<std::string>& adjectives()
        {
            static const std::vector<std::string> a = {
                "Amber", "Azure", "Bright", "Bold", "Calm", "Cobalt", "Coral", "Crimson",
                "Dark", "Deep", "Dusty", "Electric", "Ember", "Faded", "Fierce", "Frozen",
                "Gilded", "Golden", "Grey", "Hazy", "Hidden", "Hollow", "Icy", "Idle",
                "Jade", "Lucid", "Lunar", "Molten", "Muted", "Neon", "Northern", "Ochre",
                "Onyx", "Pale", "Polar", "Prism", "Quiet", "Rapid", "Raw", "Restless",
                "Rogue", "Rust", "Sable", "Scarlet", "Silent", "Silver", "Slow", "Solar",
                "Static", "Steel", "Storm", "Sunken", "Swift", "Tidal", "Umber", "Velvet",
                "Vivid", "Warm", "Wild", "Winter", "Wired", "Woven", "Zephyr", "Zero",
            };
            return a;
        }

        static const std::vector<std::string>& nouns()
        {
            static const std::vector<std::string> n = {
                "Anchor", "Ash", "Beacon", "Bloom", "Canyon", "Cinder", "Coil", "Comet",
                "Current", "Dawn", "Delta", "Dune", "Echo", "Ember", "Fable", "Falcon",
                "Fern", "Flare", "Fox", "Glade", "Glow", "Grove", "Harbor", "Haze",
                "Horizon", "Kite", "Lantern", "Ledge", "Lily", "Lynx", "Meadow", "Mesa",
                "Mirror", "Moth", "Nebula", "Ochre", "Orbit", "Otter", "Peak", "Pier",
                "Pine", "Prism", "Pulse", "Quartz", "Raven", "Reef", "Ridge", "River",
                "Signal", "Spark", "Spire", "Stag", "Stone", "Surge", "Thorn", "Tide",
                "Torch", "Vale", "Vault", "Vertex", "Willow", "Wolf", "Wren", "Zenith",
            };
            return n;
        }

        static const std::vector<std::string>& prefixes()
        {
            static const std::vector<std::string> p = {
                "Ta", "Ve", "Lo", "Mi", "Ka", "Ru", "So", "Ne",
                "Za", "Ori", "Fen", "Bry", "Qui", "Nex", "Ash", "Vor",
            };
            return p;
        }

        static const std::vector<std::string>& suffixes()
        {
            static const std::vector<std::string> s = {
                "vo", "na", "lex", "ri", "mo", "dus", "ka", "sil",
                "ren", "tia", "oq", "bel", "nyx", "ur", "eth", "lo",
            };
            return s;
        }

        // Fixed 8-of-8 rows for SectionLetter (no shuffle).
        static const std::array<std::string, kNameRowCells>& sectionWords()
        {
            static const std::array<std::string, kNameRowCells> s = {
                "Intro", "Verse", "Chorus", "Bridge", "Drop", "Break", "Outro", "Fill"
            };
            return s;
        }

        static const std::array<std::string, kNameRowCells>& letters()
        {
            static const std::array<std::string, kNameRowCells> l = {
                "A", "B", "C", "D", "E", "F", "G", "H"
            };
            return l;
        }

        // Pick 8 DISTINCT entries from `list` by `seed` (partial Fisher-Yates via
        // an LCG). Deterministic: same seed ⇒ same 8 in the same order.
        static std::array<std::string, kNameRowCells>
        pickEight(const std::vector<std::string>& list, std::uint32_t seed)
        {
            std::array<std::string, kNameRowCells> out{};
            const int n = static_cast<int>(list.size());
            if (n == 0) return out;

            std::vector<int> pool(static_cast<std::size_t>(n));
            for (int i = 0; i < n; ++i) pool[static_cast<std::size_t>(i)] = i;

            std::uint32_t s = seed ? seed : 1u;
            for (int i = 0; i < kNameRowCells; ++i)
            {
                if (i >= n)
                {
                    // List shorter than 8 (should not happen for our lists): wrap.
                    out[static_cast<std::size_t>(i)] =
                        list[static_cast<std::size_t>(i % n)];
                    continue;
                }
                s = s * 1664525u + 1013904223u;
                const int span = n - i;
                const int j = i + static_cast<int>(s % static_cast<std::uint32_t>(span));
                std::swap(pool[static_cast<std::size_t>(i)], pool[static_cast<std::size_t>(j)]);
                out[static_cast<std::size_t>(i)] =
                    list[static_cast<std::size_t>(pool[static_cast<std::size_t>(i)])];
            }
            return out;
        }

        std::array<std::string, kNameRowCells> rowWords(NameMode mode, int row,
                                                        std::uint32_t seed)
        {
            switch (mode)
            {
                case NameMode::AdjNoun:
                    return pickEight(row == 0 ? adjectives() : nouns(), seed);
                case NameMode::Syllable:
                    return pickEight(row == 0 ? prefixes() : suffixes(), seed);
                case NameMode::SectionLetter:
                    return row == 0 ? sectionWords() : letters();
            }
            return {};
        }

        std::string compose(NameMode mode, const std::string& top,
                            const std::string& bottom)
        {
            std::string out;
            switch (mode)
            {
                case NameMode::AdjNoun:
                case NameMode::SectionLetter:
                    out = top + " " + bottom;
                    break;
                case NameMode::Syllable:
                    out = top + bottom;
                    break;
            }
            if (static_cast<int>(out.size()) > kNameMaxChars)
                out.resize(static_cast<std::size_t>(kNameMaxChars));
            return out;
        }

        std::string composeAt(NameMode mode, std::uint32_t topSeed, int topSel,
                              std::uint32_t bottomSeed, int bottomSel)
        {
            const auto top = rowWords(mode, 0, topSeed);
            const auto bot = rowWords(mode, 1, bottomSeed);
            const int ti = (topSel < 0 || topSel >= kNameRowCells) ? 0 : topSel;
            const int bi = (bottomSel < 0 || bottomSel >= kNameRowCells) ? 0 : bottomSel;
            return compose(mode, top[static_cast<std::size_t>(ti)],
                           bot[static_cast<std::size_t>(bi)]);
        }

        Seed defaultSeed(std::uint32_t hash) noexcept
        {
            Seed sd;
            // Two decorrelated non-zero row seeds derived from the content hash, so
            // reshuffling one row (bumping its seed) never disturbs the other.
            sd.topSeed    = (hash * 2654435761u) | 1u;
            sd.bottomSeed = (hash * 40503u + 0x9E3779B9u) | 1u;
            sd.topSel     = static_cast<int>(hash % kNameRowCells);
            sd.bottomSel  = static_cast<int>((hash / kNameRowCells) % kNameRowCells);
            return sd;
        }
    }
}
