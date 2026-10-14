#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <algorithm>
#include <juce_core/juce_core.h>
#include <fjord_base/core/Dsp.h>
#include "Models.h"
#include "Waveguide.h"
#include "Convolver.h"

// The soundboard, the cabinet and the microphones.
//
// A piano's strings hardly radiate themselves: what we hear is the soundboard, driven by the bridge. The board is a
// thin plate with hundreds of modes - few and long-ringing in the bass, dense and short above a few hundred hertz -
// and its mobility shapes the woody, broad attack of a piano. Hamar synthesizes the board's impulse response from
// such modes (density, damping and radiation from the model's size, the case resonances of rim or upright box, and a
// different mix of modes for every mic position) and convolves the bridge signal with it. The response is rebuilt
// on a background thread when a setting changes and swapped in without a lock.
//
// After the board: lid or panel (treble shading and the lid reflection), distance (level, air, delay) and, for an
// upright, the wall behind it.
namespace hamar::dsp
{
    struct MicSettings
    {
        float x = 0.5f, y = 0.25f, gain = 1.0f, width = 0.7f;   // x: bass .. treble end, y: close .. far
        bool operator== (const MicSettings&) const = default;
    };

    struct BoardSettings
    {
        int model = 0, quality = 1;
        float size = 0.0f, resonance = 0.5f, damping = 0.0f, brightness = 0.0f;
        int lid = 0, panel = 0;
        float rim = 0.5f, wall = 0.4f, box = 0.5f;
        MicSettings micA, micB;
        bool operator== (const BoardSettings&) const = default;
    };

    // Gains of a key into the four mic channels (A left/right, B left/right), from the key position and the mics
    inline std::array<float, 4> micGainsFor (float keyPos, const MicSettings& a, const MicSettings& b)
    {
        auto pair = [keyPos] (const MicSettings& m, float* out)
        {
            const float spread = 0.18f + 0.8f * m.y;
            const float xl = m.x - 0.28f * m.width, xr = m.x + 0.28f * m.width;
            const float gl = 1.0f / std::sqrt (1.0f + ((keyPos - xl) / spread) * ((keyPos - xl) / spread));
            const float gr = 1.0f / std::sqrt (1.0f + ((keyPos - xr) / spread) * ((keyPos - xr) / spread));
            const float norm = 1.0f / std::max (0.3f, std::sqrt (0.5f * (gl * gl + gr * gr)));
            out[0] = gl * norm * 0.8f + 0.2f * (gl + gr) * 0.5f * norm;
            out[1] = gr * norm * 0.8f + 0.2f * (gl + gr) * 0.5f * norm;
        };
        std::array<float, 4> g {};
        pair (a, g.data());
        pair (b, g.data() + 2);
        return g;
    }

    // The board's impulse responses for the four mic channels
    struct BoardResponse
    {
        std::array<Convolver::Kernel, 4> kernels;

        static float lengthSeconds (const BoardSettings& s)
        {
            return std::clamp (0.7f * model (s.model).boardSize * std::exp2 (0.4f * s.size - 0.5f * s.damping), 0.3f, 1.0f);
        }

