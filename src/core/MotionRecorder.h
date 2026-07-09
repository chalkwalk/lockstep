#pragma once

#include "Sequence.h"   // kNumTracks
#include <array>
#include <atomic>

namespace lockstep
{
    // MotionRecorder — live P-Lock (motion) recording. (ROADMAP 9.27 A3.)
    //
    // While the transport runs, the track is record-armed, and no step is held,
    // turning an encoder records that motion into the pattern: the value is written
    // as a P-Lock into every step the playhead crosses while the gesture lasts. One
    // pass round the loop records one loop; keep turning and the second pass
    // overwrites the first. That overwrite is the escape hatch, not a mode — the
    // Volca / Liven idiom. Steps you never cross keep whatever locks they had.
    //
    // The gesture's extent is a **motion window** per slot: a write opens it, and it
    // closes `windowSeconds` after the last write. Holding a knob still therefore
    // keeps painting the steps you pass (you are still "recording" that value); let
    // go and it stops within a beat or so.
    //
    // Threading. arm() is called from wherever a parameter write originates — the
    // Manipulation Zone on the message thread, a MIDI CC on the audio thread — and
    // does nothing but publish a value and a timestamp. Every *painting* decision,
    // and every lock write, happens on the audio thread inside the step scan. State
    // is a fixed table of atomics: no allocation, no locks, nothing to tear.
    //
    // This class knows nothing about steps' contents. The caller's sink decides what
    // a "lock" does — in the processor it sets the step override and, on a step with
    // no trig, promotes it to a trigless (lock-only) trig so the motion is audible
    // (DESIGN §30; without that promotion an override on a silent step is inert).
    class MotionRecorder
    {
    public:
        // Slots per track: machine params plus the post-machine FLTR/CHANNEL/ENV/
        // insert range. Comfortably above any real schema; a slot past the end
        // simply cannot be motion-recorded (and is bounds-checked, never UB).
        static constexpr int kMaxSlots = 128;

        // Default gesture tail: a knob left still for this long stops painting.
        static constexpr double kDefaultWindowSeconds = 0.15;

        struct Lock
        {
            int   track = 0;
            int   slot = 0;
            int   step = 0;
            float value = 0.0f;
        };

        void setWindowSeconds(double s) { windowSeconds_ = (s > 0.0) ? s : kDefaultWindowSeconds; }
        [[nodiscard]] double windowSeconds() const { return windowSeconds_; }

        // Any thread. Opens or refreshes this slot's motion window.
        void arm(int track, int slot, float value, double nowSec)
        {
            auto* s = at(track, slot);
            if (s == nullptr) return;
            s->value.store(value, std::memory_order_relaxed);
            s->lastSec.store(nowSec, std::memory_order_relaxed);
            s->dirty.store(true, std::memory_order_relaxed);
            s->open.store(true, std::memory_order_release);
        }

        // Audio thread. Close every window on this track whose last motion has aged
        // out. Call once per block, before painting.
        void closeExpired(int track, double nowSec)
        {
            if (!validTrack(track)) return;
            for (auto& s : slots_[static_cast<std::size_t>(track)])
            {
                if (!s.open.load(std::memory_order_acquire)) continue;
                if (nowSec - s.lastSec.load(std::memory_order_relaxed) > windowSeconds_)
                    s.open.store(false, std::memory_order_release);
            }
        }

        // Audio thread. The playhead entered `stepIdx`: every open window paints its
        // current value there, whether or not the knob is still moving. This is what
        // makes a held-still knob record across the steps it passes.
        template <class Sink>
        void paintStep(int track, int stepIdx, Sink&& sink)
        {
            paint(track, stepIdx, false, static_cast<Sink&&>(sink));
        }

        // Audio thread. Paint only the windows whose value moved since their last
        // paint, onto the step the playhead is currently within. This is what makes
        // a turn audible on the step you are already standing on, rather than only
        // from the next step onward.
        template <class Sink>
        void paintDirty(int track, int stepIdx, Sink&& sink)
        {
            paint(track, stepIdx, true, static_cast<Sink&&>(sink));
        }

        [[nodiscard]] bool isOpen(int track, int slot) const
        {
            const auto* s = at(track, slot);
            return s != nullptr && s->open.load(std::memory_order_acquire);
        }

        [[nodiscard]] int openCount(int track) const
        {
            if (!validTrack(track)) return 0;
            int n = 0;
            for (const auto& s : slots_[static_cast<std::size_t>(track)])
                if (s.open.load(std::memory_order_acquire)) ++n;
            return n;
        }

        void resetTrack(int track)
        {
            if (!validTrack(track)) return;
            for (auto& s : slots_[static_cast<std::size_t>(track)])
                s.open.store(false, std::memory_order_release);
        }

        void reset()
        {
            for (int t = 0; t < kNumTracks; ++t) resetTrack(t);
        }

    private:
        struct Slot
        {
            std::atomic<float>  value{ 0.0f };
            std::atomic<double> lastSec{ 0.0 };
            std::atomic<bool>   open{ false };
            std::atomic<bool>   dirty{ false };
        };

        static bool validTrack(int t) { return t >= 0 && t < kNumTracks; }

        Slot* at(int track, int slot)
        {
            if (!validTrack(track) || slot < 0 || slot >= kMaxSlots) return nullptr;
            return &slots_[static_cast<std::size_t>(track)][static_cast<std::size_t>(slot)];
        }
        const Slot* at(int track, int slot) const
        {
            return const_cast<MotionRecorder*>(this)->at(track, slot);
        }

        template <class Sink>
        void paint(int track, int stepIdx, bool dirtyOnly, Sink&& sink)
        {
            if (!validTrack(track) || stepIdx < 0) return;
            auto& row = slots_[static_cast<std::size_t>(track)];
            for (int slot = 0; slot < kMaxSlots; ++slot)
            {
                auto& s = row[static_cast<std::size_t>(slot)];
                if (!s.open.load(std::memory_order_acquire)) continue;
                if (dirtyOnly && !s.dirty.load(std::memory_order_relaxed)) continue;
                s.dirty.store(false, std::memory_order_relaxed);
                sink(Lock{ track, slot, stepIdx, s.value.load(std::memory_order_relaxed) });
            }
        }

        std::array<std::array<Slot, kMaxSlots>, kNumTracks> slots_{};
        double windowSeconds_ = kDefaultWindowSeconds;
    };
}
