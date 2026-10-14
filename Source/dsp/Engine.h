#pragma once
#include <array>
#include <atomic>
#include <vector>
#include "KeyVoice.h"
#include "Sympathetic.h"
#include "Soundboard.h"
#include "Noises.h"
#include "BarVoice.h"
#include "ElectricAmp.h"
#include "Dance.h"
#include "Layer.h"

// The piano: 97 keys (each its own strings and hammer, struck again where it is when it is played again, or its
// tine / reed for the bar instruments), the pedals, the sympathetic strings, the mechanics and the soundboard with its
// microphones - or, for the electric models, the pickups and the amp. The engine runs on plain
// settings; the processor fills them from the parameters once per block.
namespace hamar::dsp
{
    struct PlaySettings
    {
        float velocityCurve = 0.0f, dynamics = 0.7f, releaseVelocity = 0.5f;
        int transpose = 0;
        float hammerNoise = 0.5f, keyNoise = 0.3f, damperNoise = 0.3f, pedalNoise = 0.3f, pedalResonance = 0.5f;
        float unaCordaDepth = 0.7f, sympathetic = 0.5f;
        float actionNoise = 0.0f;   // an old action: louder clacks and knocks
    };

    struct EngineSettings
    {
        InstrumentSettings instrument;
        BoardSettings board;
        BarSettings bars;
        AmpSettings amp;
        DanceSettings dance;
        LayerSettings layer;
        PlaySettings play;
    };

    class Engine
    {
    public:
        // Keys that may sound at once, per quality level (a held pedal lets a lot of strings ring)
        static int polyphony (int quality) { return quality <= 0 ? 24 : quality == 1 ? 48 : quality == 2 ? 72 : kNumKeys; }

        void prepare (double sampleRate, int maxBlock)
        {
            fs = sampleRate;
            for (int i = 0; i < kNumKeys; ++i)
            {
                voices[(size_t) i].prepare (i + kLowestKey, fs);
                bars[(size_t) i].prepare (i + kLowestKey, fs);
            }
            amp.prepare (fs);
            organ.prepare (fs);
            layer.prepare (fs);
            danceStage.prepare (fs);
            sympathetic.prepare (fs);
            board.prepare (fs);
            noises.prepare (fs);
            for (auto& b : buses) b.assign ((size_t) std::max (16, maxBlock), 0.0f);
            haveSettings = false;
            reset();
        }

        void reset()
        {
            for (auto& v : voices) v.clear();
            for (auto& b : bars) b.clear();
            amp.reset();
            organ.reset();
            layer.reset();
            danceStage.reset();
            sympathetic.reset();
            board.reset();
            noises.reset();
            sustain = soft = 0.0f;
            sostenuto = false;
            latched.fill (false);
            held.fill (false);
            silence = true;
        }

        void allNotesOff()
        {
            for (int i = 0; i < kNumKeys; ++i)
                if (held[(size_t) i]) noteOff (i + kLowestKey, 0.5f, true);
        }

        void panic()
        {
            for (auto& v : voices) v.stop();
            for (auto& b : bars) b.stop();
            held.fill (false);
            latched.fill (false);
            sustain = 0.0f;
            sostenuto = false;
        }

        // Called once per block (cheap when nothing changed)
        void setSettings (const EngineSettings& s)
        {
            auto inst = s.instrument;
            inst.sampleRate = fs;
            // Another model: what sounds now fades out (a tine never turns into a string)
            if (haveSettings && inst.model != settings.instrument.model)
            {
                for (auto& v : voices) if (v.isActive()) v.stop();
                for (auto& b : bars) if (b.isActive()) b.stop();
                micsDirty = true;
            }
            if (! haveSettings || ! (inst == settings.instrument))
            {
                table.update (inst);
                if (! haveSettings) table.refreshAllDispersion();
                sympathetic.tune (table, model (inst.model).lowKey);
                micsDirty = true;
            }
            if (! haveSettings || ! (s.board.micA == settings.board.micA) || ! (s.board.micB == settings.board.micB)) micsDirty = true;
            board.configure (s.board);
            auto ampSettings = s.amp;
            ampSettings.model = inst.model;
            amp.configure (ampSettings);
            settings = s;
            settings.instrument = inst;
            haveSettings = true;
            if (micsDirty)
            {
                const bool electric = model (inst.model).electric;
                for (int i = 0; i < kNumKeys; ++i)
                {
                    const float pos = table[i + kLowestKey].position;
                    if (electric)
                    {
                        // Pickups: the keys a little spread from left to right
                        const auto pan = fjord::base::dsp::panGains ((pos - 0.5f) * 0.5f);
                        micGains[(size_t) i] = { pan[0], pan[1], 0.0f, 0.0f };
                    }
                    else
                        micGains[(size_t) i] = micGainsFor (pos, s.board.micA, s.board.micB);
                }
                micsDirty = false;
            }
            for (auto& v : voices)
            {
                v.setDamperNoiseLevel (s.play.damperNoise);
                v.setBow (inst.bowPressure, inst.bowSpeed);
            }
        }

