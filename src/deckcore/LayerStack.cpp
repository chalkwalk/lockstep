#include "LayerStack.h"

namespace dc
{
    void LayerStack::snapshot(int sub) noexcept
    {
        if (undo_ == nullptr || committed_ == nullptr) return;
        const int cap = committed_->capacity();
        undo_->ensureCommitted(sub, cap);
        for (int ch = 0; ch < committed_->channels(); ++ch)
            for (int k = 0; k < cap; ++k)
                undo_->write(sub, ch, k, committed_->read(sub, ch, k));
        canUndo_ = true;
    }

    void LayerStack::beginLayer(int sub, Span span) noexcept
    {
        if (! bound()) return;
        snapshot(sub);
        layer_->ensureCommitted(sub, layer_->capacity());
        layer_->clearSubTrack(sub);
        punch_ = span;
        layerActive_ = true;
    }

    void LayerStack::discardLayer(int sub) noexcept
    {
        if (! bound()) return;
        layer_->clearSubTrack(sub);
        layerActive_ = false;
    }

    void LayerStack::fold(int sub, float decay) noexcept
    {
        if (! bound() || ! layerActive_) return;

        const int cap = committed_->capacity();
        committed_->ensureCommitted(sub, cap);
        for (int ch = 0; ch < committed_->channels(); ++ch)
        {
            for (int k = 0; k < cap; ++k)
            {
                if (! punch_.contains(k)) continue;
                committed_->write(sub, ch, k,
                                  committed_->read(sub, ch, k) * decay
                                      + layer_->read(sub, ch, k));
            }
        }
        layer_->clearSubTrack(sub);
        layerActive_ = false;
    }

    void LayerStack::undoLast(int sub) noexcept
    {
        if (! bound() || undo_ == nullptr || ! canUndo_) return;

        if (layerActive_) discardLayer(sub);

        const int cap = committed_->capacity();
        committed_->ensureCommitted(sub, cap);
        for (int ch = 0; ch < committed_->channels(); ++ch)
            for (int k = 0; k < cap; ++k)
                committed_->write(sub, ch, k, undo_->read(sub, ch, k));

        // One level deep: the take it just restored is not itself undoable.
        canUndo_ = false;
    }

    float LayerStack::read(int sub, int ch, std::int64_t idx) const noexcept
    {
        if (! bound()) return 0.0f;
        float v = committed_->read(sub, ch, idx);
        if (layerActive_)
        {
            int k = 0;
            if (layer_->resolve(idx, k) && punch_.contains(k))
                v += layer_->read(sub, ch, idx);
        }
        return v;
    }
}
