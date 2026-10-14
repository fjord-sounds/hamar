#pragma once
#include <array>
#include <cmath>
#include <string>
#include <vector>
#include <juce_core/juce_core.h>
#include "Tuning.h"

// Scala tunings (.scl): a description, the number of notes, then one pitch per line - cents (with a dot) or a ratio
// (3/2, or a plain integer) - the last one being the period (usually the octave, 2/1). Lines starting with ! are
// comments. A twelve-note scale with an octave period works like a temperament: it is built on the temperament key.
// Any other scale is laid out from the key's C4 upwards and downwards, one key per scale step (microtonal keyboards).
namespace hamar::dsp
{
    struct ScalaScale
    {
        juce::String name, error;
        std::vector<double> cents;   // the pitches after the 1/1 (the last is the period)

        bool valid() const { return error.isEmpty() && ! cents.empty(); }

        static ScalaScale parse (const juce::String& text, const juce::String& fileName)
        {
            ScalaScale s;
            s.name = juce::File (fileName).getFileNameWithoutExtension();
            juce::StringArray lines;
            for (auto line : juce::StringArray::fromLines (text))
                if (! line.trimStart().startsWithChar ('!')) lines.add (line.trim());
            if (lines.size() < 2) { s.error = "Not a Scala file"; return s; }
            if (lines[0].isNotEmpty()) s.name = lines[0].substring (0, 60);
            const int count = lines[1].upToFirstOccurrenceOf (" ", false, false).getIntValue();
            if (count < 1 || count > 256) { s.error = "The scale needs 1 to 256 notes"; return s; }
            for (int i = 2; i < lines.size() && (int) s.cents.size() < count; ++i)
            {
                const auto token = lines[i].upToFirstOccurrenceOf (" ", false, false).trim();
                if (token.isEmpty()) continue;
                double c;
                if (token.containsChar ('.'))
                    c = token.getDoubleValue();
                else
                {
                    const double num = token.upToFirstOccurrenceOf ("/", false, false).getDoubleValue();
                    const double den = token.containsChar ('/') ? token.fromFirstOccurrenceOf ("/", false, false).getDoubleValue() : 1.0;
                    if (num <= 0.0 || den <= 0.0) { s.error = "Cannot read the pitch \"" + token + "\""; return s; }
                    c = 1200.0 * std::log2 (num / den);
                }
                s.cents.push_back (c);
            }
            if ((int) s.cents.size() != count) s.error = "The file lists fewer pitches than it announces";
            else if (s.cents.back() <= 0.0) s.error = "The period must be above the 1/1";
            return s;
        }

        // Cents from equal temperament for every key C0 .. C8 (root = the temperament key, 0 = C).
        std::array<float, kNumKeys> keyCents (int root) const
        {
            std::array<float, kNumKeys> out {};
            const int n = (int) cents.size();
            const double period = cents.back();
            const bool temperament = n == 12 && std::abs (period - 1200.0) < 1.0;
            const int rootKey = 60 + root;   // the scale's 1/1 sits on the key's C4 (C3 in Logic's naming is 60)
            for (int key = kLowestKey; key <= kHighestKey; ++key)
            {
                double target;
                if (temperament)
                {
                    const int step = ((key - rootKey) % 12 + 12) % 12;
                    const int octave = (key - rootKey - step) / 12;
                    target = octave * 1200.0 + (step == 0 ? 0.0 : cents[(size_t) step - 1]);
                }
                else
                {
                    const int d = key - rootKey;
                    const int octave = (int) std::floor ((double) d / n);
                    const int step = d - octave * n;
                    target = octave * period + (step == 0 ? 0.0 : cents[(size_t) step - 1]);
                }
                out[(size_t) keyIndex (key)] = (float) (target - 100.0 * (key - rootKey));
            }
            // Keep A4 at the reference pitch for twelve-note scales (as a temperament does)
            if (temperament)
            {
                const float a = out[(size_t) keyIndex (69)];
                for (auto& c : out) c -= a;
            }
            return out;
        }
    };
}
