#pragma once
#include <array>
#include <cmath>
#include <algorithm>
#include <complex>
#include <fjord_base/core/Dsp.h>
#include "KeyPhysics.h"

// One key of the piano: a felt hammer and one to three strings that meet at the bridge.
//
// Each string is a digital waveguide cut at the strike point: one part runs to the agraffe and back (rigid end,
// inverted), the other to the bridge and back. The loop also holds the string losses (one-pole), the stiffness
// (cascade of first-order allpasses: higher partials run faster and come out sharp, as in a real string) and a
// first-order Thiran allpass for the exact pitch.
//
// The hammer is a mass on a nonlinear felt spring, F = K d^p, with hysteresis. It is simulated against the string
// with sub-steps: a harder blow compresses the felt further, where it is stiffer, so the contact gets shorter and the
// tone brighter - the most important thing a piano does.
//
// The strings of a key are coupled through the bridge, a resistive impedance: strings moving together push the bridge
// and lose energy fast (prompt sound), strings moving against each other leave it at rest and ring on (aftersound).
// With the slight detuning of the unison this gives the double decay and the beating of a real piano. The bridge
// force is what the soundboard hears.
namespace hamar::dsp
{
    struct VoiceBuses
    {
        float* zone[3];     // bass / middle / treble drive of the soundboard
        float* direct[4];   // direct radiation into mic A left/right and mic B left/right
        float* bridge;      // everything the strings put into the bridge (sympathetic strings listen to it)
    };

    class KeyVoice
    {
    public:
        static constexpr float kOutputScale = 0.005f;

        void prepare (int keyNumber, double sampleRate)
        {
            key = keyNumber;
            fs = sampleRate;
            // Room for the lowest this key can be tuned (reference 400 Hz, stretched bass)
            const double fMin = 440.0 * std::exp2 ((key - 69) / 12.0) * (400.0 / 440.0) * std::exp2 (-80.0 / 1200.0);
            const int period = (int) std::ceil (fs / fMin) + 8;
            for (auto& s : strings)
            {
                s.a.prepare ((int) (0.4 * period) + 8);
                s.b1.prepare (period / 2 + 8);
                s.b2.prepare (period / 2 + 8);
            }
            duplexRing.prepare (period / 2 + 8);
            aliquotRing.prepare (period / 2 + 8);
            active = false;
            random.seed ((uint32_t) key * 7919u + 17u);
        }

        bool isActive() const { return active; }
        int getKey() const { return key; }
        float getLevel() const { return level; }
        bool isReleased() const { return dampTarget > 0.5f || ! held; }

        // Hammer blow with 'speed' in m/s. A sounding string is struck again as it is (a real piano does that).
        void strike (const KeyPhysics& k, float speed, float unaCorda)
        {
            if (! active)
            {
                setup (k);
                for (auto& s : strings)
                {
                    s.a.clear(); s.b1.clear(); s.b2.clear();
                    s.loss = 0.0f; s.ys = 0.0f; s.thiran.reset();
                    for (auto& d : s.disp) d.reset();
                }
                duplexRing.clear(); aliquotRing.clear();
                duplexLp = aliquotLp = 0.0f;
                longHp = longPrev = 0.0f;
                bowEnv = 0.0f;
                longi = {};
                damp = 0.0f;
                peak = 0.0f;
                level = 0.0f;
                kill = false;
                fade = 1.0f;
            }
            active = true;
            held = true;
            dampTarget = 0.0f;
            kill = false;
            fade = 1.0f;

            // Una corda: the action shifts, the hammer misses one string and hits the strings with a softer part of the felt
            const float u = std::clamp (unaCorda, 0.0f, 1.0f);
            // A real hammer never meets the strings of a unison quite evenly: that starts the aftersound right away
            hammer.w = { 1.0f + 0.25f * keyRandom (key, 11), 1.0f + 0.25f * keyRandom (key, 12), 1.0f + 0.25f * keyRandom (key, 13) };
            if (numStrings == 3) hammer.w[2] *= 1.0f - u;
            else if (numStrings == 2) hammer.w[1] *= 1.0f - 0.7f * u;
            hammer.wSum = 0.0f;
            for (int i = 0; i < numStrings; ++i) hammer.wSum += hammer.w[(size_t) i];
            hammer.mass = k.hammerMass;
            hammer.k = k.hammerK * (1.0f - 0.45f * u);
            hammer.p = k.hammerP;
            hammer.alpha = k.hysteresis;
            hammer.v = speed * (1.0f - 0.2f * u);   // the shifted action meets the strings with the unused, softer felt
            hammer.pluck = k.plucked;
            rattle = k.rattle;
            bowing = false;
            if (k.bowed)
            {
                // No hammer: the bow takes the string, faster with a harder key stroke
                bowing = true;
                bowVelocity = std::clamp (speed / 3.0f, 0.3f, 1.5f);
                bowAttack = 1.0f / (0.15f * (float) fs);
                bowRelease = 1.0f / (0.25f * (float) fs);
            }
            if (k.plucked)
            {
                // A plectrum: a stiff little spring driven by the key. It bends the string until it slips off -
                // the touch changes little (a bit faster and a bit more force with a harder key stroke)
                const float t = std::clamp (speed / 5.5f, 0.0f, 1.0f);
                hammer.w = { 1.0f, 1.0f, 1.0f };
                hammer.wSum = (float) numStrings;
                hammer.k = 3000.0f;
                hammer.p = 1.0f;
                hammer.alpha = 0.0f;
                hammer.v = 0.15f + 0.35f * t;
                hammer.release = 2.5f + 2.5f * t;
            }
            hammer.y = stringDisplacement();
            hammer.dpPrev = 0.0f;
            hammer.time = 0.0f;
            hammer.lastContact = 0.0f;
            hammer.contacted = false;
            hammer.active = ! k.bowed;
        }

