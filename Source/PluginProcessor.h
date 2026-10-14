#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Parameters.h"
#include "dsp/Engine.h"
#include "dsp/PlayTools.h"
#include "dsp/Scala.h"
#include "presets/PresetManager.h"
#include <fjord_base/fx/Chain.h>
#include <fjord_base/fx/OrderState.h>
#include <fjord_base/fx/ChainPresets.h>
#include <fjord_base/plugin/PluginHelpers.h>

class HamarProcessor : public juce::AudioProcessor
{
public:
    HamarProcessor();
    ~HamarProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }  // undamped strings, reverb and delay tails

    // Factory presets also show up in the host's preset / program list.
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;                 // on-screen keyboard
    hamar::PresetManager presetManager { apvts, &undoManager };
    fjord::base::fx::ChainPresets chainPresets { apvts };   // effect chains (shared by every Fjord plugin)
    fjord::base::fx::Meters fxMeters;
    fjord::base::fx::OrderState fxOrder { apvts, &undoManager };

    // Visual feedback: the logo dot flashes with every note
    uint32_t getNoteCount() const { return noteCount.load(); }
    float getLastVelocity() const { return lastVelocity.load(); }
    std::array<float, 2> takeOutputPeaks() { return outputPeaks.take(); }

    void requestPanic() { panicRequested.store (true); }

    // The piano itself (the UI reads key levels and pedals from its display)
    const hamar::dsp::Engine& getEngine() const { return engine; }

    // Engine settings from the current parameter values, for drawings on the message thread
    hamar::dsp::EngineSettings currentSettings() const;

    // Scala tuning (message thread): returns an error text, empty when it worked. Stored in the state ("scalaText",
    // "scalaName") and in presets; clearScala goes back to the temperament.
    juce::String loadScala (const juce::String& text, const juce::String& name);
    void clearScala();
    juce::String getScalaName() const { return apvts.state.getProperty ("scalaName").toString(); }

    // The sustain pedal on screen (clicking it holds the pedal until clicked again)
    void setScreenSustain (bool down) { screenSustain.store (down ? 1 : 0); }
    bool getScreenSustain() const { return screenSustainHeld.load(); }
    juce::String getDebugReport() const;

private:
    void processChunk (juce::AudioBuffer<float>&, juce::MidiBuffer&, double ppq, bool playing, double bpm);

    hamar::params::Reader reader { apvts };
    hamar::params::Values values;
    hamar::dsp::EngineSettings makeSettings (const hamar::params::Values&) const;
    mutable hamar::params::Reader uiReader { apvts };
    std::atomic<int> screenSustain { -1 };
    std::atomic<bool> screenSustainHeld { false };
    // The pedals as the host last set them: the engine starts fresh in prepareToPlay, a held pedal must stay held
    float midiSustain = 0.0f, midiSoft = 0.0f;
    bool midiSostenuto = false;
    void applyPedals();
    hamar::dsp::Engine engine;
    hamar::dsp::ChordTool chordTool;
    struct Item { int at; int kind; hamar::dsp::ChordTool::Event note; juce::MidiMessage message; };   // one block's events in time order
    std::array<Item, 512> items {};
    std::array<int, 128> noteChannel {};
    // The Scala tuning as cents per key for all twelve roots, double buffered: the message thread fills the inactive
    // half and then switches; the audio thread reads the active half (-1: no Scala tuning)
    std::array<std::array<std::array<float, hamar::dsp::kNumKeys>, 12>, 2> scaleTables {};
    std::atomic<int> scaleActive { -1 }, scaleVersion { 0 };
    void applyScaleFromState();   // the MIDI channel each note was last played on (for channel pressure / MPE)
    fjord::base::fx::Chain fxChain;
    fjord::base::plugin::BlockSplitter splitter;
    fjord::base::plugin::PeakMeter outputPeaks;
    juce::SmoothedValue<float> masterGain, presetLevel;
    std::array<int, 128> notePositions {};
    std::atomic<uint32_t> noteCount { 0 };
    std::atomic<float> lastVelocity { 0.8f };
    std::atomic<bool> panicRequested { false };
    double currentSampleRate = 44100.0;
    int maxBlockSize = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HamarProcessor)
};
