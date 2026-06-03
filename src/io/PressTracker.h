#pragma once

#include <optional>
#include <unordered_map>
#include <juce_gui_basics/juce_gui_basics.h>
#include "ControllerEvent.h"

namespace lockstep
{
    // Single source of truth for which logical buttons are currently held,
    // fed by both keyboard (keyed on rawCode) and mouse (kMouseSource).
    // Paint code queries isKeyHeld / isMouseHeld instead of polling
    // juce::KeyPress::isKeyCurrentlyDown, so mouse clicks highlight identically.
    class PressTracker
    {
    public:
        static constexpr int kMouseSource      = 0;   // keyboard uses positive raw key codes
        static constexpr int kControllerSource = -1;  // hardware controller (distinct from mouse/keyboard)

        // Register a button-down.  source = rawKeyCode (keyboard) or kMouseSource (mouse).
        void press(int source, ControllerButton button, int index = -1)
        {
            entries_[source] = { button, index };
        }

        void release(int source) { entries_.erase(source); }
        void releaseAll()        { entries_.clear(); }

        // True if the given raw key code is currently held by the keyboard path.
        [[nodiscard]] bool isKeyHeld(int rawCode) const
        {
            return entries_.count(rawCode) > 0;
        }

        // True if the mouse is currently holding the given (button, index) cell.
        // Pass index = -1 to match any held index for that button.
        [[nodiscard]] bool isMouseHeld(ControllerButton button, int index = -1) const
        {
            const auto it = entries_.find(kMouseSource);
            if (it == entries_.end())
                return false;
            if (it->second.button != button)
                return false;
            return (index == -1 || it->second.index == index);
        }

        // Returns the current mouse-held entry if one exists, or nullopt.
        struct Entry { ControllerButton button; int index; };
        [[nodiscard]] std::optional<Entry> mouseEntry() const
        {
            const auto it = entries_.find(kMouseSource);
            if (it == entries_.end()) return std::nullopt;
            return it->second;
        }

        // Call fn(source, button, index) for every keyboard entry whose key is no
        // longer physically down.  Used by keyStateChanged to synthesize ButtonUp events.
        template<typename F>
        void forEachReleasedKeyboard(F&& fn) const
        {
            for (const auto& [src, entry] : entries_)
            {
                if (src <= 0)   // skip kMouseSource (0) and kControllerSource (-1)
                    continue;
                if (!juce::KeyPress::isKeyCurrentlyDown(src))
                    fn(src, entry.button, entry.index);
            }
        }

        [[nodiscard]] bool anyHeld() const { return !entries_.empty(); }

    private:
        std::unordered_map<int, Entry> entries_;
    };
}
