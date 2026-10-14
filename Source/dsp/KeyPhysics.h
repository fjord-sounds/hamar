#pragma once
#include <array>
#include "Models.h"
#include "Tuning.h"
#include "Waveguide.h"

// Everything a key needs to sound: its strings, hammer, decay times, dampers and where it sits on the soundboard.
// The table is computed from the model and the plugin parameters (Instrument::update); a note copies what it needs
// when it is struck, so a sounding string is never changed under it.
namespace hamar::dsp
{
    constexpr int kMaxStrings = 3, kMaxDispersion = 12;

    struct KeyPhysics
    {
        float f1 = 440.0f;          // first partial (as tuned)
        float b = 0.0f;             // inharmonicity
        float z = 2.0f;             // wave impedance of one string (kg/s)
        float hammerMass = 3.0e-3f, hammerK = 4.5e9f, hammerP = 2.5f, hysteresis = 0.0f;
        float strike = 0.12f;       // strike position (fraction of the length)
        std::array<float, kMaxStrings> strikeMul { 0.92f, 1.0f, 1.08f };   // per string: a wide hammer meets them a little apart
        bool plucked = false;       // a plectrum instead of a hammer
        bool bowed = false;         // a bow instead of a hammer
        float rattle = 0.0f;        // a loose object on the strings (prepared piano)
        float pitchCoupling = 1.6f;
        Pressure pressure = Pressure::None;
        int strings = 3;
        float after = 20.0f, prompt = 4.0f, damped = 0.3f;   // T60 in seconds
        float hfFreq = 2000.0f, hfAfter = 1.0f, hfDamped = 0.05f;
        std::array<float, kMaxStrings> detune {};          // cents per string
        float dispA = 0.0f;
        int dispM = 0;
        bool hasDamper = true;
        float position = 0.5f;      // 0 bass end .. 1 treble end of the keyboard
        std::array<float, 3> zone {}; // drive of the bass / middle / treble region of the soundboard
        float phantom = 0.0f, duplex = 0.0f, aliquot = 0.0f, longitudinal = 1000.0f;
        float stretchForce = 0.0f;   // longitudinal force per squared displacement at the strike point (N/m^2)
        float duplexRatio = 3.0f;
        float gain = 1.0f;
    };

    // Plain numbers the engine passes to the table (taken from the parameters)
    struct InstrumentSettings
    {
        int model = 0, quality = 1, unisons = 0;   // unisons: 0 = the model's, else 1..3
        float inharm = 1.0f, length = 0.0f, detune = 1.0f, sustain = 0.0f, coupling = 0.0f, brightness = 0.0f;
        float hardness = 0.0f, mass = 0.0f, position = 0.0f, hysteresis = 0.5f, voiceBass = 0.0f, voiceTreble = 0.0f, felt = 0.0f;
        float phantom = 0.5f, duplex = 0.5f, aliquot = 0.0f;
        float damperStrength = 0.6f;
        int damperTop = 0;
        int registerStop = 1;       // harpsichord: 8', 8'+8', 8'+4', 8'+8'+4', lute
        float stab = 0.0f;          // dance: shorter notes
        float prep = 0.6f;          // prepared piano: how many strings are prepared
        float bowPressure = 0.5f, bowSpeed = 0.5f;
        float outOfTune = 0.0f, wear = 0.0f, damperLeak = 0.0f;   // condition of the instrument
        float buff = 0.0f;
        TuningSettings tuning;
        double sampleRate = 48000.0;

        bool operator== (const InstrumentSettings&) const = default;
    };

    inline int dispersionSections (int quality) { return quality <= 0 ? 2 : quality == 1 ? 4 : quality == 2 ? 6 : 10; }

    // Small deterministic random numbers per key, so every key has its own (but reproducible) detuning
    inline float keyRandom (int key, int salt)
    {
        uint32_t h = (uint32_t) key * 2654435761u + (uint32_t) salt * 40503u + 0x9e3779b9u;
        h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
        return (float) (h & 0xffffff) / 16777215.0f * 2.0f - 1.0f;
    }

