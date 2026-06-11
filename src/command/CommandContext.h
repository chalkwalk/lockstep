#pragma once

#include "../core/Arrangement.h"
#include "../core/Sequence.h"
#include "../core/SoundPool.h"
#include "../io/Clipboard.h"
#include "../io/EditContext.h"
#include "../io/EditMode.h"
#include "../machine/IMachine.h"
#include "../state/UiState.h"

namespace lockstep
{
  // Narrow pure-virtual catalog seam: exposes only the per-track schema queries
  // CommandCore needs, so the test fixture doesn't need to pull in
  // LockstepProcessor. Grow methods only as needed.
    class IMachineCatalog
    {
    public:
        virtual ~IMachineCatalog() = default;

        virtual int numParams(int track) const = 0;
        virtual ParamSpec paramSpec(int track, int slot) const = 0;
        virtual SectionInfo section(int track, int idx) const = 0;
        virtual const char* machineId(int track) const = 0;
    };

  // All model references CommandCore handlers operate on.
  // Populated by the editor before each dispatchDown/Up/Verb call.
  // CommandCore must not cache this struct across calls.
    struct CommandContext
    {
        Arrangement& arrangement;
        Sequence& sequence;      // working sequence (arrangement.workingSequence())
        EditContext& editContext;
        EditMode& editMode;
        UiState& uiState;
        Clipboard& clipboard;
        SoundPool& soundPool;
        const IMachineCatalog& catalog;
    };
}
