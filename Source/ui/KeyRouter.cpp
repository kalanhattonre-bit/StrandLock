#include "KeyRouter.h"

namespace sl::ui
{
    namespace
    {
        int lowerKey (int code) noexcept
        {
            return code >= 'A' && code <= 'Z' ? code - 'A' + 'a' : code;
        }

        /** 1 .. 8 for the digit keys and the number pad's, else 0. */
        int modeDigit (int code) noexcept
        {
            if (code >= '1' && code <= '8')
                return code - '0';
            const int pad[] = { juce::KeyPress::numberPad1, juce::KeyPress::numberPad2, juce::KeyPress::numberPad3, juce::KeyPress::numberPad4,
                                juce::KeyPress::numberPad5, juce::KeyPress::numberPad6, juce::KeyPress::numberPad7, juce::KeyPress::numberPad8 };
            for (int i = 0; i < 8; ++i)
                if (code == pad[i])
                    return i + 1;
            return 0;
        }
    }

    KeyRouter::Hit KeyRouter::shortcutFor (const juce::KeyPress& key)
    {
        const int code = key.getKeyCode();
        if (code == juce::KeyPress::spaceKey)
            return {};
        const auto mods = key.getModifiers();
        const int k = lowerKey (code);
        if (mods.isCommandDown() || mods.isCtrlDown())
        {
            if (k == 'z')
                return { mods.isShiftDown() ? Shortcut::redo : Shortcut::undo, 0 };
            if (k == 'y')
                return { Shortcut::redo, 0 };
            if (k == 'c')
                return { Shortcut::copyLanes, 0 };
            if (k == 'v')
                return { Shortcut::pasteLanes, 0 };
            if (k == 'd')
                return { Shortcut::duplicate, 0 };
            if (k == 'a')
                return { Shortcut::selectAll, 0 };
            return {};
        }
        if (mods.isAltDown())
            return {};
        if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
            return { Shortcut::deleteSelection, 0 };
        if (code == juce::KeyPress::escapeKey)
            return { Shortcut::clearSelection, 0 };
        if (! mods.isShiftDown())   // Shift with a digit types a sign ('!'), which the prototype does not read as a mode
            if (const int m = modeDigit (code))
                return { Shortcut::mode, m };
        if (k == 'b')
            return { Shortcut::armLink, 0 };
        if (k == 'n')
            return { Shortcut::toggleNodes, 0 };
        return {};
    }

    bool KeyRouter::keeps (const juce::KeyPress& key, bool keysOn) noexcept
    {
        // Keys off: everything goes to Cubase. Space always does (play and stop).
        return keysOn && key.getKeyCode() != juce::KeyPress::spaceKey;
    }

    bool KeyRouter::route (const juce::KeyPress& key, bool keysOn) const
    {
        if (! keeps (key, keysOn))
            return false;

        const auto run = [] (const std::function<void()>& fn)
        {
            if (fn)
                fn();
        };
        const Hit hit = shortcutFor (key);
        // A held key repeats its press, and JUCE does not say which presses are repeats. Only B and N change
        // something when they repeat (they toggle), so a B or N that is still held does nothing (fix pass F4). Another
        // key's press ends the repeats, as Windows stops repeating a key when another one goes down.
        if (hit.what == Shortcut::armLink || hit.what == Shortcut::toggleNodes)
        {
            if (toggleKeyHeld != hit.what)
                run (hit.what == Shortcut::armLink ? actions.armLink : actions.toggleNodes);
            toggleKeyHeld = hit.what;
            return true;
        }
        toggleKeyHeld = Shortcut::none;
        switch (hit.what)
        {
            case Shortcut::undo:            run (actions.undo); break;
            case Shortcut::redo:            run (actions.redo); break;
            case Shortcut::deleteSelection: run (actions.deleteSelection); break;
            case Shortcut::selectAll:       run (actions.selectAll); break;
            case Shortcut::duplicate:       run (actions.duplicate); break;
            case Shortcut::copyLanes:       run (actions.copyLanes); break;
            case Shortcut::pasteLanes:      run (actions.pasteLanes); break;
            case Shortcut::clearSelection:  run (actions.clearSelection); break;
            case Shortcut::armLink:         break;   // above
            case Shortcut::toggleNodes:     break;   // above
            case Shortcut::mode:
                if (actions.setMode)
                    actions.setMode (hit.mode);
                break;
            case Shortcut::none:
                break;
        }
        // Keys on: StrandLock keeps every key it gets but Space (M0's rule), shortcut or not.
        return true;
    }
}
