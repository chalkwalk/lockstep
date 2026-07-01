#pragma once

namespace lockstep
{
    // Mixin for machines that carry slice data (MG.3 Slice trig mode).
    // Both SampleMachine and SliceMachine implement this so the
    // processor can handle both polymorphically via dynamic_cast.
    struct ISliceable
    {
        virtual ~ISliceable() = default;
        [[nodiscard]] virtual int numSlices() const = 0;
        virtual void setEqualSlices(int count) = 0;
        virtual void clearSlices() = 0;
        // Run transient detection and populate slice positions.
        // No-op if no sample is loaded or the machine does not support it.
        virtual void detectTransientSlices() = 0;
        virtual void detectTransientSlices(int count) = 0;

        [[nodiscard]] bool hasSlices() const { return numSlices() > 0; }
    };
}
