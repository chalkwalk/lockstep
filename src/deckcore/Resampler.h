#pragma once

// deckcore/Resampler.h is now a seam, not an implementation.
//
// The deck engine was extracted into chalkwalk-tape (MIT, JUCE-free) and
// Remanence grew on it: a windowed medium, a rate-sized resampling bank, a
// commit mark measured in tape rather than in memory. Keeping a second copy
// here is how two copies drift apart, and they had already started to.
//
// The names stay in `dc` deliberately. Every consumer in this project says
// `dc::Medium` and includes "deckcore/Medium.h", and rewriting several
// hundred of those would be a large diff that changed no behaviour -- which
// is exactly the diff you do not want wrapped around a library swap, because
// it buries the changes that DO alter the sound. `dc` is still a real
// namespace rather than an alias, because `dc::hermite4` (Interpolation.h)
// is ours and stays.

#include <chalkwalk/tape/Resampler.h>

namespace dc
{
    using chalkwalk::tape::Resampler;

    // ---- THIS PROJECT'S KERNEL BANK, AND WHY IT IS NOT THE LIBRARY'S ----
    //
    // chalkwalk-tape's `sharedKernels()` defaults to a cutoff guarded at 1.15x
    // below Nyquist, chosen for a tape machine that shuttles at twelve times
    // speed, where folding is the dominant defect and a duller top end is a
    // price worth paying. This project does modest varispeed and had its
    // passband settled years earlier, so it keeps 1.0.
    //
    // NOT A PREFERENCE -- A MEASUREMENT. Against the fixed sixteen-tap bank
    // this project used before the library, at rate 1.25, relative to the
    // input:
    //
    //                 before    guard 1.15    guard 1.0
    //     <= 4 kHz    same      same          same
    //       12 kHz    -0.71     -1.85         -0.49
    //       17 kHz    -6.08    -12.42         -6.09
    //
    // Guard 1.0 lands on the old behaviour to within a few tenths of a decibel
    // while keeping what the library actually brought: a bank sized by rate,
    // so alias rejection improves with speed instead of thinning out.
    //
    // At rate 1.0 and below every column above is IDENTICAL -- bit-identical,
    // measured -- so this choice is only audible where the machine speeds up.
    //
    // Deviating is a decision for this project to take deliberately, by
    // changing the two numbers here and listening. It is not something to
    // inherit from whatever a sibling's tape deck happens to want.
    inline const Resampler& kernels()
    {
        static const Resampler r(12.0, 1.0);
        return r;
    }
}
