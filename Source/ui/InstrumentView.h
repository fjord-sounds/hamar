#pragma once
#include "Widgets.h"

// The instrument seen from above: every string sits right above its key on the on-screen keyboard, as long as the
// model's string is, the bass strings cross over the tenor as in a real grand. Strings glow and shake while they
// sound, hammers jump when they strike, dampers lift with the keys and the pedal, and the two mic pairs can be dragged
// around the instrument (the mouse wheel over a pair changes its width). An upright shows its tall case instead.
namespace hamar::ui
{
    class InstrumentView : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        // keyCentre: the centre of a key on the on-screen keyboard, in the given component's coordinates
        using KeyCentre = std::function<float (int, const juce::Component&)>;

        InstrumentView (HamarProcessor& p, KeyCentre keyCentre)
            : proc (p), keyX (std::move (keyCentre)),
              micAX (p.apvts, P::MicAX), micAY (p.apvts, P::MicAY), micAW (p.apvts, P::MicAWidth),
              micBX (p.apvts, P::MicBX), micBY (p.apvts, P::MicBY), micBW (p.apvts, P::MicBWidth)
        {
            setTooltip ("Drag a microphone pair to move it, scroll over it to change its width");
        }

        void tick()
        {
            const auto settings = proc.currentSettings();
            if (! haveTable || ! (settings.instrument == tableFor))
            {
                tableFor = settings.instrument;
                table.update (settings.instrument);
                haveTable = true;
            }
            board = settings.board;
            const auto& d = proc.getEngine().display();
            for (int i = 0; i < dsp::kNumKeys; ++i)
            {
                level[(size_t) i] = d.level[(size_t) i].load (std::memory_order_relaxed);
                free[(size_t) i] = d.free[(size_t) i].load (std::memory_order_relaxed);
                const uint32_t s = d.strikes[(size_t) i].load (std::memory_order_relaxed);
                if (s != strikes[(size_t) i]) { strikes[(size_t) i] = s; flash[(size_t) i] = 1.0f; }
                else flash[(size_t) i] *= 0.82f;
            }
            phase += 0.9f;
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            const auto r = getLocalBounds().toFloat();
            const auto& spec = dsp::model (tableFor.model);
            const bool grand = spec.cabinet == dsp::Cabinet::Grand;
            const int low = spec.lowKey;
            computeGeometry (r, spec);
            if (spec.mechanism != dsp::Mechanism::Strings)
            {
                paintBars (g, spec, p, dark);
                drawCard (g, spec, r, p);
                return;
            }

            // Soft glow of the brass behind the instrument
            drawGlow (g, outline.getBounds().reduced (40.0f, 20.0f), dark, 0.5f);

            // Case and soundboard
            g.setColour (caseColour (dark));
            g.fillPath (outline);
            g.setColour (caseEdge (dark));
            g.strokePath (outline, juce::PathStrokeType (1.5f));
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (boardPath);
                const auto b = boardPath.getBounds();
                g.setGradientFill ({ boardColour (dark).brighter (dark ? 0.05f : 0.0f), b.getX(), b.getY(),
                                     boardColour (dark).darker (dark ? 0.1f : 0.04f), b.getRight(), b.getBottom(), false });
                g.fillRect (b);
                g.setColour (boardGrain (dark).withAlpha (0.55f));   // spruce grain, diagonal as on a real board
                for (float x = b.getX() - b.getHeight(); x < b.getRight(); x += 9.0f)
                    g.drawLine (x, b.getBottom(), x + b.getHeight() * 0.55f, b.getY(), 1.0f);
                // The iron frame (brass coloured) with its round openings
                g.setColour (brassAt (dark, 0.35f).withAlpha (dark ? 0.16f : 0.22f));
                g.fillPath (framePath);
                g.setColour (brassAt (dark, 0.5f).withAlpha (dark ? 0.5f : 0.6f));
                g.strokePath (framePath, juce::PathStrokeType (1.2f));
                for (const auto& hole : holes)
                {
                    g.setColour (boardColour (dark));
                    g.fillEllipse (hole);
                    g.setColour (brassAt (dark, 0.5f).withAlpha (0.6f));
                    g.drawEllipse (hole, 1.2f);
                }
            }

