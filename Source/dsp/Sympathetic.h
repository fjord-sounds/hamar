#pragma once
#include <array>
#include <cmath>
#include <algorithm>
#include "KeyPhysics.h"

// Sympathetic resonance: every string without its damper on (pedal down, key held, the undamped top octaves) rings
// along with what the bridge does. One loop per key (delay, loss, linear fractional read) listens to the bridge; the
// keys that are sounding themselves are left out (their own strings already are the voice).
namespace hamar::dsp
{
    class SympatheticBank
    {
    public:
        void prepare (double sampleRate)
        {
            fs = sampleRate;
            for (int i = 0; i < kNumKeys; ++i)
            {
                const double fMin = 440.0 * std::exp2 ((i + kLowestKey - 69) / 12.0) * (400.0 / 440.0) * std::exp2 (-80.0 / 1200.0);
                strings[(size_t) i].ring.prepare ((int) std::ceil (fs / fMin) + 8);
                strings[(size_t) i].active = false;
            }
        }

        void reset()
        {
            for (auto& s : strings)
            {
                s.ring.clear();
                s.lp = 0.0f;
                s.level = 0.0f;
                s.active = false;
            }
        }

        // Pitches and decay from the key table
        void tune (const KeyTable& table, int lowKey)
        {
            for (int i = 0; i < kNumKeys; ++i)
            {
                auto& s = strings[(size_t) i];
                const auto& k = table[i + kLowestKey];
                const double period = fs / k.f1 - 0.5;   // the one-pole below adds about half a sample
                s.len = std::clamp ((int) std::floor (period), 2, s.ring.capacity() - 1);
                s.frac = (float) (period - s.len);
                s.free = (float) std::exp (-6.9078 / (k.f1 * k.after * 0.7));
                s.damped = (float) std::exp (-6.9078 / (k.f1 * 0.08));
                s.zone = k.zone;
                s.exists = i + kLowestKey >= lowKey;
            }
        }

        // 0 = damper on .. 1 = free; sounding = the key is playing as a voice
        void setFree (int index, float free, bool sounding)
        {
            auto& s = strings[(size_t) index];
            s.target = std::clamp (free, 0.0f, 1.0f);
            s.sounding = sounding;
            if (! s.active && s.target > 0.05f && ! sounding && s.exists)
            {
                s.ring.clear();
                s.lp = 0.0f;
                s.level = 1.0f;
                s.gain = s.damped;
                s.active = true;
            }
        }

        void process (const float* bridge, float* const* zone, int n, float amount)
        {
            const float inGain = 0.004f * amount;
            const float outGain = 1.0f;
            for (auto& s : strings)
            {
                if (! s.active) continue;
                const float target = s.damped + (s.free - s.damped) * s.target;
                const float in = s.sounding ? 0.0f : inGain;
                float peak = 0.0f;
                for (int i = 0; i < n; ++i)
                {
                    s.gain += 0.002f * (target - s.gain);
                    const float y0 = s.ring.read (s.len), y1 = s.ring.read (s.len + 1);
                    const float y = y0 + s.frac * (y1 - y0);
                    s.lp += 0.7f * (y - s.lp);
                    s.ring.push (bridge[i] * in + s.lp * s.gain);
                    const float o = y * outGain;
                    zone[0][i] += o * s.zone[0];
                    zone[1][i] += o * s.zone[1];
                    zone[2][i] += o * s.zone[2];
                    peak = std::max (peak, std::abs (y));
                }
                s.level = std::max (peak, s.level * 0.95f);
                if ((s.target < 0.05f || s.sounding) && s.level < 1.0e-7f)
                    s.active = false;
            }
        }

    private:
        struct Str
        {
            Ring ring;
            int len = 100;
            float frac = 0.0f, free = 0.999f, damped = 0.9f, gain = 0.9f, target = 0.0f, lp = 0.0f, level = 0.0f;
            std::array<float, 3> zone {};
            bool active = false, sounding = false, exists = true;
        };

        double fs = 48000.0;
        std::array<Str, kNumKeys> strings;
    };
}
