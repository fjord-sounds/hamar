#pragma once
#include "Theme.h"
#include "../PluginProcessor.h"
#include "../presets/Locks.h"

// Hamar's own small widgets: the model menu, the pedals, the touch curve, curves over the keyboard, the
// temperament wheel and the felt curve.
namespace hamar::ui
{
    namespace P = params;

    // Sets one parameter from a drag or a click, as one undo step / host gesture
    struct ParamHandle
    {
        juce::RangedAudioParameter* param = nullptr;

        ParamHandle (APVTS& s, P::Id id) : param (s.getParameter (P::paramId (id))) {}
        float get() const { return param->convertFrom0to1 (param->getValue()); }
        void begin() { param->beginChangeGesture(); }
        void set (float plain) { param->setValueNotifyingHost (param->convertTo0to1 (plain)); }
        void end() { param->endChangeGesture(); }
        void setOnce (float plain) { begin(); set (plain); end(); }
    };

    // A padlock in the title row of a panel (drawPanel): this part stays when another preset is loaded
    class PanelLock : public LockButton
    {
    public:
        PanelLock (APVTS& state, int group) : LockButton (state, locks::set(), group, false) {}

        void place (juce::Rectangle<int> panel)
        {
            setBounds (panel.reduced (16, 0).removeFromTop (30).removeFromRight (18).withSizeKeepingCentre (18, 18));
        }
    };

    inline std::unique_ptr<Knob> makeKnob (APVTS& s, P::Id id, const juce::String& label)
    {
        return std::make_unique<Knob> (s, P::paramId (id), label);
    }

    inline std::unique_ptr<juce::Component> makeChoice (APVTS& s, P::Id id, const juce::String& label)
    {
        return labelled (std::make_unique<Choice> (s, P::paramId (id)), label);
    }

    // The key table as the UI sees it (built from the current parameters on the message thread, for the curves)
    struct TableWatcher
    {
        dsp::KeyTable table;
        dsp::InstrumentSettings settings;
        bool valid = false;

        // true when something changed since the last call
        bool update (const HamarProcessor& proc)
        {
            const auto now = proc.currentSettings().instrument;
            if (valid && now == settings) return false;
            settings = now;
            table.update (now);
            valid = true;
            return true;
        }

        const dsp::KeyPhysics& operator[] (int key) const { return table[key]; }
        int lowKey() const { return dsp::model (settings.model).lowKey; }
    };

    inline const char* categoryName (dsp::Category c)
    {
        static const char* names[] = { "Grand", "Upright", "Electric", "Historic", "Dance", "Fantasy" };
        return names[(int) c];
    }

    // The model: a pill with the category colour; the menu lists the models by category
    class ModelMenu : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit ModelMenu (APVTS& s) : model (s, P::Model)
        {
            setTooltip ("The instrument: every model is its own set of strings, hammers, soundboard and case");
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        void tick()
        {
            const int m = juce::roundToInt (model.get());
            if (m != shown) { shown = m; repaint(); }
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            const auto& spec = dsp::model (juce::roundToInt (model.get()));
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (isMouseOver() ? p.accentSoft : p.panel);
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            g.setColour (p.line);
            g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
            auto inner = r.reduced (12.0f, 0.0f);
            const auto dot = inner.removeFromLeft (8.0f).withSizeKeepingCentre (8.0f, 8.0f);
            g.setColour (categoryColour ((int) spec.category, dark));
            g.fillEllipse (dot);
            inner.removeFromLeft (8.0f);
            {
                const auto a = inner.removeFromRight (10.0f).withSizeKeepingCentre (9.0f, 5.0f);
                juce::Path chevron;
                chevron.startNewSubPath (a.getX(), a.getY());
                chevron.lineTo (a.getCentreX(), a.getBottom());
                chevron.lineTo (a.getRight(), a.getY());
                g.setColour (p.textDim);
                g.strokePath (chevron, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour (p.textDim);
            drawSpacedText (g, juce::String (categoryName (spec.category)).toUpperCase(), inner.removeFromLeft (70.0f), 9.0f,
                            juce::Justification::centredLeft, 0.18f);
            g.setColour (p.text);
            g.setFont (font (13.0f, true));
            g.drawText (spec.name, inner, juce::Justification::centredLeft);
        }

        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }

        void mouseDown (const juce::MouseEvent&) override
        {
            juce::PopupMenu menu;
            const int current = juce::roundToInt (model.get());
            const auto& list = dsp::models();
            for (int c = 0; c < 6; ++c)
            {
                bool header = false;
                for (int i = 0; i < (int) list.size(); ++i)
                {
                    if ((int) list[(size_t) i].category != c) continue;
                    if (! header) { menu.addSectionHeader (categoryName ((dsp::Category) c)); header = true; }
                    menu.addItem (i + 1, list[(size_t) i].name, true, i == current);
                }
            }
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()),
                                [safe = juce::Component::SafePointer<ModelMenu> (this)] (int result)
                                {
                                    if (safe != nullptr && result > 0) safe->model.setOnce ((float) (result - 1));
                                });
        }