        // Key released / pedal: 0 = string free, 1 = damper fully on. Seconds = how fast the damper settles.
        void setDamp (float target, float seconds)
        {
            if (target > dampTarget + 0.3f && level > 1.0e-4f)
                damperNoise = std::min (1.0f, level * 6.0f);   // the felt touching a vibrating string buzzes for a moment
            dampTarget = std::clamp (target, 0.0f, 1.0f);
            dampCoef = std::exp (-1.0f / std::max (1.0f, seconds * (float) fs));
        }

        void release() { held = false; bowing = false; pressure = 0.0f; }

        // Key pressure (aftertouch) 0..1: the clavichord's Bebung raises the pitch, the bow presses harder
        void setPressure (float p) { pressure = std::clamp (p, 0.0f, 1.0f); }

        // The bow: pressure 0..1, speed 0..1 (set while the key is down)
        void setBow (float pressure, float speed) { bowPressure = pressure; bowSpeed = speed; }
        bool isHeld() const { return held; }

        void stop() { kill = true; }   // polyphony limit: fade out within a few milliseconds
        void clear() { active = false; hammer.active = false; }

        void setDamperNoiseLevel (float v) { damperNoiseAmount = v; }

        void render (int n, const VoiceBuses& bus, const std::array<float, 4>& micGains)
        {
            if (! active) return;
            const float dt = 1.0f / (float) fs;
            const int ns = numStrings;
            constexpr int subSteps = 4;
            const float dts = dt / (float) subSteps;
            const float fadeStep = 1.0f / (0.004f * (float) fs);
            float blockPeak = 0.0f;

            // Bebung: pressing the key harder stretches the string - up to about 14 cents (a shorter loop)
            if (pressureMode == Pressure::Bebung || bendSamples < 0.0f)
            {
                const float target = pressureMode == Pressure::Bebung ? -loopLength * (1.0f - std::exp2 (-14.0f * pressure / 1200.0f)) : 0.0f;
                bendSamples += (target - bendSamples) * std::min (1.0f, (float) n / (0.01f * (float) fs));
                bendSamples = std::max (bendSamples, -0.45f * (float) std::max (1, strings[0].lb2 - 3));
                if (bendSamples > -1.0e-4f && target == 0.0f) bendSamples = 0.0f;
            }
            const float bowPressureNow = pressureMode == Pressure::Bow ? bowPressure + (1.0f - bowPressure) * pressure : bowPressure;
            const float bowSpeedNow = pressureMode == Pressure::Bow ? bowSpeed + (1.0f - bowSpeed) * 0.6f * pressure : bowSpeed;

            for (int i = 0; i < n; ++i)
            {
                // Damper (smoothed) sets the loss filter between free and damped
                damp = dampTarget + (damp - dampTarget) * dampCoef;
                const float g = lossU.g + (lossD.g - lossU.g) * damp;
                const float a = lossU.a + (lossD.a - lossU.a) * damp;

                // Waves arriving at the strike point and at the bridge
                std::array<float, kMaxStrings> vin {}, inA {}, inB {}, arrive {}, force {};
                for (int s = 0; s < ns; ++s)
                {
                    auto& st = strings[(size_t) s];
                    inA[(size_t) s] = -st.a.read (st.la);
                    inB[(size_t) s] = bendSamples < -1.0e-4f ? st.b2.readFrac ((float) st.lb2 + bendSamples) : st.b2.read (st.lb2);
                    arrive[(size_t) s] = st.b1.read (st.lb1);
                    vin[(size_t) s] = inA[(size_t) s] + inB[(size_t) s];
                }

                if (hammer.active)
                    runHammer (vin, force, ns, dts, subSteps, dt);
                else
                    for (int s = 0; s < ns; ++s)
                    {
                        auto& st = strings[(size_t) s];
                        st.ys = (st.ys + vin[(size_t) s] * dt) * 0.99995f;
                    }

                // Bow (bowed piano): friction against the first string - sticks while the relative speed is small,
                // slips when it grows (the classic bow table of the bowed-string waveguide)
                float bowAdd = 0.0f;
                if (bowEnv > 1.0e-5f || bowing)
                {
                    bowEnv += ((bowing ? 1.0f : 0.0f) - bowEnv) * (bowing ? bowAttack : bowRelease);
                    const float vb = bowEnv * (0.04f + 0.3f * bowSpeedNow) * bowVelocity;
                    const float dv = vb - vin[0];
                    const float slope = 5.0f - 4.0f * bowPressureNow;
                    const float refl = std::clamp (std::pow (std::abs (dv * slope) + 0.75f, -4.0f), 0.01f, 0.98f);
                    bowAdd = dv * refl * 0.5f;
                }

                // Damper buzz: noise into the strings while the felt settles
                float buzz = 0.0f;
                if (damperNoise > 1.0e-5f)
                {
                    buzz = random.nextBipolar() * damperNoise * damperNoiseAmount * 0.02f;
                    damperNoise *= damperNoiseDecay;
                }

                // Strike point: forces go into both directions
                for (int s = 0; s < ns; ++s)
                {
                    auto& st = strings[(size_t) s];
                    const float f2z = force[(size_t) s] * halfAdmittance;
                    const float bow = s == 0 ? bowAdd : 0.0f;
                    st.a.push (inB[(size_t) s] + f2z + bow);
                    st.b1.push (inA[(size_t) s] + f2z + buzz + bow);
                }

                // Bridge: coupled reflection, then losses, stiffness and fine tuning on the way back
                float sum = 0.0f;
                for (int s = 0; s < ns; ++s) sum += arrive[(size_t) s];
                // The board moves easily at low frequencies and hardly at high ones: the bridge admittance is a low
                // pass (positive real, so still passive). Low partials hand their energy to the board quickly, high ones
                // ring on - the tone gets relatively brighter as it decays, as in a real piano.
                bridgeLp += bridgePole * (sum - bridgeLp);
                const float vb = bridgeCoupling * bridgeLp;
                for (int s = 0; s < ns; ++s)
                {
                    auto& st = strings[(size_t) s];
                    float x = vb - arrive[(size_t) s];
                    st.loss = g * (1.0f + a) * x - a * st.loss;
                    x = st.loss;
                    for (int d = 0; d < dispM; ++d) x = st.disp[(size_t) d].process (x);
                    x = st.thiran.process (x);
                    st.b2.push (x);
                }

                // The strings of a unison cross the bridge a little apart, and the board answers each place a little
                // differently (rocking of the bridge, the second polarisation): motion against each other, which leaves
                // the bridge's centre at rest and rings on for a long time, still radiates. That is the aftersound.
                float spread = 0.0f;
                for (int s = 0; s < ns; ++s) spread += radiation[(size_t) s] * arrive[(size_t) s];
                float out = bridgeForce * (sum + spread);
                if (rattle > 0.0f)
                {
                    // A loose screw: whatever the string swings beyond its clearance comes back as a noisy buzz
                    const float excess = sum - std::clamp (sum, -0.04f, 0.04f);
                    out += rattle * bridgeForce * excess * (0.4f + 1.2f * random.next01()) * 3.0f;
                }

                // Longitudinal force: the string is stretched by its transverse displacement (squared), which reaches the
                // bridge long before the transverse wave - the shock at the start of a piano tone - and keeps adding sum
                // and difference tones (phantom partials). The longitudinal modes ring with it for a moment.
                if (phantomGain > 0.0f)
                {
                    float e = 0.0f;
                    for (int s = 0; s < ns; ++s) e += strings[(size_t) s].ys * strings[(size_t) s].ys;
                    const float f = stretchForce * e;
                    longHp = 0.9985f * (longHp + f - longPrev);
                    longPrev = f;
                    const float modes = longi[0].process (longHp) + 0.6f * longi[1].process (longHp) + 0.35f * longi[2].process (longHp);
                    out += phantomGain * (0.5f * longHp + modes);
                }

                // Duplex scale and aliquot strings: short resonating string parts tuned to the note's partials
                if (duplexGain > 0.0f)
                {
                    const float y = duplexRing.read (duplexLen);
                    duplexLp += 0.5f * (y - duplexLp);
                    duplexRing.push (out * 0.002f + duplexLp * duplexFeedback);
                    out += duplexGain * y * 40.0f;
                }
                if (aliquotGain > 0.0f)
                {
                    const float y = aliquotRing.read (aliquotLen);
                    aliquotLp += 0.6f * (y - aliquotLp);
                    aliquotRing.push (out * 0.002f + aliquotLp * aliquotFeedback);
                    out += aliquotGain * y * 40.0f;
                }

                if (kill) fade = std::max (0.0f, fade - fadeStep);
                out *= outputGain * fade;
                blockPeak = std::max (blockPeak, std::abs (out));

                bus.direct[0][i] += out * micGains[0];
                bus.direct[1][i] += out * micGains[1];
                bus.direct[2][i] += out * micGains[2];
                bus.direct[3][i] += out * micGains[3];
                bus.bridge[i] += out;
            }

            level = std::max (blockPeak, level * 0.9f);
            peak = std::max (peak, blockPeak);
            // Free the key when it is inaudible (or faded out by the polyphony limit)
            if ((kill && fade <= 0.0f) || (! hammer.active && blockPeak < 2.0e-6f && level < 2.0e-6f && peak > 0.0f))
                active = false;
        }