    class KeyTable
    {
    public:
        const KeyPhysics& operator[] (int key) const { return keys[(size_t) keyIndex (key)]; }

        // Recomputes everything except the dispersion filters (cheap); dispersion is refreshed by refreshDispersion.
        void update (const InstrumentSettings& s)
        {
            const auto& m = model (s.model);
            const double fs = s.sampleRate;
            settings = s;

            auto lengthOf = [&] (float key)
            {
                const float w = std::clamp ((60.0f - key) / 39.0f, 0.0f, 1.0f);
                const float bass = m.bassLength * std::exp2 (0.6f * s.length) / 1.95f;
                return scale::length (key) * std::pow (bass, w) * (key > 60 ? std::exp2 (0.25f * s.length) : 1.0f);
            };
            auto inharmOf = [&] (float key)
            {
                const float ratio = scale::length (key) / lengthOf (key);
                return scale::inharmonicity (key) * ratio * ratio * m.inharm * s.inharm;
            };

            const auto tuning = computeTuning (s.tuning, inharmOf);
            const float felt = std::clamp (m.felt + s.felt, 0.0f, 1.0f);
            const float range = (float) (m.highKey - m.lowKey);

            for (int key = kLowestKey; key <= kHighestKey; ++key)
            {
                auto& k = keys[(size_t) keyIndex (key)];
                const float fk = (float) key;
                // An old instrument: every key has drifted a little (fixed per key), the unisons further apart
                k.f1 = tuning[(size_t) keyIndex (key)] * std::exp2 (s.outOfTune * 14.0f * keyRandom (key, 60) / 1200.0f);
                k.b = inharmOf (fk);
                const float length = lengthOf (fk);
                k.z = scale::tension (fk) / (2.0f * length * k.f1);

                // Hammers: voicing per register, felt strip, metal tacks
                const float t = std::clamp ((fk - 21.0f) / 87.0f, 0.0f, 1.0f);
                const float voicing = s.voiceBass * (1.0f - t) + s.voiceTreble * t;
                k.hammerMass = scale::hammerMass (fk) * m.hammerMass * std::exp2 (0.8f * s.mass) * (1.0f + 0.15f * felt);
                // Hardness is the felt's stiffness. The exponent stays the register's: with compressions well below a
                // millimetre a larger exponent would make the felt softer, not harder.
                k.hammerK = scale::hammerK (fk) * m.hammerK * std::exp2 (2.5f * (s.hardness + 0.6f * voicing) + 4.0f * m.hammerP)
                            * (1.0f - 0.9f * felt) * (1.0f + 30.0f * m.tack);
                k.hammerP = scale::hammerP (fk);
                // Worn hammers: the felt is packed hard where it meets the strings, unevenly from key to key
                k.hammerK *= std::exp2 (s.wear * (1.6f + 1.0f * keyRandom (key, 61)) * std::clamp ((fk - 30.0f) / 30.0f, 0.3f, 1.0f));
                k.hysteresis = 4.0e-5f * (1.0f - 0.6f * s.wear) * (0.25f + s.hysteresis) * (1.0f + 1.5f * felt) * (1.0f - 0.7f * m.tack);
                k.strike = std::clamp (scale::strike (fk) * m.strike * std::exp2 (0.6f * s.position), 0.03f, 0.35f);

                // Strings per key
                k.strings = key <= m.monoTop ? 1 : key <= m.biTop ? 2 : 3;
                if (s.unisons > 0) k.strings = s.unisons;
                if (s.quality == 0) k.strings = std::min (k.strings, 2);
                const float spread = m.detune * s.detune * (1.0f + 5.0f * s.outOfTune);
                k.detune = { -0.5f * spread * (1.0f + 0.35f * keyRandom (key, 1)),
                              0.5f * spread * (1.0f + 0.35f * keyRandom (key, 2)),
                              0.15f * spread * keyRandom (key, 3) };
                if (k.strings == 2) k.detune[1] = 0.5f * spread * (1.0f + 0.35f * keyRandom (key, 2));
                k.strikeMul = { 0.92f, 1.0f, 1.08f };
                k.plucked = m.exciter == Exciter::Plectrum;
                if (k.plucked)
                {
                    // Harpsichord registers: choirs of strings, each plucked at its own point; the 4' choir an octave up
                    const float d = 0.5f * std::max (0.5f, spread);
                    switch (std::clamp (s.registerStop, 0, 4))
                    {
                        case 0:  k.strings = 1; k.strikeMul = { 1.0f, 1.0f, 1.0f }; k.detune = { 0.0f, 0.0f, 0.0f }; break;
                        case 2:  k.strings = 2; k.strikeMul = { 1.0f, 0.8f, 1.0f }; k.detune = { 0.0f, 1200.0f + d, 0.0f }; break;
                        case 3:  k.strings = 3; k.strikeMul = { 1.0f, 0.62f, 0.8f }; k.detune = { -d, d, 1200.0f }; break;
                        case 4:  k.strings = 1; k.strikeMul = { 0.35f, 1.0f, 1.0f }; k.detune = { 0.0f, 0.0f, 0.0f }; break;   // lute: plucked right at the end
                        default: k.strings = 2; k.strikeMul = { 1.0f, 0.62f, 1.0f }; k.detune = { -d, d, 0.0f }; break;
                    }
                    if (s.quality == 0) k.strings = std::min (k.strings, 2);
                }

                // Decay: aftersound, prompt sound, high frequencies, damped
                const float sustain = std::exp2 (1.5f * s.sustain);
                k.after = scale::afterDecay (fk) * m.after * sustain * (1.0f - 0.85f * s.buff)    // the buff stop's felt
                          * (1.0f - 0.8f * std::clamp (m.stab + s.stab, 0.0f, 1.0f));             // dance stabs
                k.prompt = k.after / (scale::promptRatio (fk) * m.prompt * std::exp2 (1.2f * s.coupling));
                if (k.strings == 1) k.prompt = k.after * 0.6f;
                const float bright = std::clamp (m.brightness + s.brightness - 1.2f * s.buff, -1.5f, 1.5f);
                k.hfFreq = (float) std::min (std::max (8.0 * k.f1, 1500.0), 0.35 * fs);
                const float b3 = 1.0e-7f * std::exp2 (-1.6f * bright);   // sigma = b1 + b3 f^2 (Bensa et al.)
                k.hfAfter = 6.9078f / (6.9078f / k.after + b3 * (k.hfFreq * k.hfFreq - k.f1 * k.f1));
                const float damperStrength = std::exp2 (2.0f * (s.damperStrength - 0.6f));
                k.damped = scale::dampedDecay (fk) / (damperStrength * m.dampers) * (1.0f + 4.0f * s.damperLeak * (0.6f + 0.4f * keyRandom (key, 62)));
                k.hfDamped = std::min (k.damped * 0.25f, k.hfAfter);
                k.hasDamper = key <= m.damperTop + s.damperTop || m.mechanism != Mechanism::Strings;

                // Prepared piano: some strings carry a bolt (gong-like, inharmonic, the unison spread apart), a piece of
                // rubber (a short thud) or a loose screw (it rattles). Which and where is fixed per key.
                k.bowed = m.exciter == Exciter::Bow;
                k.rattle = 0.0f;
                if (m.prepared > 0.0f && (keyRandom (key, 51) + 1.0f) * 0.5f < s.prep * m.prepared)
                {
                    switch ((int) ((keyRandom (key, 52) + 1.0f) * 1.4999f))
                    {
                        case 0:  k.b *= 25.0f; k.detune = { -30.0f, 25.0f, 45.0f }; k.after *= 0.5f; break;                      // bolt
                        case 1:  k.after *= 0.12f; k.hfAfter = std::min (k.hfAfter, k.after * 0.08f); break;                     // rubber
                        default: k.b *= 6.0f; k.rattle = 0.6f + 0.4f * keyRandom (key, 53); break;                               // screw
                    }
                    k.prompt = std::min (k.prompt, k.after);
                }

                // Position on the keyboard and on the soundboard (bass bridge, middle, treble)
                k.position = std::clamp ((fk - (float) m.lowKey) / range, 0.0f, 1.0f);
                const float p = k.position;
                k.zone = { std::max (0.0f, 1.0f - 2.0f * p), 1.0f - std::abs (2.0f * p - 1.0f), std::max (0.0f, 2.0f * p - 1.0f) };

                // Nonlinear and resonating extras
                // Longitudinal motion: the blow stretches the string, a = strike side, b = bridge side:
                // dL = y^2 / 2 (1/a + 1/b), force = E A dL / L. Quadratic in the displacement, so it grows with the
                // dynamics: the "clank" of a forte bass note and the phantom partials
                k.phantom = s.phantom;
                {
                    const float a = k.strike * length, b = (1.0f - k.strike) * length;
                    k.stretchForce = scale::axialStiffness (fk) / length * 0.5f * (1.0f / a + 1.0f / b);
                }
                k.longitudinal = std::min (17.0f * k.f1 * std::exp2 (0.3f * keyRandom (key, 7)), 4000.0f);
                k.duplex = m.duplex * s.duplex * 2.0f * std::clamp ((fk - 50.0f) / 20.0f, 0.0f, 1.0f);
                k.duplexRatio = 3.0f + 0.04f * keyRandom (key, 5);
                k.aliquot = s.aliquot * std::clamp ((fk - 50.0f) / 10.0f, 0.0f, 1.0f);
                k.gain = m.gain;
                k.pitchCoupling = m.pitchCoupling;
                k.pressure = m.pressure;
            }

            // The dispersion design only depends on pitch, inharmonicity and the number of sections: redesign the keys
            // where one of them really moved
            dispersionM = dispersionSections (s.quality);
            for (int i = 0; i < kNumKeys; ++i)
            {
                const auto& k = keys[(size_t) i];
                const auto& d = designedFor[(size_t) i];
                if (d.m != dispersionM || std::abs (k.f1 / d.f1 - 1.0f) > 0.002f || std::abs (k.b / std::max (1.0e-9f, d.b) - 1.0f) > 0.01f)
                    dispersionDirty[(size_t) i] = true;
                keys[(size_t) i].dispA = d.a;     // keep the last design until the new one is ready
                keys[(size_t) i].dispM = d.usedM;
            }
        }

