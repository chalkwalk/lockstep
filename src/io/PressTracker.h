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
        static constexpr int kMouseSource = 0;   // keyboard uses positive raw key codes
        static constexpr int kControllerSource = -1;  // hardware controller (distinct from mouse/keyboard)

        // Register a button-down.  source = rawKeyCode (keyboard) or kMouseSource (mouse).
        void press(int source, ControllerButton button, int index = -1)
        {
            entries_[source] = { button, index };
        }

        void release(int source) { entries_.erase(source); }
        void releaseAll() { entries_.clear(); }

        // True if the given raw key code is currently held by the keyboard path.
        [[nodiscard]] bool isKeyHeld(int rawCode) const
        {
            return entries_.count(rawCode) > 0;
        }

        // True if the given source is currently holding the (button, index) cell.
        [[nodiscard]] bool isSourceHeld(int source, ControllerButton button, int index = -1) const
        {
            const auto it = entries_.find(source);
            if (it == entries_.end())
                return false;
            if (it->second.button != button)
                return false;
            return (index == -1 || it->second.index == index);
        }

        // True if the mouse is currently holding the given (button, index) cell.
        // Pass index = -1 to match any held index for that button.
        [[nodiscard]] bool isMouseHeld(ControllerButton button, int index = -1) const
        {
            return isSourceHeld(kMouseSource, button, index);
        }

        // True if a hardware controller is currently holding the (button, index)
        // cell. Lets controller presses highlight on screen just like mouse/keyboard.
        [[nodiscard]] bool isControllerHeld(ControllerButton button, int index = -1) const
        {
            return isSourceHeld(kControllerSource, button, index);
        }

        // Returns the current mouse-held entry if one exists, or nullopt.
        struct Entry
        {
            ControllerButton button;
            int index;
        };
        [[nodiscard]] std::optional<Entry> mouseEntry() const
        {
            const auto it = entries_.find(kMouseSource);
            if (it == entries_.end()) return std::nullopt;
            return it->second;
        }

        // Is this raw key code physically down right now?
        //
        // JUCE gives no key-UP callback, so every release in the product is found by
        // asking the OS this question about each tracked key (see
        // forEachReleasedKeyboard and the editor's keyStateChanged). That makes the
        // OS the sole authority on release -- and a synthetic key press, which the
        // OS has never heard of, is therefore released instantly. So the oracle is
        // substitutable: a test supplies its own key-state answer and the whole
        // release path downstream runs unmodified, which is the point (a harness
        // that skipped this path would be testing a fiction).
        //
        // §20: PressTracker already owns press state, so it owns this question too.
        [[nodiscard]] bool physicallyDown(int rawCode) const { return keyDownFn_(rawCode); }

        using KeyDownFn = bool (*)(int rawCode);

        // Test seam ONLY (tests/UiDriver.h). Plain function pointer: no allocation,
        // no indirection cost in the default path. Passing nullptr restores the OS.
        void setKeyDownFn(KeyDownFn fn) noexcept { keyDownFn_ = (fn != nullptr) ? fn : &defaultKeyDown; }

        // Call fn(source, button, index) for every keyboard entry whose key is no
        // longer physically down.  Used by keyStateChanged to synthesize ButtonUp events.
        template <typename F>
        void forEachReleasedKeyboard(F&& fn) const
        {
            for (const auto& [src, entry] : entries_)
            {
                if (src <= 0)   // skip kMouseSource (0) and kControllerSource (-1)
                    continue;
                if (!physicallyDown(src))
                    fn(src, entry.button, entry.index);
            }
        }

        [[nodiscard]] bool anyHeld() const { return !entries_.empty(); }

    private:
        static bool defaultKeyDown(int rawCode) { return juce::KeyPress::isKeyCurrentlyDown(rawCode); }

        std::unordered_map<int, Entry> entries_;
        KeyDownFn keyDownFn_ = &defaultKeyDown;
    };
}
