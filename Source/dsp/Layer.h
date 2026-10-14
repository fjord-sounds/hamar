#pragma once
#include <array>
#include <cmath>
#include <algorithm>
#include <fjord_base/core/Dsp.h>
#include "Tuning.h"

// A layer under the piano: a warm pad (three detuned saws through a soft low pass), strings (five saws with a slow
// vibrato and a brighter filter) or air (a breathy band of noise around the note with a sine). One voice per key, it
// follows the key and the sustain pedal, with its own attack and release.
namespace hamar::dsp
{
    struct LayerSettings
    {
        float level = 0.0f;
        int type = 0;
        float attack = 0.5f, release = 1.5f, tone = 0.45f, detune = 0.4f;
        int octave = 0;
    };

    class Layer
    {
    public:
        void prepare (double sampleRate)
        {
            fs = (float) sampleRate;
            for (auto& v : voices) v = {};
            random.seed (9001u);
        }

        void reset() { for (auto& v : voices) v.active = false; }

        bool isActive (int index) const { return voices[(size_t) index].active; }

        void noteOn (int index, float f0, float velocity, const LayerSettings& s)
        {
            if (s.level <= 0.0f) return;
            auto& v = voices[(size_t) index];
            const float f = f0 * std::exp2 ((float) s.octave);
            const int count = s.type == 1 ? 5 : 3;
            for (int i = 0; i < 5; ++i)
            {
                const float spread = (i - (count - 1) * 0.5f) / std::max (1.0f, (count - 1) * 0.5f);   // -1 .. 1
                v.inc[(size_t) i] = std::min (0.45f, f * std::exp2 (spread * s.detune * 18.0f / 1200.0f) / fs);
                v.pan[(size_t) i] = 0.5f + 0.45f * spread;
                if (! v.active) v.phase[(size_t) i] = random.next01();
            }
            v.count = count;
            v.freq = f;
            v.velocity = 0.5f + 0.5f * velocity;
            v.free = 1.0f;
            if (! v.active) { v.env = 0.0f; v.filter[0].reset(); v.filter[1].reset(); }
            v.active = true;
        }

        void setFree (int index, float free) { voices[(size_t) index].free = free; }

        // Adds the layer to l / r
        void render (int index, float* l, float* r, int n, const LayerSettings& s)
        {
            auto& v = voices[(size_t) index];
            if (! v.active) return;
            const float attack = 1.0f / std::max (1.0f, s.attack * fs);
            const float release = std::exp (-1.0f / std::max (1.0f, s.release * 0.3f * fs));
            const float cutoff = std::min (0.45f * fs, v.freq * (1.5f + 14.0f * s.tone * s.tone) * (s.type == 1 ? 1.6f : 1.0f) + 200.0f);
            const float gain = s.level * v.velocity * 0.05f;
            for (int i = 0; i < n; ++i)
            {
                v.env = v.free > 0.5f ? std::min (1.0f, v.env + attack) : v.env * release;
                v.vibrato += 5.2f / fs;
                if (v.vibrato >= 1.0f) v.vibrato -= 1.0f;
                const float vib = s.type == 1 ? 1.0f + 0.0025f * std::sin (fjord::base::dsp::kTwoPi * v.vibrato) : 1.0f;
                float a = 0.0f, b = 0.0f;
                if (s.type == 2)
                {
                    // Air: noise around the note and a soft sine
                    const float noise = v.filter[1].process (random.nextBipolar(), v.freq * 2.0f, 0.75f, fs, fjord::base::dsp::Svf::Band);
                    const float sine = std::sin (fjord::base::dsp::kTwoPi * v.phase[0]);
                    v.phase[0] += v.inc[0];
                    if (v.phase[0] >= 1.0f) v.phase[0] -= 1.0f;
                    a = 0.6f * sine + 2.0f * noise;
                    b = 0.6f * sine - 2.0f * noise;
                }
                else
                    for (int k = 0; k < v.count; ++k)
                    {
                        const float dt = v.inc[(size_t) k] * vib;
                        float& ph = v.phase[(size_t) k];
                        const float saw = 2.0f * ph - 1.0f - fjord::base::dsp::polyBlep (ph, dt);
                        ph += dt;
                        if (ph >= 1.0f) ph -= 1.0f;
                        a += saw * (1.0f - v.pan[(size_t) k]);
                        b += saw * v.pan[(size_t) k];
                    }
                a = v.filter[0].process (a, cutoff, 0.15f, fs, fjord::base::dsp::Svf::Low);
                b = v.svfR.process (b, cutoff, 0.15f, fs, fjord::base::dsp::Svf::Low);
                l[i] += a * v.env * gain;
                r[i] += b * v.env * gain;
            }
            if (v.free < 0.5f && v.env < 1.0e-4f) v.active = false;
        }

    private:
        struct Voice
        {
            bool active = false;
            int count = 3;
            float env = 0.0f, freq = 220.0f, velocity = 1.0f, free = 1.0f, vibrato = 0.0f;
            std::array<float, 5> phase {}, inc {}, pan {};
            std::array<fjord::base::dsp::Svf, 2> filter {};
            fjord::base::dsp::Svf svfR;
        };

        float fs = 48000.0f;
        std::array<Voice, kNumKeys> voices {};
        fjord::base::dsp::Random random;
    };
}
