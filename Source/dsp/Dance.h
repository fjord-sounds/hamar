#pragma once
#include <array>
#include <cmath>
#include <algorithm>
#include <vector>
#include <fjord_base/core/Dsp.h>
#include "Tuning.h"

// The dance models: the organ layer of the classic house piano (a few sine harmonics with a quick attack and a held
// tone, as the sampled pianos of the early nineties looped them), punch (a transient shaper that pushes the attack
// against the body) and tape (saturation, wow and flutter, hiss, a narrower band).
namespace hamar::dsp
{
    struct DanceSettings
    {
        float organ = 0.0f, organTone = 0.5f, stab = 0.0f, punch = 0.0f, lofi = 0.0f, wow = 0.3f;
        bool operator== (const DanceSettings&) const = default;
    };

    // One organ voice per key
    class OrganLayer
    {
    public:
        void prepare (double sampleRate)
        {
            fs = (float) sampleRate;
            for (auto& k : keys) k = {};
        }

        void reset() { for (auto& k : keys) k.active = false; }

        void noteOn (int index, float f0, float velocity, float level, float tone, float stab)
        {
            auto& k = keys[(size_t) index];
            if (level <= 0.0f) return;
            static constexpr float ratios[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
            for (int h = 0; h < 4; ++h)
            {
                k.inc[(size_t) h] = std::min (0.45f, f0 * ratios[h] / fs);
                k.gain[(size_t) h] = std::pow (0.25f + 0.65f * tone, (float) h);   // brighter drawbars with more tone
            }
            k.level = level * (0.4f + 0.6f * velocity);
            k.attack = 1.0f / (0.004f * fs);
            k.decay = std::exp (-1.0f / ((0.25f + 1.2f * (1.0f - stab)) * fs));
            k.sustain = 0.55f * (1.0f - 0.8f * stab);
            k.release = std::exp (-1.0f / (0.12f * fs));
            k.stage = 0;
            k.active = true;
            k.free = 1.0f;
        }

        // 1 = key or pedal holds the tone, 0 = released
        void setFree (int index, float free) { keys[(size_t) index].free = free; }

        void render (int index, int n, float* const* direct, const std::array<float, 4>& gains)
        {
            auto& k = keys[(size_t) index];
            if (! k.active) return;
            for (int i = 0; i < n; ++i)
            {
                if (k.stage == 0) { k.env += k.attack; if (k.env >= 1.0f) { k.env = 1.0f; k.stage = 1; } }
                else if (k.stage == 1) k.env = k.sustain + (k.env - k.sustain) * k.decay;
                if (k.free < 0.5f) k.env *= k.release;
                float s = 0.0f;
                for (int h = 0; h < 4; ++h)
                {
                    s += k.gain[(size_t) h] * std::sin (fjord::base::dsp::kTwoPi * k.phase[(size_t) h]);
                    k.phase[(size_t) h] += k.inc[(size_t) h];
                    if (k.phase[(size_t) h] >= 1.0f) k.phase[(size_t) h] -= 1.0f;
                }
                const float out = s * k.env * k.level * 0.03f;
                for (int c = 0; c < 4; ++c) direct[c][i] += out * gains[(size_t) c];
            }
            if (k.env < 1.0e-5f && k.free < 0.5f) k.active = false;
        }

        bool isActive (int index) const { return keys[(size_t) index].active; }

    private:
        struct Key
        {
            bool active = false;
            int stage = 0;
            float env = 0.0f, attack = 0.01f, decay = 0.999f, sustain = 0.5f, release = 0.99f, level = 0.0f, free = 1.0f;
            std::array<float, 4> phase {}, inc {}, gain {};
        };

        float fs = 48000.0f;
        std::array<Key, kNumKeys> keys {};
    };

    // Punch and tape on the summed instrument
    class DanceStage
    {
    public:
        void prepare (double sampleRate)
        {
            fs = (float) sampleRate;
            for (auto& d : tape) d.assign ((size_t) (0.05f * fs) + 8, 0.0f);
            reset();
        }

