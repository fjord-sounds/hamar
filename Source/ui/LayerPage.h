#pragma once
#include "Widgets.h"

namespace hamar::ui
{
    // The layer's envelope (attack, sustain while the key is held, release)
    class LayerEnvelope : public juce::Component
    {
    public:
        explicit LayerEnvelope (APVTS& s) : attack (s, P::LayerAttack), release (s, P::LayerRelease), level (s, P::LayerLevel) {}

        void tick()
        {
            const std::array<float, 3> now { attack.get(), release.get(), level.get() };
            if (now != shown) { shown = now; repaint(); }
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            auto r = getLocalBounds().toFloat().reduced (2.0f);
            g.setColour (p.background.withAlpha (0.5f));
            g.fillRoundedRectangle (r, 6.0f);
            auto plot = r.reduced (10.0f, 12.0f);
            const float total = shown[0] + 1.5f + shown[1];
            auto xFor = [&] (float t) { return plot.getX() + plot.getWidth() * t / total; };
            const float top = plot.getBottom() - plot.getHeight() * (0.2f + 0.8f * shown[2]);
            juce::Path path;
            path.startNewSubPath (plot.getX(), plot.getBottom());
            path.lineTo (xFor (shown[0]), top);
            path.lineTo (xFor (shown[0] + 1.5f), top);
            for (int i = 1; i <= 24; ++i)
            {
                const float t = (float) i / 24.0f;
                path.lineTo (xFor (shown[0] + 1.5f + t * shown[1]), top + (plot.getBottom() - top) * (1.0f - std::exp (-4.0f * t)) / (1.0f - std::exp (-4.0f)));
            }
            juce::Path fill (path);
            fill.lineTo (plot.getRight(), plot.getBottom());
            fill.closeSubPath();
            g.setGradientFill ({ p.accent.withAlpha (dark ? 0.25f : 0.15f), 0.0f, top, p.accent.withAlpha (0.0f), 0.0f, plot.getBottom(), false });
            g.fillPath (fill);
            strokeGlow (g, path, p.accent, 1.8f, dark);
        }

    private:
        ParamHandle attack, release, level;
        std::array<float, 3> shown { -1.0f, -1.0f, -1.0f };
    };

    // One octave of keys showing the chord a key plays (from C) and the order of the strum
    class ChordPreview : public juce::Component
    {
    public:
        explicit ChordPreview (APVTS& s) : chord (s, P::ToolsChord), strum (s, P::ToolsStrum), direction (s, P::ToolsStrumDir) {}

        void tick()
        {
            const std::array<float, 3> now { chord.get(), strum.get(), direction.get() };
            if (now != shown) { shown = now; repaint(); }
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            static const int intervals[][4] = { { 0, -1, -1, -1 }, { 0, 12, -1, -1 }, { 0, 7, -1, -1 }, { 0, 4, 7, -1 }, { 0, 3, 7, -1 },
                                                { 0, 4, 7, 11 }, { 0, 3, 7, 10 }, { 0, 2, 7, -1 }, { 0, 7, 12, -1 } };
            const int type = std::clamp (juce::roundToInt (shown[0]), 0, 8);
            auto r = getLocalBounds().toFloat().reduced (2.0f);
            g.setColour (p.background.withAlpha (0.5f));
            g.fillRoundedRectangle (r, 6.0f);
            auto keys = r.reduced (10.0f, 12.0f);
            const int whites = 9;   // C .. D of the next octave
            const float w = keys.getWidth() / (float) whites;
            static const int whiteNotes[] = { 0, 2, 4, 5, 7, 9, 11, 12, 14 };
            auto inChord = [&] (int n) { for (int i = 0; i < 4; ++i) if (intervals[type][i] == n) return i; return -1; };
            for (int i = 0; i < whites; ++i)
            {
                auto k = juce::Rectangle<float> (keys.getX() + w * (float) i, keys.getY(), w - 2.0f, keys.getHeight());
                const int order = inChord (whiteNotes[i]);
                g.setColour (order >= 0 ? p.accent.withAlpha (0.85f) : p.keyWhite);
                g.fillRoundedRectangle (k, 3.0f);
                if (order >= 0) drawOrder (g, k, order, type, p);
            }
            static const int blackAfter[] = { 0, 1, 3, 4, 5, 7 };
            static const int blackNotes[] = { 1, 3, 6, 8, 10, 13 };
            for (int i = 0; i < 6; ++i)
            {
                auto k = juce::Rectangle<float> (keys.getX() + w * ((float) blackAfter[i] + 0.68f), keys.getY(), w * 0.62f, keys.getHeight() * 0.6f);
                const int order = inChord (blackNotes[i]);
                g.setColour (order >= 0 ? p.accent : p.keyBlack);
                g.fillRoundedRectangle (k, 3.0f);
                if (order >= 0) drawOrder (g, k, order, type, p);
            }
        }

