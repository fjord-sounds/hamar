#pragma once
#include "InstrumentView.h"

namespace hamar::ui
{
    // The first page: the instrument from above, the touch and the few knobs that change the character most.
    class PlayPage : public Page
    {
    public:
        PlayPage (HamarProcessor& p, InstrumentView::KeyCentre keyCentre)
            : proc (p), view (p, std::move (keyCentre)), touch (p),
              touchLock (p.apvts, locks::ids().play), toneLock (p.apvts, locks::ids().hammer), micLock (p.apvts, locks::ids().mics)
        {
            auto& s = p.apvts;
            addAndMakeVisible (view);
            addAndMakeVisible (touch);
            touchRow.add (makeKnob (s, P::PlayVelCurve, "Curve"));
            touchRow.add (makeKnob (s, P::PlayDynamics, "Dynamics"));
            touchRow.add (makeKnob (s, P::PlayReleaseVel, "Release"));
            touchRow.addTo (*this);
            toneRow.add (makeKnob (s, P::HammerHardness, "Hardness"));
            toneRow.add (makeKnob (s, P::StringBrightness, "Brightness"));
            toneRow.add (makeKnob (s, P::StringSustain, "Sustain"));
            toneRow.add (makeKnob (s, P::StringDetune, "Detune"));
            toneRow.add (makeKnob (s, P::BodyResonance, "Board"));
            toneRow.add (makeKnob (s, P::SymAmount, "Resonance"));
            toneRow.addTo (*this);
            micRow.add (makeKnob (s, P::MicALevel, "Close"));
            micRow.add (makeKnob (s, P::MicBLevel, "Player"));
            micRow.add (makeKnob (s, P::HammerNoise, "Mechanics"));
            micRow.addTo (*this);
            // Electric models: their own character and the amp instead of strings, board and mics
            electricRow.add (makeKnob (s, P::HammerHardness, "Hardness"));
            electricRow.add (makeKnob (s, P::EpBell, "Bell"));
            electricRow.add (makeKnob (s, P::EpPickup, "Bark"));
            electricRow.add (makeKnob (s, P::EpTonebar, "Tonebar"));
            electricRow.add (makeKnob (s, P::EpDecay, "Decay"));
            electricRow.add (makeKnob (s, P::EpDrive, "Drive"));
            electricRow.addTo (*this);
            ampRow.add (makeChoice (s, P::EpTremMode, "Tremolo"), 1.4f);
            ampRow.add (makeKnob (s, P::EpTremRate, "Rate"));
            ampRow.add (makeKnob (s, P::EpTremDepth, "Depth"));
            ampRow.addTo (*this);
            for (auto* l : { &touchLock, &toneLock, &micLock }) addAndMakeVisible (l);
            updateRows();
        }

        void tick() override
        {
            view.tick();
            touch.tick();
            if (dsp::model (juce::roundToInt (proc.apvts.getRawParameterValue (P::paramId (P::Model))->load())).electric != electric)
                updateRows();
        }

        void paint (juce::Graphics& g) override
        {
            drawPanel (g, touchArea.toFloat(), "Touch");
            drawPanel (g, toneArea.toFloat(), "Character");
            drawPanel (g, micArea.toFloat(), electric ? "Amp" : "Mics");
        }

        void resized() override
        {
            auto r = getLocalBounds();
            auto bottom = r.removeFromBottom (190);
            r.removeFromBottom (10);
            view.setBounds (r);
            touchArea = bottom.removeFromLeft (380);
            bottom.removeFromLeft (10);
            micArea = bottom.removeFromRight (270);
            bottom.removeFromRight (10);
            toneArea = bottom;
            touchLock.place (touchArea);
            toneLock.place (toneArea);
            micLock.place (micArea);
            auto t = touchArea.reduced (12, 0).withTrimmedTop (32).withTrimmedBottom (10);
            touch.setBounds (t.removeFromLeft (150).reduced (0, 4));
            t.removeFromLeft (8);
            touchRow.layout (t.withSizeKeepingCentre (t.getWidth(), 100));
            auto c = toneArea.reduced (12, 0).withTrimmedTop (32).withTrimmedBottom (10);
            toneRow.layout (c.withSizeKeepingCentre (c.getWidth(), 100));
            electricRow.layout (c.withSizeKeepingCentre (c.getWidth(), 100));
            auto m = micArea.reduced (12, 0).withTrimmedTop (32).withTrimmedBottom (10);
            micRow.layout (m.withSizeKeepingCentre (m.getWidth(), 100));
            ampRow.layout (m.withSizeKeepingCentre (m.getWidth(), 100));
        }

    private:
        void updateRows()
        {
            electric = dsp::model (juce::roundToInt (proc.apvts.getRawParameterValue (P::paramId (P::Model))->load())).electric;
            toneRow.setVisible (! electric);
            micRow.setVisible (! electric);
            electricRow.setVisible (electric);
            ampRow.setVisible (electric);
            micLock.setVisible (! electric);
            repaint();
        }

        HamarProcessor& proc;
        bool electric = false;
        InstrumentView view;
        TouchCurve touch;
        ControlRow touchRow, toneRow, micRow, electricRow, ampRow;
        PanelLock touchLock, toneLock, micLock;
        juce::Rectangle<int> touchArea, toneArea, micArea;
    };
}