    private:
        struct String
        {
            Ring a, b1, b2;
            int la = 1, lb1 = 1, lb2 = 1;
            Allpass1 thiran;
            std::array<Allpass1, kMaxDispersion> disp;
            float loss = 0.0f, ys = 0.0f;
        };

        struct Hammer
        {
            bool active = false, contacted = false, pluck = false;
            float release = 1.0f;
            float y = 0.0f, v = 0.0f, mass = 0.003f, k = 1.0e9f, p = 2.5f, alpha = 0.0f, dpPrev = 0.0f;
            float time = 0.0f, lastContact = 0.0f, wSum = 3.0f;
            std::array<float, kMaxStrings> w {};
        };

        // Second-order resonator (the longitudinal mode)
        struct Resonator
        {
            float b0 = 0.0f, a1 = 0.0f, a2 = 0.0f, y1 = 0.0f, y2 = 0.0f;

            void set (float freq, float q, float sampleRate)
            {
                const float w = 2.0f * fjord::base::dsp::kPi * std::min (freq, 0.45f * sampleRate) / sampleRate;
                const float r = std::exp (-w / (2.0f * q));
                a1 = -2.0f * r * std::cos (w);
                a2 = r * r;
                b0 = (1.0f - r) * 2.0f;
            }

            float process (float x)
            {
                const float y = b0 * x - a1 * y1 - a2 * y2;
                y2 = y1;
                y1 = y;
                return y;
            }
        };

