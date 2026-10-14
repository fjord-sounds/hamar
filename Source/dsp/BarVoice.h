#pragma once
#include <array>
#include <cmath>
#include <algorithm>
#include <fjord_base/core/Dsp.h>
#include "KeyPhysics.h"

// One key of a bar instrument: a tine (with its tonebar), a reed, the plucked reed of a Pianet - or, heard through
// the air instead of a pickup, the free plate of a celesta or a glass piano, the clamped rod of a toy piano, a bell.
//
// The bar is a cantilever: its bending modes sit at 1, 6.27, 17.55 … times the fundamental, each one a resonator.
// A hammer blow is a short force pulse (shorter for a harder tip and a faster blow), so the upper modes - the bell of a
// tine - come with the attack and die quickly. A tine's tonebar is a second oscillator on the fundamental, a hair
// apart, that keeps it singing. The Pianet's sticky pad pulls the reed and lets go, and lands on it again as the key
// comes up (its damper).
//
// The pickup makes the sound: a tine moves in front of a magnet (the voltage follows the change of the flux,
// 1 / sqrt ((x - x0)^2 + d^2), which turns into the famous bark when the tine swings close and far), a reed is one
// plate of a capacitor (the voltage follows x / (d - x): asymmetric, the crunch of a reed piano).
namespace hamar::dsp
{
    struct BarSettings
    {
        float pickup = 0.4f, voicing = 0.0f, bell = 0.5f, tonebar = 0.5f, decay = 0.0f, hardness = 0.0f;
        float ratio = 0.0f, shimmer = 0.3f, ring = 0.0f;   // fantasy bars: stretch of the mode ratios, beating, decay
    };

    class BarVoice
    {
    public:
        static constexpr int kModes = 5;

        void prepare (int keyNumber, double sampleRate)
        {
            key = keyNumber;
            fs = sampleRate;
            active = false;
        }

        bool isActive() const { return active; }
        float getLevel() const { return level; }
        void stop() { kill = true; }
        void clear() { active = false; }
        void release() { held = false; }