        // MIDI note -> the key that plays it (notes outside the model's range fold back by octaves)
        int keyFor (int note) const
        {
            int key = note + settings.play.transpose;
            const auto& m = model (settings.instrument.model);
            while (key < m.lowKey) key += 12;
            while (key > m.highKey) key -= 12;
            return key;
        }

        void noteOn (int note, float velocity)
        {
            const int key = keyFor (note);
            const int index = keyIndex (key);
            const auto& k = table[key];
            const auto& play = settings.play;

            // Velocity -> hammer speed (m/s): the curve shapes the touch, dynamics sets the softest blow
            const float shaped = std::pow (std::clamp (velocity, 0.0f, 1.0f), std::exp2 (-1.2f * play.velocityCurve));
            const float vMax = 5.5f, vMin = vMax * std::exp2 (-(1.5f + 4.0f * play.dynamics));
            const float speed = vMin * std::pow (vMax / vMin, shaped);

            const auto& m = model (settings.instrument.model);
            const float loud = speed / vMax;
            if (m.mechanism != Mechanism::Strings)
            {
                auto& bar = bars[(size_t) index];
                if (! bar.isActive()) limitPolyphony (index);
                bar.strike (k, m, settings.bars, loud);
            }
            else
            {
                auto& voice = voices[(size_t) index];
                if (! voice.isActive()) limitPolyphony (index);
                voice.strike (k, speed, soft * play.unaCordaDepth);
            }
            layer.noteOn (index, k.f1, loud, settings.layer);
            if (m.category == Category::Dance)
            {
                const auto& d = settings.dance;
                organ.noteOn (index, k.f1, loud, std::clamp (m.organ + d.organ, 0.0f, 1.0f), d.organTone, std::clamp (m.stab + d.stab, 0.0f, 1.0f));
            }
            held[(size_t) index] = true;
            updateDamper (index, 0.05f);
            ++strikes[(size_t) index];
            lastSpeed[(size_t) index] = speed;
            silence = false;
            if (m.electric) return;   // the pickups do not hear the action
            const float worn = 1.0f + 3.0f * play.actionNoise;
            noises.trigger (Noises::Knock, (1.0f + play.actionNoise) * play.hammerNoise * m.knock * 0.8f * loud * std::sqrt (loud) * KeyVoice::kOutputScale * 40.0f, k.zone, k.position);
            noises.trigger (Noises::KeyDown, worn * play.keyNoise * 0.012f * (0.3f + loud), k.zone, k.position);
            if (m.tack > 0.0f)
                noises.trigger (Noises::Tack, m.tack * 0.02f * loud, k.zone, k.position);
            silence = false;
        }

        void noteOff (int note, float releaseVelocity, bool quiet = false)
        {
            const int index = keyIndex (keyFor (note));
            if (! held[(size_t) index]) return;
            held[(size_t) index] = false;
            voices[(size_t) index].release();
            bars[(size_t) index].release();
            // A fast release lets the damper fall quicker
            const float rv = 0.5f + settings.play.releaseVelocity * (std::clamp (releaseVelocity, 0.0f, 1.0f) - 0.5f);
            const float pos = table[index + kLowestKey].position;
            updateDamper (index, (0.09f - 0.06f * pos) * (1.5f - rv));
            if (! quiet)
                noises.trigger (Noises::KeyUp, (1.0f + 3.0f * settings.play.actionNoise) * settings.play.keyNoise * 0.004f * (0.4f + rv),
                                table[index + kLowestKey].zone, pos);
        }

        // Sustain pedal 0..1 (half pedalling between), sostenuto, una corda 0..1
        void setSustain (float value)
        {
            const float v = std::clamp (value, 0.0f, 1.0f);
            const bool wasDown = sustain > 0.5f, isDown = v > 0.5f;
            sustain = v;
            const auto& p = settings.play;
            if (isDown != wasDown)
            {
                const std::array<float, 3> all { 0.6f, 0.8f, 0.6f };
                noises.trigger (Noises::PedalThump, p.pedalNoise * (isDown ? 0.02f : 0.014f), all, 0.5f);
                if (isDown) noises.trigger (Noises::DamperLift, p.pedalResonance * 0.0015f, all, 0.5f);
            }
            for (int i = 0; i < kNumKeys; ++i) updateDamper (i, isDown ? 0.03f : 0.12f);
        }