    private:
        void drawOrder (juce::Graphics& g, juce::Rectangle<float> key, int order, int type, const fjord::base::Palette& p)
        {
            if (shown[1] < 0.002f) return;
            static const int sizes[] = { 1, 2, 2, 3, 3, 4, 4, 3, 3 };
            const int size = sizes[type];
            const int dir = juce::roundToInt (shown[2]);
            const int n = dir == 1 ? size - order : order + 1;   // alternate shows the upward stroke
            g.setColour (p.panel);
            g.setFont (font (11.0f, true));
            g.drawText (juce::String (n), key.removeFromBottom (18.0f), juce::Justification::centred);
        }

        ParamHandle chord, strum, direction;
        std::array<float, 3> shown { -1.0f, -1.0f, -1.0f };
    };

    // Extras around the piano: a pad or strings layer, chord and strum, and the condition of an old instrument
    class LayerPage : public Page
    {
    public:
        explicit LayerPage (HamarProcessor& p)
            : envelope (p.apvts), chords (p.apvts), playLock (p.apvts, locks::ids().play),
              layerLock (p.apvts, locks::ids().layer), conditionLock (p.apvts, locks::ids().condition)
        {
            auto& s = p.apvts;
            addAndMakeVisible (envelope);
            addAndMakeVisible (chords);
            layerTop.add (makeChoice (s, P::LayerType, "Sound"), 1.6f);
            layerTop.add (makeKnob (s, P::LayerLevel, "Level"));
            layerTop.add (makeKnob (s, P::LayerOctave, "Octave"));
            layerTop.addTo (*this);
            layerRow.add (makeKnob (s, P::LayerAttack, "Attack"));
            layerRow.add (makeKnob (s, P::LayerRelease, "Release"));
            layerRow.add (makeKnob (s, P::LayerTone, "Tone"));
            layerRow.add (makeKnob (s, P::LayerDetune, "Detune"));
            layerRow.addTo (*this);
            toolTop.add (makeChoice (s, P::ToolsChord, "Chord"), 1.4f);
            toolTop.add (makeChoice (s, P::ToolsStrumDir, "Direction"), 1.4f);
            toolTop.addTo (*this);
            toolRow.add (makeKnob (s, P::ToolsStrum, "Strum"));
            toolRow.add (makeKnob (s, P::PlayTranspose, "Transpose"));
            toolRow.addTo (*this);
            conditionRow.add (makeKnob (s, P::CondDetune, "Out of tune"));
            conditionRow.add (makeKnob (s, P::CondWear, "Hammer wear"));
            conditionRow.add (makeKnob (s, P::CondDampers, "Damper leak"));
            conditionRow.add (makeKnob (s, P::CondNoise, "Action noise"));
            conditionRow.addTo (*this);
            addAndMakeVisible (playLock);
            addAndMakeVisible (layerLock);
            addAndMakeVisible (conditionLock);
        }

        void tick() override
        {
            envelope.tick();
            chords.tick();
        }

        void paint (juce::Graphics& g) override
        {
            drawPanel (g, layerArea.toFloat(), "Layer");
            drawPanel (g, toolArea.toFloat(), "Chord and strum");
            drawPanel (g, conditionArea.toFloat(), "Condition");
            const auto& p = Theme::get().palette();
            g.setColour (p.textDim);
            g.setFont (font (12.0f));
            g.drawFittedText ("Time leaves its marks: the keys drift apart, the hammers grow hard and uneven, the dampers no longer "
                              "stop every string and the action clacks.", conditionText, juce::Justification::topLeft, 3, 1.0f);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            auto top = r.removeFromTop (r.getHeight() - 200);
            r.removeFromTop (10);
            layerArea = top.removeFromLeft (top.getWidth() / 2 - 5);
            top.removeFromLeft (10);
            toolArea = top;
            conditionArea = r;
            playLock.place (toolArea);
            layerLock.place (layerArea);
            conditionLock.place (conditionArea);

            auto la = layerArea.reduced (12, 0).withTrimmedTop (36).withTrimmedBottom (12);
            layerTop.layout (la.removeFromTop (100));
            la.removeFromTop (8);
            layerRow.layout (la.removeFromBottom (100));
            la.removeFromBottom (8);
            envelope.setBounds (la);

            auto ta = toolArea.reduced (12, 0).withTrimmedTop (36).withTrimmedBottom (12);
            toolTop.layout (ta.removeFromTop (60));
            ta.removeFromTop (8);
            toolRow.layout (ta.removeFromBottom (100));
            ta.removeFromBottom (8);
            chords.setBounds (ta.withSizeKeepingCentre (std::min (ta.getWidth(), 420), ta.getHeight()));

            auto ca = conditionArea.reduced (12, 0).withTrimmedTop (36).withTrimmedBottom (10);
            conditionText = ca.removeFromLeft (ca.getWidth() / 3).reduced (6, 10);
            conditionRow.layout (ca.withHeight (110));
        }

    private:
        LayerEnvelope envelope;
        ChordPreview chords;
        ControlRow layerTop, layerRow, toolTop, toolRow, conditionRow;
        PanelLock playLock, layerLock, conditionLock;
        juce::Rectangle<int> layerArea, toolArea, conditionArea, conditionText;
    };
}
