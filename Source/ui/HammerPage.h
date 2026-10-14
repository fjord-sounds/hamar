#pragma once
#include "Widgets.h"

namespace hamar::ui
{
    // The hammers: felt stiffness (its curve and its voicing over the keyboard), mass, where they strike, the felt
    // strip of the felt pianos, and the noises of the action.
    class HammerPage : public Page
    {
    public:
        explicit HammerPage (HamarProcessor& p)
            : proc (p),
              voicing ("Felt stiffness  " + utf8 ("\xc2\xb7") + "  relative to a concert grand", true, 0.1f, 10.0f,
                       [] (float v) { return v >= 1.0f ? juce::String (v, 0) + utf8 ("\xc3\x97") : juce::String (v, 2) + utf8 ("\xc3\x97"); }),
              hammerLock (p.apvts, locks::ids().hammer)
        {
            auto& s = p.apvts;
            addAndMakeVisible (felt);
            addAndMakeVisible (voicing);
            feltRow.add (makeKnob (s, P::HammerHardness, "Hardness"));
            feltRow.add (makeKnob (s, P::HammerVoiceBass, "Bass"));
            feltRow.add (makeKnob (s, P::HammerVoiceTreble, "Treble"));
            feltRow.add (makeKnob (s, P::HammerHysteresis, "Damping"));
            feltRow.add (makeKnob (s, P::HammerFelt, "Felt strip"));
            feltRow.addTo (*this);
            blowRow.add (makeKnob (s, P::HammerMass, "Mass"));
            blowRow.add (makeKnob (s, P::HammerPosition, "Strike point"));
            blowRow.add (makeKnob (s, P::HammerNoise, "Knock"));
            blowRow.add (makeKnob (s, P::HammerKeyNoise, "Key noise"));
            blowRow.addTo (*this);
            addAndMakeVisible (hammerLock);
        }

        void tick() override
        {
            if (! table.update (proc)) return;
            const auto& m = dsp::model (table.settings.model);
            felt.update (table[60], dsp::scale::hammerK (60.0f) * m.hammerK * std::exp2 (4.0f * m.hammerP) * (1.0f - 0.9f * m.felt) * (1.0f + 30.0f * m.tack));
            const auto reference = [] (int key) { return dsp::scale::hammerK ((float) key); };
            voicing.setRange (table.lowKey(), dsp::kHighestKey);
            voicing.setLines ({ { [this, reference] (int key) { return table[key].hammerK / reference (key); }, "Felt", true } });
        }

        void paint (juce::Graphics& g) override
        {
            drawPanel (g, feltArea.toFloat(), "Felt");
            drawPanel (g, blowArea.toFloat(), "Blow and action");
        }

        void resized() override
        {
            auto r = getLocalBounds();
            auto graphs = r.removeFromTop (r.getHeight() - 200);
            r.removeFromTop (10);
            felt.setBounds (graphs.removeFromLeft (graphs.getWidth() / 3));
            graphs.removeFromLeft (10);
            voicing.setBounds (graphs);
            feltArea = r.removeFromLeft (r.getWidth() * 56 / 100);
            r.removeFromLeft (10);
            blowArea = r;
            hammerLock.place (feltArea);
            feltRow.layout (feltArea.reduced (12, 0).withTrimmedTop (40).withHeight (110));
            blowRow.layout (blowArea.reduced (12, 0).withTrimmedTop (40).withHeight (110));
        }

    private:
        HamarProcessor& proc;
        TableWatcher table;
        FeltCurve felt;
        KeyCurve voicing;
        ControlRow feltRow, blowRow;
        PanelLock hammerLock;
        juce::Rectangle<int> feltArea, blowArea;
    };
}
