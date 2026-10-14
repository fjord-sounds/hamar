# Contributing to Hamar

Thanks for your interest! Ideas that make Hamar better are very welcome.

By contributing you agree that your work is licensed under the [AGPL-3.0](LICENSE), like the rest of the project.

## Build

Hamar is built on [fjord-base](https://github.com/fjord-sounds/fjord-base) (shared UI, effects, build helpers).
Clone it next to this repository; without it CMake downloads the pinned version (`FJORD_BASE_TAG`).

```bash
git clone https://github.com/fjord-sounds/fjord-base.git
git clone https://github.com/fjord-sounds/hamar.git && cd hamar
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFJORD_COPY_PLUGIN=OFF
cmake --build build -j8
cmake --build build --target HamarRenderTest -j8
./build/HamarRenderTest_artefacts/Release/HamarRenderTest build/renders      # must print ALL PASSED
```

`./build.sh` is the macOS shortcut that also installs the plugins and runs `auval`. Changes to the effects, the
header, the preset browser or the controls belong in fjord-base – see its
[docs](https://github.com/fjord-sounds/fjord-base/tree/main/docs).

## Rules that keep old projects and presets working

- All parameters are listed once in `Source/Parameters.h`. IDs never change; new parameters go to the **end** of the
  list and need a neutral default.
- Choice lists are append-only – that includes the list of models (`Source/dsp/Models.h`, `HAMAR_MODEL_NAMES`),
  temperaments, registers and the chord types. Presets and the project state store plain values.
- Parameters are offsets around a model's own voicing: a new model brings its numbers in `Models.h`, not new defaults.
- If a change needs old project state to be converted, raise `kStateVersion` and convert it in `setStateInformation`
  (`fjord_base/presets/Migration.h` helps).
- The audio thread never allocates or locks.

## Presets

Factory presets live in `presets/Factory/<Category>/*.hamar` (one idea each, the six families as categories) and are
loudness matched to each other (`HamarRenderTest --loudness`). They never set the master volume or the quality level.

## Pull requests

- Keep them focused. Say what changes and why.
- UI changes: attach screenshots in the light **and** the dark theme (`HamarRenderTest --screenshot …`).
- Update the README and [CHANGELOG.md](CHANGELOG.md) (section "Unreleased"), and refresh the README images
  (`tools/screenshots/shoot.sh`) when the UI or the presets change.
- Everything in the repository is English.
