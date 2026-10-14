#pragma once
#include <array>
#include <cmath>
#include <algorithm>
#include <fjord_base/core/Dsp.h>

// The mechanics around the strings, all synthesized: the knock of the hammer blow in the frame and the board, the key
// hitting the keybed and coming back up, the pedal mechanism, and the swoosh of all dampers lifting off the strings
// (which goes into the sympathetic strings, so they answer it).
namespace hamar::dsp
{
    class Noises
    {
    public:
        enum Type { Knock, KeyDown, KeyUp, PedalThump, DamperLift, Tack };

        void prepare (double sampleRate)
        {
            fs = (float) sampleRate;
            for (auto& e : events) e.active = false;
            random.seed (4242u);
        }

        void reset() { for (auto& e : events) e.active = false; }

        // gain: linear level; zone: where on the board (bass/middle/treble); position 0..1 along the keyboard
        void trigger (Type type, float gain, const std::array<float, 3>& zone, float position)
        {
            if (gain <= 1.0e-6f) return;
            Event* e = &events[0];
            for (auto& x : events)
                if (! x.active) { e = &x; break; }
                else if (x.env < e->env) e = &x;
            *e = {};
            e->active = true;
            e->type = type;
            e->gain = gain;
            e->zone = zone;
            e->filter.reset();
            e->filter2.reset();
            switch (type)
            {
                case Knock:      // a short half-sine force pulse, longer in the bass
                    e->pulseLength = (int) ((0.0012f + 0.0025f * (1.0f - position)) * fs);
                    break;
                case KeyDown:    e->freq = 260.0f + 120.0f * position; e->reso = 0.45f; e->freq2 = 90.0f; setEnv (*e, 0.0015f, 0.035f); break;
                case KeyUp:      e->freq = 900.0f + 300.0f * position; e->reso = 0.5f; e->freq2 = 180.0f; setEnv (*e, 0.001f, 0.018f); break;
                case PedalThump: e->freq = 110.0f; e->reso = 0.55f; e->freq2 = 60.0f; setEnv (*e, 0.004f, 0.06f); break;
                case DamperLift: e->freq = 1400.0f; e->reso = 0.1f; e->freq2 = 400.0f; setEnv (*e, 0.03f, 0.16f); break;
                case Tack:       e->freq = 5200.0f + 2000.0f * position; e->reso = 0.6f; e->freq2 = 3000.0f; setEnv (*e, 0.0003f, 0.006f); break;
            }
        }

        // Board events go into the zones and a little into the direct mics, damper noise into the sympathetic strings
        void process (float* const* zone, float* const* direct, float* sympathetic, int n)
        {
            for (auto& e : events)
            {
                if (! e.active) continue;
                for (int i = 0; i < n; ++i)
                {
                    float x;
                    if (e.type == Knock)
                    {
                        if (e.pos >= e.pulseLength) { e.active = false; break; }
                        x = std::sin (fjord::base::dsp::kPi * (float) e.pos / (float) e.pulseLength) * e.gain;
                        ++e.pos;
                    }
                    else
                    {
                        e.env = e.attacking ? e.env + e.attack : e.env * e.decay;
                        if (e.attacking && e.env >= 1.0f) { e.env = 1.0f; e.attacking = false; }
                        if (! e.attacking && e.env < 1.0e-4f) { e.active = false; break; }
                        const float noise = random.nextBipolar();
                        x = e.filter.process (noise, e.freq, e.reso, fs, fjord::base::dsp::Svf::Band)
                            + 0.6f * e.filter2.process (noise, e.freq2, 0.3f, fs, fjord::base::dsp::Svf::Low);
                        x *= e.env * e.gain;
                    }
                    if (e.type == DamperLift)
                        sympathetic[i] += x;
                    else
                    {
                        zone[0][i] += x * e.zone[0];
                        zone[1][i] += x * e.zone[1];
                        zone[2][i] += x * e.zone[2];
                        const float d = e.type == Knock ? 0.0f : 0.5f * x;
                        for (int c = 0; c < 4; ++c) direct[c][i] += d;
                    }
                }
            }
        }

    private:
        struct Event
        {
            bool active = false, attacking = true;
            Type type = Knock;
            float gain = 0.0f, env = 0.0f, attack = 0.1f, decay = 0.99f, freq = 300.0f, freq2 = 100.0f, reso = 0.3f;
            int pos = 0, pulseLength = 100;
            std::array<float, 3> zone {};
            fjord::base::dsp::Svf filter, filter2;
        };

        void setEnv (Event& e, float attackSeconds, float decaySeconds)
        {
            e.attack = 1.0f / std::max (1.0f, attackSeconds * fs);
            e.decay = std::exp (-1.0f / (decaySeconds * fs));
        }

        float fs = 48000.0f;
        std::array<Event, 32> events {};
        fjord::base::dsp::Random random;
    };
}
