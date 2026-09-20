#pragma once

// deckcore/Medium.h is now a seam, not an implementation.
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

#include <chalkwalk/tape/Medium.h>

namespace dc
{
    using chalkwalk::tape::Topology;
    using chalkwalk::tape::Depth;
    using chalkwalk::tape::Store;
    using chalkwalk::tape::Medium;
}