        void build (const BoardSettings& s, double fs, int block, juce::dsp::FFT& fft)
        {
            const auto& m = model (s.model);
            const bool grand = m.cabinet == Cabinet::Grand;
            const int length = (int) (lengthSeconds (s) * fs);
            std::vector<float> ir ((size_t) length);

            const float sizeScale = 1.0f / (m.boardSize * std::exp2 (0.5f * s.size));
            const float fLow = m.boardLow * std::pow (sizeScale, 0.7f);
            const float fTop = (float) std::min (14000.0, 0.45 * fs);
            const float fHigh = 4000.0f * std::exp2 (1.2f * s.brightness);   // radiation falls above this
            const float density = s.quality >= 2 ? 1.6f : 1.0f;              // more modes at High / Render

            for (int c = 0; c < 4; ++c)
            {
                std::fill (ir.begin(), ir.end(), 0.0f);
                fjord::base::dsp::Random rnd;
                rnd.seed (1234u + (uint32_t) s.model * 977u);   // the same board for every channel, heard from four places
                const auto& mic = c < 2 ? s.micA : s.micB;
                const float mx = mic.x + (c % 2 == 0 ? -0.28f : 0.28f) * mic.width, my = mic.y + (c < 2 ? 0.0f : 0.5f);

                auto addMode = [&] (float f, float q, float amp, float kx, float ky, float phase)
                {
                    // Every mic hears the mode with its own weight (the mode shape at its position)
                    const float w = std::cos (fjord::base::dsp::kTwoPi * (kx * mx + ky * my) + phase);
                    const float a = amp * (0.35f + 0.65f * std::abs (w)) * (w < 0.0f ? -1.0f : 1.0f);
                    const double omega = 2.0 * kPiD * f / fs;
                    const double r = std::exp (-kPiD * f / (q * fs));
                    const float cr = (float) (r * std::cos (omega)), ci = (float) (r * std::sin (omega));
                    float re = 1.0f, im = 0.0f;
                    const int n = std::min (length, (int) (q / (kPiD * f) * 9.2 * fs) + 1);   // until about -80 dB
                    for (int i = 0; i < n; ++i)
                    {
                        ir[(size_t) i] += a * im;
                        const float nr = cr * re - ci * im;
                        im = ci * re + cr * im;
                        re = nr;
                    }
                };

                float f = fLow;
                while (f < fTop)
                {
                    // Plate modes: Q grows from the soft bass to the stiff middle; radiation rises to the low middle
                    // and falls in the treble (brightness)
                    const float q = m.boardQ * (0.45f + 0.55f * f / (f + 400.0f)) * (0.7f + 0.6f * rnd.next01()) * std::exp2 (-0.8f * s.damping);
                    const float rise = f / std::sqrt (f * f + (1.6f * fLow) * (1.6f * fLow));
                    const float fall = 1.0f / (1.0f + std::pow (f / fHigh, 1.4f));
                    const float rayleigh = std::sqrt (-2.0f * std::log (std::max (1.0e-4f, rnd.next01()))) * 0.6f;
                    const float kx = (0.6f + 2.4f * rnd.next01()) * std::sqrt (f / 100.0f);
                    const float ky = (0.6f + 2.4f * rnd.next01()) * std::sqrt (f / 100.0f);
                    const float phase = rnd.next01() * fjord::base::dsp::kTwoPi;
                    // Amplitude per mode follows the spacing, so the energy per octave stays even
                    const float spacing = m.boardSpacing * sizeScale * (1.0f + f / 2500.0f) / density;
                    addMode (f, q, rise * fall * rayleigh * std::sqrt (spacing / 14.0f) * std::sqrt (f / 300.0f + 0.3f) / std::sqrt (q / 30.0f),
                             kx, ky, phase);
                    f += spacing * (0.3f + 1.4f * rnd.next01());
                }

                // The case: the rim of a grand (low and round), the box of an upright (boxy middle)
                if (grand)
                {
                    addMode (92.0f * sizeScale, 7.0f, 2.5f * s.rim, 0.3f, 0.2f, 0.0f);
                    addMode (148.0f * sizeScale, 6.0f, 1.8f * s.rim, 0.4f, 0.3f, 1.0f);
                }
                else
                {
                    addMode (230.0f, 5.0f, 2.0f * s.box, 0.3f, 0.2f, 0.5f);
                    addMode (520.0f, 6.0f, 1.3f * s.box, 0.5f, 0.3f, 1.5f);
                }

                // Normalise the energy (the level knob and the mic chain set the result), soft end
                double energy = 0.0;
                for (float x : ir) energy += (double) x * x;
                // Unit energy at 48 kHz: at other rates the response has more (or fewer) taps for the same sound, so the
                // energy scales with the rate - keep the level independent of it
                const float norm = (float) (1.0 / std::sqrt (std::max (1.0e-12, energy) * fs / 48000.0));
                const int fade = length / 5;
                for (int i = 0; i < length; ++i)
                {
                    float g = norm;
                    if (i > length - fade) g *= 0.5f + 0.5f * (float) std::cos (kPiD * (i - (length - fade)) / (double) fade);
                    ir[(size_t) i] *= g;
                }
                kernels[(size_t) c].build (ir.data(), length, block, fft);
            }
        }
    };

    class Soundboard : private juce::Thread
    {
    public:
        Soundboard() : juce::Thread ("Hamar soundboard") {}
        ~Soundboard() override { stopThread (2000); }