        // Designs the dispersion filters of up to 'count' keys whose design is out of date. Returns true while work remains.
        bool refreshDispersion (int count)
        {
            bool remaining = false;
            for (int i = 0; i < kNumKeys; ++i)
            {
                if (! dispersionDirty[(size_t) i]) continue;
                if (count-- <= 0) { remaining = true; break; }
                designDispersion (i);
            }
            return remaining;
        }

        void refreshAllDispersion() { while (refreshDispersion (kNumKeys)) {} }

        const InstrumentSettings& getSettings() const { return settings; }

    private:
        void designDispersion (int i)
        {
            auto& k = keys[(size_t) i];
            dispersionDirty[(size_t) i] = false;
            const double fs = settings.sampleRate;
            const double period = fs / k.f1;
            int m = dispersionM;
            // Short treble loops get fewer sections: the loop must keep a few samples of plain delay
            while (m > 1 && period < 6.0 * m) --m;
            const auto loss = design::loss (k.f1, fs, k.after, k.hfFreq, k.hfAfter);
            const double maxDelay = std::max (0.5, period - 4.5 - design::lossDelay (loss.a, 2.0 * kPiD * k.f1 / fs));
            k.dispM = m;
            k.dispA = design::dispersion (k.f1, k.b, m, fs, maxDelay, loss.a);
            designedFor[(size_t) i] = { k.f1, k.b, dispersionM, m, k.dispA };
        }

        struct Design
        {
            float f1 = 1.0f, b = 0.0f;
            int m = -1, usedM = 0;
            float a = 0.0f;
        };

        std::array<KeyPhysics, kNumKeys> keys {};
        std::array<bool, kNumKeys> dispersionDirty {};
        std::array<Design, kNumKeys> designedFor {};
        InstrumentSettings settings;
        int dispersionM = 4;
    };
}
