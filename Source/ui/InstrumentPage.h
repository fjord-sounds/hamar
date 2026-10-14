#pragma once
#include "Widgets.h"

namespace hamar::ui
{
    // A few words about every model (same order as dsp::models())
    inline const char* modelAbout (int index)
    {
        static const char* text[] = {
            "A modern nine-foot concert grand: long bass strings with little inharmonicity, three strings from the tenor up, a big board and a long, even sustain.",
            "The extended-bass grand reaching down to C0: the longest strings, softer hammers, no duplex scale and a dark, wide bass.",
            "A Viennese grand: lighter hammers and softer felt, a sweet, singing middle and a round, rich treble.",
            "A seven-foot studio grand: a little shorter, a little brighter, the instrument of countless recordings.",
            "A parlour grand for the salon: shorter strings, more colour in the bass, a warm and close sound.",
            "A baby grand for the living room: short bass strings with a lot of inharmonicity, small board, quick decay.",
            "A concert grand voiced for pop: hard hammers, bright treble and a clear attack that cuts through a mix.",
            "A grand with a felt strip between hammers and strings: soft, dark and intimate - the sound of quiet late-night pianos.",
            "A tall studio upright: a big board for an upright, a clear and present tone close to a small grand.",
            "A home upright: short strings, a boxy middle, a little out of tune between the strings of a unison.",
            "A small spinet: the shortest strings, the most inharmonic bass, a quick and thin decay.",
            "An upright with its felt strip down (moderator): muffled, warm, breathy - the bedroom piano.",
            "An old upright with strongly mistuned unisons: the shimmering, wobbly bar-room sound.",
            "An upright with metal tacks in its hammers: a bright, jangly, almost harpsichord-like attack.",
            "A stage piano with tines: steel tines and their tonebars struck by soft tips, heard through magnetic pickups - bell, bark and endless sustain.",
            "The tine piano in its suitcase: the same tines through the built-in amp and speakers, with the stereo vibrato that pans from side to side.",
            "A reed piano: steel reeds struck by small felt hammers and heard through an electrostatic pickup - sweet when soft, crunchy and growling when hard.",
            "An electric grand: short grand strings with piezo pickups under the bridge, no soundboard - bright, punchy and very eighties.",
            "Reeds plucked by sticky pads: a short, woody, slightly nasal tone that stops as soon as the key comes up.",
            "A clavichord gone electric: strings hit by rubber tips from below, heard through pickups, stopped by yarn - funky and percussive.",
            "A Viennese fortepiano of Mozart's time: tiny leather hammers, two thin strings per note, a light, singing and quickly fading tone.",
            "A fortepiano of Beethoven's and Schubert's late years: a wider range, three strings in the treble, a rounder bass and a moderator.",
            "A square piano: strings across a rectangular case, small hammers, a sweet and slightly nasal parlour sound.",
            "A double-manual harpsichord: strings plucked by plectra. Choose the register (two 8' choirs, the 4' an octave up, the lute stop) and the buff stop.",
            "A clavichord: a brass tangent strikes the string and stays on it while the key is down - the most intimate keyboard there is.",
            "The house piano of the early nineties: a bright, short piano with a layer of organ harmonics that holds the chord - made for stabs.",
            "The rave piano: brighter, harder and punchier, the organ layer a little lower - big chords for big rooms.",
            "Deep house keys: a warm, slightly muted grand with a soft organ layer underneath, for late-night chords.",
            "The garage piano: a bright upright with lots of punch and a short tail, for skippy, syncopated chords.",
            "A felt upright through a tired tape machine: saturation, wow and flutter, hiss and a narrow band.",
            "Glass bars struck by soft mallets: long, pure, shimmering tones, the overtones a little apart from the harmonic series.",
            "Bell-like bars of ice: bright, sparkling, strongly inharmonic overtones and a long ring.",
            "A grand with objects between its strings: bolts make some notes ring like gongs, rubber turns others into a thud, loose screws rattle.",
            "A toy piano: small hammers on clamped metal rods - tinny, sweet, a little out of tune and over quickly.",
            "A celesta: felt hammers on steel plates over wooden resonators - the soft, glowing bell of the music box.",
            "A grand whose strings are bowed instead of struck: the tone swells and holds while the key is down.",
        };
        return text[std::clamp (index, 0, 35)];
    }

