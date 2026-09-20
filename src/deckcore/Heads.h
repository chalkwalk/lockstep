#pragma once

// deckcore/Heads.h is now a seam, not an implementation.
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
#include "ChannelView.h"
#include "Medium.h"
#include "Resampler.h"

#include <chalkwalk/tape/Heads.h>

namespace dc
{
    using chalkwalk::tape::sharedKernels;
    using chalkwalk::tape::Head;
    using chalkwalk::tape::ReadHead;
    using chalkwalk::tape::WriteHead;
}
