#pragma once
#include "Widgets.h"

namespace hamar::ui
{
    // The strings: stiffness (inharmonicity over the keyboard), the unison, decay (prompt and aftersound), brightness
    // and the longitudinal motion; the duplex scale.
    class StringsPage : public Page
    {
    public:
        explicit StringsPage (HamarProcessor& p)
            : proc (p),
              inharm ("Inharmonicity B", true, 5.0e-5f, 5.0e-2f, [] (float v) { return juce::String (v, v < 1.0e-3f ? 5 : 3); }),
              decay ("Decay T60  " + utf8 ("\xc2\xb7") + "  aftersound and prompt sound", true, 0.5f, 60.0f,
                     [] (float v) { return juce::String (v, v < 10.0f ? 1 : 0) + " s"; }),
              stringLock (p.apvts, locks::ids().strings)
        {
            auto& s = p.apvts;
            addAndMakeVisible (inharm);
            addAndMakeVisible (decay);
            stringRow.add (makeKnob (s, P::StringInharm, "Stiffness"));
            stringRow.add (makeKnob (s, P::StringLength, "Length"));
            stringRow.add (makeChoice (s, P::StringUnisons, "Unisons"), 1.2f);
            stringRow.add (makeKnob (s, P::StringDetune, "Detune"));
            stringRow.add (makeKnob (s, P::StringDuplex, "Duplex"));
            stringRow.add (makeKnob (s, P::StringPhantom, "Longitudinal"));
            stringRow.addTo (*this);
            decayRow.add (makeKnob (s, P::StringSustain, "Sustain"));
            decayRow.add (makeKnob (s, P::StringCoupling, "Coupling"));
            decayRow.add (makeKnob (s, P::StringBrightness, "Brightness"));
            decayRow.addTo (*this);
            addAndMakeVisible (stringLock);
        }

        void tick() override
        {
            if (! table.update (proc)) return;
            inharm.setRange (table.lowKey(), dsp::kHighestKey);
            decay.setRange (table.lowKey(), dsp::kHighestKey);
            inharm.setLines ({ { [this] (int key) { return table[key].b; }, "B", true } });
            decay.setLines ({ { [this] (int key) { return table[key].after; }, "Aftersound", true },
                              { [this] (int key) { return table[key].prompt; }, "Prompt", false } });
        }

        void paint (juce::Graphics& g) override
        {
            drawPanel (g, stringArea.toFloat(), "Strings");
            drawPanel (g, decayArea.toFloat(), "Decay");
        }

        void resized() override
        {
            auto r = getLocalBounds();
            auto graphs = r.removeFromTop (r.getHeight() - 200);
            r.removeFromTop (10);
            inharm.setBounds (graphs.removeFromLeft (graphs.getWidth() / 2 - 5));
            graphs.removeFromLeft (10);
            decay.setBounds (graphs);
            stringArea = r.removeFromLeft (r.getWidth() * 64 / 100);
            r.removeFromLeft (10);
            decayArea = r;
            stringLock.place (stringArea);
            stringRow.layout (stringArea.reduced (12, 0).withTrimmedTop (40).withHeight (110));
            decayRow.layout (decayArea.reduced (12, 0).withTrimmedTop (40).withHeight (110));
        }

    private:
        HamarProcessor& proc;
        TableWatcher table;
        KeyCurve inharm, decay;
        ControlRow stringRow, decayRow;
        PanelLock stringLock;
        juce::Rectangle<int> stringArea, decayArea;
    };
}
