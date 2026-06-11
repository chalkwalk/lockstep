#pragma once

#include <algorithm>
#include <cmath>

namespace lockstep::dsp
{

// ADSR/AHDSR envelope with exponential decay and release.
// Attack is a linear ramp from the level at gateOn() time to 1.0,
// so legato re-triggers are click-free. hardRetrigger() zeros the level
// for a clean restart (use with a ghost-fade to avoid output clicks).
    class Envelope
    {
    public:
        enum class Stage
        {
            Idle,
            Attack,
            Hold,
            Decay,
            Sustain,
            Release
        };

        void prepare(double sampleRate) noexcept
        {
            sr_ = sampleRate;
            reset();
        }

        void reset() noexcept
        {
            stage_ = Stage::Idle;
            level_ = 0.0f;
            attackInc_ = 0.0f;
            attackRemain_ = 0;
            attackStartLevel_ = 0.0f;
            holdRemain_ = 0;
            decayMul_ = 0.0f;
            releaseMul_ = 0.0f;
            sustain_ = 1.0f;
            holdTotal_ = 0;
            forceZeroSustain_ = false;
        }

  // holdMs=0 skips the Hold stage. forceZeroSustain collapses Sustain→Release
  // immediately (use for AHD drums).
        void setADSR(float attackMs, float decayMs, float sustain01,
                     float releaseMs, float holdMs = 0.0f,
                     bool forceZeroSustain = false) noexcept
        {
            attackTotal_ = std::max(1, msToSamples(attackMs));
            holdTotal_ = msToSamples(holdMs);
            decayMul_ = expMul(msToSamples(decayMs));
            sustain_ = std::clamp(sustain01, 0.0f, 1.0f);
            releaseMul_ = expMul(msToSamples(releaseMs));
            forceZeroSustain_ = forceZeroSustain;
        }

  // Begin/resume attack from current level → 1.0 over attackMs.
  // Safe to call in any stage; no-click legato re-entry.
        void gateOn() noexcept
        {
            attackStartLevel_ = (stage_ == Stage::Idle) ? 0.0f : level_;
            attackRemain_ = attackTotal_;
            attackInc_ = (attackTotal_ > 0)
                             ? (1.0f - attackStartLevel_) / static_cast<float>(attackTotal_)
                             : (1.0f - attackStartLevel_);
            stage_ = Stage::Attack;
        }

  // Zero the level and start attack from 0. Use for RETRIG mode (pair with
  // a ghost-fade to hide the discontinuity at the call site).
        void hardRetrigger() noexcept
        {
            level_ = 0.0f;
            attackStartLevel_ = 0.0f;
            attackRemain_ = attackTotal_;
            attackInc_ = (attackTotal_ > 0) ? 1.0f / static_cast<float>(attackTotal_) : 1.0f;
            stage_ = Stage::Attack;
        }

  // Enter Release from current level. No-op if already Idle or in Release.
        void gateOff() noexcept
        {
            if (stage_ != Stage::Idle && stage_ != Stage::Release)
                stage_ = Stage::Release;
        }

  // Advance one sample and return the updated level.
        float tick() noexcept
        {
            switch (stage_)
            {
                case Stage::Idle:
                    return 0.0f;

                case Stage::Attack:
                    level_ += attackInc_;
                    if (--attackRemain_ <= 0 || level_ >= 1.0f)
                    {
                        level_ = 1.0f;
                        attackRemain_ = 0;
                        if (holdTotal_ > 0)
                        {
                            holdRemain_ = holdTotal_;
                            stage_ = Stage::Hold;
                        }
                        else
                        {
                            stage_ = nextAfterHold();
                        }
                    }
                    return level_;

                case Stage::Hold:
                    if (--holdRemain_ <= 0)
                        stage_ = nextAfterHold();
                    return 1.0f;

                case Stage::Decay:
                    level_ = std::max(level_ * decayMul_, sustain_);
                    if (level_ <= sustain_ + 1e-4f)
                    {
                        level_ = sustain_;
                        stage_ = forceZeroSustain_ ? Stage::Release : Stage::Sustain;
                    }
                    return level_;

                case Stage::Sustain:
                    return sustain_;

                case Stage::Release:
                    level_ *= releaseMul_;
                    if (level_ < 1e-4f)
                    {
                        level_ = 0.0f;
                        stage_ = Stage::Idle;
                    }
                    return level_;
            }
            return 0.0f;
        }

        [[nodiscard]] float currentLevel() const noexcept { return level_; }
        [[nodiscard]] Stage stage() const noexcept { return stage_; }
        [[nodiscard]] bool isActive() const noexcept { return stage_ != Stage::Idle; }

    private:
        double sr_ = 44100.0;
        Stage stage_ = Stage::Idle;
        float level_ = 0.0f;

        int attackTotal_ = 1;
        int attackRemain_ = 0;
        float attackStartLevel_ = 0.0f;
        float attackInc_ = 0.0f;

        int holdTotal_ = 0;
        int holdRemain_ = 0;

        float decayMul_ = 0.0f;
        float sustain_ = 1.0f;
        float releaseMul_ = 0.0f;
        bool forceZeroSustain_ = false;

        [[nodiscard]] Stage nextAfterHold() const noexcept
        {
    // D=0 or sustain==1 → skip Decay, go straight to Sustain.
            if (decayMul_ <= 0.0f || sustain_ >= 1.0f - 1e-4f)
                return forceZeroSustain_ ? Stage::Release : Stage::Sustain;
            return Stage::Decay;
        }

        [[nodiscard]] int msToSamples(float ms) const noexcept
        {
            return static_cast<int>(static_cast<double>(ms) * 0.001 * sr_);
        }

        [[nodiscard]] static float expMul(int samples) noexcept
        {
            if (samples <= 0) return 0.0f;
            return std::exp(-1.0f / static_cast<float>(samples));
        }
    };

} // namespace lockstep::dsp
