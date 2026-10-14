#pragma once
#include <array>
#include <cstdint>
#include <algorithm>

// Play tools: one key plays a chord (octave, fifth, triads, sevenths, sus2, power), its notes strummed up, down or
// alternating. Strummed notes wait in a small queue and come out sample-accurately; releasing the key stops every note
// of its chord, also those that have not sounded yet.
namespace hamar::dsp
{
    class ChordTool
    {
    public:
        struct Event
        {
            int at = 0, note = 0;
            float velocity = 0.0f;
            bool on = true;
            int channel = 1;
        };

        void reset()
        {
            pendingCount = 0;
            now = 0;
            for (auto& c : chords) c.count = 0;
            alternate = false;
        }

        // A key goes down at sample 'at' of the current chunk: immediate notes are written to out, strummed ones queued
        void noteOn (int at, int note, float velocity, int chord, float strum, int direction, double sampleRate, Event* out, int& count, int max,
                     int channel = 1)
        {
            static const int sizes[] = { 1, 2, 2, 3, 3, 4, 4, 3, 3 };
            static const int intervals[][4] = { { 0 }, { 0, 12 }, { 0, 7 }, { 0, 4, 7 }, { 0, 3, 7 }, { 0, 4, 7, 11 }, { 0, 3, 7, 10 },
                                                { 0, 2, 7 }, { 0, 7, 12 } };
            const int type = std::clamp (chord, 0, 8);
            const int size = sizes[type];
            auto& c = chords[(size_t) (note & 127)];
            c.count = 0;
            const bool down = direction == 1 || (direction == 2 && alternate);
            alternate = ! alternate;
            for (int i = 0; i < size; ++i)
            {
                const int k = note + intervals[type][i];
                if (k < 0 || k > 127) continue;
                c.notes[(size_t) c.count++] = k;
                const int order = down ? size - 1 - i : i;
                const int delay = (int) (order * strum * sampleRate);
                if (delay == 0 || size == 1)
                {
                    if (count < max) out[count++] = { at, k, velocity * (i == 0 ? 1.0f : 0.92f), true, channel };
                }
                else if (pendingCount < (int) pending.size())
                    pending[(size_t) pendingCount++] = { now + at + delay, k, velocity * 0.92f, note & 127, channel };
            }
        }

        void noteOff (int at, int note, Event* out, int& count, int max)
        {
            auto& c = chords[(size_t) (note & 127)];
            if (c.count == 0) { if (count < max) out[count++] = { at, note, 0.5f, false }; return; }
            for (int i = 0; i < c.count; ++i)
                if (count < max) out[count++] = { at, c.notes[(size_t) i], 0.5f, false };
            c.count = 0;
            // Strummed notes of this chord that have not sounded yet never will
            int w = 0;
            for (int i = 0; i < pendingCount; ++i)
                if (pending[(size_t) i].source != (note & 127)) pending[(size_t) w++] = pending[(size_t) i];
            pendingCount = w;
        }

        // Queued notes due in the next n samples; then time moves on by n
        void collect (int n, Event* out, int& count, int max)
        {
            int w = 0;
            for (int i = 0; i < pendingCount; ++i)
            {
                const auto& p = pending[(size_t) i];
                if (p.due < now + n)
                {
                    if (count < max) out[count++] = { (int) std::max<int64_t> (0, p.due - now), p.note, p.velocity, true, p.channel };
                }
                else
                    pending[(size_t) w++] = p;
            }
            pendingCount = w;
            now += n;
        }

    private:
        struct Pending { int64_t due = 0; int note = 0; float velocity = 0.0f; int source = 0; int channel = 1; };
        struct Chord { std::array<int, 4> notes {}; int count = 0; };

        std::array<Pending, 256> pending {};
        int pendingCount = 0;
        int64_t now = 0;
        std::array<Chord, 128> chords {};
        bool alternate = false;
    };
}