        void setSostenuto (bool down)
        {
            if (down && ! sostenuto)
                for (int i = 0; i < kNumKeys; ++i) latched[(size_t) i] = held[(size_t) i];
            if (! down) latched.fill (false);
            sostenuto = down;
            for (int i = 0; i < kNumKeys; ++i) updateDamper (i, 0.08f);
        }

        void setSoft (float value) { soft = std::clamp (value, 0.0f, 1.0f); }

        // Aftertouch: polyphonic (one note) or channel pressure (every held key)
        void setPressure (int note, float value)
        {
            voices[(size_t) keyIndex (keyFor (note))].setPressure (value);
        }

        // Adds the piano to l / r
        void render (float* l, float* r, int n)
        {
            if (! haveSettings) return;
            table.refreshDispersion (3);

            bool any = false;
            for (auto& v : voices) any |= v.isActive();
            for (auto& b : bars) any |= b.isActive();
            for (int i = 0; i < kNumKeys; ++i) any |= organ.isActive (i) || layer.isActive (i);
            if (! any && silence && ! sympatheticActive) { publishDisplay(); return; }

            for (auto& b : buses) std::fill (b.begin(), b.begin() + n, 0.0f);
            auto bus = voiceBuses();
            float* zones[3] = { bus.zone[0], bus.zone[1], bus.zone[2] };

            // Electric models: strings or bars into the pickups, then the amp
            if (model (settings.instrument.model).electric)
            {
                for (int i = 0; i < kNumKeys; ++i)
                {
                    auto& v = voices[(size_t) i];
                    if (v.isActive()) v.render (n, bus, micGains[(size_t) i]);
                    auto& b = bars[(size_t) i];
                    if (b.isActive()) b.render (n, bus.direct, micGains[(size_t) i].data(), 2);
                }
                amp.process (bus.direct[0], bus.direct[1], l, r, n);
                renderLayer (l, r, n);
                publishDisplay();
                float peak = 0.0f;
                for (int i = 0; i < n; ++i) peak = std::max (peak, std::abs (l[i]) + std::abs (r[i]));
                silence = ! any && peak < 1.0e-6f;
                return;
            }

            noises.process (zones, bus.direct, buses[9].data(), n);
            for (int i = 0; i < kNumKeys; ++i)
            {
                auto& v = voices[(size_t) i];
                if (v.isActive()) v.render (n, bus, micGains[(size_t) i]);
                auto& b = bars[(size_t) i];
                if (b.isActive()) b.render (n, bus.direct, micGains[(size_t) i].data(), 4);   // celesta, glass, toy: through the air
                if (organ.isActive (i))
                {
                    organ.setFree (i, freeAmount (i));
                    organ.render (i, n, bus.direct, micGains[(size_t) i]);
                }
            }

            // Sympathetic strings listen to the bridge (and to the dampers lifting)
            auto* bridge = bus.bridge;
            for (int i = 0; i < n; ++i) bridge[i] += buses[9][(size_t) i];
            if (settings.instrument.quality > 0 && settings.play.sympathetic > 0.0f)
            {
                for (int i = 0; i < kNumKeys; ++i)
                    sympathetic.setFree (i, freeAmount (i), voices[(size_t) i].isActive());
                sympathetic.process (bridge, zones, n, settings.play.sympathetic);
                sympatheticActive = true;
            }

            // Mechanics and sympathetic strings reach the mics through the board as well
            for (int i = 0; i < n; ++i)
            {
                const float z = 0.6f * (zones[0][i] + zones[1][i] + zones[2][i]);
                for (int c = 0; c < 4; ++c) bus.direct[c][i] += z;
            }
            board.process (bus.direct, l, r, n);
            renderLayer (l, r, n);
            if (const auto& m = model (settings.instrument.model); m.category == Category::Dance)
                danceStage.process (l, r, n, std::clamp (m.punch + settings.dance.punch, 0.0f, 1.0f),
                                    std::clamp (m.lofi + settings.dance.lofi, 0.0f, 1.0f), settings.dance.wow);
            publishDisplay();

            // Stay running a little after the last note (board and noise tails)
            float peak = 0.0f;
            for (int i = 0; i < n; ++i) peak = std::max (peak, std::abs (l[i]) + std::abs (r[i]));
            silence = ! any && peak < 1.0e-6f;
            if (silence) sympatheticActive = false;
        }

        // What the UI draws: written once per block by the audio thread, read by the message thread
        struct Display
        {
            std::array<std::atomic<float>, kNumKeys> level {}, free {};
            std::array<std::atomic<uint32_t>, kNumKeys> strikes {};
            std::atomic<float> sustain { 0.0f }, soft { 0.0f };
            std::atomic<bool> sostenuto { false };
            std::atomic<int> voices { 0 };
            std::array<std::atomic<float>, 2> micPeak {};   // close / player pair
        };
        const Display& display() const { return shown; }