        // speed: hammer speed relative to the hardest blow (0..1)
        void strike (const KeyPhysics& k, const ModelSpec& m, const BarSettings& s, float speed)
        {
            mechanism = m.mechanism;
            f0 = k.f1;
            const double w0 = 2.0 * kPiD * f0 / fs;
            const float pos = k.position;

            // Modes of the bar: ratio, level, decay (relative to the fundamental's)
            struct ModeDef { float ratio, level, decay; };
            std::array<ModeDef, kModes> defs {};
            int count = 0;
            const float bell = (0.25f + 1.5f * s.bell) * m.bell;
            const float bar = 1.0f + 0.01f * keyRandom (key, 40);
            switch (mechanism)
            {
                case Mechanism::Tine:
                    defs = { { { 1.0f, 1.0f, 1.0f }, { 6.27f * bar, 0.45f * bell, 0.05f }, { 17.55f * bar, 0.22f * bell, 0.02f },
                               { 34.39f * bar, 0.1f * bell, 0.01f }, { 1.0f + 0.0011f * (1.0f + keyRandom (key, 41)), 0.0f, 1.25f } } };
                    defs[4].level = 0.55f * s.tonebar * m.tonebar;
                    count = 5;
                    break;
                case Mechanism::Reed:
                    defs = { { { 1.0f, 1.0f, 1.0f }, { 6.6f * bar, 0.3f * bell, 0.08f }, { 19.5f * bar, 0.1f * bell, 0.03f } } };
                    count = 3;
                    break;
                case Mechanism::Pianet:
                    defs = { { { 1.0f, 1.0f, 1.0f }, { 6.3f * bar, 0.25f * bell, 0.08f } } };
                    count = 2;
                    break;
                case Mechanism::Plate:   // free-free bar (celesta, glass), with a shimmering partner of the fundamental
                    defs = { { { 1.0f, 1.0f, 1.0f }, { 2.756f * bar, 0.5f * bell, 0.45f }, { 5.404f * bar, 0.3f * bell, 0.25f },
                               { 8.933f * bar, 0.15f * bell, 0.15f }, { 1.0f + 0.0016f * (1.0f + keyRandom (key, 42)), 0.6f * s.shimmer, 1.1f } } };
                    count = 5;
                    break;
                case Mechanism::Rod:     // clamped rod (toy piano)
                    defs = { { { 1.0f, 1.0f, 1.0f }, { 6.27f * bar, 0.6f * bell, 0.2f }, { 17.55f * bar, 0.3f * bell, 0.08f } } };
                    count = 3;
                    break;
                default:                 // bell
                    defs = { { { 1.0f, 1.0f, 1.0f }, { 2.32f * bar, 0.8f * bell, 0.7f }, { 4.25f * bar, 0.6f * bell, 0.5f },
                               { 6.63f * bar, 0.4f * bell, 0.35f }, { 9.38f * bar, 0.25f * bell, 0.25f } } };
                    count = 5;
                    break;
            }
            acoustic = mechanism == Mechanism::Plate || mechanism == Mechanism::Rod || mechanism == Mechanism::Bell;
            if (acoustic)
                for (int i = 1; i < count; ++i)   // stretch the overtones (not the shimmer partner)
                    if (defs[(size_t) i].ratio > 1.5f)
                        defs[(size_t) i].ratio = 1.0f + (defs[(size_t) i].ratio - 1.0f) * std::exp2 (0.6f * s.ratio);

            const float decay = k.after * std::exp2 (1.5f * (s.decay + (acoustic ? s.ring : 0.0f)));
            numModes = count;
            for (int i = 0; i < count; ++i)
            {
                auto& md = modes[(size_t) i];
                const double f = f0 * defs[(size_t) i].ratio;
                if (f > 0.45 * fs) { md.gain = 0.0f; md.a1 = md.a2 = 0.0f; continue; }
                const double w = 2.0 * kPiD * f / fs;
                md.cosw = (float) std::cos (w);
                md.free = (float) std::exp (-6.9078 / (std::max (0.02, (double) decay * defs[(size_t) i].decay) * fs));
                md.damped = (float) std::exp (-6.9078 / ((key > 80 ? 0.25 : 0.1) * fs));
                md.gain = defs[(size_t) i].level * (float) std::sin (w) / std::sqrt (defs[(size_t) i].ratio);
                md.shape = (i % 2 == 0 ? 1.0f : -1.0f) * (i == count - 1 && mechanism == Mechanism::Tine ? 0.0f : 1.0f);
                if (! active) md.y1 = md.y2 = 0.0f;
                setCoefficients (md, md.free);
            }
            tonebarIndex = mechanism == Mechanism::Tine ? 4 : -1;

            // The blow: a half-sine force pulse, shorter for a hard tip and a fast blow (and in the treble)
            const float contact = 0.0011f * std::pow (262.0f / f0, 0.3f) * std::exp2 (-0.8f * s.hardness) * std::pow (std::max (0.05f, speed), -0.15f);
            pulseLength = std::max (2, (int) (contact * (float) fs));
            pulsePos = 0;
            pulseGain = speed * 1.6f / (0.6366f * (float) pulseLength);   // the pulse's area sets the swing, its length the colour

            if (mechanism == Mechanism::Pianet)
            {
                // The pad pulls the reed and lets go: the reed starts from a displacement, at rest
                pulseLength = 0;
                const float a = speed * 1.1f;
                for (int i = 0; i < count; ++i)
                {
                    auto& md = modes[(size_t) i];
                    const float amp = a * (i == 0 ? 1.0f : 0.25f * bell) * md.shape;
                    md.y1 = amp;
                    md.y2 = amp * md.cosw;
                }
            }

            // Pickup
            distance = mechanism == Mechanism::Tine ? 2.4f - 1.9f * s.pickup : 1.6f + 1.6f * (1.0f - s.pickup);
            offset = 0.9f * s.voicing;
            fluxPrev = flux (0.0f);
            hp = hpPrev = 0.0f;
            outGain = (mechanism == Mechanism::Tine ? 1.6f * distance * distance : 0.6f) * std::pow (f0 / 262.0f, mechanism == Mechanism::Tine ? -0.75f : 0.0f);
            if (acoustic) outGain = 3.0f * std::pow (262.0f / f0, 0.5f);
            outGain *= k.gain;   // the model's trim
            xPrev = 0.0f;

            damp = 0.0f;
            dampTarget = 0.0f;
            active = true;
            held = true;
            kill = false;
            fade = 1.0f;
            level = std::max (level, 1.0e-4f);
            silentBlocks = 0;
        }