        void prepare (double sampleRate)
        {
            stopThread (2000);
            fs = (float) sampleRate;
            block = Convolver::blockFor (sampleRate);
            const int maxParts = (int) std::ceil (1.0 * sampleRate / block) + 1;
            for (auto& c : convolvers) c.prepare (block, maxParts);
            for (auto& d : micDelay) d.prepare ((int) (0.02f * fs) + 8);
            for (auto& d : combs) d.prepare ((int) (0.012f * fs) + 8);
            int order = 0;
            while ((1 << order) < 2 * block) ++order;
            builderFft = std::make_unique<juce::dsp::FFT> (order);
            state.store (Idle);
            hasResponse = false;
            configured = false;
            reset();
            startThread (juce::Thread::Priority::low);
        }

        void reset()
        {
            for (auto& c : convolvers) c.reset();
            for (auto& d : micDelay) d.clear();
            for (auto& d : combs) d.clear();
            for (auto& f : filters) f.reset();
        }

        // Called once per block. The first call builds the response right away (prepare time); later changes are built
        // in the background and swapped in when ready.
        void configure (const BoardSettings& s)
        {
            if (state.load() == Ready)   // a finished response is waiting: swap it in
            {
                std::swap (front, back);
                for (int c = 0; c < 4; ++c) convolvers[(size_t) c].setKernel (&responses[(size_t) front].kernels[(size_t) c]);
                state.store (Idle);
            }

            if (! hasResponse)
            {
                responses[(size_t) front].build (s, fs, block, *builderFft);
                for (int c = 0; c < 4; ++c) convolvers[(size_t) c].setKernel (&responses[(size_t) front].kernels[(size_t) c]);
                responseSettings = s;
                hasResponse = true;
            }
            else if (! sameResponse (s, responseSettings) && state.load() == Idle)
            {
                pending = s;
                responseSettings = s;
                state.store (Requested);
                notify();
            }

            if (configured && s == settings) return;
            settings = s;
            configured = true;
            configureChain (s);
        }

        // Adds the result to l / r. bus: the bridge signal for the four mic channels (A left/right, B left/right)
        void process (const float* const* bus, float* l, float* r, int n)
        {
            for (int i = 0; i < n; ++i)
            {
                float outL = 0.0f, outR = 0.0f;
                for (int p = 0; p < 2; ++p)
                {
                    auto& c = chain[(size_t) p];
                    const float inL = bus[p * 2][i], inR = bus[p * 2 + 1][i];
                    float sl = c.body * convolvers[(size_t) p * 2].process (inL) + c.dry * inL;
                    float sr = c.body * convolvers[(size_t) p * 2 + 1].process (inR) + c.dry * inR;
                    auto& lp = filters[(size_t) p * 2];
                    auto& hp = filters[(size_t) p * 2 + 1];
                    sl = hp.process (lp.process (sl, 0), 0);
                    sr = hp.process (lp.process (sr, 1), 1);
                    // Lid reflection (a short comb)
                    auto& comb = combs[(size_t) p * 2];
                    const float cl = comb.read (c.combDelay);
                    comb.push (sl + sr);
                    sl += c.comb * cl;
                    sr += c.comb * cl;
                    // Distance
                    auto& dl = micDelay[(size_t) p * 2];
                    auto& dr = micDelay[(size_t) p * 2 + 1];
                    dl.push (sl);
                    dr.push (sr);
                    const float ml = c.gain * dl.read (c.delay), mr = c.gain * dr.read (c.delay);
                    pairPeak[(size_t) p] = std::max (pairPeak[(size_t) p], std::max (std::abs (ml), std::abs (mr)));
                    outL += ml;
                    outR += mr;
                }
                // An upright stands at a wall: its back radiates into it
                if (wallGain > 0.0f)
                {
                    auto& wl = combs[1];
                    auto& wr = combs[3];
                    const float rl = wl.read (wallDelay), rr = wr.read (wallDelay);
                    wl.push (outL);
                    wr.push (outR);
                    outL += wallGain * rr;
                    outR += wallGain * rl;
                }
                l[i] += outL;
                r[i] += outR;
            }
        }

    private:
        enum State { Idle, Requested, Building, Ready };

