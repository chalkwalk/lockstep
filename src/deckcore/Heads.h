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

    // ---- THE HEADS CARRY THIS PROJECT'S BANK BY CONSTRUCTION ----
    //
    // Not a `using`, and not a `setKernels(kernels())` at every call site. The
    // library's heads default to `sharedKernels()`, whose cutoff is guarded for
    // a tape machine that shuttles at twelve times speed; this project keeps its
    // own guard (see Resampler.h for the measurement).
    //
    // The first version of this DID sprinkle `setKernels` -- seven call sites
    // across two machines and three test files. It worked, and it was wrong:
    // the eighth head somebody adds silently gets the tape machine's passband,
    // nothing fails, and the difference is a fraction of a decibel above 12 kHz
    // at rates over unity. That is not a defect anyone finds by listening for
    // it. Making the bank a property of the TYPE means a head cannot be created
    // without it.
    //
    // Cheap: `kernels()` is a reference to one function-local static, so these
    // add a pointer store to construction and nothing to the audio path.
    struct ReadHead : chalkwalk::tape::ReadHead
    {
        ReadHead() noexcept
        {
            // `dc::`, not bare `kernels()`: unqualified, that binds to the
            // INHERITED `Head::kernels()` accessor -- which compiles, returns
            // the default bank, and makes this constructor a no-op.
            setKernels(dc::kernels());
        }
    };

    struct WriteHead : chalkwalk::tape::WriteHead
    {
        WriteHead() noexcept
        {
            // `dc::`, not bare `kernels()`: unqualified, that binds to the
            // INHERITED `Head::kernels()` accessor -- which compiles, returns
            // the default bank, and makes this constructor a no-op.
            setKernels(dc::kernels());
        }
    };
}