        void setDamp (float target, float seconds)
        {
            dampTarget = std::clamp (target, 0.0f, 1.0f);
            dampCoef = std::exp (-1.0f / std::max (1.0f, seconds * (float) fs));
        }

        // Adds the pickup (or radiated) signal to the outputs, each with its gain (pickups: pan, acoustic: the mics)
        void render (int n, float* const* outs, const float* gains, int numOuts)
        {
            if (! active) return;
            // Dampers: blend the decay of every mode between free and damped, once per block
            damp = dampTarget + (damp - dampTarget) * std::pow (dampCoef, (float) n);
            for (int i = 0; i < numModes; ++i)
            {
                auto& md = modes[(size_t) i];
                setCoefficients (md, md.free + (md.damped - md.free) * damp);
            }
            const float fadeStep = 1.0f / (0.004f * (float) fs);
            float peak = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                float force = 0.0f;
                if (pulsePos < pulseLength)
                {
                    force = pulseGain * std::sin (fjord::base::dsp::kPi * ((float) pulsePos + 0.5f) / (float) pulseLength);
                    ++pulsePos;
                }
                float x = 0.0f, tonebar = 0.0f;
                for (int m = 0; m < numModes; ++m)
                {
                    auto& md = modes[(size_t) m];
                    const float y = md.a1 * md.y1 - md.a2 * md.y2 + md.gain * force;
                    md.y2 = md.y1;
                    md.y1 = y;
                    if (m == tonebarIndex) tonebar = y; else x += md.shape * y;
                }
                // The tonebar sings through the tine (its fundamental moves the tine a little)
                x += 0.35f * tonebar;

                float v;
                if (acoustic)
                {
                    v = (x - xPrev) * outGain * (float) fs / (2.0f * fjord::base::dsp::kPi * 262.0f);   // the air hears the bar's velocity
                    xPrev = x;
                }
                else if (mechanism == Mechanism::Tine)
                {
                    const float phi = flux (x);
                    v = (phi - fluxPrev) * outGain * (float) fs / (2.0f * fjord::base::dsp::kPi * 262.0f);
                    fluxPrev = phi;
                }
                else
                {
                    const float d = std::max (0.25f, distance - x);
                    v = x / d * distance * outGain;
                }
                // DC out (the asymmetric pickups leave an offset)
                hp = 0.9995f * (hp + v - hpPrev);
                hpPrev = v;
                if (kill) fade = std::max (0.0f, fade - fadeStep);
                const float out = hp * fade * 0.25f;
                for (int c = 0; c < numOuts; ++c) outs[c][i] += out * gains[c];
                peak = std::max (peak, std::abs (out));
            }
            level = std::max (peak, level * 0.9f);
            silentBlocks = peak < 2.0e-6f ? silentBlocks + 1 : 0;
            if ((kill && fade <= 0.0f) || silentBlocks > 8)
                active = false;
        }

    private:
        struct Mode
        {
            float a1 = 0.0f, a2 = 0.0f, gain = 0.0f, y1 = 0.0f, y2 = 0.0f, cosw = 1.0f, free = 0.999f, damped = 0.99f, shape = 1.0f;
        };

        static void setCoefficients (Mode& md, float r)
        {
            md.a1 = 2.0f * r * md.cosw;
            md.a2 = r * r;
        }

        float flux (float x) const
        {
            const float u = x - offset;
            return 1.0f / std::sqrt (u * u + distance * distance);
        }

        int key = 60;
        double fs = 48000.0;
        Mechanism mechanism = Mechanism::Tine;
        bool active = false, held = false, kill = false;
        float f0 = 261.6f;
        std::array<Mode, kModes> modes {};
        int numModes = 0, tonebarIndex = -1;
        int pulseLength = 0, pulsePos = 0;
        float pulseGain = 0.0f;
        float distance = 1.5f, offset = 0.0f, fluxPrev = 0.0f, outGain = 1.0f, hp = 0.0f, hpPrev = 0.0f, xPrev = 0.0f;
        bool acoustic = false;
        float damp = 0.0f, dampTarget = 0.0f, dampCoef = 0.99f, fade = 1.0f, level = 0.0f;
        int silentBlocks = 0;
    };
}
