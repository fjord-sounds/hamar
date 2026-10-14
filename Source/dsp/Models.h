#pragma once
#include <array>
#include <cmath>
#include <algorithm>
#include <initializer_list>
#include <utility>
#include <string_view>

// The instrument models: every piano is a set of physical numbers (string scale, hammers, decay, soundboard,
// cabinet) on top of one engine. The parameters of the plugin are offsets around these numbers, so a sound keeps its
// character when the model changes and every model starts from its own voicing.
namespace hamar::dsp
{
    enum class Category { Grand, Upright, Electric, Historic, Dance, Fantasy };
    enum class Cabinet { Grand, Upright };
    // What vibrates: strings, or bars - a tine, a reed, a plucked reed, a free plate (celesta, glass), a clamped rod (toy
    // piano), a bell-like bar
    enum class Mechanism { Strings, Tine, Reed, Pianet, Plate, Rod, Bell };
    enum class Exciter { Hammer, Plectrum, Bow };            // what sets strings going: a hammer (or tangent), a plectrum, a bow
    enum class Pressure { None, Bebung, Bow };               // what key pressure (aftertouch) does: nothing (a piano), the
                                                             // clavichord's Bebung (the tangent stretches the string), the bow

    struct ModelSpec
    {
        const char* name;
        Category category;
        Cabinet cabinet;
        int lowKey;          // lowest key (21 = A0, 12 = C0 for the extended bass)
        float bassLength;    // speaking length of the lowest A in metres (a concert grand has about 1.95 m)
        float inharm;        // multiplier on the inharmonicity (on top of what the string length gives)
        int monoTop, biTop;  // keys up to monoTop have one string, up to biTop two, above three
        float hammerMass, hammerK, hammerP, strike;   // multipliers on mass and felt stiffness, voicing (stiffness in octaves / 4), strike position
        float felt, tack;    // felt strip between hammer and strings, metal tacks in the hammers
        float after, prompt; // multipliers on the slow (aftersound) and fast (prompt) decay
        float brightness;    // string losses at high frequencies (-1 dark .. 1 bright)
        int damperTop;       // highest key with a damper
        float boardLow, boardSpacing, boardQ, boardSize, boardLevel;  // soundboard: lowest mode, mode spacing (Hz), Q, size, level
        float detune;        // spread of the unison strings in cents
        float duplex;        // level of the duplex scale (short resonating string parts behind the bridge)
        float knock;         // body knock of the hammer blow
        float gain;          // output trim, so models sit at similar levels

        // Electric and bar instruments (defaults: an acoustic string instrument)
        Mechanism mechanism = Mechanism::Strings;
        bool electric = false;   // heard through pickups and an amp instead of a soundboard and mics
        int highKey = 108;       // highest key (shorter keyboards fold the notes above back by octaves)
        float bell = 1.0f;       // level of the upper bar modes (the bell of a tine)
        float tonebar = 1.0f;    // the tonebar resonating with a tine
        Exciter exciter = Exciter::Hammer;
        float dampers = 1.0f;    // how well the dampers stop the strings (early pianos: weaker, a clavichord's cloth: at once)
        bool sustainPedal = true;   // harpsichord and clavichord have none
        // Dance: the organ layer of the classic house piano, punch, tape, short stabs (added to the parameters)
        float organ = 0.0f, punch = 0.0f, lofi = 0.0f, stab = 0.0f;
        float prepared = 0.0f;   // objects on the strings (screws, rubber, bolts)
        float pitchCoupling = 1.6f;   // how much the coupled strings' pitch shift is tuned out (measured per model, see KeyVoice)
        Pressure pressure = Pressure::None;
    };

