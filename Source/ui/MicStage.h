#pragma once
#include "Widgets.h"

// The microphones around the instrument. The instrument sits in the middle of a stage with distance rings; each mic
// pair orbits it - left is the bass side, right the treble side, further out is further away - and points at it with
// its pickup pattern drawn as a soft lobe. Drag a pair to move it, drag a capsule (or scroll over the pair) to change
// its width. A meter beside each label shows what the pair hears. Electric models have no mics: they are heard
// through their pickups.
namespace hamar::ui
{
    class MicStage : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit MicStage (HamarProcessor& p)
            : proc (p),
              ax (p.apvts, P::MicAX), ay (p.apvts, P::MicAY), aw (p.apvts, P::MicAWidth), al (p.apvts, P::MicALevel),
              bx (p.apvts, P::MicBX), by (p.apvts, P::MicBY), bw (p.apvts, P::MicBWidth), bl (p.apvts, P::MicBLevel)
        {
            setTooltip ("Drag a pair around the instrument; drag a capsule or scroll over a pair to change its width");
        }

        void tick()
        {
            const auto& d = proc.getEngine().display();
            for (int m = 0; m < 2; ++m)
                meter[(size_t) m] = std::max (d.micPeak[(size_t) m].load (std::memory_order_relaxed), meter[(size_t) m] * 0.85f);
            modelIndex = juce::roundToInt (proc.apvts.getRawParameterValue (P::paramId (P::Model))->load());
            phase += 0.05f;
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            const auto r = getLocalBounds().toFloat();
            const auto& spec = dsp::model (modelIndex);
            g.setColour (p.background.withAlpha (0.45f));
            g.fillRoundedRectangle (r, 12.0f);

            // Distance rings (metres) and the angle the pairs can take
            const auto c = centre();
            for (int i = 0; i <= 3; ++i)
            {
                const float d = 0.5f * (float) i;
                const float rr = radiusFor (d);
                juce::Path arc;
                arc.addCentredArc (c.x, c.y, rr, rr, 0.0f, -maxAngle - 0.15f, maxAngle + 0.15f, true);
                const float dash[] = { 3.0f, 5.0f };
                juce::Path dashed;
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, arc, dash, 2);
                g.setColour (p.line.withAlpha (i == 0 ? 0.9f : 0.7f));
                g.fillPath (dashed);
                g.setColour (p.textDim);
                g.setFont (font (9.0f));
                g.drawText (juce::String (metres (d), 1) + " m", juce::Rectangle<float> (60.0f, 12.0f).withCentre ({ c.x + rr * std::sin (maxAngle + 0.28f), c.y - rr * std::cos (maxAngle + 0.28f) }),
                            juce::Justification::centred);
            }
            g.setColour (p.textDim);
            drawSpacedText (g, "BASS", juce::Rectangle<float> (60.0f, 14.0f).withCentre ({ c.x - radiusFor (1.5f) * 0.98f, c.y + 18.0f }), 8.5f, juce::Justification::centred, 0.2f);
            drawSpacedText (g, "TREBLE", juce::Rectangle<float> (60.0f, 14.0f).withCentre ({ c.x + radiusFor (1.5f) * 0.98f, c.y + 18.0f }), 8.5f, juce::Justification::centred, 0.2f);

            drawInstrument (g, spec, dark, p);

            if (spec.electric)
            {
                auto box = r.withSizeKeepingCentre (360.0f, 58.0f).withY (r.getY() + 24.0f);
                g.setColour (p.panel.withAlpha (0.92f));
                g.fillRoundedRectangle (box, 10.0f);
                g.setColour (p.line);
                g.drawRoundedRectangle (box, 10.0f, 1.0f);
                g.setColour (p.text);
                drawSpacedText (g, "HEARD THROUGH ITS PICKUPS", box.removeFromTop (32.0f), 10.0f, juce::Justification::centred, 0.2f, true);
                g.setColour (p.textDim);
                g.setFont (font (11.0f));
                g.drawText ("Electric models have no mics - the amp is on the Instrument page", box, juce::Justification::centredTop);
                return;
            }