        float stringDisplacement() const
        {
            float sum = 0.0f;
            for (int s = 0; s < numStrings; ++s) sum += hammer.w[(size_t) s] * strings[(size_t) s].ys;
            return hammer.wSum > 0.0f ? sum / hammer.wSum : 0.0f;
        }

        void runHammer (const std::array<float, kMaxStrings>& vin, std::array<float, kMaxStrings>& force, int ns,
                        float dts, int subSteps, float dt)
        {
            auto& h = hammer;
            bool contact = false;
            for (int step = 0; step < subSteps; ++step)
            {
                const float d = h.y - stringDisplacement();
                float f = 0.0f;
                if (h.pluck)
                {
                    // Plectrum: linear spring, pushed at constant speed, lets go at its release force
                    f = d > 0.0f ? h.k * d : 0.0f;
                    if (f > h.release) { h.active = false; f = 0.0f; }
                    else if (f > 0.0f) contact = true;
                }
                else if (d > 0.0f)
                {
                    const float dp = std::pow (d, h.p);
                    f = std::max (0.0f, h.k * (dp + h.alpha * (dp - h.dpPrev) / dts));
                    h.dpPrev = dp;
                    contact = true;
                }
                else
                    h.dpPrev = 0.0f;

                for (int s = 0; s < ns; ++s)
                {
                    const float fs = h.wSum > 0.0f ? f * h.w[(size_t) s] / h.wSum : 0.0f;
                    auto& st = strings[(size_t) s];
                    st.ys += (vin[(size_t) s] + fs * halfAdmittance) * dts;
                    force[(size_t) s] += fs / (float) subSteps;
                }
                if (! h.pluck) h.v -= f / h.mass * dts;
                h.y += h.v * dts;
                if (! h.active) break;
            }
            for (int s = 0; s < ns; ++s) strings[(size_t) s].ys *= 0.99995f;

            h.time += dt;
            if (contact) { h.contacted = true; h.lastContact = h.time; }
            // The hammer has left the strings for good (it falls back into the check)
            if ((! h.pluck && h.contacted && ! contact && h.v < 0.0f && h.time - h.lastContact > 0.003f) || h.time > 0.08f)
                h.active = false;
        }

