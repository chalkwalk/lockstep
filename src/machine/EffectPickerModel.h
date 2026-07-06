#pragma once

#include "EffectFactory.h"
#include <vector>

namespace lockstep
{
    // Single owner of "which catalogue entries appear in an FX picker, in what
    // order, on which page" (9.24 S12). Before the catalogue grew past 16 entries
    // the three picker sites (SurfaceModel render, KeyboardArea input, PluginEditor
    // apply-guard) each assumed `cell index == catalogue index`. That breaks once
    // the master picker exceeds one 16-cell page, so every site now routes through
    // these pure helpers instead.
    //
    // Context-filtering rules (deliberate compaction — gaps are removed, not dimmed):
    //   TrackInsert  — drops masterOnly and sendOnly (track slots take neither).
    //   MasterInsert — drops sendOnly (the External send is send-slot only).
    //   MasterSend   — keeps everything master, including the External send.
    enum class FxPickerCtx
    {
        TrackInsert,
        MasterInsert,
        MasterSend
    };

    inline constexpr int kFxPickerCellsPerPage = 16;

    // Catalogue indices (into availableEffects()) visible in `ctx`, in display order.
    inline std::vector<int> fxPickerEntries(FxPickerCtx ctx)
    {
        const auto all = availableEffects();
        std::vector<int> out;
        out.reserve(all.size());
        for (int i = 0; i < static_cast<int>(all.size()); ++i)
        {
            const auto& e = all[static_cast<std::size_t>(i)];
            bool keep = true;
            switch (ctx)
            {
                case FxPickerCtx::TrackInsert:  keep = !e.masterOnly && !e.sendOnly; break;
                case FxPickerCtx::MasterInsert: keep = !e.sendOnly;                  break;
                case FxPickerCtx::MasterSend:   keep = true;                         break;
            }
            if (keep) out.push_back(i);
        }
        return out;
    }

    // Number of 16-cell pages needed for `ctx` (>= 1).
    inline int fxPickerPageCount(FxPickerCtx ctx)
    {
        const int n = static_cast<int>(fxPickerEntries(ctx).size());
        return n <= 0 ? 1 : (n + kFxPickerCellsPerPage - 1) / kFxPickerCellsPerPage;
    }

    // Catalogue index shown at (page, cell 0..15), or -1 if that cell is empty.
    inline int fxPickerCellToCatalogue(FxPickerCtx ctx, int page, int cell)
    {
        if (cell < 0 || cell >= kFxPickerCellsPerPage || page < 0) return -1;
        const auto entries = fxPickerEntries(ctx);
        const int flat = page * kFxPickerCellsPerPage + cell;
        if (flat < 0 || flat >= static_cast<int>(entries.size())) return -1;
        return entries[static_cast<std::size_t>(flat)];
    }
}