    // The instrument itself: the model, its case (grand: lid, rim, aliquot; upright: panel, cabinet, wall) and the
    // soundboard. The case panel shows the controls of the model's category.
    class InstrumentPage : public Page
    {
    public:
        explicit InstrumentPage (HamarProcessor& p)
            : proc (p),
              lengths ("Speaking length of the strings", true, 0.04f, 2.4f, [] (float v) { return juce::String (v, v < 1.0f ? 2 : 1) + " m"; }),
              lid (p.apvts, P::paramId (P::GrandLid)), panel (p.apvts, P::paramId (P::UprightPanel)),
              modelLock (p.apvts, locks::ids().model), bodyLock (p.apvts, locks::ids().body), categoryLock (p.apvts, locks::ids().category)
        {
            auto& s = p.apvts;
            addAndMakeVisible (lengths);
            addAndMakeVisible (lid);
            addChildComponent (panel);
            grandRow.add (makeKnob (s, P::GrandRim, "Rim"));
            grandRow.add (makeKnob (s, P::GrandAliquot, "Aliquot"));
            grandRow.addTo (*this);
            uprightRow.add (makeKnob (s, P::UprightBox, "Cabinet"));
            uprightRow.add (makeKnob (s, P::UprightWall, "Wall"));
            uprightRow.addTo (*this);
            boardRow.add (makeKnob (s, P::BodySize, "Size"));
            boardRow.add (makeKnob (s, P::BodyResonance, "Resonance"));
            boardRow.add (makeKnob (s, P::BodyDamping, "Damping"));
            boardRow.add (makeKnob (s, P::BodyBrightness, "Radiation"));
            boardRow.addTo (*this);
            pickupRow.add (makeKnob (s, P::EpPickup, "Bark"));
            pickupRow.add (makeKnob (s, P::EpVoicing, "Offset"));
            pickupRow.add (makeKnob (s, P::EpBell, "Bell"));
            pickupRow.add (makeKnob (s, P::EpTonebar, "Tonebar"));
            pickupRow.add (makeKnob (s, P::EpDecay, "Decay"));
            pickupRow.addTo (*this);
            ampRow.add (makeKnob (s, P::EpDrive, "Drive"));
            ampRow.add (makeKnob (s, P::EpBass, "Bass"));
            ampRow.add (makeKnob (s, P::EpTreble, "Treble"));
            ampRow.add (makeChoice (s, P::EpTremMode, "Tremolo"), 1.3f);
            ampRow.add (makeKnob (s, P::EpTremRate, "Rate"));
            ampRow.add (makeKnob (s, P::EpTremDepth, "Depth"));
            ampRow.addTo (*this);
            stopsRow.add (makeChoice (s, P::HistRegister, "Register"), 2.0f);
            stopsRow.add (makeKnob (s, P::HistBuff, "Buff"));
            stopsRow.add (makeKnob (s, P::HammerFelt, "Moderator"));
            stopsRow.add (makeKnob (s, P::PedalUnaCorda, "Una corda"));
            stopsRow.addTo (*this);
            danceRow.add (makeKnob (s, P::DanceOrgan, "Organ"));
            danceRow.add (makeKnob (s, P::DanceOrganTone, "Organ tone"));
            danceRow.add (makeKnob (s, P::DanceStab, "Stab"));
            danceRow.add (makeKnob (s, P::DancePunch, "Punch"));
            danceRow.add (makeKnob (s, P::DanceLofi, "Tape"));
            danceRow.add (makeKnob (s, P::DanceWow, "Wow"));
            danceRow.addTo (*this);
            fantasyRow.add (makeKnob (s, P::FanRatio, "Ratios"));
            fantasyRow.add (makeKnob (s, P::FanShimmer, "Shimmer"));
            fantasyRow.add (makeKnob (s, P::FanRing, "Ring"));
            fantasyRow.add (makeKnob (s, P::FanPrep, "Preparation"));
            fantasyRow.add (makeKnob (s, P::FanBowPressure, "Bow pressure"));
            fantasyRow.add (makeKnob (s, P::FanBowSpeed, "Bow speed"));
            fantasyRow.addTo (*this);
            addAndMakeVisible (modelLock);
            addAndMakeVisible (categoryLock);
            addAndMakeVisible (bodyLock);
            lid.setTooltip ("The lid of the grand: open, on the short stick, closed or taken off");
            panel.setTooltip ("The front of the upright: closed, open at the top or with the lower front panel removed");
        }