        // Copies what the strings need from the key table: loop lengths, filters, coupling
        void setup (const KeyPhysics& k)
        {
            numStrings = std::clamp (k.strings, 1, kMaxStrings);
            pressureMode = k.pressure;
            loopLength = (float) (fs / k.f1);
            dispM = k.dispM;
            halfAdmittance = 1.0f / (2.0f * k.z);
            const double fsd = fs;

            lossU = design::loss (k.f1, fsd, k.after, k.hfFreq, k.hfAfter);
            lossD = design::loss (k.f1, fsd, k.damped, k.hfFreq, k.hfDamped);

            // Bridge: extra loss per period of the in-phase motion turns the aftersound into the prompt sound
            const double extra = std::max (0.0, 6.9078 / (k.f1 * k.prompt) - 6.9078 / (k.f1 * k.after));
            const double rho = std::exp (-extra);
            const double kappa = (1.0 - rho) / (1.0 + rho);
            const double bridgeCut = 500.0;
            bridgePole = (float) (1.0 - std::exp (-2.0 * kPiD * bridgeCut / fsd));
            const double w0 = 2.0 * kPiD * k.f1 / fsd;
            const double lpGain = bridgePole / std::sqrt (1.0 + (1.0 - bridgePole) * (1.0 - bridgePole) - 2.0 * (1.0 - bridgePole) * std::cos (w0));
            bridgeCoupling = (float) std::min (0.5 / numStrings, 2.0 / numStrings * kappa / (1.0 + kappa) / lpGain);
            bridgeLp = 0.0f;
            // The coupled reflection shifts the phase of the in-phase motion a little. The heard pitch is a mix of the
            // shifted in-phase motion and the unshifted motion against each other; how much of the shift is tuned out
            // was measured per model (Models.h)
            {
                const std::complex<double> z1 = std::polar (1.0, -w0);
                const std::complex<double> h = (double) bridgePole / (1.0 - (1.0 - (double) bridgePole) * z1);
                bridgeDelay = -std::arg (1.0 - numStrings * (double) bridgeCoupling * h) / w0 * k.pitchCoupling;
            }
            bridgeForce = (float) (2.0 * k.z / (1.0 + kappa));

            for (int s = 0; s < numStrings; ++s)
            {
                auto& st = strings[(size_t) s];
                const double f = k.f1 * std::exp2 (k.detune[(size_t) s] / 1200.0);
                const double w = 2.0 * kPiD * f / fsd;
                const double period = fsd / f;
                for (int d = 0; d < kMaxDispersion; ++d) st.disp[(size_t) d].a = k.dispA;
                double rest = period - dispM * design::allpassDelay (k.dispA, w) - design::lossDelay (lossU.a, w) - bridgeDelay;
                rest = std::max (rest, 3.6);
                int total = (int) std::floor (rest - 0.5);
                double frac = rest - total;
                // The Thiran allpass delays a bit differently at f than at DC: correct its coefficient once or twice
                double designed = frac, ta = (1.0 - frac) / (1.0 + frac);
                for (int it = 0; it < 3; ++it)
                {
                    designed = std::clamp (designed + frac - design::allpassDelay (ta, w), 0.05, 2.0);
                    ta = (1.0 - designed) / (1.0 + designed);
                }
                st.thiran.a = (float) ta;
                total = std::max (total, 3);
                // The hammer is wide and never quite square to the strings: each string of the unison is struck at a
                // slightly different point, which softens the strike-point notches (a single point is a plucked guitar)
                const double strike = k.strike * k.strikeMul[(size_t) s] * (1.0 + 0.03 * keyRandom (key, 20 + s));
                st.la = std::clamp ((int) std::lround (strike * (total + frac)), 1, std::min (total - 2, st.a.capacity()));
                st.lb1 = std::max (1, (total - st.la) / 2);
                st.lb2 = std::max (1, total - st.la - st.lb1);
                st.lb1 = std::min (st.lb1, st.b1.capacity());
                st.lb2 = std::min (st.lb2, st.b2.capacity());
            }

            // Radiation of the single strings around the common part (zero mean, so the sum stays the sum)
            radiation = {};
            if (numStrings == 2) radiation = { 0.8f, -0.8f, 0.0f };
            if (numStrings == 3) radiation = { 1.0f + 0.4f * keyRandom (key, 30), -0.4f + 0.6f * keyRandom (key, 31), 0.0f };
            if (numStrings == 3) radiation[2] = -(radiation[0] + radiation[1]);

            zone = k.zone;
            outputGain = kOutputScale * k.gain;
            phantomGain = 2.5f * k.phantom;   // the downbearing angle at the bridge passes the longitudinal force on to the board
            stretchForce = k.stretchForce;
            for (int m = 0; m < 3; ++m)
            {
                const float f = k.longitudinal * (float) (m + 1) * (1.0f + 0.004f * (float) m);
                longi[(size_t) m].set (f, fjord::base::dsp::kPi * f * 0.25f / 6.9078f, (float) fs);   // T60 about 0.25 s
            }

            duplexGain = k.duplex;
            duplexLen = std::clamp ((int) std::lround (fsd / (k.f1 * k.duplexRatio)), 2, duplexRing.capacity());
            duplexFeedback = (float) std::exp (-6.9078 / (k.f1 * k.duplexRatio * 0.6));
            aliquotGain = k.aliquot;
            aliquotLen = std::clamp ((int) std::lround (fsd / (k.f1 * 2.0)), 2, aliquotRing.capacity());
            aliquotFeedback = (float) std::exp (-6.9078 / (k.f1 * 2.0 * 2.0));
            damperNoiseDecay = std::exp (-1.0f / (0.04f * (float) fs));
        }