        void reset()
        {
            for (auto& d : tape) std::fill (d.begin(), d.end(), 0.0f);
            write = 0;
            fastEnv = slowEnv = 0.0f;
            wowPhase = flutterPhase = 0.0f;
            lp.fill (0.0f);
            random.seed (777u);
        }

        void process (float* l, float* r, int n, float punch, float lofi, float wow)
        {
            if (punch <= 0.001f && lofi <= 0.001f) return;
            const float fastA = 1.0f - std::exp (-1.0f / (0.0008f * fs)), fastR = 1.0f - std::exp (-1.0f / (0.03f * fs));
            const float slowA = 1.0f - std::exp (-1.0f / (0.03f * fs)), slowR = 1.0f - std::exp (-1.0f / (0.25f * fs));
            const float cutoff = 18000.0f * std::pow (0.22f, lofi);
            const float lpc = fjord::base::dsp::onePoleCoef (cutoff, fs);
            const int size = (int) tape[0].size();
            for (int i = 0; i < n; ++i)
            {
                float a = l[i], b = r[i];
                if (punch > 0.001f)
                {
                    // Transient shaper: the attack (fast follower above the slow one) is lifted
                    const float x = std::abs (a) + std::abs (b);
                    fastEnv += (x > fastEnv ? fastA : fastR) * (x - fastEnv);
                    slowEnv += (x > slowEnv ? slowA : slowR) * (x - slowEnv);
                    const float ratio = (fastEnv + 1.0e-6f) / (slowEnv + 1.0e-6f);
                    const float g = std::pow (std::clamp (ratio, 0.5f, 4.0f), 0.9f * punch) / (1.0f + 0.4f * punch);
                    a *= g;
                    b *= g;
                }
                if (lofi > 0.001f)
                {
                    // Tape: saturation, band, wow and flutter (a modulated delay), hiss
                    const float drive = 1.0f + 3.0f * lofi;
                    a = std::tanh (a * drive) / drive * (1.0f + 0.5f * lofi);
                    b = std::tanh (b * drive) / drive * (1.0f + 0.5f * lofi);
                    lp[0] += lpc * (a - lp[0]); lp[1] += lpc * (lp[0] - lp[1]);
                    lp[2] += lpc * (b - lp[2]); lp[3] += lpc * (lp[2] - lp[3]);
                    a = lp[1]; b = lp[3];
                    tape[0][(size_t) write] = a;
                    tape[1][(size_t) write] = b;
                    wowPhase += 0.55f / fs; if (wowPhase >= 1.0f) wowPhase -= 1.0f;
                    flutterPhase += 6.3f / fs; if (flutterPhase >= 1.0f) flutterPhase -= 1.0f;
                    const float delay = (0.008f + wow * lofi * (0.0022f * std::sin (fjord::base::dsp::kTwoPi * wowPhase)
                                                                + 0.00025f * std::sin (fjord::base::dsp::kTwoPi * flutterPhase))) * fs;
                    const float pos = (float) write - delay;
                    const int i0 = (int) std::floor (pos);
                    const float frac = pos - (float) i0;
                    auto at = [&] (int c, int idx) { return tape[(size_t) c][(size_t) ((idx % size + size) % size)]; };
                    a = at (0, i0) + frac * (at (0, i0 + 1) - at (0, i0));
                    b = at (1, i0) + frac * (at (1, i0 + 1) - at (1, i0));
                    write = (write + 1) % size;
                    const float hiss = 0.0015f * lofi;
                    a += hiss * random.nextBipolar();
                    b += hiss * random.nextBipolar();
                }
                l[i] = a;
                r[i] = b;
            }
        }

    private:
        float fs = 48000.0f;
        std::array<std::vector<float>, 2> tape;
        int write = 0;
        float fastEnv = 0.0f, slowEnv = 0.0f, wowPhase = 0.0f, flutterPhase = 0.0f;
        std::array<float, 4> lp {};
        fjord::base::dsp::Random random;
    };
}
