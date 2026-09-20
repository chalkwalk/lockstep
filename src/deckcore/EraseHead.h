#pragma once

// deckcore/EraseHead.h is now a seam, not an implementation.
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

// The sibling seams for what this header needs, so a consumer that includes
// only this one still sees every name it used to in `dc`.
#include "Heads.h"

#include <chalkwalk/tape/EraseHead.h>

namespace dc
{
    // Same reasoning as the heads in Heads.h: the erase gap must be measured
    // against the bank the WRITE head deposits with, and that is this project's
    // rather than the library's. `minGapFor(rate)` on the library type reads
    // `sharedKernels()` and would hand back the tape machine's number.
    //
    // NOT `kMinGap`, EVER. It is the bank's worst case -- 8 while this project
    // owned a fixed sixteen-tap bank, 128 once the bank is sized by rate -- so
    // every caller that asked for "the erase gap" silently asked for one
    // sixteen times too large. Nine HeadsTest cases caught it; the production
    // site in TapeMachine.cpp had the same bug where no test could see it. The
    // constant is deliberately not re-exported here.
    struct EraseHead : chalkwalk::tape::EraseHead
    {
        EraseHead() noexcept
        {
            // `dc::`, not bare `kernels()`: unqualified, that binds to the
            // INHERITED `Head::kernels()` accessor -- which compiles, returns
            // the default bank, and makes this constructor a no-op.
            setKernels(dc::kernels());
        }

        [[nodiscard]] static double minGapFor(double rate) noexcept
        {
            return chalkwalk::tape::EraseHead::minGapFor(rate, dc::kernels());
        }

        using chalkwalk::tape::EraseHead::leadFor;
    };
}
