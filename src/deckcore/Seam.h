#pragma once

#include "Medium.h"

#include <cmath>

namespace dc
{
    // Splicing a loop (DESIGN §40.3).
    //
    // A loop recorded from a performance is discontinuous at its seam: the last
    // recorded sample and the first are two unrelated moments, and playback jumps
    // between them once per iteration. That jump is a click.
    //
    // **A crossfade on playback cannot fix it.** The read is circular, so just
    // after the wrap the kernel's taps reach backwards across the seam into the
    // tail. The discontinuity lands *inside* the read window at [0, kernel), no
    // matter what gain the player applies on its way out of the loop's end. The
    // content has to be made continuous, once — and then every reader of that
    // content benefits: this deck, a Player pointed at the same pool slot, a
    // promoted WAV.
    //
    // So splice, the way a razor blade and a diagonal cut splice tape. What must
    // be true is only this: the sample that PRECEDES loop[0] in the recording must
    // also precede it in the loop. That sample is `lead[last]` — the input just
    // before the take began.
    //
    //      recorded:  [ lead ][ loop .......................... ]
    //                   -X  -1  0                            L-1
    //      spliced:   loop[L-len+i] = loop[L-len+i]*fadeOut + lead[i]*fadeIn
    //
    // At i = len-1 the loop's last sample is (almost) lead[last], which is what
    // precedes loop[0]. The wrap is continuous by construction, whatever the
    // material.
    //
    // The alteration lands on the loop's END — its ring-out — and never on its
    // head, where the downbeat and the attack live. That is why a pre-roll splice
    // beats a post-roll one: fading the take's continuation into the head would
    // buy the same continuity at the cost of the one sample a loop cannot spare.
    //
    // Equal power, because lead and tail are two different moments of the
    // performance and do not correlate.
    inline void spliceLoopEnd(Medium& loop, int sub, int loopLen,
                              const Medium& lead, int leadSub, int leadStart,
                              int len) noexcept
    {
        if (! loop.bound() || ! lead.bound()) return;
        if (loopLen <= 0 || len <= 0 || len > loopLen) return;
        if (leadStart < 0 || leadStart + len > lead.capacity()) return;

        constexpr double kHalfPi = 1.57079632679489661923;
        const int chans = std::min(loop.channels(), lead.channels());

        for (int i = 0; i < len; ++i)
        {
            const double t = (static_cast<double>(i) + 0.5) / static_cast<double>(len);
            const auto fadeIn = static_cast<float>(std::sin(t * kHalfPi));
            const auto fadeOut = static_cast<float>(std::cos(t * kHalfPi));
            const int at = loopLen - len + i;

            for (int ch = 0; ch < chans; ++ch)
            {
                const float tail = loop.read(sub, ch, at);
                const float pre = lead.read(leadSub, ch, leadStart + i);
                loop.write(sub, ch, at, tail * fadeOut + pre * fadeIn);
            }
        }
    }
}