            for (int m = 1; m >= 0; --m) drawPair (g, m, p, dark);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (dsp::model (modelIndex).electric) return;
            hit = hitTest (e.position);
            if (hit.pair < 0) return;
            if (hit.capsule) width (hit.pair).begin();
            else { x (hit.pair).begin(); y (hit.pair).begin(); }
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (hit.pair < 0) return;
            const auto c = centre();
            const auto v = e.position - c;
            if (hit.capsule)
            {
                // Width: the capsule's distance from the pair's centre
                const auto mid = pairCentre (hit.pair);
                const float half = e.position.getDistanceFrom (mid);
                width (hit.pair).set (std::clamp (half / (0.28f * spanWidth()), 0.0f, 1.0f));
                return;
            }
            const float angle = std::atan2 (v.x, -v.y);
            const float dist = distanceFor (v.getDistanceFromOrigin());
            x (hit.pair).set (std::clamp (0.5f + angle / (2.0f * maxAngle), 0.0f, 1.0f));
            y (hit.pair).set (std::clamp (dist - (hit.pair == 1 ? 0.5f : 0.0f), 0.0f, 1.0f));
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (hit.pair < 0) return;
            if (hit.capsule) width (hit.pair).end();
            else { x (hit.pair).end(); y (hit.pair).end(); }
            hit = {};
        }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            const auto h = hitTest (e.position);
            if (h.pair < 0) return;
            width (h.pair).setOnce (std::clamp (width (h.pair).get() + w.deltaY * 0.25f, 0.0f, 1.0f));
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const auto h = hitTest (e.position);
            setMouseCursor (h.pair < 0 ? juce::MouseCursor::NormalCursor
                                       : h.capsule ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
            if (h.pair != hover) { hover = h.pair; repaint(); }
        }

        void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

    private:
        struct Hit { int pair = -1; bool capsule = false; };
        static constexpr float maxAngle = 1.25f;   // radians either side of straight up

        ParamHandle& x (int m) { return m == 0 ? ax : bx; }
        ParamHandle& y (int m) { return m == 0 ? ay : by; }
        ParamHandle& width (int m) { return m == 0 ? aw : bw; }

        juce::Point<float> centre() const { return { (float) getWidth() * 0.5f, (float) getHeight() * 0.78f }; }
        float innerRadius() const { return std::min ((float) getWidth() * 0.14f, (float) getHeight() * 0.27f); }
        float outerRadius() const { return std::min ((float) getWidth() * 0.47f, (float) getHeight() * 0.74f); }
        float radiusFor (float d) const { return innerRadius() + d / 1.5f * (outerRadius() - innerRadius()); }
        float distanceFor (float radius) const { return (radius - innerRadius()) / std::max (1.0f, outerRadius() - innerRadius()) * 1.5f; }
        static float metres (float d) { return 0.3f + 2.4f * d; }
        float spanWidth() const { return innerRadius() * 1.6f; }

        juce::Point<float> pairCentre (int m) const
        {
            const float px = (m == 0 ? ax : bx).get(), py = (m == 0 ? ay : by).get() + (m == 1 ? 0.5f : 0.0f);
            const float angle = (px - 0.5f) * 2.0f * maxAngle, rr = radiusFor (py);
            const auto c = centre();
            return { c.x + rr * std::sin (angle), c.y - rr * std::cos (angle) };
        }

        // Capsule positions: perpendicular to the line to the centre
        std::array<juce::Point<float>, 2> capsules (int m) const
        {
            const auto mid = pairCentre (m);
            const auto dir = (centre() - mid) / std::max (1.0f, centre().getDistanceFrom (mid));
            const juce::Point<float> side (-dir.y, dir.x);
            const float half = std::max (8.0f, 0.28f * (m == 0 ? aw : bw).get() * spanWidth());
            return { mid - side * half, mid + side * half };
        }

        Hit hitTest (juce::Point<float> pos) const
        {
            for (int m = 0; m < 2; ++m)
            {
                for (const auto& cap : capsules (m))
                    if (pos.getDistanceFrom (cap) < 12.0f) return { m, true };
                if (pos.getDistanceFrom (pairCentre (m)) < 22.0f) return { m, false };
            }
            return {};
        }

        void drawInstrument (juce::Graphics& g, const dsp::ModelSpec& spec, bool dark, const fjord::base::Palette& p)
        {
            const auto c = centre();
            const float w = innerRadius() * 1.1f, h = innerRadius() * 1.05f;
            juce::Path shape;
            if (spec.cabinet == dsp::Cabinet::Grand && ! spec.electric && spec.mechanism == dsp::Mechanism::Strings)
            {
                // Grand from above, keyboard towards the bottom, the bentside on the treble side
                const float l = c.x - w * 0.5f, t = c.y - h, b = c.y + h * 0.12f, rr = l + w;
                shape.startNewSubPath (l, b);
                shape.lineTo (l, t + h * 0.12f);
                shape.quadraticTo (l, t, l + w * 0.15f, t);
                shape.cubicTo (l + w * 0.42f, t, l + w * 0.5f, t + h * 0.32f, l + w * 0.62f, t + h * 0.55f);
                shape.cubicTo (l + w * 0.72f, t + h * 0.74f, rr, t + h * 0.72f, rr, t + h * 0.86f);
                shape.lineTo (rr, b);
                shape.closeSubPath();
            }
            else
                shape.addRoundedRectangle (c.x - w * 0.5f, c.y - h * 0.55f, w, h * 0.67f, 8.0f);
            drawGlow (g, shape.getBounds(), dark, 0.6f);
            g.setColour (caseColour (dark));
            g.fillPath (shape);
            g.setColour (caseEdge (dark));
            g.strokePath (shape, juce::PathStrokeType (1.5f));
            // The keyboard edge and the sound that comes off the board
            const auto bb = shape.getBounds();
            g.setColour (feltColour (dark).withAlpha (0.8f));
            g.fillRoundedRectangle (juce::Rectangle<float> (bb.getWidth() * 0.9f, 6.0f).withCentre ({ bb.getCentreX(), bb.getBottom() - 4.0f }), 2.0f);
            const float level = std::max (meter[0], meter[1]);
            if (level > 1.0e-4f)
            {
                const float t = std::clamp ((juce::Decibels::gainToDecibels (level) + 50.0f) / 45.0f, 0.0f, 1.0f);
                for (int i = 0; i < 3; ++i)
                {
                    const float ring = std::fmod (phase + (float) i / 3.0f, 1.0f);
                    const float rr = innerRadius() * (0.7f + 1.4f * ring);
                    g.setColour (p.accent.withAlpha (0.2f * t * (1.0f - ring)));
                    g.drawEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (bb.getCentre()), 1.5f);
                }
            }
            g.setColour (p.textDim);
            drawSpacedText (g, juce::String (spec.name).toUpperCase(), juce::Rectangle<float> (220.0f, 14.0f).withCentre ({ c.x, bb.getBottom() + 16.0f }),
                            8.5f, juce::Justification::centred, 0.2f);
        }

        void drawPair (juce::Graphics& g, int m, const fjord::base::Palette& p, bool dark)
        {
            const auto colour = m == 0 ? p.accent : brassAt (dark, 0.95f);
            const float gain = (m == 0 ? al : bl).get();
            const bool silent = gain <= -39.9f;
            const auto c = centre();
            const auto caps = capsules (m);
            const auto mid = pairCentre (m);
            const float alpha = silent ? 0.35f : 1.0f;

            // Pickup pattern: a cardioid lobe from each capsule towards the instrument
            for (const auto& cap : caps)
            {
                const float facing = std::atan2 (c.x - cap.x, cap.y - c.y);
                juce::Path lobe;
                const float size = 16.0f + 0.05f * cap.getDistanceFrom (c);
                for (int i = 0; i <= 48; ++i)
                {
                    const float phi = juce::MathConstants<float>::twoPi * (float) i / 48.0f;
                    const float rad = size * (0.5f + 0.5f * std::cos (phi));
                    const juce::Point<float> pt (cap.x + rad * std::sin (facing + phi), cap.y - rad * std::cos (facing + phi));
                    if (i == 0) lobe.startNewSubPath (pt); else lobe.lineTo (pt);
                }
                lobe.closeSubPath();
                g.setColour (colour.withAlpha ((dark ? 0.1f : 0.06f) * alpha * (hover == m ? 1.8f : 1.0f)));
                g.fillPath (lobe);
                g.setColour (colour.withAlpha (0.35f * alpha));
                g.strokePath (lobe, juce::PathStrokeType (0.9f));
            }
            // Stereo bar and capsules
            g.setColour (colour.withAlpha (0.5f * alpha));
            g.drawLine ({ caps[0], caps[1] }, 1.4f);
            for (const auto& cap : caps)
            {
                const float facing = std::atan2 (c.x - cap.x, cap.y - c.y);
                juce::Path body;
                body.addRoundedRectangle (-5.0f, -12.0f, 10.0f, 24.0f, 5.0f);
                body.applyTransform (juce::AffineTransform::rotation (facing).translated (cap));
                g.setColour (p.panel);
                g.fillPath (body);
                g.setColour (colour.withAlpha (alpha));
                g.strokePath (body, juce::PathStrokeType (1.6f));
                g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (cap + juce::Point<float> (std::sin (facing), -std::cos (facing)) * 7.0f));
            }
            // Label with distance and a meter
            const float d = (m == 0 ? ay : by).get() + (m == 1 ? 0.5f : 0.0f);
            auto label = juce::Rectangle<float> (150.0f, 30.0f).withCentre ({ mid.x, mid.y + (mid.y < c.y - 30.0f ? -30.0f : 30.0f) });
            g.setColour (colour.withAlpha (alpha));
            drawSpacedText (g, (m == 0 ? juce::String ("CLOSE") : juce::String ("PLAYER")) + "  " + utf8 ("\xc2\xb7") + "  "
                                + juce::String (metres (d), 1) + " M", label.removeFromTop (14.0f), 8.5f, juce::Justification::centred, 0.18f, true);
            auto meterArea = label.withSizeKeepingCentre (80.0f, 4.0f).translated (0.0f, 3.0f);
            g.setColour (p.line);
            g.fillRoundedRectangle (meterArea, 2.0f);
            const float level = std::clamp ((juce::Decibels::gainToDecibels (meter[(size_t) m]) + 60.0f) / 60.0f, 0.0f, 1.0f);
            g.setColour (colour.withAlpha (alpha));
            g.fillRoundedRectangle (meterArea.withWidth (meterArea.getWidth() * level), 2.0f);
        }

        HamarProcessor& proc;
        ParamHandle ax, ay, aw, al, bx, by, bw, bl;
        std::array<float, 2> meter {};
        int modelIndex = 0, hover = -1;
        float phase = 0.0f;
        Hit hit;
    };

    // Quick placements of both pairs
    class MicPresets : public juce::Component
    {
    public:
        explicit MicPresets (APVTS& s) : state (s)
        {
            static const char* names[] = { "Inside", "Close", "Player", "Room", "Wide AB" };
            for (int i = 0; i < 5; ++i)
            {
                auto* b = buttons.add (new PillButton (names[i], false));
                b->onClick = [this, i] { apply (i); };
                addAndMakeVisible (b);
            }
        }

        void resized() override
        {
            auto r = getLocalBounds();
            const int w = r.getWidth() / buttons.size();
            for (auto* b : buttons) b->setBounds (r.removeFromLeft (w).reduced (3, 0));
        }

    private:
        void apply (int i)
        {
            // close x, y, width, level | player x, y, width, level
            static const float table[5][8] = {
                { 0.5f, 0.0f, 0.9f, 0.0f,    0.5f, 0.3f, 0.5f, -14.0f },   // inside: under the lid, wide over the strings
                { 0.5f, 0.2f, 0.7f, 0.0f,    0.45f, 0.5f, 0.5f, -8.0f },   // close (the default)
                { 0.4f, 0.45f, 0.5f, -6.0f,  0.35f, 0.4f, 0.6f, 0.0f },    // at the player's ears
                { 0.55f, 0.6f, 0.6f, -12.0f, 0.5f, 1.0f, 0.9f, 0.0f },     // in the room
                { 0.5f, 0.3f, 1.0f, 0.0f,    0.5f, 0.8f, 1.0f, -4.0f },    // wide spaced pairs
            };
            const P::Id ids[8] = { P::MicAX, P::MicAY, P::MicAWidth, P::MicALevel, P::MicBX, P::MicBY, P::MicBWidth, P::MicBLevel };
            for (int k = 0; k < 8; ++k)
            {
                ParamHandle h (state, ids[k]);
                h.setOnce (table[i][k]);
            }
        }

        APVTS& state;
        juce::OwnedArray<PillButton> buttons;
    };
}