        // Only for tests and offline tools (not thread safe)
        const KeyTable& getTable() const { return table; }
        int activeVoices() const
        {
            int c = 0;
            for (int i = 0; i < kNumKeys; ++i) c += voices[(size_t) i].isActive() || bars[(size_t) i].isActive() ? 1 : 0;
            return c;
        }

    private:
        void renderLayer (float* l, float* r, int n)
        {
            for (int i = 0; i < kNumKeys; ++i)
                if (layer.isActive (i))
                {
                    layer.setFree (i, freeAmount (i));
                    layer.render (i, l, r, n, settings.layer);
                }
        }

        void publishDisplay()
        {
            int count = 0;
            for (int i = 0; i < kNumKeys; ++i)
            {
                const auto& v = voices[(size_t) i];
                const auto& b = bars[(size_t) i];
                count += v.isActive() || b.isActive() ? 1 : 0;
                const float lv = std::max (v.isActive() ? v.getLevel() : 0.0f, b.isActive() ? b.getLevel() * 4.0f : 0.0f);
                shown.level[(size_t) i].store (lv, std::memory_order_relaxed);
                shown.free[(size_t) i].store (freeAmount (i), std::memory_order_relaxed);
                shown.strikes[(size_t) i].store (strikes[(size_t) i], std::memory_order_relaxed);
            }
            shown.voices.store (count, std::memory_order_relaxed);
            shown.sustain.store (sustain, std::memory_order_relaxed);
            shown.soft.store (soft, std::memory_order_relaxed);
            shown.sostenuto.store (sostenuto, std::memory_order_relaxed);
            const auto peaks = board.takePairPeaks();
            for (int m = 0; m < 2; ++m)
                shown.micPeak[(size_t) m].store (std::max (peaks[(size_t) m], shown.micPeak[(size_t) m].load (std::memory_order_relaxed) * 0.9f),
                                                  std::memory_order_relaxed);
        }

        VoiceBuses voiceBuses()
        {
            return { { buses[0].data(), buses[1].data(), buses[2].data() },
                     { buses[3].data(), buses[4].data(), buses[5].data(), buses[6].data() },
                     buses[7].data() };
        }

        // How free a key's strings are: held, latched by the sostenuto, without damper, or by the sustain pedal
        float freeAmount (int index) const
        {
            if (held[(size_t) index] || latched[(size_t) index] || ! table[index + kLowestKey].hasDamper) return 1.0f;
            if (! model (settings.instrument.model).sustainPedal) return 0.0f;
            const float x = std::clamp ((sustain - 0.15f) / 0.55f, 0.0f, 1.0f);
            return x * x * (3.0f - 2.0f * x);   // half pedal: the dampers only touch the strings lightly
        }

        void updateDamper (int index, float seconds)
        {
            auto& v = voices[(size_t) index];
            if (v.isActive()) v.setDamp (1.0f - freeAmount (index), seconds);
            auto& b = bars[(size_t) index];
            if (b.isActive()) b.setDamp (1.0f - freeAmount (index), seconds);
        }

        // Too many strings: let the quietest released key go
        void limitPolyphony (int except)
        {
            if (activeVoices() < polyphony (settings.instrument.quality)) return;
            int victim = -1;
            float best = 1.0e9f;
            for (int i = 0; i < kNumKeys; ++i)
            {
                const auto& v = voices[(size_t) i];
                const auto& b = bars[(size_t) i];
                if ((! v.isActive() && ! b.isActive()) || i == except) continue;
                const float score = std::max (v.getLevel(), b.getLevel()) * (held[(size_t) i] ? 100.0f : 1.0f);
                if (score < best) { best = score; victim = i; }
            }
            if (victim >= 0) { voices[(size_t) victim].stop(); bars[(size_t) victim].stop(); }
        }

        double fs = 48000.0;
        std::array<KeyVoice, kNumKeys> voices;
        std::array<BarVoice, kNumKeys> bars;
        ElectricAmp amp;
        OrganLayer organ;
        Layer layer;
        DanceStage danceStage;
        KeyTable table;
        SympatheticBank sympathetic;
        Soundboard board;
        Noises noises;
        EngineSettings settings;
        bool haveSettings = false, micsDirty = true, silence = true, sympatheticActive = false;
        std::array<std::vector<float>, 10> buses;     // 0-2 zones, 3-6 direct, 7 bridge, 9 damper noise
        std::array<std::array<float, 4>, kNumKeys> micGains {};
        std::array<bool, kNumKeys> held {}, latched {};
        std::array<float, kNumKeys> lastSpeed {};
        Display shown;
        std::array<uint32_t, kNumKeys> strikes {};
        float sustain = 0.0f, soft = 0.0f;
        bool sostenuto = false;
    };
}