            // Strings: copper-wound in the bass, steel above; sounding strings glow and shake
            for (int key = low; key <= spec.highKey; ++key)
            {
                const auto& s = strings[(size_t) dsp::keyIndex (key)];
                const float lv = level[(size_t) dsp::keyIndex (key)];
                const float glow = lv > 1.0e-5f ? std::clamp ((juce::Decibels::gainToDecibels (lv) + 70.0f) / 55.0f, 0.0f, 1.0f) : 0.0f;
                const auto base = key < 33 ? copperColour (dark) : steelColour (dark);
                if (glow <= 0.0f)
                {
                    g.setColour (base.withAlpha (dark ? 0.55f : 0.7f));
                    g.drawLine (s.x0, s.y0, s.x1, s.y1, key < 33 ? 1.4f : 0.9f);
                    continue;
                }
                juce::Path path;
                const int steps = 18;
                for (int i = 0; i <= steps; ++i)
                {
                    const float t = (float) i / (float) steps;
                    const float wobble = 2.2f * glow * std::sin (juce::MathConstants<float>::pi * t) * std::sin (phase * (1.0f + 0.01f * (float) key) + (float) key);
                    const juce::Point<float> pt (s.x0 + (s.x1 - s.x0) * t + wobble, s.y0 + (s.y1 - s.y0) * t);
                    if (i == 0) path.startNewSubPath (pt); else path.lineTo (pt);
                }
                g.setColour (brassAt (dark, 0.2f + 0.6f * s.pos).withAlpha (0.25f * glow));
                g.strokePath (path, juce::PathStrokeType (5.0f * glow + 1.0f));
                g.setColour (base.interpolatedWith (p.accent, glow).withAlpha (0.7f + 0.3f * glow));
                g.strokePath (path, juce::PathStrokeType (key < 33 ? 1.6f : 1.1f));
            }

            // Dampers: a wooden rail across the strings with a small felt on each damped string; a lifted damper (key held,
            // pedal down) rises off its string and glows faintly. In front, the key slip of the case. Hammers show only
            // when they strike: a glowing felt head that jumps and settles.
            g.setColour (caseEdge (dark).withAlpha (0.8f));
            g.fillRoundedRectangle (juce::Rectangle<float> (spanLeft - 14.0f, damperY - 1.5f, spanRight - spanLeft + 28.0f, 3.0f), 1.5f);
            g.setColour (caseColour (dark).brighter (dark ? 0.12f : 0.25f));
            g.fillRoundedRectangle (juce::Rectangle<float> (spanLeft - 18.0f, hammerY + 3.0f, spanRight - spanLeft + 36.0f, 7.0f), 2.0f);
            for (int key = low; key <= spec.highKey; ++key)
            {
                const int i = dsp::keyIndex (key);
                const auto& s = strings[(size_t) i];
                const float w = std::max (1.5f, keyWidth * 0.32f);
                if (table[key].hasDamper)
                {
                    const float lift = free[(size_t) i];
                    auto felt = juce::Rectangle<float> (w, 3.0f).withCentre ({ s.x0, damperY - 4.0f - 4.0f * lift });
                    g.setColour ((lift > 0.05f ? p.accent : feltColour (dark)).withAlpha (0.35f + 0.4f * lift));
                    g.fillRoundedRectangle (felt, 1.0f);
                }
                const float f = flash[(size_t) i];
                if (f > 0.04f)
                {
                    auto hammer = juce::Rectangle<float> (keyWidth * 0.5f, 6.0f).withCentre ({ s.x0, hammerY - 6.0f * f });
                    drawGlow (g, hammer.expanded (8.0f), dark, 0.6f * f);
                    g.setColour (feltColour (dark).interpolatedWith (p.accent, 0.5f).withAlpha (std::min (1.0f, 2.0f * f)));
                    g.fillRoundedRectangle (hammer, 2.0f);
                }
            }

            // Lid / front panel
            const float cover = grand ? (board.lid == 2 ? 0.6f : board.lid == 1 ? 0.18f : 0.0f) : (board.panel == 0 ? 0.42f : board.panel == 1 ? 0.15f : 0.0f);
            if (cover > 0.0f)
            {
                g.setColour (caseColour (dark).withAlpha (cover));
                g.fillPath (outline);
            }

