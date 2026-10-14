#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <fjord_base/params/Params.h>
#include <fjord_base/fx/FxParams.h>

// The model list is the order of the "model" choice: append only.
#define HAMAR_MODEL_NAMES "Concert Grand|Imperial Grand|Viennese Grand|Studio Grand|Parlour Grand|Baby Grand|Pop Grand|Felt Grand|" \
                          "Studio Upright|Home Upright|Spinet|Felt Upright|Honky-Tonk|Tack Piano|" \
                          "Tine Stage|Tine Suitcase|Reed 200|Electric Grand|Pianet|Clav|" \
                          "Fortepiano 1795|Fortepiano 1820|Square Piano|Harpsichord|Clavichord|" \
                          "House Stab|90s Rave Piano|Deep House Keys|Garage Piano|Lo-Fi Tape Piano|" \
                          "Glass Piano|Ice Crystal|Prepared Piano|Toy Piano|Celesta|Bowed Piano"

namespace hamar::params
{
    // Every parameter is listed once here. The columns are:
    //   enum name, parameter ID, display name, kind (F float, I int, C choice, B bool), min, max, default,
    //   curve (0 linear, -1 logarithmic, > 0 skewed so that this value sits in the middle), value format, choices
    //
    // Rules that keep projects and presets working:
    //   - IDs never change. New parameters go to the END of the list.
    //   - Choice lists are append-only.
    //   - New parameters need neutral defaults (presets without them fall back to the default).
    //   - The shared effect block (FJORD_FX_PARAMS) stays complete and in one piece.