        int key = 60;
        double fs = 48000.0;
        bool active = false, held = false, kill = false;
        int numStrings = 3, dispM = 0;
        std::array<String, kMaxStrings> strings;
        Hammer hammer;
        design::Loss lossU, lossD;
        float damp = 0.0f, dampTarget = 0.0f, dampCoef = 0.99f;
        float bridgeLp = 0.0f, bridgePole = 0.1f;
        float rattle = 0.0f, pressure = 0.0f, bendSamples = 0.0f, loopLength = 100.0f;
        Pressure pressureMode = Pressure::None;
        bool bowing = false;
        float bowEnv = 0.0f, bowPressure = 0.5f, bowSpeed = 0.5f, bowVelocity = 1.0f, bowAttack = 0.001f, bowRelease = 0.001f;
        double bridgeDelay = 0.0;
        std::array<float, kMaxStrings> radiation {};
        float halfAdmittance = 0.25f, bridgeCoupling = 0.0f, bridgeForce = 1.0f, outputGain = 0.02f, fade = 1.0f;
        std::array<float, 3> zone {};
        float phantomGain = 0.0f, stretchForce = 0.0f, longHp = 0.0f, longPrev = 0.0f;
        std::array<Resonator, 3> longi {};
        Ring duplexRing, aliquotRing;
        int duplexLen = 2, aliquotLen = 2;
        float duplexGain = 0.0f, duplexFeedback = 0.0f, duplexLp = 0.0f, aliquotGain = 0.0f, aliquotFeedback = 0.0f, aliquotLp = 0.0f;
        float damperNoise = 0.0f, damperNoiseDecay = 0.999f, damperNoiseAmount = 0.3f;
        float level = 0.0f, peak = 0.0f;
        fjord::base::dsp::Random random;
    };
}
