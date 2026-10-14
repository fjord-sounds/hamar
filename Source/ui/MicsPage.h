#pragma once
#include "MicStage.h"

namespace hamar::ui
{
    // The two microphone pairs on a stage around the instrument: close and at the player's position (or further away),
    // quick placements, and the knobs for position, distance, width and level.
    class MicsPage : public Page
    {
    public:
        explicit MicsPage (HamarProcessor& p)
            : stage (p), placements (p.apvts), lock (p.apvts, locks::ids().mics)
        {
            auto& s = p.apvts;
            addAndMakeVisible (stage);
            addAndMakeVisible (placements);
            closeRow.add (makeKnob (s, P::MicAX, "Position"));
            closeRow.add (makeKnob (s, P::MicAY, "Distance"));
            closeRow.add (makeKnob (s, P::MicAWidth, "Width"));
            closeRow.add (makeKnob (s, P::MicALevel, "Level"));
            closeRow.addTo (*this);
            playerRow.add (makeKnob (s, P::MicBX, "Position"));
            playerRow.add (makeKnob (s, P::MicBY, "Distance"));
            playerRow.add (makeKnob (s, P::MicBWidth, "Width"));
            playerRow.add (makeKnob (s, P::MicBLevel, "Level"));
            playerRow.addTo (*this);
            addAndMakeVisible (lock);
        }

        void tick() override { stage.tick(); }

        void paint (juce::Graphics& g) override
        {
            drawPanel (g, closeArea.toFloat(), "Close pair");
            drawPanel (g, playerArea.toFloat(), "Player pair");
        }

        void resized() override
        {
            auto r = getLocalBounds();
            auto bottom = r.removeFromBottom (190);
            r.removeFromBottom (10);
            stage.setBounds (r);
            placements.setBounds (r.reduced (14, 12).removeFromTop (28).removeFromLeft (440));
            closeArea = bottom.removeFromLeft (bottom.getWidth() / 2 - 5);
            bottom.removeFromLeft (10);
            playerArea = bottom;
            lock.place (playerArea);
            closeRow.layout (closeArea.reduced (12, 0).withTrimmedTop (36).withHeight (110));
            playerRow.layout (playerArea.reduced (12, 0).withTrimmedTop (36).withHeight (110));
        }

    private:
        MicStage stage;
        MicPresets placements;
        ControlRow closeRow, playerRow;
        PanelLock lock;
        juce::Rectangle<int> closeArea, playerArea;
    };
}