    #define HAMAR_PARAMS(X)                                                                                                        \
        /* Global */                                                                                                                \
        X (MasterGain,      "master_gain",        "Master",          F, -36, 6, -3,        0, Db, "")                                 \
        X (PresetLevel,     "preset_level",       "Preset Level",    F, -24, 12, 0,        0, Db, "")                                 \
        X (Model,           "model",              "Model",           C, 0, 35, 0,          0, None, HAMAR_MODEL_NAMES)                \
        X (Quality,         "quality",            "Quality",         C, 0, 3, 1,           0, None, "Eco|Standard|High|Render")      \
        /* Play */                                                                                                                  \
        X (PlayVelCurve,    "play_vel_curve",     "Velocity Curve",  F, -1, 1, 0,          0, Bip, "")                                \
        X (PlayDynamics,    "play_dynamics",      "Dynamics",        F, 0, 1, 0.7f,        0, Pct, "")                                \
        X (PlayReleaseVel,  "play_release_vel",   "Release Velocity",F, 0, 1, 0.5f,        0, Pct, "")                                \
        X (PlayTranspose,   "play_transpose",     "Transpose",       I, -24, 24, 0,        0, Semi, "")                               \
        /* Hammer */                                                                                                                \
        X (HammerHardness,  "hammer_hardness",    "Hardness",        F, -1, 1, 0,          0, Bip, "")                                \
        X (HammerMass,      "hammer_mass",        "Mass",            F, -1, 1, 0,          0, Bip, "")                                \
        X (HammerPosition,  "hammer_position",    "Strike Point",    F, -1, 1, 0,          0, Bip, "")                                \
        X (HammerHysteresis,"hammer_hysteresis",  "Felt Damping",    F, 0, 1, 0.5f,        0, Pct, "")                                \
        X (HammerVoiceBass, "hammer_voice_bass",  "Bass Voicing",    F, -1, 1, 0,          0, Bip, "")                                \
        X (HammerVoiceTreble,"hammer_voice_treble","Treble Voicing", F, -1, 1, 0,          0, Bip, "")                                \
        X (HammerFelt,      "hammer_felt",        "Felt Strip",      F, 0, 1, 0,           0, Pct, "")                                \
        X (HammerNoise,     "hammer_noise",       "Hammer Knock",    F, 0, 1, 0.5f,        0, Pct, "")                                \
        X (HammerKeyNoise,  "hammer_key_noise",   "Key Noise",       F, 0, 1, 0.3f,        0, Pct, "")                                \
        /* Strings */                                                                                                               \
        X (StringInharm,    "string_inharm",      "Inharmonicity",   F, 0, 2, 1,           0, Mult, "")                               \
        X (StringUnisons,   "string_unisons",     "Unisons",         C, 0, 3, 0,           0, None, "Model|1|2|3")                    \
        X (StringDetune,    "string_detune",      "Unison Detune",   F, 0, 3, 1,           0, Mult, "")                               \
        X (StringSustain,   "string_sustain",     "Sustain",         F, -1, 1, 0,          0, Bip, "")                                \
        X (StringCoupling,  "string_coupling",    "Bridge Coupling", F, -1, 1, 0,          0, Bip, "")                                \
        X (StringBrightness,"string_brightness",  "Brightness",      F, -1, 1, 0,          0, Bip, "")                                \
        X (StringLength,    "string_length",      "String Length",   F, -1, 1, 0,          0, Bip, "")                                \
        X (StringPhantom,   "string_phantom",     "Longitudinal",    F, 0, 1, 0.5f,        0, Pct, "")                                \
        X (StringDuplex,    "string_duplex",      "Duplex",          F, 0, 1, 0.5f,        0, Pct, "")                                \
        /* Resonance, dampers, pedals */                                                                                            \
        X (SymAmount,       "sym_amount",         "Sympathetic",     F, 0, 1, 0.5f,        0, Pct, "")                                \
        X (DamperStrength,  "damper_strength",    "Damper Strength", F, 0, 1, 0.6f,        0, Pct, "")                                \
        X (DamperNoise,     "damper_noise",       "Damper Noise",    F, 0, 1, 0.3f,        0, Pct, "")                                \
        X (DamperTop,       "damper_top",         "Damper Range",    I, -12, 12, 0,        0, Semi, "")                               \
        X (PedalNoise,      "pedal_noise",        "Pedal Noise",     F, 0, 1, 0.3f,        0, Pct, "")                                \
        X (PedalResonance,  "pedal_resonance",    "Pedal Resonance", F, 0, 1, 0.5f,        0, Pct, "")                                \
        X (PedalUnaCorda,   "pedal_una_corda",    "Una Corda",       F, 0, 1, 0.7f,        0, Pct, "")                                \
        /* Body */                                                                                                                  \
        X (BodySize,        "body_size",          "Board Size",      F, -1, 1, 0,          0, Bip, "")                                \
        X (BodyResonance,   "body_resonance",     "Board Resonance", F, 0, 1, 0.5f,        0, Pct, "")                                \
        X (BodyDamping,     "body_damping",       "Board Damping",   F, -1, 1, 0,          0, Bip, "")                                \
        X (BodyBrightness,  "body_brightness",    "Radiation",       F, -1, 1, 0,          0, Bip, "")                                \
        /* Tuning */                                                                                                                \
        X (TuneA4,          "tune_a4",            "Reference",       F, 415, 466, 440,     0, Hz, "")                                 \
        X (TuneTemperament, "tune_temperament",   "Temperament",     C, 0, 6, 0,           0, None, "Equal|Werckmeister III|Vallotti|Kirnberger III|Meantone|Just|Pythagorean") \
        X (TuneKey,         "tune_key",           "Temperament Key", C, 0, 11, 0,          0, None, "C|C#|D|D#|E|F|F#|G|G#|A|A#|B")   \
        X (TuneStretch,     "tune_stretch",       "Stretch",         F, 0, 2, 1,           0, Mult, "")                               \
        /* Microphones */                                                                                                           \
        X (MicAX,           "mic_a_x",            "Close X",         F, 0, 1, 0.5f,        0, Pct, "")                                \
        X (MicAY,           "mic_a_y",            "Close Distance",  F, 0, 1, 0.2f,        0, Pct, "")                                \
        X (MicALevel,       "mic_a_level",        "Close Level",     F, -40, 6, 0,         0, Db, "")                                 \
        X (MicAWidth,       "mic_a_width",        "Close Width",     F, 0, 1, 0.7f,        0, Pct, "")                                \
        X (MicBX,           "mic_b_x",            "Player X",        F, 0, 1, 0.45f,       0, Pct, "")                                \
        X (MicBY,           "mic_b_y",            "Player Distance", F, 0, 1, 0.5f,        0, Pct, "")                                \
        X (MicBLevel,       "mic_b_level",        "Player Level",    F, -40, 6, -8,        0, Db, "")                                 \
        X (MicBWidth,       "mic_b_width",        "Player Width",    F, 0, 1, 0.5f,        0, Pct, "")                                \
        /* Grand */                                                                                                                 \
        X (GrandLid,        "grand_lid",          "Lid",             C, 0, 3, 0,           0, None, "Open|Half|Closed|Removed")       \
        X (GrandAliquot,    "grand_aliquot",      "Aliquot",         F, 0, 1, 0,           0, Pct, "")                                \
        X (GrandRim,        "grand_rim",          "Rim",             F, 0, 1, 0.5f,        0, Pct, "")                                \
        /* Upright */                                                                                                               \
        X (UprightPanel,    "upright_panel",      "Panel",           C, 0, 2, 0,           0, None, "Closed|Open top|Front off")      \
        X (UprightWall,     "upright_wall",       "Wall",            F, 0, 1, 0.4f,        0, Pct, "")                                \
        X (UprightBox,      "upright_box",        "Cabinet",         F, 0, 1, 0.5f,        0, Pct, "")                                \
        /* Electric: pickups and amp */                                                                                             \
        X (EpPickup,        "ep_pickup",          "Bark",            F, 0, 1, 0.4f,        0, Pct, "")                                \
        X (EpVoicing,       "ep_voicing",         "Pickup Offset",   F, -1, 1, 0,          0, Bip, "")                                \
        X (EpBell,          "ep_bell",            "Bell",            F, 0, 1, 0.5f,        0, Pct, "")                                \
        X (EpTonebar,       "ep_tonebar",         "Tonebar",         F, 0, 1, 0.5f,        0, Pct, "")                                \
        X (EpDecay,         "ep_decay",           "Decay",           F, -1, 1, 0,          0, Bip, "")                                \
        X (EpDrive,         "ep_drive",           "Drive",           F, 0, 1, 0.2f,        0, Pct, "")                                \
        X (EpBass,          "ep_bass",            "Amp Bass",        F, -12, 12, 0,        0, Db, "")                                 \
        X (EpTreble,        "ep_treble",          "Amp Treble",      F, -12, 12, 0,        0, Db, "")                                 \
        X (EpTremMode,      "ep_trem_mode",       "Tremolo",         C, 0, 2, 0,           0, None, "Off|Tremolo|Auto-pan")           \
        X (EpTremRate,      "ep_trem_rate",       "Tremolo Rate",    F, 0.5f, 12, 4,       -1, Hz, "")                                \
        X (EpTremDepth,     "ep_trem_depth",      "Tremolo Depth",   F, 0, 1, 0.5f,        0, Pct, "")                                \
        /* Historic: harpsichord stops */                                                                                           \
        X (HistRegister,    "hist_register",      "Register",        C, 0, 4, 1,           0, None, "8'|8' + 8'|8' + 4'|8' + 8' + 4'|Lute") \
        X (HistBuff,        "hist_buff",          "Buff Stop",       F, 0, 1, 0,           0, Pct, "")                                \
        /* Dance */                                                                                                                 \
        X (DanceOrgan,      "dance_organ",        "Organ",           F, 0, 1, 0,           0, Pct, "")                                \
        X (DanceOrganTone,  "dance_organ_tone",   "Organ Tone",      F, 0, 1, 0.5f,        0, Pct, "")                                \
        X (DanceStab,       "dance_stab",         "Stab",            F, 0, 1, 0,           0, Pct, "")                                \
        X (DancePunch,      "dance_punch",        "Punch",           F, 0, 1, 0,           0, Pct, "")                                \
        X (DanceLofi,       "dance_lofi",         "Tape",            F, 0, 1, 0,           0, Pct, "")                                \
        X (DanceWow,        "dance_wow",          "Wow",             F, 0, 1, 0.3f,        0, Pct, "")                                \
        /* Fantasy */                                                                                                               \
        X (FanRatio,        "fan_ratio",          "Bar Ratios",      F, -1, 1, 0,          0, Bip, "")                                \
        X (FanShimmer,      "fan_shimmer",        "Shimmer",         F, 0, 1, 0.3f,        0, Pct, "")                                \
        X (FanRing,         "fan_ring",           "Ring",            F, -1, 1, 0,          0, Bip, "")                                \
        X (FanPrep,         "fan_prep",           "Preparation",     F, 0, 1, 0.6f,        0, Pct, "")                                \
        X (FanBowPressure,  "fan_bow_pressure",   "Bow Pressure",    F, 0, 1, 0.5f,        0, Pct, "")                                \
        X (FanBowSpeed,     "fan_bow_speed",      "Bow Speed",       F, 0, 1, 0.5f,        0, Pct, "")                                \
        /* Condition */                                                                                                             \
        X (CondDetune,      "cond_detune",        "Out of Tune",     F, 0, 1, 0,           0, Pct, "")                                \
        X (CondWear,        "cond_wear",          "Hammer Wear",     F, 0, 1, 0,           0, Pct, "")                                \
        X (CondDampers,     "cond_dampers",       "Damper Leak",     F, 0, 1, 0,           0, Pct, "")                                \
        X (CondNoise,       "cond_noise",         "Action Noise",    F, 0, 1, 0,           0, Pct, "")                                \
        /* Layer */                                                                                                                 \
        X (LayerLevel,      "layer_level",        "Layer",           F, 0, 1, 0,           0, Pct, "")                                \
        X (LayerType,       "layer_type",         "Layer Sound",     C, 0, 2, 0,           0, None, "Warm Pad|Strings|Air")           \
        X (LayerAttack,     "layer_attack",       "Layer Attack",    F, 0.005f, 4, 0.5f,   0.4f, Time, "")                            \
        X (LayerRelease,    "layer_release",      "Layer Release",   F, 0.02f, 8, 1.5f,    1, Time, "")                               \
        X (LayerTone,       "layer_tone",         "Layer Tone",      F, 0, 1, 0.45f,       0, Pct, "")                                \
        X (LayerDetune,     "layer_detune",       "Layer Detune",    F, 0, 1, 0.4f,        0, Pct, "")                                \
        X (LayerOctave,     "layer_octave",       "Layer Octave",    I, -1, 1, 0,          0, Oct, "")                                \
        /* Play tools */                                                                                                            \
        X (ToolsChord,      "tools_chord",        "Chord",           C, 0, 8, 0,           0, None, "Off|Octave|Fifth|Major|Minor|Maj7|Min7|Sus2|Power") \
        X (ToolsStrum,      "tools_strum",        "Strum",           F, 0, 0.15f, 0,       0.03f, Time, "")                           \
        X (ToolsStrumDir,   "tools_strum_dir",    "Strum Direction", C, 0, 2, 0,           0, None, "Up|Down|Alternate")              \
        /* Effects (shared with every Fjord plugin) */                                                                              \
        FJORD_FX_PARAMS (X)

    enum Id : int { HAMAR_PARAMS (FJORD_PARAM_ENUM) Count };

    using Kind = fjord::base::params::Kind;
    using Format = fjord::base::params::Format;
    using Spec = fjord::base::params::Spec;

    const std::array<Spec, Count>& specs();
    inline const Spec& spec (Id id) { return specs()[(size_t) id]; }
    inline juce::String paramId (Id id) { return spec (id).paramId; }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Plain values of all parameters, read once per audio block.
    using Values = fjord::base::params::ValuesT<Id, Count>;

    class Reader : public fjord::base::params::ReaderT<Id, Count>
    {
    public:
        explicit Reader (juce::AudioProcessorValueTreeState& state) : ReaderT (state, specs()) {}
    };

    // The effect block inside the values (fjord-base effects read it directly)
    inline fjord::base::fx::Values fxValues (const Values& v) { return { v.data() + FxSatOn }; }
    static_assert (FxOutCeiling - FxSatOn + 1 == fjord::base::fx::NumParams, "the effect block must be complete and in order");
}
