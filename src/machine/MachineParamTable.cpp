#include "MachineParamTable.h"
#include "IMachine.h"
#include <span>
#include <cstring>

namespace lockstep
{
    ParamSpec toParamSpec(const ParamRow& row)
    {
        ParamSpec ps;
        ps.id            = row.id    ? juce::String(row.id)    : juce::String{};
        ps.label         = row.label ? juce::String(row.label) : juce::String{};
        ps.minValue      = row.minValue;
        ps.maxValue      = row.maxValue;
        ps.defaultValue  = row.defaultValue;
        ps.skew          = row.skew;
        ps.isStepped     = (row.isStepped != 0u);
        ps.unit          = static_cast<ParamSpec::Unit>(row.unit);
        ps.role          = static_cast<ParamSpec::Role>(row.role);
        ps.variant       = static_cast<ParamSpec::Variant>(row.variant);
        ps.sectionIndex  = row.sectionIndex;
        ps.zeroCrossingSnap = (row.zeroCrossingSnap != 0u);

        if (row.valueLabels != nullptr)
        {
            // Count up to the nullptr sentinel.
            std::size_t count = 0;
            while (row.valueLabels[count] != nullptr)
                ++count;
            ps.valueLabels = std::span<const char* const>(row.valueLabels, count);
        }

        return ps;
    }

} // namespace lockstep