        void tick() override
        {
            if (! table.update (proc)) return;
            const auto& spec = dsp::model (table.settings.model);
            grand = spec.cabinet == dsp::Cabinet::Grand;
            electric = spec.electric;
            historic = spec.category == dsp::Category::Historic;
            dance = spec.category == dsp::Category::Dance;
            fantasy = spec.category == dsp::Category::Fantasy;
            const bool plainCase = ! electric && ! historic && ! dance && ! fantasy;
            lid.setVisible (grand && plainCase);
            panel.setVisible (! grand && plainCase);
            grandRow.setVisible (grand && plainCase);
            uprightRow.setVisible (! grand && plainCase);
            stopsRow.setVisible (historic);
            categoryLock.setVisible (! plainCase);
            danceRow.setVisible (dance);
            fantasyRow.setVisible (fantasy);
            boardRow.setVisible (! electric);
            pickupRow.setVisible (electric);
            ampRow.setVisible (electric);
            bodyLock.setVisible (! electric);
            lengths.setRange (spec.lowKey, dsp::kHighestKey);
            const float scaleFactor = spec.bassLength * std::exp2 (0.6f * table.settings.length) / 1.95f;
            lengths.setLines ({ { [scaleFactor] (int key)
                                  {
                                      const float w = std::clamp ((60.0f - (float) key) / 39.0f, 0.0f, 1.0f);
                                      return dsp::scale::length ((float) key) * std::pow (scaleFactor, w);
                                  }, "Model", true },
                                { [] (int key) { return dsp::scale::length ((float) key); }, "Concert grand", false } });
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            drawPanel (g, modelArea.toFloat(), "Model");
            if (electric)
            {
                drawPanel (g, caseArea.toFloat(), "Pickups");
                drawPanel (g, boardArea.toFloat(), "Amp");
            }
            else if (fantasy)
            {
                drawPanel (g, caseArea.toFloat(), "Fantasy");
                drawPanel (g, boardArea.toFloat(), "Soundboard");
            }
            else if (dance)
            {
                drawPanel (g, caseArea.toFloat(), "Dance");
                drawPanel (g, boardArea.toFloat(), "Soundboard");
            }
            else if (historic)
            {
                drawPanel (g, caseArea.toFloat(), "Stops");
                drawPanel (g, boardArea.toFloat(), "Soundboard");
            }
            else
            {
                drawPanel (g, caseArea.toFloat(), grand ? "Case  " + utf8 ("\xc2\xb7") + "  grand" : "Case  " + utf8 ("\xc2\xb7") + "  upright");
                drawPanel (g, boardArea.toFloat(), "Soundboard");
            }
            if (! table.valid) return;

            const auto& spec = dsp::model (table.settings.model);
            auto r = modelArea.toFloat().reduced (16.0f, 0.0f).withTrimmedTop (36.0f).withTrimmedBottom (12.0f);
            g.setColour (categoryColour ((int) spec.category, dark));
            drawSpacedText (g, juce::String (categoryName (spec.category)).toUpperCase(), r.removeFromTop (14.0f), 9.0f,
                            juce::Justification::centredLeft, 0.2f, true);
            g.setColour (p.text);
            g.setFont (font (22.0f, true));
            g.drawText (spec.name, r.removeFromTop (30.0f), juce::Justification::centredLeft);
            g.setColour (p.textDim);
            g.setFont (font (12.5f));
            g.drawFittedText (modelAbout (table.settings.model), r.removeFromTop (54.0f).toNearestInt(), juce::Justification::topLeft, 4, 1.0f);
            r.removeFromTop (6.0f);

            // Key facts from the key table
            int mono = 0, bi = 0, damperTop = 0;
            for (int key = spec.lowKey; key <= spec.highKey; ++key)
            {
                mono += table[key].strings == 1 ? 1 : 0;
                bi += table[key].strings == 2 ? 1 : 0;
                if (table[key].hasDamper) damperTop = key;
            }
            const auto note = [] (int k) { return fjord::base::params::noteName (k); };
            std::vector<std::pair<juce::String, juce::String>> facts;
            facts.push_back ({ "Keys", juce::String (spec.highKey - spec.lowKey + 1) + "  (" + note (spec.lowKey) + " " + utf8 ("\xe2\x80\x93") + " " + note (spec.highKey) + ")" });
            if (spec.mechanism == dsp::Mechanism::Strings)
            {
                facts.push_back ({ "Single / double strings", juce::String (mono) + " / " + juce::String (bi) });
                facts.push_back ({ "Dampers up to", note (damperTop) });
                facts.push_back ({ "Lowest string", juce::String (lengthAt (spec.lowKey, spec), 2) + " m" });
            }
            else
            {
                static const char* sources[] = { "", "Tines and tonebars", "Struck reeds", "Plucked reeds", "Free plates", "Clamped rods", "Bell bars" };
                facts.push_back ({ "Sound source", sources[(int) spec.mechanism] });
                if (spec.electric) facts.push_back ({ "Pickup", spec.mechanism == dsp::Mechanism::Tine ? "Magnetic" : "Electrostatic" });
            }
            if (spec.electric && spec.mechanism == dsp::Mechanism::Strings)
                facts.push_back ({ "Pickup", juce::String (spec.name) == "Clav" ? "Magnetic" : "Piezo under the bridge" });
            for (const auto& [label, value] : facts)
            {
                auto row = r.removeFromTop (20.0f);
                g.setColour (p.textDim);
                g.setFont (font (11.5f));
                g.drawText (label, row, juce::Justification::centredLeft);
                g.setColour (p.text);
                g.setFont (font (11.5f, true));
                g.drawText (value, row, juce::Justification::centredRight);
            }
        }

