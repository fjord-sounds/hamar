#pragma once
#include <fjord_base/presets/PresetManagerBase.h>
#include "Locks.h"

namespace hamar
{
    // Presets are XML files (.hamar) with plain values:
    //   <HamarPreset name="Concert Hall Grand" category="Grand" author="Fjord" tags="Concert,Warm" fxOrder="...">
    //     <Param id="model" value="0"/> …
    // Missing parameters get their default value – this keeps old presets compatible with new versions.
    // The master gain and the quality level belong to the user (CPU), presets never change them.
    class PresetManager : public fjord::base::presets::PresetManagerBase
    {
    public:
        PresetManager (juce::AudioProcessorValueTreeState& s, juce::UndoManager* um)
            : PresetManagerBase (s, um, { ".hamar", "HamarPreset", false, {}, { "master_gain", "quality" } })
        {
            setLocks (locks::set());   // locked parts (Locks.h) keep their values when a preset is loaded
            initialise();
        }

        // The Scala tuning travels with presets: <Scala name="…">file text</Scala>. Set by the processor.
        std::function<void (const juce::String& text, const juce::String& name)> loadScala;
        std::function<void()> clearScala;

    protected:
        void applyExtras (const juce::XmlElement& xml, bool isInit) override
        {
            if (isLockedProperty ("tuningLock")) return;
            if (auto* scala = xml.getChildByName ("Scala"); scala != nullptr && ! isInit)
            {
                if (loadScala) loadScala (scala->getAllSubText(), scala->getStringAttribute ("name"));
            }
            else if (clearScala)
                clearScala();
        }

        void writeExtras (juce::XmlElement& xml) const override
        {
            const auto text = state.state.getProperty ("scalaText").toString();
            if (text.isEmpty()) return;
            auto* scala = xml.createNewChildElement ("Scala");
            scala->setAttribute ("name", state.state.getProperty ("scalaName").toString());
            scala->addTextElement (text);
        }
    };
}
