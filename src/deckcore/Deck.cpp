#include "Deck.h"

namespace dc
{
    DeckEdge Deck::firePending() noexcept
    {
        DeckEdge e;
        switch (pendingAction_)
        {
            case Pending::None:
                break;
            case Pending::StartRecording:
                state_ = DeckState::Recording;
                e.startRecording = true;
                break;
            case Pending::Stop:
                state_ = DeckState::Stopped;
                break;
            case Pending::Replay:
                state_ = DeckState::Playing;
                e.restartPlayback = true;
                break;
            case Pending::CloseRecording:
                state_ = DeckState::Playing;
                e.closeRecording = true;
                break;
        }
        pendingAction_ = Pending::None;
        return e;
    }

    DeckEdge Deck::applyCommand(DeckCmd c, bool immediate, const TransportSnapshot& t,
                                bool haveTake) noexcept
    {
        DeckEdge e;

        // Edge timing follows the one shared launch grid (§25). A period of 0 —
        // Free / Instant — fires now. A double-tap forces the edge now, overriding
        // the quantize and cancelling anything pending: the universal override.
        const double period = t.launchQuantPeriodSamples;
        const bool quantStart = ! immediate && period > 0.0;  // may arm while stopped
        const bool quantPlay = ! immediate && period > 0.0 && t.running;

        switch (c)
        {
            case DeckCmd::None:
                break;

            case DeckCmd::RecordCycle:
                switch (state_)
                {
                    case DeckState::Idle:
                    case DeckState::Stopped:
                        if (quantStart)
                        {
                            state_ = DeckState::Armed;
                            pendingAction_ = Pending::StartRecording;
                        }
                        else
                        {
                            state_ = DeckState::Recording;
                            e.startRecording = true;
                        }
                        break;

                    case DeckState::Armed:
                        // Double-tap starts now; a single tap cancels the arm.
                        if (immediate)
                        {
                            state_ = DeckState::Recording;
                            e.startRecording = true;
                            pendingAction_ = Pending::None;
                        }
                        else
                        {
                            state_ = haveTake ? DeckState::Stopped : DeckState::Idle;
                            pendingAction_ = Pending::None;
                        }
                        break;

                    case DeckState::Recording:
                        // Quantized punch-out: arm the close for the next boundary
                        // rather than cutting instantly.
                        if (quantPlay)
                        {
                            pendingAction_ = Pending::CloseRecording;
                        }
                        else
                        {
                            state_ = DeckState::Playing;
                            e.closeRecording = true;
                        }
                        break;

                    case DeckState::Playing:
                        if (haveTake)
                        {
                            state_ = DeckState::Overdubbing;
                            e.beginOverdub = true;
                        }
                        break;

                    case DeckState::Overdubbing:
                        state_ = DeckState::Playing;
                        e.endOverdub = true;
                        break;
                }
                break;

            case DeckCmd::PlayStop:
                switch (state_)
                {
                    case DeckState::Recording:
                        if (quantPlay)
                        {
                            pendingAction_ = Pending::CloseRecording;
                        }
                        else
                        {
                            state_ = DeckState::Playing;
                            e.closeRecording = true;
                        }
                        break;

                    case DeckState::Playing:
                    case DeckState::Overdubbing:
                        if (quantPlay) pendingAction_ = Pending::Stop;
                        else state_ = DeckState::Stopped;
                        break;

                    case DeckState::Stopped:
                        if (haveTake)
                        {
                            if (quantPlay)
                            {
                                pendingAction_ = Pending::Replay;
                            }
                            else
                            {
                                state_ = DeckState::Playing;
                                e.restartPlayback = true;
                            }
                        }
                        break;

                    case DeckState::Armed:
                        state_ = haveTake ? DeckState::Stopped : DeckState::Idle;
                        pendingAction_ = Pending::None;
                        break;

                    case DeckState::Idle:
                        break;
                }
                break;

            case DeckCmd::Clear:
                e.clear = true;
                state_ = DeckState::Idle;
                pendingAction_ = Pending::None;
                break;

            case DeckCmd::Undo:
                // Undo lands on a playing take: the layer it removes was recorded
                // over one.
                if (haveTake)
                {
                    e.undo = true;
                    state_ = DeckState::Playing;
                }
                break;

            case DeckCmd::Halve:
                if (haveTake && (state_ == DeckState::Playing || state_ == DeckState::Overdubbing
                                 || state_ == DeckState::Stopped))
                    e.halve = true;
                break;

            case DeckCmd::Double:
                if (haveTake && (state_ == DeckState::Playing || state_ == DeckState::Overdubbing
                                 || state_ == DeckState::Stopped))
                    e.doubleLen = true;
                break;
        }

        return e;
    }
}
