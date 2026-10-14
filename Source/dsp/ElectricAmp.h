#pragma once
#include <array>
#include <cmath>
#include <algorithm>
#include <string_view>
#include <fjord_base/core/Dsp.h>
#include "Models.h"

// The amp of the electric models: preamp (soft, slightly asymmetric saturation), bass and treble, tremolo or auto-pan
// (the suitcase's stereo vibrato), and the voicing of the instrument's own electronics and speakers.
namespace hamar::dsp
{
    struct AmpSettings
    {
        int model = 0;
        float drive = 0.2f, bass = 0.0f, treble = 0.0f;
        int tremMode = 0;
        float tremRate = 4.0f, tremDepth = 0.5f;
        bool operator== (const AmpSettings&) const = default;
    };

    class ElectricAmp
    {
    public:
        void prepare (double sampleRate)
        {
            fs = sampleRate;
            configured = false;
            reset();
        }

        void reset()
        {
            for (auto& f : filters) f.reset();
            phase = 0.0;
        }

        void configure (const AmpSettings& s)
        {
            if (configured && s == settings) return;
            settings = s;
            configured = true;
            const auto& m = model (s.model);
            // Voicing of the instrument's electronics and speakers
            float hp = 40.0f, peakF = 800.0f, peakDb = -2.0f, peakQ = 0.7f, lp = 10000.0f;
            const std::string_view name (m.name);
            if (name == "Tine Suitcase")       { hp = 55.0f; peakF = 140.0f; peakDb = 3.0f; peakQ = 0.9f; lp = 5800.0f; }
            else if (name == "Reed 200")       { hp = 90.0f; peakF = 1200.0f; peakDb = 4.0f; peakQ = 1.0f; lp = 5200.0f; }
            else if (name == "Electric Grand") { hp = 70.0f; peakF = 3500.0f; peakDb = 3.0f; peakQ = 0.8f; lp = 12000.0f; }
            else if (name == "Pianet")         { hp = 100.0f; peakF = 2000.0f; peakDb = 3.0f; peakQ = 0.9f; lp = 6500.0f; }
            else if (name == "Clav")           { hp = 80.0f; peakF = 2500.0f; peakDb = 4.0f; peakQ = 1.0f; lp = 10000.0f; }
            filters[0].highpass (fs, hp, 0.707);
            peaking (filters[1], peakF, peakQ, peakDb);
            lowShelf (filters[2], 120.0, s.bass);
            highShelf (filters[3], 3000.0, s.treble);
            filters[4].lowpass (fs, std::min (lp, 0.45f * (float) fs), 0.707);
            drive = 1.0f + 9.0f * std::pow (s.drive, 1.5f);
            makeUp = 1.0f / std::pow (drive, 0.6f);
        }

        // Adds the amp's output to l / r (inputs: the pickups, left / right)
        void process (const float* inL, const float* inR, float* l, float* r, int n)
        {
            const double inc = settings.tremRate / fs;
            for (int i = 0; i < n; ++i)
            {
                float a = inL[i], b = inR[i];
                // Preamp: a little asymmetric, so it gets warm before it gets hard
                a = (std::tanh (drive * a + 0.15f) - 0.148885f) * makeUp;
                b = (std::tanh (drive * b + 0.15f) - 0.148885f) * makeUp;
                for (size_t k = 0; k < filters.size(); ++k) { a = filters[k].process (a, 0); b = filters[k].process (b, 1); }

                phase += inc;
                if (phase >= 1.0) phase -= 1.0;
                const float lfo = (float) std::sin (2.0 * 3.14159265358979 * phase);
                if (settings.tremMode == 1)
                {
                    const float g = 1.0f - settings.tremDepth * (0.5f + 0.5f * lfo);
                    a *= g;
                    b *= g;
                }
                else if (settings.tremMode == 2)
                {
                    // Auto-pan: equal power around the centre
                    const float pan = settings.tremDepth * lfo;
                    const float angle = (pan + 1.0f) * 0.25f * fjord::base::dsp::kPi;
                    const float mono = 0.5f * (a + b);
                    a = mono * std::cos (angle) * 1.41421356f;
                    b = mono * std::sin (angle) * 1.41421356f;
                }
                l[i] += a;
                r[i] += b;
            }
        }

    private:
        void peaking (fjord::base::dsp::Biquad& f, double freq, double q, double gainDb)
        {
            const double a = std::pow (10.0, gainDb / 40.0), w = 2.0 * 3.14159265358979 * freq / fs, alpha = std::sin (w) / (2.0 * q);
            f.set (1 + alpha * a, -2 * std::cos (w), 1 - alpha * a, 1 + alpha / a, -2 * std::cos (w), 1 - alpha / a);
        }

        void shelf (fjord::base::dsp::Biquad& f, double freq, double gainDb, bool high)
        {
            const double a = std::pow (10.0, gainDb / 40.0), w = 2.0 * 3.14159265358979 * freq / fs;
            const double c = std::cos (w), alpha = std::sin (w) / 2.0 * std::sqrt (2.0), sq = 2.0 * std::sqrt (a) * alpha;
            if (high)
                f.set (a * ((a + 1) + (a - 1) * c + sq), -2 * a * ((a - 1) + (a + 1) * c), a * ((a + 1) + (a - 1) * c - sq),
                       (a + 1) - (a - 1) * c + sq, 2 * ((a - 1) - (a + 1) * c), (a + 1) - (a - 1) * c - sq);
            else
                f.set (a * ((a + 1) - (a - 1) * c + sq), 2 * a * ((a - 1) - (a + 1) * c), a * ((a + 1) - (a - 1) * c - sq),
                       (a + 1) + (a - 1) * c + sq, -2 * ((a - 1) + (a + 1) * c), (a + 1) + (a - 1) * c - sq);
        }

        void lowShelf (fjord::base::dsp::Biquad& f, double freq, double gainDb) { shelf (f, freq, gainDb, false); }
        void highShelf (fjord::base::dsp::Biquad& f, double freq, double gainDb) { shelf (f, freq, gainDb, true); }

        double fs = 48000.0, phase = 0.0;
        AmpSettings settings;
        bool configured = false;
        float drive = 1.0f, makeUp = 1.0f;
        std::array<fjord::base::dsp::Biquad, 5> filters {};
    };
}