    private:
        ParamHandle model;
        int shown = -1;
    };

    // Una corda, sostenuto, sustain: brass pedals that go down with the MIDI pedals; clicking the right one holds the
    // sustain pedal (handy without a pedal at hand)
    class PedalStrip : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit PedalStrip (HamarProcessor& p) : proc (p)
        {
            setTooltip ("Pedals (MIDI CC 67, 66, 64). Click the right pedal to hold the sustain pedal.");
        }

        void tick()
        {
            const auto& d = proc.getEngine().display();
            const std::array<float, 3> now { d.soft.load(), d.sostenuto.load() ? 1.0f : 0.0f, d.sustain.load() };
            if (now != shown) { shown = now; repaint(); }
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            auto r = getLocalBounds().toFloat();
            g.setColour (p.panel);
            g.fillRoundedRectangle (r, 10.0f);
            g.setColour (p.line);
            g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
            static const char* names[] = { "UNA CORDA", "SOSTENUTO", "SUSTAIN" };
            const float w = r.getWidth() / 3.0f;
            for (int i = 0; i < 3; ++i)
            {
                auto cell = juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight()).reduced (6.0f, 8.0f);
                auto label = cell.removeFromBottom (14.0f);
                const float down = shown[(size_t) i];
                auto pedal = cell.withSizeKeepingCentre (std::min (22.0f, cell.getWidth()), cell.getHeight() - 6.0f).translated (0.0f, 6.0f * down);
                juce::Path shape;
                shape.addRoundedRectangle (pedal.getX(), pedal.getY(), pedal.getWidth(), pedal.getHeight(), 5.0f, 5.0f, false, false, true, true);
                const auto c0 = brassAt (dark, 0.15f), c1 = brassAt (dark, 0.75f);
                g.setGradientFill ({ c0.withMultipliedAlpha (down > 0.01f ? 1.0f : 0.55f), pedal.getX(), pedal.getY(),
                                     c1.withMultipliedAlpha (down > 0.01f ? 1.0f : 0.55f), pedal.getRight(), pedal.getBottom(), false });
                g.fillPath (shape);
                if (down > 0.01f) drawGlow (g, pedal.expanded (6.0f), dark, 0.6f * down);
                g.setColour (down > 0.01f ? p.text : p.textDim);
                drawSpacedText (g, names[i], label, 8.0f, juce::Justification::centred, 0.12f);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.position.x > (float) getWidth() * 2.0f / 3.0f)
                proc.setScreenSustain (! proc.getScreenSustain());
        }

    private:
        HamarProcessor& proc;
        std::array<float, 3> shown { -1.0f, -1.0f, -1.0f };
    };

    // Touch: how key velocity becomes hammer speed. Drag up / down to bend the curve; the dot is the last note.
    class TouchCurve : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        TouchCurve (HamarProcessor& p) : proc (p), curve (p.apvts, P::PlayVelCurve), dynamics (p.apvts, P::PlayDynamics)
        {
            setTooltip ("Touch: key velocity (across) to hammer speed (up). Drag up or down to bend the curve.");
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        }

        void tick()
        {
            const float v = proc.getLastVelocity();
            const uint32_t n = proc.getNoteCount();
            if (v != lastVel || n != lastCount || curve.get() != lastCurve || dynamics.get() != lastDyn)
            {
                lastVel = v; lastCount = n; lastCurve = curve.get(); lastDyn = dynamics.get();
                repaint();
            }
        }

        // Hammer speed (log, 0..1 over the widest range) for a velocity, as the engine maps it
        float map (float vel) const
        {
            const float shaped = std::pow (std::clamp (vel, 0.0f, 1.0f), std::exp2 (-1.2f * curve.get()));
            const float range = (1.5f + 4.0f * dynamics.get()) / 5.5f;   // octaves of hammer speed, relative to the widest
            return 1.0f - range * (1.0f - shaped);
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            auto r = getLocalBounds().toFloat().reduced (2.0f);
            g.setColour (p.background.withAlpha (0.5f));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (p.line);
            for (int i = 1; i < 4; ++i)
            {
                g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) i / 4.0f), r.getX(), r.getRight());
                g.drawVerticalLine ((int) (r.getX() + r.getWidth() * (float) i / 4.0f), r.getY(), r.getBottom());
            }
            juce::Path path;
            for (int i = 0; i <= 48; ++i)
            {
                const float x = (float) i / 48.0f;
                const juce::Point<float> pt (r.getX() + x * r.getWidth(), r.getBottom() - map (x) * r.getHeight());
                if (i == 0) path.startNewSubPath (pt); else path.lineTo (pt);
            }
            strokeGlow (g, path, p.accent, 2.0f, dark);
            if (lastCount > 0)
            {
                const juce::Point<float> pt (r.getX() + lastVel * r.getWidth(), r.getBottom() - map (lastVel) * r.getHeight());
                g.setColour (p.accent);
                g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (pt));
            }
        }

        void mouseDown (const juce::MouseEvent&) override { startValue = curve.get(); curve.begin(); }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            curve.set (juce::jlimit (-1.0f, 1.0f, startValue - (float) e.getDistanceFromDragStartY() / 80.0f));
        }
        void mouseUp (const juce::MouseEvent&) override { curve.end(); }
        void mouseDoubleClick (const juce::MouseEvent&) override { curve.setOnce (0.0f); }

    private:
        HamarProcessor& proc;
        ParamHandle curve, dynamics;
        float lastVel = -1.0f, lastCurve = -9.0f, lastDyn = -9.0f, startValue = 0.0f;
        uint32_t lastCount = 0;
    };

    // A value over the keyboard (voicing, inharmonicity, decay, stretch …), with a soft fill and a scale
    class KeyCurve : public juce::Component
    {
    public:
        struct Line
        {
            std::function<float (int key)> value;   // in display units
            juce::String label;
            bool primary = true;
        };

        KeyCurve (juce::String titleText, bool logScale, float minValue, float maxValue, std::function<juce::String (float)> formatter)
            : title (std::move (titleText)), log (logScale), lo (minValue), hi (maxValue), format (std::move (formatter)) {}

        void setLines (std::vector<Line> l) { lines = std::move (l); repaint(); }
        void setRange (int lowKey, int highKey) { first = lowKey; last = highKey; }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            auto r = getLocalBounds().toFloat();
            g.setColour (p.background.withAlpha (0.5f));
            g.fillRoundedRectangle (r, 8.0f);
            auto plot = r.reduced (10.0f).withTrimmedLeft (34.0f).withTrimmedTop (14.0f).withTrimmedBottom (12.0f);
            g.setColour (p.textDim);
            drawSpacedText (g, title.toUpperCase(), r.reduced (10.0f, 4.0f).removeFromTop (14.0f), 8.5f, juce::Justification::centredLeft, 0.16f);
            // scale
            g.setFont (font (9.0f));
            for (int i = 0; i <= 2; ++i)
            {
                const float t = (float) i / 2.0f;
                const float v = log ? lo * std::pow (hi / lo, t) : lo + t * (hi - lo);
                const float y = plot.getBottom() - t * plot.getHeight();
                g.setColour (p.line);
                g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
                g.setColour (p.textDim);
                g.drawText (format (v), juce::Rectangle<float> (r.getX() + 2.0f, y - 6.0f, 40.0f, 12.0f), juce::Justification::centredLeft);
            }
            for (int c = 1; c <= 7; ++c)   // the Cs
            {
                const int key = 12 * c + 12;
                if (key < first || key > last) continue;
                const float x = xFor (key, plot);
                g.setColour (p.line.withAlpha (0.6f));
                g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
                g.setColour (p.textDim);
                g.drawText ("C" + juce::String (c - 1), juce::Rectangle<float> (x - 12.0f, plot.getBottom() + 1.0f, 24.0f, 11.0f), juce::Justification::centred);
            }
            for (const auto& line : lines)
            {
                juce::Path path;
                for (int key = first; key <= last; ++key)
                {
                    const float v = std::clamp (line.value (key), lo, hi);
                    const float t = log ? std::log (v / lo) / std::log (hi / lo) : (v - lo) / (hi - lo);
                    const juce::Point<float> pt (xFor (key, plot), plot.getBottom() - t * plot.getHeight());
                    if (key == first) path.startNewSubPath (pt); else path.lineTo (pt);
                }
                if (line.primary)
                {
                    juce::Path fill (path);
                    fill.lineTo (plot.getRight(), plot.getBottom());
                    fill.lineTo (plot.getX(), plot.getBottom());
                    fill.closeSubPath();
                    g.setGradientFill ({ p.accent.withAlpha (dark ? 0.22f : 0.14f), 0.0f, plot.getY(), p.accent.withAlpha (0.0f), 0.0f, plot.getBottom(), false });
                    g.fillPath (fill);
                    strokeGlow (g, path, p.accent, 1.8f, dark);
                }
                else
                {
                    g.setColour (p.textDim.withAlpha (0.8f));
                    const float dash[] = { 4.0f, 3.0f };
                    juce::Path dashed;
                    juce::PathStrokeType (1.2f).createDashedStroke (dashed, path, dash, 2);
                    g.fillPath (dashed);
                }
            }
        }

    private:
        float xFor (int key, juce::Rectangle<float> plot) const
        {
            return plot.getX() + plot.getWidth() * (float) (key - first) / (float) std::max (1, last - first);
        }

        juce::String title;
        bool log;
        float lo, hi;
        std::function<juce::String (float)> format;
        std::vector<Line> lines;
        int first = 21, last = 108;
    };

    // The twelve notes on a circle: how far each sits from equal temperament (and the key it is built on, clickable)
    class TemperamentWheel : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit TemperamentWheel (APVTS& s) : temperament (s, P::TuneTemperament), root (s, P::TuneKey)
        {
            setTooltip ("Cents from equal temperament for every note. Click a note to build the temperament on it.");
        }

        void tick()
        {
            const int t = juce::roundToInt (temperament.get()), k = juce::roundToInt (root.get());
            if (t != shownT || k != shownK) { shownT = t; shownK = k; repaint(); }
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            const auto r = getLocalBounds().toFloat();
            const auto c = r.getCentre();
            const float radius = std::min (r.getWidth(), r.getHeight()) * 0.5f - 30.0f;
            const auto& table = dsp::temperaments()[(size_t) std::clamp (shownT, 0, 6)];
            static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            const float aOffset = table[(size_t) ((9 - shownK + 12) % 12)];
            // reference circle (equal) and +-25 cents
            g.setColour (p.line);
            g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c), 1.0f);
            const float dash[] = { 3.0f, 4.0f };
            for (float cents : { -25.0f, 25.0f })
            {
                const float rr = radius * (1.0f + cents / 80.0f);
                juce::Path circle, dashed;
                circle.addEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (c));
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, circle, dash, 2);
                g.setColour (p.line.withAlpha (0.7f));
                g.fillPath (dashed);
            }
            juce::Path shape;
            for (int i = 0; i < 12; ++i)
            {
                // circle of fifths order around the wheel
                const int pc = (i * 7) % 12;
                const float cents = table[(size_t) ((pc - shownK + 12) % 12)] - aOffset;
                const float angle = juce::MathConstants<float>::twoPi * (float) i / 12.0f;
                const float rr = radius * (1.0f + std::clamp (cents, -40.0f, 40.0f) / 80.0f);
                const juce::Point<float> pt (c.x + rr * std::sin (angle), c.y - rr * std::cos (angle));
                if (i == 0) shape.startNewSubPath (pt); else shape.lineTo (pt);
                const juce::Point<float> label (c.x + (radius + 12.0f) * std::sin (angle), c.y - (radius + 12.0f) * std::cos (angle));
                g.setColour (pc == shownK ? p.accent : p.textDim);
                g.setFont (font (pc == shownK ? 11.5f : 10.0f, pc == shownK));
                g.drawText (names[pc], juce::Rectangle<float> (28.0f, 14.0f).withCentre (label), juce::Justification::centred);
                g.setColour (p.accent);
                g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (pt));
            }
            shape.closeSubPath();
            g.setColour (p.accent.withAlpha (dark ? 0.16f : 0.1f));
            g.fillPath (shape);
            strokeGlow (g, shape, p.accent, 1.6f, dark);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto c = getLocalBounds().toFloat().getCentre();
            float angle = std::atan2 (e.position.x - c.x, c.y - e.position.y);
            if (angle < 0.0f) angle += juce::MathConstants<float>::twoPi;
            const int i = juce::roundToInt (angle / juce::MathConstants<float>::twoPi * 12.0f) % 12;
            root.setOnce ((float) ((i * 7) % 12));
        }

    private:
        ParamHandle temperament, root;
        int shownT = -1, shownK = -1;
    };

    // The felt: force against compression on the middle C as the hammer sees it - now (line) and as the model comes
    // (dashed). Up to 1.5 mm and 120 N: a soft blow compresses the felt by a few tenths of a millimetre, a hard one by more.
    class FeltCurve : public juce::Component
    {
    public:
        void update (const dsp::KeyPhysics& middle, float referenceK)
        {
            k = middle.hammerK;
            pExp = middle.hammerP;
            refK = referenceK;
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            auto r = getLocalBounds().toFloat();
            g.setColour (p.background.withAlpha (0.5f));
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (p.textDim);
            drawSpacedText (g, "FELT  " + utf8 ("\xc2\xb7") + "  FORCE OVER COMPRESSION (C4)", r.reduced (10.0f, 4.0f).removeFromTop (14.0f), 8.5f,
                            juce::Justification::centredLeft, 0.16f);
            auto plot = r.reduced (12.0f).withTrimmedTop (14.0f).withTrimmedBottom (12.0f).withTrimmedLeft (30.0f);
            g.setFont (font (9.0f));
            for (int i = 0; i <= 2; ++i)
            {
                const float y = plot.getBottom() - plot.getHeight() * (float) i / 2.0f;
                g.setColour (p.line);
                g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
                g.setColour (p.textDim);
                g.drawText (juce::String (60 * i) + " N", juce::Rectangle<float> (r.getX() + 4.0f, y - 6.0f, 36.0f, 12.0f), juce::Justification::centredLeft);
            }
            for (int i = 0; i <= 3; ++i)
            {
                const float x = plot.getX() + plot.getWidth() * (float) i / 3.0f;
                g.setColour (p.textDim);
                g.drawText (juce::String (0.5f * (float) i, 1) + " mm", juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 1.0f, 40.0f, 11.0f), juce::Justification::centred);
            }
            auto curve = [&] (float stiffness)
            {
                juce::Path path;
                for (int i = 0; i <= 80; ++i)
                {
                    const float d = 0.0015f * (float) i / 80.0f;
                    const float f = stiffness * std::pow (d, pExp);
                    const juce::Point<float> pt (plot.getX() + plot.getWidth() * (float) i / 80.0f,
                                                 plot.getBottom() - plot.getHeight() * std::min (1.0f, f / 120.0f));
                    if (i == 0) path.startNewSubPath (pt); else path.lineTo (pt);
                    if (f > 120.0f) break;
                }
                return path;
            };
            juce::Path dashed;
            const float dash[] = { 4.0f, 3.0f };
            juce::PathStrokeType (1.2f).createDashedStroke (dashed, curve (refK), dash, 2);
            g.setColour (p.textDim);
            g.fillPath (dashed);
            strokeGlow (g, curve (k), p.accent, 2.0f, dark);
        }

    private:
        float k = 1.0e9f, refK = 1.0e9f, pExp = 2.5f;
    };

}
