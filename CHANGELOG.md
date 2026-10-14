# Changelog

All notable changes to Hamar. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]
- First version, built on fjord-base: a physically modelled piano without samples.
- Engine: nonlinear felt hammers, stiff waveguide strings (one to three per key) coupled at a frequency-dependent
  bridge, longitudinal string motion, dampers with half pedalling, sostenuto and una corda, sympathetic strings,
  duplex and aliquot resonance, a soundboard synthesized from plate modes and convolved without latency, two
  microphone pairs, temperaments and inharmonicity-based stretch tuning, four quality levels.
- 36 models in six families: grands, uprights, electric pianos (tines, reeds, piezo strings, clav, with an amp),
  historic keyboards (fortepianos, square piano, harpsichord with registers and buff stop, clavichord), dance pianos
  (organ layer, stab, punch, tape) and fantasy instruments (glass, ice, prepared, toy, celesta, bowed).
- Pad, strings or air layer; chord mode with strum; condition of an old instrument.
- Key pressure (polyphonic aftertouch, channel pressure, MPE): the clavichord's Bebung and the bow pressure of the
  bowed piano.
- Scala tunings (`.scl`): twelve-note scales as a temperament on the chosen key, other scales microtonal over the keys;
  saved with the project and with presets.
- The tuning of coupled strings is corrected per model (measured).
- Pages: Play (the instrument from above), Instrument, Hammer, Strings, Pedals, Mics (a stage around the instrument),
  Tuning, Layer and the shared FX page; locks for every part of the sound.
- 150 factory presets, loudness matched on chords; none drives the clipper in normal playing.