        void resized() override
        {
            auto r = getLocalBounds();
            auto top = r.removeFromTop (r.getHeight() - 200);
            r.removeFromTop (10);
            modelArea = top.removeFromLeft (360);
            top.removeFromLeft (10);
            lengths.setBounds (top);
            caseArea = r.removeFromLeft (r.getWidth() * 45 / 100);
            r.removeFromLeft (10);
            boardArea = r;
            modelLock.place (modelArea);
            bodyLock.place (boardArea);
            categoryLock.place (caseArea);
            auto c = caseArea.reduced (12, 0).withTrimmedTop (40).withHeight (110);
            auto sel = c.removeFromLeft (c.getWidth() * 55 / 100);
            lid.setBounds (sel.withSizeKeepingCentre (sel.getWidth() - 10, 28).translated (0, -6));
            panel.setBounds (lid.getBounds());
            grandRow.layout (c);
            uprightRow.layout (c);
            boardRow.layout (boardArea.reduced (12, 0).withTrimmedTop (40).withHeight (110));
            pickupRow.layout (caseArea.reduced (12, 0).withTrimmedTop (40).withHeight (110));
            stopsRow.layout (caseArea.reduced (12, 0).withTrimmedTop (40).withHeight (110));
            danceRow.layout (caseArea.reduced (12, 0).withTrimmedTop (40).withHeight (110));
            fantasyRow.layout (caseArea.reduced (12, 0).withTrimmedTop (40).withHeight (110));
            ampRow.layout (boardArea.reduced (12, 0).withTrimmedTop (40).withHeight (110));
        }

    private:
        float lengthAt (int key, const dsp::ModelSpec& spec) const
        {
            const float w = std::clamp ((60.0f - (float) key) / 39.0f, 0.0f, 1.0f);
            return dsp::scale::length ((float) key) * std::pow (spec.bassLength * std::exp2 (0.6f * table.settings.length) / 1.95f, w);
        }

        HamarProcessor& proc;
        TableWatcher table;
        KeyCurve lengths;
        Segmented lid, panel;
        ControlRow grandRow, uprightRow, boardRow, pickupRow, ampRow, stopsRow, danceRow, fantasyRow;
        PanelLock modelLock, bodyLock, categoryLock;
        juce::Rectangle<int> modelArea, caseArea, boardArea;
        bool grand = true, electric = false, historic = false, dance = false, fantasy = false;
    };
}
