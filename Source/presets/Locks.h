#pragma once
#include <fjord_base/presets/Locks.h>
#include <fjord_base/ui/LockMenu.h>

namespace hamar::locks
{
    // Parts of a sound that stay when another preset is loaded. The mechanism (LockSet, padlock menu, LockButton,
    // preset manager hook) is fjord-base; this is Hamar's list. The switches are properties of the project state – their
    // names never change. A group protects the parameters whose ids start with one of its prefixes.
    struct Ids
    {
        int model, play, hammer, strings, resonance, body, tuning, mics, category, layer, condition;
    };

    inline const std::pair<const fjord::base::locks::LockSet&, Ids>& setup()
    {
        static const auto pair = []
        {
            static fjord::base::locks::LockSet set;
            Ids ids {};
            ids.model     = set.add ({ "modelLock",     "Model",               "model" });
            ids.play      = set.add ({ "playLock",      "Touch and chords",    "play_|tools_" });
            ids.hammer    = set.add ({ "hammerLock",    "Hammers",             "hammer_" });
            ids.strings   = set.add ({ "stringLock",    "Strings",             "string_" });
            ids.resonance = set.add ({ "resonanceLock", "Dampers and pedals",  "sym_|damper_|pedal_" });
            ids.body      = set.add ({ "bodyLock",      "Soundboard and case", "body_|grand_|upright_" });
            ids.tuning    = set.add ({ "tuningLock",    "Tuning",              "tune_" });
            ids.mics      = set.add ({ "micLock",       "Microphones",         "mic_" });
            ids.category  = set.add ({ "categoryLock",  "Electric, historic, dance and fantasy settings", "ep_|hist_|dance_|fan_" });
            ids.layer     = set.add ({ "layerLock",     "Layer",               "layer_" });
            ids.condition = set.add ({ "conditionLock", "Condition",           "cond_" });
            set.addEffectGroups();   // the whole chain and one lock per effect – the same in every plugin
            return std::pair<const fjord::base::locks::LockSet&, Ids> { set, ids };
        }();
        return pair;
    }

    inline const fjord::base::locks::LockSet& set() { return setup().first; }
    inline const Ids& ids() { return setup().second; }

    inline fjord::base::ui::MenuLayout menuLayout()
    {
        const auto& i = ids();
        fjord::base::ui::MenuLayout layout;
        layout.header ("Keep when switching presets")
              .item (i.model)
              .item (i.play)
              .item (i.tuning)
              .item (i.mics)
              .separator()
              .item (i.hammer)
              .item (i.strings)
              .item (i.resonance)
              .item (i.body)
              .item (i.category)
              .separator()
              .item (i.layer)
              .item (i.condition)
              .separator()
              .effects (set());
        return layout;
    }
}
