#pragma once

namespace lockstep
{
    // Tracks whether a step is held (by QWERTY or held MIDI note) such that
    // incoming CC / parameter writes should land in the Step Override rather
    // than the Track Base. Wired up in M5.
    class EditContext
    {
    public:
        bool isActiveForEditing() const { return active_; }
        int  heldStepIndex() const      { return heldStep_; }
        int  heldTrackIndex() const     { return heldTrack_; }
        int  activeSlot() const         { return activeSlot_; }

        void setActiveSlot(int slot)    { activeSlot_ = slot; }

        void hold(int trackIndex, int stepIndex)
        {
            active_ = true;
            heldTrack_ = trackIndex;
            heldStep_  = stepIndex;
            paramWritten_ = false;
        }

        void release()
        {
            active_ = false;
            heldTrack_ = -1;
            heldStep_ = -1;
            paramWritten_ = false;
        }

        // Called by the parameter-write path when a P-Lock is applied.
        void markParamWritten() { paramWritten_ = true; }
        bool wasParamWritten() const { return paramWritten_; }

    private:
        bool active_ = false;
        int  heldTrack_ = -1;
        int  heldStep_ = -1;
        bool paramWritten_ = false;
        int  activeSlot_ = -1;
    };
}