        struct MicChain
        {
            float body = 1.0f, dry = 0.0f, gain = 1.0f, comb = 0.0f;
            int delay = 1, combDelay = 1;
        };

        // Settings that change the response itself (everything else is the mic chain)
        static bool sameResponse (const BoardSettings& a, const BoardSettings& b)
        {
            return a.model == b.model && a.quality == b.quality && a.size == b.size && a.damping == b.damping
                && a.brightness == b.brightness && a.rim == b.rim && a.box == b.box
                && a.micA.x == b.micA.x && a.micA.y == b.micA.y && a.micA.width == b.micA.width
                && a.micB.x == b.micB.x && a.micB.y == b.micB.y && a.micB.width == b.micB.width;
        }

        void run() override
        {
            while (! threadShouldExit())
            {
                if (state.load() == Requested)
                {
                    state.store (Building);
                    const auto s = pending;
                    responses[(size_t) back].build (s, fs, block, *builderFft);
                    state.store (Ready);
                }
                wait (20);
            }
        }

        void configureChain (const BoardSettings& s)
        {
            const auto& m = model (s.model);
            const bool grand = m.cabinet == Cabinet::Grand;
            float lidCut = 14000.0f, lidGain = 1.0f, lidComb = 0.0f, lidDelay = 0.0013f;
            if (grand)
            {
                switch (s.lid)
                {
                    case 1:  lidCut = 8000.0f; lidComb = 0.2f; lidDelay = 0.0018f; break;   // half stick
                    case 2:  lidCut = 2800.0f; lidGain = 0.6f; break;                       // closed
                    case 3:  lidCut = 16000.0f; lidGain = 1.1f; break;                      // removed
                    default: lidComb = 0.28f; break;                                         // open
                }
            }
            else
            {
                switch (s.panel)
                {
                    case 1:  lidCut = 8000.0f; break;                       // open top
                    case 2:  lidCut = 14000.0f; lidGain = 1.25f; break;     // front panel off
                    default: lidCut = 5000.0f; lidGain = 0.85f; break;      // closed
                }
            }
            const float bright = std::exp2 (0.6f * s.brightness);
            const float level = m.boardLevel * (0.4f + 1.2f * s.resonance);
            for (int p = 0; p < 2; ++p)
            {
                const auto& mic = p == 0 ? s.micA : s.micB;
                const float dist = mic.y + (p == 1 ? 0.5f : 0.0f);
                auto& c = chain[(size_t) p];
                const float near = 1.0f / (0.45f + 1.4f * dist);
                c.body = 0.7f * level * lidGain * near;
                c.dry = 0.0f;   // the strings themselves hardly radiate: everything comes from the board
                c.gain = mic.gain;
                c.delay = std::clamp ((int) ((0.0005f + 0.009f * dist) * fs), 1, micDelay[0].capacity());
                const float cut = std::min (lidCut * bright * std::exp2 (-1.4f * dist), 0.45f * fs);
                filters[(size_t) p * 2].lowpass (fs, cut, 0.6);
                filters[(size_t) p * 2 + 1].highpass (fs, grand ? 35.0 : 50.0, 0.7);
                c.comb = p == 0 ? lidComb : lidComb * 0.5f;
                c.combDelay = std::clamp ((int) (lidDelay * fs), 1, combs[0].capacity());
            }
            wallGain = grand ? 0.0f : 0.35f * s.wall;
            wallDelay = std::clamp ((int) (0.004f * fs), 1, combs[0].capacity());
        }

        float fs = 48000.0f;
        int block = 128;
        BoardSettings settings, responseSettings, pending;
        bool configured = false, hasResponse = false;
        std::array<BoardResponse, 2> responses;
        int front = 0, back = 1;
        std::atomic<int> state { Idle };
        std::unique_ptr<juce::dsp::FFT> builderFft;
        std::array<Convolver, 4> convolvers;
        std::array<MicChain, 2> chain {};
        std::array<fjord::base::dsp::Biquad, 4> filters {};
        std::array<Ring, 4> micDelay, combs;
        float wallGain = 0.0f;
        int wallDelay = 1;
        std::array<float, 2> pairPeak {};

    public:
        // Peak of each mic pair since the last call (for the meters)
        std::array<float, 2> takePairPeaks() { const auto p = pairPeak; pairPeak = {}; return p; }
    };
}