            if (! spec.electric) drawMics (g, p, dark);
            drawCard (g, spec, r, p);
        }

        void drawCard (juce::Graphics& g, const dsp::ModelSpec& spec, juce::Rectangle<float> r, const fjord::base::Palette& p)
        {
            // Model card
            auto card = r.reduced (14.0f, 10.0f).removeFromTop (40.0f).removeFromRight (240.0f);
            g.setColour (p.textDim);
            static const char* sources[] = { "", "TINES AND TONEBARS", "STRUCK REEDS", "PLUCKED REEDS", "FREE PLATES", "CLAMPED RODS", "BELL BARS" };
            const juce::String detail = spec.mechanism == dsp::Mechanism::Strings
                                            ? juce::String (juce::roundToInt (bassLength * 100.0f)) + " CM BASS STRING"
                                            : juce::String (sources[(int) spec.mechanism]);
            drawSpacedText (g, juce::String (categoryName (spec.category)).toUpperCase() + "  " + utf8 ("\xc2\xb7") + "  " + detail,
                            card.removeFromTop (14.0f), 8.5f, juce::Justification::centredRight, 0.16f);
            g.setColour (p.text);
            g.setFont (font (17.0f, true));
            g.drawText (spec.name, card, juce::Justification::centredRight);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragging = hitMic (e.position);
            if (dragging == 0) { micAX.begin(); micAY.begin(); }
            if (dragging == 1) { micBX.begin(); micBY.begin(); }
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragging < 0) return;
            const float x = std::clamp ((e.position.x - spanLeft) / std::max (1.0f, spanRight - spanLeft), 0.0f, 1.0f);
            const float d = distanceAt (e.position.y);
            if (dragging == 0) { micAX.set (x); micAY.set (std::clamp (d, 0.0f, 1.0f)); }
            else               { micBX.set (x); micBY.set (std::clamp (d - 0.5f, 0.0f, 1.0f)); }
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragging == 0) { micAX.end(); micAY.end(); }
            if (dragging == 1) { micBX.end(); micBY.end(); }
            dragging = -1;
        }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            const int m = hitMic (e.position);
            if (m < 0) return;
            auto& width = m == 0 ? micAW : micBW;
            width.setOnce (std::clamp (width.get() + w.deltaY * 0.25f, 0.0f, 1.0f));
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            setMouseCursor (hitMic (e.position) >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        }

    private:
        struct StringLine { float x0 = 0, y0 = 0, x1 = 0, y1 = 0, pos = 0; };

        // Tines (with tonebars and magnetic pickups at their tips) or reeds (under the comb of an electrostatic pickup;
        // the Pianet's sticky pads sit on them and lift with the key)
        void paintBars (juce::Graphics& g, const dsp::ModelSpec& spec, const fjord::base::Palette& p, bool dark)
        {
            drawGlow (g, outline.getBounds().reduced (40.0f, 20.0f), dark, 0.5f);
            g.setColour (caseColour (dark));
            g.fillPath (outline);
            g.setColour (caseEdge (dark));
            g.strokePath (outline, juce::PathStrokeType (1.5f));
            const auto inner = outline.getBounds().reduced (10.0f, 10.0f);
            g.setGradientFill ({ caseColour (dark).brighter (dark ? 0.35f : 0.25f), inner.getX(), inner.getY(),
                                 caseColour (dark).brighter (dark ? 0.15f : 0.1f), inner.getX(), inner.getBottom(), false });
            g.fillRoundedRectangle (inner, 8.0f);

            const bool tine = spec.mechanism == dsp::Mechanism::Tine;
            const float w = std::max (2.0f, keyWidth * 0.5f);
            float tipY = 0.0f;
            for (int key = spec.lowKey; key <= spec.highKey; ++key)
            {
                const int i = dsp::keyIndex (key);
                const auto& s = strings[(size_t) i];
                const float lv = level[(size_t) i];
                const float glow = lv > 1.0e-5f ? std::clamp ((juce::Decibels::gainToDecibels (lv) + 70.0f) / 55.0f, 0.0f, 1.0f) : 0.0f;
                if (tine)
                {
                    // Tonebar beside the tine (brass), then the tine (steel)
                    g.setColour (brassAt (dark, 0.45f).withAlpha (0.55f + 0.35f * glow));
                    g.fillRoundedRectangle (juce::Rectangle<float> (std::max (1.5f, w * 0.45f), (s.y0 - s.y1) * 0.82f)
                                                .withBottomY (s.y0).withCentre ({ s.x0 + w * 0.55f, s.y0 - (s.y0 - s.y1) * 0.41f }), 1.0f);
                }
                juce::Path path;
                for (int k = 0; k <= 10; ++k)
                {
                    const float t = (float) k / 10.0f;
                    const float wobble = 2.5f * glow * t * t * std::sin (phase * 1.3f + (float) key);
                    const juce::Point<float> pt (s.x0 + wobble, s.y0 + (s.y1 - s.y0) * t);
                    if (k == 0) path.startNewSubPath (pt); else path.lineTo (pt);
                }
                if (glow > 0.0f)
                {
                    g.setColour (brassAt (dark, 0.2f + 0.6f * s.pos).withAlpha (0.25f * glow));
                    g.strokePath (path, juce::PathStrokeType (5.0f * glow + 1.0f));
                }
                g.setColour (steelColour (dark).interpolatedWith (p.accent, glow).withAlpha (0.75f + 0.25f * glow));
                g.strokePath (path, juce::PathStrokeType (tine ? 1.3f : 2.2f));

                // Pickups: a magnet in front of every tine tip; reeds share one comb (drawn below)
                if (tine)
                {
                    const auto disc = juce::Rectangle<float> (w * 0.9f, w * 0.9f).withCentre ({ s.x1, s.y1 - w * 0.9f });
                    g.setColour (caseColour (dark));
                    g.fillEllipse (disc);
                    g.setColour (brassAt (dark, 0.7f).withAlpha (0.8f));
                    g.drawEllipse (disc, 1.0f);
                }
                else if (spec.mechanism == dsp::Mechanism::Pianet)
                {
                    const float lift = free[(size_t) i];
                    auto pad = juce::Rectangle<float> (w * 1.1f, 6.0f).withCentre ({ s.x1, s.y1 + 4.0f - 6.0f * lift });
                    g.setColour (feltColour (dark).withAlpha (0.85f - 0.3f * lift));
                    g.fillRoundedRectangle (pad, 2.0f);
                }
                tipY = std::min (tipY == 0.0f ? s.y1 : tipY, s.y1);

                const float f = flash[(size_t) i];
                if (f > 0.04f && spec.mechanism != dsp::Mechanism::Pianet)
                {
                    auto hammer = juce::Rectangle<float> (w, 6.0f).withCentre ({ s.x0, hammerY - 6.0f * f });
                    drawGlow (g, hammer.expanded (8.0f), dark, 0.6f * f);
                    g.setColour (feltColour (dark).interpolatedWith (p.accent, 0.5f).withAlpha (std::min (1.0f, 2.0f * f)));
                    g.fillRoundedRectangle (hammer, 2.0f);
                }
            }
            const bool reeds = spec.mechanism == dsp::Mechanism::Reed || spec.mechanism == dsp::Mechanism::Pianet;
            if (reeds)
            {
                // The comb of the electrostatic pickup across the reed tips
                juce::Path comb;
                for (int key = spec.lowKey; key <= spec.highKey; ++key)
                {
                    const auto& s = strings[(size_t) dsp::keyIndex (key)];
                    const juce::Point<float> pt (s.x1, s.y1 - 10.0f);
                    if (key == spec.lowKey) comb.startNewSubPath (pt); else comb.lineTo (pt);
                }
                g.setColour (brassAt (dark, 0.6f).withAlpha (0.75f));
                g.strokePath (comb, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour (p.textDim);
            drawSpacedText (g, tine ? "MAGNETIC PICKUPS" : reeds ? "ELECTROSTATIC PICKUP" : "STRUCK BY MALLETS",
                            outline.getBounds().reduced (24.0f, 16.0f).removeFromTop (14.0f), 8.5f, juce::Justification::centredLeft, 0.2f);
        }

        void computeGeometry (juce::Rectangle<float> r, const dsp::ModelSpec& spec)
        {
            const bool grand = spec.cabinet == dsp::Cabinet::Grand;
            const int low = spec.lowKey;
            spanLeft = kx (low);
            spanRight = kx (spec.highKey);
            keyWidth = (spanRight - spanLeft) / (float) std::max (1, (spec.highKey - low) * 7 / 12);
            const float front = r.getBottom() - 4.0f;
            hammerY = front - 12.0f;
            damperY = hammerY - 12.0f;
            const float startY = damperY - 6.0f;
            const float top = r.getY() + (grand ? 12.0f : 30.0f);
            const float avail = startY - top - 14.0f;

            if (spec.mechanism != dsp::Mechanism::Strings)
            {
                // Bars: a flat harp of tines or reeds, longest in the bass (length ~ 1 / sqrt (f))
                const float left = std::max (2.0f, spanLeft - 22.0f), right = std::min (r.getRight() - 2.0f, spanRight + 22.0f);
                outline.clear();
                const float h = avail * (spec.mechanism == dsp::Mechanism::Tine ? 0.72f : 0.5f);
                outline.addRoundedRectangle (left, startY - h - 30.0f, right - left, front - (startY - h - 30.0f), 14.0f);
                for (int key = low; key <= spec.highKey; ++key)
                {
                    auto& s = strings[(size_t) dsp::keyIndex (key)];
                    const float len = h * std::pow (2.0f, -(float) (key - low) / 24.0f);
                    s.x0 = s.x1 = kx (key);
                    s.y0 = startY;
                    s.y1 = startY - std::max (10.0f, len);
                    s.pos = (float) (key - low) / (float) std::max (1, spec.highKey - low);
                }
                return;
            }

            // Longest string of the model sets the scale
            float longest = 0.01f;
            for (int key = low; key <= dsp::kHighestKey; ++key) longest = std::max (longest, lengthOf (key, spec));
            bassLength = lengthOf (low, spec);

            const float pad = 16.0f;
            const float left = std::max (2.0f, spanLeft - pad - 6.0f), right = std::min (r.getRight() - 2.0f, spanRight + pad + 6.0f), w = right - left;
            outline.clear();
            if (grand)
            {
                // The classic outline: straight bass side, round tail, the bentside curving in and out to the cheek.
                // Shorter grands are drawn shorter.
                const float h = avail * std::clamp (longest / 2.1f, 0.5f, 1.0f) + 10.0f;
                const float t = startY - h;
                outline.startNewSubPath (left, front);
                outline.lineTo (left, t + 0.14f * h);
                outline.quadraticTo (left, t, left + 0.11f * w, t);
                outline.cubicTo (left + 0.36f * w, t, left + 0.47f * w, t + 0.30f * h, left + 0.60f * w, t + 0.55f * h);
                outline.cubicTo (left + 0.70f * w, t + 0.73f * h, left + 0.86f * w, t + 0.74f * h, right - 0.025f * w, t + 0.79f * h);
                outline.quadraticTo (right, t + 0.81f * h, right, t + 0.87f * h);
                outline.lineTo (right, front);
                outline.closeSubPath();
            }
            else
                outline.addRoundedRectangle (left, top - 14.0f, w, front - top + 14.0f, 8.0f);

            // Upper edge of the case at every x (for the bridge, which follows it)
            std::array<float, 256> edge {};
            edge.fill (front);
            for (juce::PathFlatteningIterator it (outline); it.next();)
            {
                const float steps = std::max (1.0f, std::abs (it.x2 - it.x1));
                for (float u = 0.0f; u <= 1.0f; u += 1.0f / steps)
                {
                    const float x = it.x1 + (it.x2 - it.x1) * u, y = it.y1 + (it.y2 - it.y1) * u;
                    const int bin = std::clamp ((int) ((x - left) / w * 255.0f), 0, 255);
                    edge[(size_t) bin] = std::min (edge[(size_t) bin], y);
                }
            }
            auto bridgeAt = [&] (float x) { return edge[(size_t) std::clamp ((int) ((x - left) / w * 255.0f), 0, 255)] + 20.0f; };

            for (int key = low; key <= dsp::kHighestKey; ++key)
            {
                auto& s = strings[(size_t) dsp::keyIndex (key)];
                s.x0 = kx (key);
                s.y0 = startY;
                s.pos = (float) (key - low) / (float) (dsp::kHighestKey - low);
                if (grand)
                {
                    // Overstrung bass: the wound strings fan out to the right over the tenor
                    const float cross = key < spec.biTop ? (float) (spec.biTop - key) / (float) (spec.biTop - low) : 0.0f;
                    s.x1 = s.x0 + cross * 0.17f * (spanRight - spanLeft);
                    s.y1 = std::min (startY - 8.0f, bridgeAt (s.x1));
                }
                else
                {
                    s.x1 = s.x0;
                    s.y1 = s.y0 - lengthOf (key, spec) / longest * avail;
                }
            }
            boardPath = outline;
            boardPath.applyTransform (juce::AffineTransform::scale (0.985f, 0.975f, outline.getBounds().getCentreX(), front));

            // Frame: the outline drawn in once more, with round openings between the string groups
            framePath = outline;
            framePath.applyTransform (juce::AffineTransform::scale (0.95f, 0.93f, outline.getBounds().getCentreX(), front - 14.0f));
            holes.clear();
            if (grand)
                for (float u : { 0.18f, 0.36f, 0.52f })
                {
                    const float x = left + u * w;
                    const float y = (bridgeAt (x) + startY) * 0.5f;
                    holes.add (juce::Rectangle<float> (24.0f, 16.0f).withCentre ({ x, y }));
                }
        }

        float lengthOf (int key, const dsp::ModelSpec& spec) const
        {
            const float w = std::clamp ((60.0f - (float) key) / 39.0f, 0.0f, 1.0f);
            const float bass = spec.bassLength * std::exp2 (0.6f * tableFor.length) / 1.95f;
            return dsp::scale::length ((float) key) * std::pow (bass, w);
        }

        // Mic distance (0 .. 1.5) <-> height in the view
        float yForDistance (float d) const { return damperY - 14.0f - d / 1.5f * (damperY - 30.0f); }
        float distanceAt (float y) const { return (damperY - 14.0f - y) / std::max (1.0f, damperY - 30.0f) * 1.5f; }

        juce::Point<float> micCentre (int m) const
        {
            const auto& mic = m == 0 ? board.micA : board.micB;
            const float d = mic.y + (m == 1 ? 0.5f : 0.0f);
            return { spanLeft + mic.x * (spanRight - spanLeft), yForDistance (d) };
        }

        int hitMic (juce::Point<float> pos) const
        {
            for (int m = 1; m >= 0; --m)
                if (pos.getDistanceFrom (micCentre (m)) < 30.0f) return m;
            return -1;
        }

        void drawMics (juce::Graphics& g, const fjord::base::Palette& p, bool dark)
        {
            for (int m = 0; m < 2; ++m)
            {
                const auto& mic = m == 0 ? board.micA : board.micB;
                const auto c = micCentre (m);
                const float half = 0.28f * mic.width * (spanRight - spanLeft);
                const bool silent = mic.gain <= 0.0f;
                const auto colour = (m == 0 ? p.accent : brassAt (dark, 0.9f)).withMultipliedAlpha (silent ? 0.35f : 1.0f);
                g.setColour (colour.withAlpha (0.5f));
                g.drawLine (c.x - half, c.y, c.x + half, c.y, 1.2f);
                for (float x : { c.x - half, c.x + half })
                {
                    auto capsule = juce::Rectangle<float> (10.0f, 16.0f).withCentre ({ x, c.y });
                    g.setColour (p.panel);
                    g.fillRoundedRectangle (capsule, 5.0f);
                    g.setColour (colour);
                    g.drawRoundedRectangle (capsule, 5.0f, 1.6f);
                    g.fillEllipse (capsule.withSizeKeepingCentre (4.0f, 4.0f).translated (0.0f, -3.0f));
                }
                g.setColour (colour);
                drawSpacedText (g, m == 0 ? "CLOSE" : "PLAYER", juce::Rectangle<float> (80.0f, 12.0f).withCentre ({ c.x, c.y + 16.0f }),
                                8.5f, juce::Justification::centred, 0.18f, true);
            }
        }

        HamarProcessor& proc;
        KeyCentre keyX;
        float kx (int key) const { return keyX (key, *this); }
        ParamHandle micAX, micAY, micAW, micBX, micBY, micBW;
        dsp::KeyTable table;
        dsp::InstrumentSettings tableFor;
        dsp::BoardSettings board;
        bool haveTable = false;
        std::array<StringLine, dsp::kNumKeys> strings {};
        std::array<float, dsp::kNumKeys> level {}, free {}, flash {};
        std::array<uint32_t, dsp::kNumKeys> strikes {};
        juce::Path outline, boardPath, framePath;
        juce::Array<juce::Rectangle<float>> holes;
        float spanLeft = 0, spanRight = 1, keyWidth = 10, hammerY = 0, damperY = 0, phase = 0, bassLength = 1.95f;
        int dragging = -1;
    };
}
