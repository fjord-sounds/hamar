#pragma once
#include "Widgets.h"

namespace hamar::ui
{
    // Dampers and pedals: how fast the dampers stop the strings (and how far up they reach), their noise, the pedal
    // mechanics, una corda and the sympathetic strings that ring along when the dampers are up.
    class PedalsPage : public Page
    {
    public:
        explicit PedalsPage (HamarProcessor& p)
            : proc (p), pedals (p),
              damped ("Damped decay T60  " + utf8 ("\xc2\xb7") + "  the line ends where the dampers end", true, 0.05f, 5.0f,
                      [] (float v) { return juce::String (v, 2) + " s"; }),
              lock (p.apvts, locks::ids().resonance)
        {
            auto& s = p.apvts;
            addAndMakeVisible (pedals);
            addAndMakeVisible (damped);
            damperRow.add (makeKnob (s, P::DamperStrength, "Strength"));
            damperRow.add (makeKnob (s, P::DamperTop, "Range"));
            damperRow.add (makeKnob (s, P::DamperNoise, "Noise"));
            damperRow.addTo (*this);
            pedalRow.add (makeKnob (s, P::SymAmount, "Sympathetic"));
            pedalRow.add (makeKnob (s, P::PedalResonance, "Pedal swell"));
            pedalRow.add (makeKnob (s, P::PedalNoise, "Mechanics"));
            pedalRow.add (makeKnob (s, P::PedalUnaCorda, "Una corda"));
            pedalRow.addTo (*this);
            addAndMakeVisible (lock);
        }

        void tick() override
        {
            pedals.tick();
            if (! table.update (proc)) return;
            damped.setRange (table.lowKey(), dsp::kHighestKey);
            damped.setLines ({ { [this] (int key) { return table[key].hasDamper ? table[key].damped : 0.05f; }, "Damped", true } });
        }

        void paint (juce::Graphics& g) override
        {
            drawPanel (g, damperArea.toFloat(), "Dampers");
            drawPanel (g, pedalArea.toFloat(), "Pedals and resonance");
        }

        void resized() override
        {
            auto r = getLocalBounds();
            auto graphs = r.removeFromTop (r.getHeight() - 200);
            r.removeFromTop (10);
            pedals.setBounds (graphs.removeFromRight (300));
            graphs.removeFromRight (10);
            damped.setBounds (graphs);
            damperArea = r.removeFromLeft (r.getWidth() * 44 / 100);
            r.removeFromLeft (10);
            pedalArea = r;
            lock.place (pedalArea);
            damperRow.layout (damperArea.reduced (12, 0).withTrimmedTop (40).withHeight (110));
            pedalRow.layout (pedalArea.reduced (12, 0).withTrimmedTop (40).withHeight (110));
        }

    private:
        HamarProcessor& proc;
        TableWatcher table;
        PedalStrip pedals;
        KeyCurve damped;
        ControlRow damperRow, pedalRow;
        PanelLock lock;
        juce::Rectangle<int> damperArea, pedalArea;
    };
}
