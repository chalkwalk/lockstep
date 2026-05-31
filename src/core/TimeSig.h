#pragma once

namespace lockstep
{
    // Per-Section time signature.  Drives launch-quantize grid, metronome
    // downbeat, and seeds the default length of newly-created Phrases.
    // Does NOT constrain existing Phrase lengths — polymeter is unrestricted.
    // See DESIGN §4.8.
    struct TimeSig
    {
        int numerator   = 4;
        int denominator = 4;

        // Bar length in quarter-note PPQ: numerator * (4.0 / denominator).
        // e.g. 4/4 → 4.0 PPQ, 7/8 → 3.5 PPQ.
        [[nodiscard]] double barPpq() const
        {
            return static_cast<double>(numerator) * (4.0 / static_cast<double>(denominator));
        }

        // Default phrase length in steps at 1/16-note resolution:
        //   steps = numerator * (4 / denominator), integer division.
        // e.g. 4/4 → 16, 7/8 → 7, 3/4 → 12.
        [[nodiscard]] int defaultPhraseLength() const
        {
            return numerator * (4 / denominator);
        }

        bool operator==(const TimeSig& o) const
        {
            return numerator == o.numerator && denominator == o.denominator;
        }
    };
}
