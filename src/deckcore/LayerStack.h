#pragma once

#include "Medium.h"

#include <cstdint>

namespace dc
{
    // Overdub, undo, decay, and punch (DESIGN §40.3).
    //
    // A take is committed content plus at most one **fresh layer**. The write
    // head is add-only, so a layer is where the current pass lands; at each loop
    // iteration (or at the end of a punch) the layer folds down into the
    // committed content, optionally attenuating what was there — which is what a
    // sound-on-sound looper's "decay" is.
    //
    // **Punch is not a fourth verb.** It is a layer whose contribution is gated
    // to a span: outside the span the fold leaves the committed take exactly as
    // it was, so punching out is never destructive and undo always has something
    // to restore. That is what keeps fence #8 satisfied while still feeling like
    // tape. A *replace* punch additionally runs an EraseHead inside the span
    // ahead of the write; the fold itself never subtracts.
    //
    // Undo is one level deep, by construction rather than by budget: it is the
    // committed content as it stood before the current layer began. Two levels
    // would mean deciding how much tape to keep, and the answer to that is a
    // take-group on disk, not more RAM.
    //
    // The stack owns no storage. Three media of identical geometry are bound to
    // it — committed, layer, undo — and the host decides where they live.
    class LayerStack
    {
    public:
        // A span of the medium, in resolved medium indices. `begin >= end` means
        // the span wraps the seam of a circular medium (a punch across the loop
        // point is an ordinary punch).
        struct Span
        {
            int begin = 0;
            int end = 0;
            bool bounded = false;  // unbounded: the whole medium

            [[nodiscard]] bool contains(int k) const noexcept
            {
                if (! bounded) return true;
                if (begin <= end) return k >= begin && k < end;
                return k >= begin || k < end;  // wrapped
            }
        };

        void bind(Medium* committed, Medium* layer, Medium* undo) noexcept
        {
            committed_ = committed;
            layer_ = layer;
            undo_ = undo;
            layerActive_ = false;
            canUndo_ = false;
        }

        [[nodiscard]] bool bound() const noexcept
        {
            return committed_ != nullptr && layer_ != nullptr;
        }

        [[nodiscard]] Medium* committed() const noexcept { return committed_; }
        [[nodiscard]] Medium* layer() const noexcept { return layer_; }

        [[nodiscard]] bool layerActive() const noexcept { return layerActive_; }
        [[nodiscard]] bool canUndo() const noexcept { return canUndo_; }
        [[nodiscard]] const Span& punch() const noexcept { return punch_; }

        // Open a fresh layer over `sub`, snapshotting the committed content so
        // the pass can be undone. An unbounded span is an ordinary overdub; a
        // bounded one is a punch.
        void beginLayer(int sub, Span span) noexcept;
        void beginLayer(int sub) noexcept { beginLayer(sub, Span{}); }

        // Throw the layer away. The committed take never saw it.
        void discardLayer(int sub) noexcept;

        // committed = committed * decay + layer, inside the punch span only, then
        // clear the layer. `decay` of 1 is a lossless sound-on-sound stack; less
        // is a loop that forgets. Outside the span nothing is read or written.
        void fold(int sub, float decay) noexcept;

        // Restore the committed content to what it was when the layer opened.
        // Discards any open layer: undoing mid-pass undoes the pass.
        void undoLast(int sub) noexcept;

        // Spot read of the audible content (committed + the gated layer). The
        // audio path does not use this — it runs a head over each medium and sums,
        // which is the same thing at kernel resolution — but tests and metering do.
        [[nodiscard]] float read(int sub, int ch, std::int64_t idx) const noexcept;

    private:
        void snapshot(int sub) noexcept;

        Medium* committed_ = nullptr;
        Medium* layer_ = nullptr;
        Medium* undo_ = nullptr;
        Span punch_{};
        bool layerActive_ = false;
        bool canUndo_ = false;
    };
}