    // How much of the coupled strings' pitch shift each model tunes out (KeyVoice::setup). Measured with
    // HamarRenderTest --analyse: grands and most uprights hear the shifted in-phase motion, instruments with strongly
    // detuned unisons (honky-tonk, tack, spinet, square piano, early fortepiano) and the harpsichord (its plectra
    // pluck the choirs unevenly) hear the motion against each other, which is not shifted.
    inline std::array<ModelSpec, 36> withMeasuredPitchCoupling (std::array<ModelSpec, 36> list)
    {
        static const std::pair<const char*, float> measured[] = {
            { "Home Upright", 1.0f }, { "Spinet", 0.0f }, { "Honky-Tonk", 0.0f }, { "Tack Piano", 0.0f }, { "Electric Grand", 0.85f },
            { "Clav", 1.0f }, { "Fortepiano 1795", 0.0f }, { "Square Piano", 0.0f }, { "Harpsichord", 0.0f }, { "Clavichord", 0.8f },
            { "House Stab", 0.4f }, { "90s Rave Piano", 1.7f }, { "Deep House Keys", 1.9f }, { "Garage Piano", 1.2f },
            { "Lo-Fi Tape Piano", 1.7f }, { "Prepared Piano", 1.5f }, { "Bowed Piano", 1.9f } };
        for (auto& m : list)
        {
            m.pitchCoupling = 1.85f;   // grands and the studio and felt uprights
            for (const auto& [name, value] : measured)
                if (std::string_view (m.name) == name) m.pitchCoupling = value;
            if (std::string_view (m.name) == "Clavichord") m.pressure = Pressure::Bebung;
            if (m.exciter == Exciter::Bow) m.pressure = Pressure::Bow;
        }
        return list;
    }

