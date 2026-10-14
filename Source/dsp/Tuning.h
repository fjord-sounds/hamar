#pragma once
#include <array>
#include <cmath>
#include <algorithm>

// Tuning of the 97 keys (C0 .. C8): temperament around a key, reference pitch and the stretch a piano tuner gives a
// real piano. Stiff strings have sharp upper partials, so tuners match octaves on the partials (2:1 in the treble,
// 4:2 in the bass); the octaves come out wider than 2:1 (the Railsback curve). The stretch here is derived from the
// inharmonicity of the model, so a small upright gets more of it than a concert grand.
namespace hamar::dsp
{
    constexpr int kLowestKey = 12, kHighestKey = 108, kNumKeys = kHighestKey - kLowestKey + 1;

    inline int keyIndex (int key) { return std::clamp (key, kLowestKey, kHighestKey) - kLowestKey; }

    // Cents from equal temperament for C, C#, D … B, with the temperament built on C. Order = the choice parameter.
    inline const std::array<std::array<float, 12>, 7>& temperaments()
    {
        static const std::array<std::array<float, 12>, 7> t { {
            { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },                                                         // equal
            { 0, -9.78f, -7.82f, -5.87f, -9.78f, -1.96f, -11.73f, -3.91f, -7.82f, -11.73f, -3.91f, -7.82f }, // Werckmeister III
            { 0, -5.87f, -1.96f, -3.91f, -7.82f, 1.96f, -7.82f, -1.96f, -3.91f, -5.87f, 0.0f, -9.78f },      // Vallotti
            { 0, -9.78f, -6.84f, -5.87f, -13.69f, -1.96f, -9.78f, -3.42f, -7.82f, -10.26f, -3.91f, -11.73f },// Kirnberger III
            { 0, -23.95f, -6.84f, 10.26f, -13.69f, 3.42f, -20.53f, -3.42f, -27.37f, -10.26f, 6.84f, -17.11f },// quarter-comma meantone
            { 0, 11.73f, 3.91f, 15.64f, -13.69f, -1.96f, -9.78f, 1.96f, 13.69f, -15.64f, 17.60f, -11.73f },  // just (5-limit)
            { 0, 13.69f, 3.91f, -5.87f, 7.82f, -1.96f, 11.73f, 1.96f, -7.82f, 5.87f, -3.91f, 9.78f },        // Pythagorean
        } };
        return t;
    }

    struct TuningSettings
    {
        float a4 = 440.0f;
        int temperament = 0, root = 0;
        float stretch = 1.0f;
        const float* custom = nullptr;   // a Scala tuning: cents from equal temperament per key (replaces the temperament)
        int customVersion = 0;
        bool operator== (const TuningSettings&) const = default;
    };

    // Frequencies of the first partial of every key. inharm(key) is the model's B curve.
    template <typename InharmFn>
    std::array<float, kNumKeys> computeTuning (const TuningSettings& s, InharmFn inharm)
    {
        // Stretch in cents per key from octave matching on the partials, anchored at A4
        std::array<double, kNumKeys> stretchCents {};
        auto partialRatio = [] (double n, double b) { return n * std::sqrt ((1.0 + b * n * n) / (1.0 + b)); };
        std::array<double, 10> anchorCents {};   // the A of each octave: A0 (21) is index 1, A4 (69) is index 5
        auto aKey = [] (int i) { return 9 + 12 * i; };
        anchorCents[5] = 0.0;
        for (int i = 6; i < 10; ++i)              // upwards: the upper fundamental sits on the second partial of the lower note
        {
            const double b = inharm ((float) aKey (i - 1));
            anchorCents[(size_t) i] = anchorCents[(size_t) i - 1] + 1200.0 * std::log2 (partialRatio (2, b) / 2.0);
        }
        for (int i = 4; i >= 0; --i)              // downwards: 4:2 octaves (the lower 4th partial on the upper 2nd)
        {
            const double bl = inharm ((float) aKey (i)), bu = inharm ((float) aKey (i + 1));
            anchorCents[(size_t) i] = anchorCents[(size_t) i + 1] - 1200.0 * std::log2 ((partialRatio (4, bl) / 4.0) / (partialRatio (2, bu) / 2.0));
        }
        for (int k = kLowestKey; k <= kHighestKey; ++k)
        {
            const double pos = (k - 9) / 12.0;    // position in units of A octaves
            const int i = std::clamp ((int) std::floor (pos), 0, 8);
            const double t = pos - i;
            stretchCents[(size_t) keyIndex (k)] = anchorCents[(size_t) i] + t * (anchorCents[(size_t) i + 1] - anchorCents[(size_t) i]);
        }

        const auto& temp = temperaments()[(size_t) std::clamp (s.temperament, 0, 6)];
        auto offset = [&] (int pitchClass) { return (double) temp[(size_t) ((pitchClass - s.root + 12) % 12)]; };
        const double aOffset = offset (9);

        std::array<float, kNumKeys> f {};
        for (int k = kLowestKey; k <= kHighestKey; ++k)
        {
            const double base = s.custom != nullptr ? (double) s.custom[keyIndex (k)] : offset (k % 12) - aOffset;
            const double cents = base + s.stretch * stretchCents[(size_t) keyIndex (k)];
            f[(size_t) keyIndex (k)] = (float) (s.a4 * std::exp2 ((k - 69) / 12.0 + cents / 1200.0));
        }
        return f;
    }
}
