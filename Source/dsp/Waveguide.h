#pragma once
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>

// Building blocks of the string model: integer delay rings, first-order allpasses and the design of the filters in a
// string loop (losses, dispersion, fine tuning). Everything a string needs is designed from physical numbers: the
// fundamental, the inharmonicity B and the decay times.
namespace hamar::dsp
{
    // Power-of-two ring buffer with integer delays (read before push: read (1) is the sample pushed last).
    struct Ring
    {
        std::vector<float> buffer;
        int mask = 0, write = 0;

        void prepare (int maxDelay)
        {
            int size = 4;
            while (size < maxDelay + 4) size <<= 1;
            buffer.assign ((size_t) size, 0.0f);
            mask = size - 1;
            write = 0;
        }

        int capacity() const { return mask - 2; }
        void clear() { std::fill (buffer.begin(), buffer.end(), 0.0f); }
        float read (int delay) const { return buffer[(size_t) ((write - delay) & mask)]; }

        // Fractional read (Catmull-Rom), for small pitch modulation
        float readFrac (float delay) const
        {
            const int i = (int) std::floor (delay);
            const float f = delay - (float) i;
            const float xm1 = read (i - 1), x0 = read (i), x1 = read (i + 1), x2 = read (i + 2);
            const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * f + c2) * f + c1) * f + x0;
        }
        void push (float x) { buffer[(size_t) write] = x; write = (write + 1) & mask; }
    };

    // First-order allpass (a + z^-1) / (1 + a z^-1)
    struct Allpass1
    {
        float a = 0.0f, x1 = 0.0f, y1 = 0.0f;

        float process (float x)
        {
            const float y = a * (x - y1) + x1;
            x1 = x;
            y1 = y;
            return y;
        }

        void reset() { x1 = y1 = 0.0f; }
    };

    constexpr double kPiD = 3.14159265358979323846;

    namespace design
    {
        // Phase delay (samples) of the first-order allpass at w (radians per sample)
        inline double allpassDelay (double a, double w)
        {
            return 1.0 - 2.0 / w * std::atan2 (a * std::sin (w), 1.0 + a * std::cos (w));
        }

        // Phase delay of the loss filter g (1 + a) / (1 + a z^-1)
        inline double lossDelay (double a, double w)
        {
            return -std::atan2 (a * std::sin (w), 1.0 + a * std::cos (w)) / w;
        }

        inline double lossMagnitude (double a, double w)   // without g
        {
            return (1.0 + a) / std::sqrt (1.0 + 2.0 * a * std::cos (w) + a * a);
        }

        struct Loss
        {
            float g = 0.999f, a = 0.0f;
        };

        // One-pole loss filter of a string loop: the fundamental decays with T60 'decay0' (seconds), a higher frequency
        // fh with 'decayH'. The loop runs once per period, so the gain per pass is exp(-sigma / f0).
        inline Loss loss (double f0, double fs, double decay0, double fh, double decayH)
        {
            const double sigma0 = 6.9078 / std::max (0.005, decay0), sigmaH = 6.9078 / std::max (0.002, decayH);
            const double w0 = 2.0 * kPiD * f0 / fs, wh = 2.0 * kPiD * std::min (fh, 0.45 * fs) / fs;
            const double target = std::exp (-std::max (0.0, sigmaH - sigma0) / f0);   // |H(wh)| / |H(w0)|
            double lo = -0.95, hi = 0.0;                                              // ratio falls as a goes to -1
            for (int i = 0; i < 40; ++i)
            {
                const double mid = 0.5 * (lo + hi);
                const double ratio = lossMagnitude (mid, wh) / lossMagnitude (mid, w0);
                (ratio > target ? hi : lo) = mid;
            }
            const double a = 0.5 * (lo + hi);
            Loss l;
            l.a = (float) a;
            l.g = (float) (std::exp (-sigma0 / f0) / lossMagnitude (a, w0));
            return l;
        }

        // Coefficient of a cascade of m identical first-order allpasses that bends the partials of the loop onto
        // n f0 sqrt ((1 + B n^2) / (1 + B)). The partials of the loop follow from w D(w) = 2 pi n, where D is the total
        // phase delay of the loop; the coefficient is searched (golden section) for the smallest weighted error in cents
        // over the partials that matter. maxDelay limits the dispersion delay at f0 (the loop must keep a few samples
        // of plain delay).
        inline float dispersion (double f0, double b, int m, double fs, double maxDelay, double lossA = 0.0)
        {
            if (m <= 0 || b <= 1.0e-7) return 0.0f;
            const double w0 = 2.0 * kPiD * f0 / fs, period = fs / f0;
            auto dispDelay = [&] (double a, double w) { return m * allpassDelay (a, w); };

            // Most negative coefficient that still fits into the loop
            double lo = -0.98, hi = 0.0;
            if (dispDelay (lo, w0) > maxDelay)
            {
                for (int i = 0; i < 40; ++i)
                {
                    const double mid = 0.5 * (lo + hi);
                    (dispDelay (mid, w0) > maxDelay ? lo : hi) = mid;
                }
                lo = hi;
                hi = 0.0;
            }

            int numPartials = 0;
            std::array<double, 12> target {};
            for (int n = 1; n <= 12; ++n)
            {
                const double f = n * f0 * std::sqrt ((1.0 + b * n * n) / (1.0 + b));
                if (f > std::min (0.42 * fs, 12000.0)) break;
                target[(size_t) n - 1] = f;
                numPartials = n;
            }
            if (numPartials < 2) return (float) hi;

            auto error = [&] (double a)
            {
                const double rest = period - dispDelay (a, w0) - lossDelay (lossA, w0);   // frequency independent part
                double e = 0.0;
                for (int n = 2; n <= numPartials; ++n)
                {
                    double w = 2.0 * kPiD * target[(size_t) n - 1] / fs;
                    for (int it = 0; it < 3; ++it)
                        w = 2.0 * kPiD * n / (rest + dispDelay (a, w) + lossDelay (lossA, w));
                    const double cents = 1200.0 * std::log2 (w * fs / (2.0 * kPiD) / target[(size_t) n - 1]);
                    e += cents * cents / (double) (n * n);   // the strong low partials matter most
                }
                return e;
            };

            const double phi = 0.6180339887;
            double x1 = hi - phi * (hi - lo), x2 = lo + phi * (hi - lo);
            double e1 = error (x1), e2 = error (x2);
            for (int i = 0; i < 22; ++i)
            {
                if (e1 < e2) { hi = x2; x2 = x1; e2 = e1; x1 = hi - phi * (hi - lo); e1 = error (x1); }
                else         { lo = x1; x1 = x2; e1 = e2; x2 = lo + phi * (hi - lo); e2 = error (x2); }
            }
            return (float) (0.5 * (lo + hi));
        }
    }
}