    // The order is the order of the "model" choice parameter: append only.
    inline const std::array<ModelSpec, 36>& models()
    {
        using C = Category;
        using K = Cabinet;
        static const std::array<ModelSpec, 36> list = withMeasuredPitchCoupling ({ {
            //  name              cat        cab        low  bass  inh   mono bi   mass  K     p      strike felt  tack after prompt bright damp  bLow  bSp   bQ  bSize bLvl detune dup  knock gain
            { "Concert Grand",  C::Grand,   K::Grand,   21, 1.95f, 1.0f, 28, 40, 1.00f, 1.00f, 0.00f, 1.00f, 0.0f, 0.0f, 1.00f, 1.00f, 0.00f, 89, 48.0f, 14.0f, 38.0f, 1.00f, 1.0f, 0.8f, 0.40f, 1.0f, 1.00f },
            { "Imperial Grand", C::Grand,   K::Grand,   12, 2.10f, 0.85f,30, 42, 1.05f, 0.80f, -0.05f,1.00f, 0.0f, 0.0f, 1.15f, 1.05f, -0.10f,89, 42.0f, 12.0f, 40.0f, 1.15f, 1.1f, 0.7f, 0.00f, 1.1f, 0.95f },
            { "Viennese Grand", C::Grand,   K::Grand,   21, 1.75f, 1.1f, 27, 39, 0.92f, 0.70f, -0.10f,1.02f, 0.0f, 0.0f, 1.05f, 1.10f, -0.20f,89, 55.0f, 15.0f, 36.0f, 0.95f, 1.0f, 1.0f, 0.30f, 0.9f, 1.05f },
            { "Studio Grand",   C::Grand,   K::Grand,   21, 1.65f, 1.2f, 28, 40, 1.00f, 1.15f, 0.05f, 1.00f, 0.0f, 0.0f, 0.95f, 1.00f, 0.10f, 89, 55.0f, 15.0f, 36.0f, 0.90f, 1.0f, 1.0f, 0.40f, 1.0f, 1.00f },
            { "Parlour Grand",  C::Grand,   K::Grand,   21, 1.45f, 1.5f, 27, 39, 0.97f, 1.00f, 0.00f, 1.00f, 0.0f, 0.0f, 0.85f, 0.95f, 0.00f, 89, 62.0f, 17.0f, 34.0f, 0.80f, 1.0f, 1.2f, 0.30f, 1.0f, 1.05f },
            { "Baby Grand",     C::Grand,   K::Grand,   21, 1.25f, 2.0f, 26, 38, 0.95f, 1.00f, 0.00f, 1.00f, 0.0f, 0.0f, 0.75f, 0.90f, 0.00f, 88, 70.0f, 19.0f, 32.0f, 0.70f, 1.0f, 1.5f, 0.20f, 1.0f, 1.10f },
            { "Pop Grand",      C::Grand,   K::Grand,   21, 1.75f, 1.15f,28, 40, 1.00f, 2.00f, 0.20f, 0.95f, 0.0f, 0.0f, 0.95f, 0.95f, 0.40f, 89, 52.0f, 14.0f, 36.0f, 0.95f, 0.9f, 1.0f, 0.50f, 1.0f, 0.90f },
            { "Felt Grand",     C::Grand,   K::Grand,   21, 1.75f, 1.1f, 28, 40, 1.00f, 0.55f, -0.10f,1.00f, 0.8f, 0.0f, 0.90f, 1.00f, -0.30f,89, 52.0f, 14.0f, 36.0f, 0.95f, 1.1f, 0.9f, 0.10f, 1.3f, 1.30f },
            { "Studio Upright", C::Upright, K::Upright, 21, 1.30f, 1.9f, 27, 39, 0.95f, 1.20f, 0.05f, 1.05f, 0.0f, 0.0f, 0.75f, 0.85f, 0.05f, 88, 75.0f, 20.0f, 30.0f, 0.65f, 1.0f, 1.5f, 0.10f, 1.1f, 1.05f },
            { "Home Upright",   C::Upright, K::Upright, 21, 1.15f, 2.4f, 26, 38, 0.92f, 1.00f, 0.00f, 1.05f, 0.0f, 0.0f, 0.65f, 0.80f, -0.05f,88, 85.0f, 22.0f, 28.0f, 0.60f, 1.0f, 2.5f, 0.00f, 1.2f, 1.10f },
            { "Spinet",         C::Upright, K::Upright, 21, 0.95f, 3.2f, 25, 37, 0.88f, 1.10f, 0.05f, 1.10f, 0.0f, 0.0f, 0.50f, 0.75f, 0.00f, 87, 100.0f,26.0f, 26.0f, 0.50f, 0.9f, 3.0f, 0.00f, 1.2f, 1.20f },
            { "Felt Upright",   C::Upright, K::Upright, 21, 1.20f, 2.2f, 26, 38, 0.95f, 0.55f, -0.10f,1.05f, 0.85f,0.0f, 0.60f, 0.85f, -0.30f,88, 82.0f, 21.0f, 28.0f, 0.60f, 1.1f, 2.0f, 0.00f, 1.4f, 1.40f },
            { "Honky-Tonk",     C::Upright, K::Upright, 21, 1.15f, 2.5f, 26, 38, 0.95f, 1.40f, 0.10f, 1.05f, 0.0f, 0.0f, 0.60f, 0.70f, 0.10f, 88, 85.0f, 22.0f, 26.0f, 0.60f, 0.9f, 14.0f,0.00f, 1.1f, 1.00f },
            { "Tack Piano",     C::Upright, K::Upright, 21, 1.15f, 2.4f, 26, 38, 0.95f, 1.00f, 0.00f, 1.05f, 0.0f, 1.0f, 0.55f, 0.70f, 0.20f, 88, 85.0f, 22.0f, 26.0f, 0.60f, 0.9f, 5.0f, 0.00f, 1.0f, 0.85f },
            // Electric: tines and reeds (bars) or strings, heard through pickups and an amp
            //  name              cat          cab        low  bass  inh      mono bi   mass  K      p      strike felt  tack after prompt bright damp  bLow  bSp   bQ  bSize bLvl detune dup  knock gain   mechanism         elec  high bell  tonebar
            { "Tine Stage",     C::Electric, K::Grand,  28, 1.00f, 0.0001f, 108, 108, 1.00f, 1.00f, 0.00f, 1.00f, 0.0f, 0.0f, 0.55f, 1.00f, 0.00f, 108, 60.0f, 20.0f, 30.0f, 0.5f, 1.0f, 0.0f, 0.0f, 0.6f, 1.00f, Mechanism::Tine,   true, 100, 1.00f, 1.0f },
            { "Tine Suitcase",  C::Electric, K::Grand,  28, 1.00f, 0.0001f, 108, 108, 1.00f, 1.00f, 0.00f, 1.00f, 0.0f, 0.0f, 0.60f, 1.00f, -0.10f,108, 60.0f, 20.0f, 30.0f, 0.5f, 1.0f, 0.0f, 0.0f, 0.6f, 1.00f, Mechanism::Tine,   true, 100, 0.85f, 1.2f },
            { "Reed 200",       C::Electric, K::Grand,  33, 1.00f, 0.0001f, 108, 108, 1.00f, 1.00f, 0.00f, 1.00f, 0.0f, 0.0f, 0.25f, 1.00f, 0.00f, 108, 60.0f, 20.0f, 30.0f, 0.5f, 1.0f, 0.0f, 0.0f, 0.5f, 1.00f, Mechanism::Reed,   true,  96, 0.60f, 0.0f },
            { "Electric Grand", C::Electric, K::Grand,  28, 1.05f, 2.2f,     30, 108, 0.90f, 1.60f, 0.10f, 1.00f, 0.0f, 0.0f, 0.60f, 0.80f, 0.30f,  96, 60.0f, 20.0f, 30.0f, 0.5f, 1.0f, 1.2f, 0.0f, 0.6f, 1.00f, Mechanism::Strings, true, 100, 1.00f, 0.0f },
            { "Pianet",         C::Electric, K::Grand,  29, 1.00f, 0.0001f, 108, 108, 1.00f, 1.00f, 0.00f, 1.00f, 0.0f, 0.0f, 0.12f, 1.00f, 0.00f, 108, 60.0f, 20.0f, 30.0f, 0.5f, 1.0f, 0.0f, 0.0f, 0.3f, 1.00f, Mechanism::Pianet, true,  89, 0.50f, 0.0f },
            { "Clav",           C::Electric, K::Upright,  29, 0.75f, 1.5f,    108, 108, 0.40f, 40.0f, 0.00f, 0.30f, 0.0f, 0.0f, 0.30f, 0.70f, 0.60f, 108, 60.0f, 20.0f, 30.0f, 0.5f, 1.0f, 0.0f, 0.0f, 0.4f, 1.00f, Mechanism::Strings, true,  88, 1.00f, 0.0f },
            // Historic: leather hammers on thin strings, a plectrum, a tangent
            //  name                cat          cab         low  bass  inh   mono bi   mass  K      p      strike felt  tack after prompt bright damp  bLow   bSp   bQ    bSize bLvl detune dup  knock gain   mechanism           elec   high bell  tonebar exciter            dampers
            { "Fortepiano 1795",  C::Historic, K::Grand,   29, 1.55f, 0.6f,  0, 108, 0.35f, 2.50f, 0.00f, 1.00f, 0.0f, 0.0f, 0.45f, 0.80f, 0.40f,  91, 95.0f, 26.0f, 26.0f, 0.50f, 1.1f, 2.0f, 0.0f, 1.2f, 1.40f, Mechanism::Strings, false,  91, 1.0f, 0.0f, Exciter::Hammer,   0.60f },
            { "Fortepiano 1820",  C::Historic, K::Grand,   24, 1.75f, 0.7f,  0,  40, 0.50f, 1.80f, 0.00f, 1.00f, 0.0f, 0.0f, 0.55f, 0.85f, 0.25f,  96, 80.0f, 22.0f, 28.0f, 0.60f, 1.1f, 1.5f, 0.0f, 1.1f, 1.30f, Mechanism::Strings, false, 101, 1.0f, 0.0f, Exciter::Hammer,   0.70f },
            { "Square Piano",     C::Historic, K::Upright, 29, 1.50f, 0.8f,  0, 108, 0.30f, 2.00f, 0.00f, 1.00f, 0.0f, 0.0f, 0.40f, 0.80f, 0.20f,  89, 110.0f,30.0f, 22.0f, 0.45f, 1.0f, 3.0f, 0.0f, 1.3f, 1.50f, Mechanism::Strings, false,  89, 1.0f, 0.0f, Exciter::Hammer,   0.50f },
            { "Harpsichord",      C::Historic, K::Grand,   29, 1.85f, 0.3f, 108, 108, 1.00f, 1.00f, 0.00f, 0.90f, 0.0f, 0.0f, 0.75f, 1.20f, 0.80f, 108, 85.0f, 20.0f, 40.0f, 0.70f, 1.2f, 0.5f, 0.0f, 0.5f, 6.00f, Mechanism::Strings, false,  89, 1.0f, 0.0f, Exciter::Plectrum, 1.00f, false },
            { "Clavichord",       C::Historic, K::Upright, 36, 1.20f, 0.5f,  0, 108, 0.30f, 40.0f, 0.00f, 0.25f, 0.0f, 0.0f, 0.60f, 0.90f, 0.50f, 108, 120.0f,35.0f, 25.0f, 0.35f, 1.0f, 1.0f, 0.0f, 0.2f, 2.50f, Mechanism::Strings, false,  88, 1.0f, 0.0f, Exciter::Hammer,   3.00f, false },
            // Dance: string models with the organ layer, punch and tape of house and garage records
            //  name                cat        cab         low  bass  inh   mono bi   mass  K      p      strike felt  tack after prompt bright damp  bLow  bSp   bQ    bSize bLvl detune dup  knock gain   mechanism          elec   high bell tonebar exciter          dampers sustain organ  punch lofi  stab
            { "House Stab",       C::Dance, K::Upright, 21, 1.40f, 1.6f, 26, 38, 0.95f, 2.50f, 0.10f, 1.00f, 0.0f, 0.0f, 0.35f, 0.80f, 0.60f, 88, 70.0f, 20.0f, 30.0f, 0.65f, 0.9f, 0.6f, 0.0f, 0.6f, 1.00f, Mechanism::Strings, false, 108, 1.0f, 0.0f, Exciter::Hammer, 1.0f,  true, 0.55f, 0.30f, 0.0f, 0.3f },
            { "90s Rave Piano",   C::Dance, K::Upright, 21, 1.30f, 1.8f, 26, 38, 0.95f, 3.00f, 0.15f, 1.00f, 0.0f, 0.0f, 0.30f, 0.80f, 0.80f, 88, 70.0f, 20.0f, 30.0f, 0.65f, 0.9f, 0.8f, 0.0f, 0.6f, 0.95f, Mechanism::Strings, false, 108, 1.0f, 0.0f, Exciter::Hammer, 1.0f,  true, 0.35f, 0.50f, 0.0f, 0.4f },
            { "Deep House Keys",  C::Dance, K::Grand,   21, 1.70f, 1.1f, 28, 40, 1.00f, 0.80f, -0.05f,1.00f, 0.2f, 0.0f, 0.60f, 0.90f, -0.20f,89, 52.0f, 14.0f, 36.0f, 0.95f, 1.0f, 1.0f, 0.2f, 0.9f, 1.10f, Mechanism::Strings, false, 108, 1.0f, 0.0f, Exciter::Hammer, 1.0f,  true, 0.45f, 0.10f, 0.1f, 0.0f },
            { "Garage Piano",     C::Dance, K::Upright, 21, 1.20f, 2.2f, 26, 38, 0.95f, 2.00f, 0.10f, 1.00f, 0.0f, 0.0f, 0.40f, 0.75f, 0.50f, 88, 85.0f, 22.0f, 28.0f, 0.60f, 1.0f, 2.0f, 0.0f, 1.0f, 1.00f, Mechanism::Strings, false, 108, 1.0f, 0.0f, Exciter::Hammer, 1.0f,  true, 0.10f, 0.60f, 0.0f, 0.2f },
            { "Lo-Fi Tape Piano", C::Dance, K::Upright, 21, 1.20f, 2.2f, 26, 38, 0.95f, 0.60f, -0.10f,1.05f, 0.6f, 0.0f, 0.60f, 0.85f, -0.30f,88, 82.0f, 21.0f, 28.0f, 0.60f, 1.1f, 2.5f, 0.0f, 1.3f, 1.30f, Mechanism::Strings, false, 108, 1.0f, 0.0f, Exciter::Hammer, 1.0f,  true, 0.00f, 0.00f, 0.6f, 0.0f },
            // Fantasy: plates, rods and bells struck by mallets, prepared strings, bowed strings
            //  name                cat          cab         low  bass  inh      mono bi   mass  K      p      strike felt  tack after prompt bright damp  bLow   bSp   bQ    bSize bLvl detune dup  knock gain   mechanism           elec   high bell  tonebar exciter          dampers sustain organ punch lofi stab prepared
            { "Glass Piano",      C::Fantasy, K::Grand,   36, 1.00f, 0.0001f, 108, 108, 1.00f, 1.00f, 0.00f, 1.00f, 0.0f, 0.0f, 2.20f, 1.00f, 0.00f, 108, 120.0f, 40.0f, 60.0f, 0.40f, 0.8f, 0.0f, 0.0f, 0.2f, 0.35f, Mechanism::Plate,   false,  96, 0.8f, 0.0f, Exciter::Hammer, 1.0f, true, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
            { "Ice Crystal",      C::Fantasy, K::Grand,   48, 1.00f, 0.0001f, 108, 108, 1.00f, 1.00f, 0.00f, 1.00f, 0.0f, 0.0f, 1.40f, 1.00f, 0.00f, 108, 160.0f, 50.0f, 70.0f, 0.30f, 0.7f, 0.0f, 0.0f, 0.1f, 0.22f, Mechanism::Bell,    false, 108, 1.2f, 0.0f, Exciter::Hammer, 1.0f, true, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
            { "Prepared Piano",   C::Fantasy, K::Grand,   21, 1.95f, 1.0f,    28,  40, 1.00f, 1.00f, 0.00f, 1.00f, 0.0f, 0.0f, 0.90f, 1.00f, 0.00f,  89, 48.0f,  14.0f, 38.0f, 1.00f, 1.0f, 0.8f, 0.2f, 1.2f, 1.00f, Mechanism::Strings, false, 108, 1.0f, 0.0f, Exciter::Hammer, 1.0f, true, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f },
            { "Toy Piano",        C::Fantasy, K::Upright, 53, 1.00f, 0.0001f, 108, 108, 1.00f, 1.00f, 0.00f, 1.00f, 0.0f, 0.0f, 0.25f, 1.00f, 0.00f, 108, 300.0f, 60.0f, 20.0f, 0.25f, 0.8f, 0.0f, 0.0f, 1.5f, 0.60f, Mechanism::Rod,     false,  89, 1.4f, 0.0f, Exciter::Hammer, 1.0f, true, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
            { "Celesta",          C::Fantasy, K::Upright, 60, 1.00f, 0.0001f, 108, 108, 1.00f, 1.00f, 0.00f, 1.00f, 0.0f, 0.0f, 0.80f, 1.00f, 0.00f, 108, 180.0f, 45.0f, 30.0f, 0.35f, 1.2f, 0.0f, 0.0f, 0.6f, 0.45f, Mechanism::Plate,   false, 108, 0.5f, 0.0f, Exciter::Hammer, 1.0f, true, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
            { "Bowed Piano",      C::Fantasy, K::Grand,   21, 1.95f, 1.0f,    28,  40, 1.00f, 1.00f, 0.00f, 1.00f, 0.0f, 0.0f, 1.00f, 1.00f, 0.00f,  89, 48.0f,  14.0f, 38.0f, 1.00f, 1.0f, 0.8f, 0.4f, 0.1f, 3.00f, Mechanism::Strings, false, 108, 1.0f, 0.0f, Exciter::Bow,    1.0f, true, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
        } });
        return list;
    }

    inline const ModelSpec& model (int index)
    {
        return models()[(size_t) std::clamp (index, 0, (int) models().size() - 1)];
    }

    // Piecewise interpolation over keys; log = interpolate the logarithm (for quantities that grow by ratios)
    inline float keyCurve (float key, std::initializer_list<std::pair<float, float>> points, bool log)
    {
        const auto* p = points.begin();
        const auto n = points.size();
        if (key <= p[0].first) return p[0].second;
        for (size_t i = 1; i < n; ++i)
            if (key <= p[i].first)
            {
                const float t = (key - p[i - 1].first) / (p[i].first - p[i - 1].first);
                return log ? std::exp (std::log (p[i - 1].second) + t * (std::log (p[i].second) - std::log (p[i - 1].second)))
                           : p[i - 1].second + t * (p[i].second - p[i - 1].second);
            }
        return p[n - 1].second;
    }

    // Physical data of a modern concert grand (speaking lengths, inharmonicity, tension, hammers, decay times). The
    // numbers follow published measurements (Conklin, Chaigne & Askenfelt, Weinreich) and are smoothed over the keys.
    namespace scale
    {
        inline float length (float key)       // metres
        {
            return keyCurve (key, { { 12, 2.05f }, { 21, 1.95f }, { 36, 1.55f }, { 48, 1.05f }, { 60, 0.62f },
                                    { 72, 0.33f }, { 84, 0.17f }, { 96, 0.088f }, { 108, 0.05f } }, true);
        }

        inline float inharmonicity (float key)
        {
            return keyCurve (key, { { 12, 5.0e-4f }, { 21, 3.5e-4f }, { 28, 2.2e-4f }, { 36, 1.6e-4f }, { 45, 1.4e-4f },
                                    { 52, 2.0e-4f }, { 60, 3.5e-4f }, { 72, 8.0e-4f }, { 84, 2.5e-3f }, { 96, 8.0e-3f },
                                    { 108, 2.2e-2f } }, true);
        }

        inline float tension (float key)      // newtons per string
        {
            return keyCurve (key, { { 12, 1400 }, { 21, 1300 }, { 36, 1000 }, { 48, 820 }, { 60, 760 }, { 84, 700 }, { 108, 650 } }, true);
        }

        inline float hammerMass (float key)   // kg: hammer heads of a concert grand (11 g bass .. 6 g treble)
        {
            return keyCurve (key, { { 12, 11.5e-3f }, { 21, 11.0e-3f }, { 36, 10.0e-3f }, { 60, 8.5e-3f }, { 84, 7.0e-3f }, { 108, 5.5e-3f } }, true);
        }

        inline float hammerK (float key)      // felt stiffness (N/m^p), calibrated against recordings of a concert grand
        {
            return 0.24f * keyCurve (key, { { 12, 3.0e8f }, { 21, 4.0e8f }, { 36, 1.0e9f }, { 60, 4.5e9f }, { 91, 1.0e11f }, { 108, 6.0e11f } }, true);
        }

        inline float hammerP (float key)      // felt exponent
        {
            return keyCurve (key, { { 12, 2.2f }, { 21, 2.2f }, { 36, 2.3f }, { 60, 2.5f }, { 91, 3.0f }, { 108, 3.2f } }, false);
        }

        inline float strike (float key)       // strike position as a fraction of the speaking length
        {
            return keyCurve (key, { { 12, 0.112f }, { 21, 0.112f }, { 60, 0.118f }, { 84, 0.10f }, { 108, 0.07f } }, false);
        }

        inline float afterDecay (float key)   // T60 of the slow decay in seconds
        {
            return keyCurve (key, { { 12, 45 }, { 21, 40 }, { 48, 30 }, { 60, 22 }, { 72, 14 }, { 84, 9 }, { 96, 4.0f }, { 108, 1.8f } }, true);
        }

        inline float promptRatio (float key)  // after / prompt decay
        {
            return keyCurve (key, { { 21, 3.0f }, { 36, 3.0f }, { 60, 10.0f }, { 72, 6.0f }, { 84, 2.5f }, { 96, 2.0f } }, false);
        }

        inline float axialStiffness (float key)   // E A of the steel (core) wire in newtons: wound bass strings have thick cores
        {
            return keyCurve (key, { { 12, 5.0e5f }, { 21, 4.5e5f }, { 40, 3.5e5f }, { 52, 1.7e5f }, { 108, 1.0e5f } }, true);
        }

        inline float dampedDecay (float key)  // T60 with the damper on the string
        {
            return keyCurve (key, { { 12, 1.4f }, { 21, 1.2f }, { 60, 0.35f }, { 88, 0.15f } }, true);
        }
    }
}
