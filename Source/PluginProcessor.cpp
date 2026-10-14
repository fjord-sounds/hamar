#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace P = hamar::params;

// Name, logo, colours, categories and tags of Hamar: everything the shared UI of fjord-base draws comes from here.
// Every colour can be set per plugin (see fjord-base/docs/design-system.md); familyLight / familyDark derive the
// family palette from one accent.
const fjord::base::Brand& fjord::base::brand()
{
    static const Brand b = []
    {
        Brand x;
        x.name = "Hamar";
        x.logo = "hamar";
        x.tagline = "MODELLED PIANO";
        x.githubUrl = "https://github.com/fjord-sounds/hamar";
        x.settingsFolder = "Fjord/Hamar";
        x.userPresetFolder = "Fjord/Hamar/Presets";
        x.light = familyLight (juce::Colour (0xff8a6526));
        x.dark = familyDark (juce::Colour (0xffe6c88a));
        // Ivory -> brass -> walnut: the keys, the frame and the case
        x.gradientLight = { juce::Colour (0xffb8913f), juce::Colour (0xff8a6526), juce::Colour (0xff5c3a1e) };
        x.gradientDark = { juce::Colour (0xfff4e7c8), juce::Colour (0xffe6c88a), juce::Colour (0xffbf8a4e) };
        x.glow = { juce::Colour (0xfff4e7c8), juce::Colour (0xffe6c88a), juce::Colour (0xffbf8a4e) };
        x.flash = juce::Colour (0xfff4e7c8);
        x.categories = { "Grand", "Upright", "Electric", "Historic", "Dance", "Fantasy" };
        x.tags = { "Warm", "Bright", "Soft", "Intimate", "Concert", "Pop", "Felt", "Vintage", "Lo-Fi", "Dark" };
        return x;
    }();
    return b;
}

namespace
{
    // Version of the saved project state (not the plugin version). Raise it when a change needs the old state
    // to be converted (fjord-base/source/fjord_base/presets/Migration.h helps), and convert it in setStateInformation.
    constexpr int kStateVersion = 1;
}

HamarProcessor::HamarProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "HamarState", P::createLayout())
{
    presetManager.loadScala = [this] (const juce::String& text, const juce::String& name) { loadScala (text, name); };
    presetManager.clearScala = [this] { clearScala(); };
    undoManager.clearUndoHistory();
}

juce::String HamarProcessor::loadScala (const juce::String& text, const juce::String& name)
{
    const auto scale = hamar::dsp::ScalaScale::parse (text, name);
    if (! scale.valid()) return scale.error;
    const int target = scaleActive.load() == 0 ? 1 : 0;
    for (int root = 0; root < 12; ++root)
        scaleTables[(size_t) target][(size_t) root] = scale.keyCents (root);
    scaleActive.store (target);
    scaleVersion.fetch_add (1);
    apvts.state.setProperty ("scalaText", text, nullptr);
    apvts.state.setProperty ("scalaName", scale.name, nullptr);
    return {};
}

void HamarProcessor::clearScala()
{
    scaleActive.store (-1);
    scaleVersion.fetch_add (1);
    apvts.state.removeProperty ("scalaText", nullptr);
    apvts.state.removeProperty ("scalaName", nullptr);
}

void HamarProcessor::applyScaleFromState()
{
    const auto text = apvts.state.getProperty ("scalaText").toString();
    if (text.isNotEmpty()) loadScala (text, apvts.state.getProperty ("scalaName").toString());
    else { scaleActive.store (-1); scaleVersion.fetch_add (1); }
}

void HamarProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    maxBlockSize = std::max (16, samplesPerBlock);
    engine.prepare (sampleRate, maxBlockSize);
    chordTool.reset();
    reader.read (values);
    engine.setSettings (makeSettings (values));
    applyPedals();
    fxChain.prepare (sampleRate, maxBlockSize);
    splitter.prepare (maxBlockSize);
    masterGain.reset (sampleRate, 0.02);
    masterGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (P::paramId (P::MasterGain))->load()));
    presetLevel.reset (sampleRate, 0.02);
    presetLevel.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (P::paramId (P::PresetLevel))->load()));
}

bool HamarProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void HamarProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    if (panicRequested.exchange (false))
    {
        midi.clear();
        engine.panic();
        engine.reset();
        midiSustain = midiSoft = 0.0f;
        midiSostenuto = false;
        chordTool.reset();
        fxChain.reset();
    }

    // Hosts may send bigger blocks than announced: the splitter keeps every chunk within maxBlockSize.
    const auto transport = fjord::base::plugin::Transport::read (*this);
    splitter.process (buffer, midi, [&] (juce::AudioBuffer<float>& part, juce::MidiBuffer& partMidi, int offset)
    {
        processChunk (part, partMidi, transport.ppq + transport.bpm / 60.0 * offset / currentSampleRate, transport.playing, transport.bpm);
    });
}

void HamarProcessor::processChunk (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, double ppq, bool playing, double bpm)
{
    const int n = buffer.getNumSamples();
    reader.read (values);
    engine.setSettings (makeSettings (values));
    if (const int screen = screenSustain.exchange (-1); screen >= 0)
    {
        screenSustainHeld.store (screen == 1);
        applyPedals();
    }
    auto* l = buffer.getWritePointer (0);
    auto* r = buffer.getWritePointer (1);

    // Notes (through the chord tool) and pedals are applied sample-accurately: collect them in time order, then render
    // up to each one and apply it
    std::array<hamar::dsp::ChordTool::Event, 128> notes;
    int numItems = 0, numNoteEvents = 0;
    const int chord = values.i (P::ToolsChord), direction = values.i (P::ToolsStrumDir);
    const float strum = values[P::ToolsStrum];
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int at = juce::jlimit (0, n - 1, meta.samplePosition);
        if (m.isNoteOn())
            chordTool.noteOn (at, m.getNoteNumber(), m.getFloatVelocity(), chord, strum, direction, currentSampleRate,
                              notes.data(), numNoteEvents, (int) notes.size(), m.getChannel());
        else if (m.isNoteOff())
            chordTool.noteOff (at, m.getNoteNumber(), notes.data(), numNoteEvents, (int) notes.size());
        else if (numItems < (int) items.size())
            items[(size_t) numItems++] = { at, 1, {}, m };
    }
    chordTool.collect (n, notes.data(), numNoteEvents, (int) notes.size());
    for (int i = 0; i < numNoteEvents && numItems < (int) items.size(); ++i)
        items[(size_t) numItems++] = { notes[(size_t) i].at, 0, notes[(size_t) i], {} };
    for (int i = 1; i < numItems; ++i)   // insertion sort: stable and allocation free (std::stable_sort may allocate)
        for (int j = i; j > 0 && items[(size_t) j - 1].at > items[(size_t) j].at; --j)
            std::swap (items[(size_t) j - 1], items[(size_t) j]);

    int pos = 0, numNotes = 0;
    for (int i = 0; i < numItems; ++i)
    {
        const auto& item = items[(size_t) i];
        const int at = juce::jlimit (0, n, item.at);
        engine.render (l + pos, r + pos, at - pos);
        pos = at;
        if (item.kind == 0)
        {
            if (item.note.on)
            {
                engine.noteOn (item.note.note, item.note.velocity);
                noteChannel[(size_t) (item.note.note & 127)] = item.note.channel;
                lastVelocity.store (item.note.velocity);
                noteCount.fetch_add (1);
                if (numNotes < (int) notePositions.size()) notePositions[(size_t) numNotes++] = at;
            }
            else
                engine.noteOff (item.note.note, item.note.velocity);
            continue;
        }
        const auto& m = item.message;
        if (m.isController() && m.getControllerNumber() == 64)
        {
            midiSustain = (float) m.getControllerValue() / 127.0f;   // continuous: half pedalling works
            engine.setSustain (std::max (midiSustain, screenSustainHeld.load() ? 1.0f : 0.0f));
        }
        else if (m.isController() && m.getControllerNumber() == 66) { midiSostenuto = m.getControllerValue() >= 64; engine.setSostenuto (midiSostenuto); }
        else if (m.isController() && m.getControllerNumber() == 67) { midiSoft = (float) m.getControllerValue() / 127.0f; engine.setSoft (midiSoft); }
        else if (m.isAftertouch())                    engine.setPressure (m.getNoteNumber(), (float) m.getAfterTouchValue() / 127.0f);
        else if (m.isChannelPressure())
        {
            // Channel pressure reaches the notes of its channel (MPE: one note per channel; otherwise every note)
            for (int k = 0; k < 128; ++k)
                if (noteChannel[(size_t) k] == m.getChannel()) engine.setPressure (k, (float) m.getChannelPressureValue() / 127.0f);
        }
        else if (m.isAllSoundOff())                   { engine.panic(); midiSustain = midiSoft = 0.0f; midiSostenuto = false; }
        else if (m.isAllNotesOff())                   engine.allNotesOff();
    }
    engine.render (l + pos, r + pos, n - pos);

    presetLevel.setTargetValue (juce::Decibels::decibelsToGain (values[P::PresetLevel]));
    presetLevel.applyGain (buffer, n);

    // The shared effects, in the user's order; the clipper (Out) always comes last
    fjord::base::fx::Context ctx;
    ctx.bpm = bpm;
    ctx.ppq = ppq;
    ctx.playing = playing;
    ctx.triggers = notePositions.data();   // the filter envelope starts with each note
    ctx.numTriggers = numNotes;
    fxChain.process ({ l, r, n }, P::fxValues (values), fxOrder.get(), ctx, &fxMeters);

    masterGain.setTargetValue (juce::Decibels::decibelsToGain (values[P::MasterGain]));
    masterGain.applyGain (buffer, n);
    outputPeaks.push (buffer, 0, n);
}

// Plain engine settings from the parameter values (once per block; the engine only recomputes what changed)
hamar::dsp::EngineSettings HamarProcessor::makeSettings (const P::Values& v) const
{
    hamar::dsp::EngineSettings s;
    auto& in = s.instrument;
    in.model = v.i (P::Model);
    in.quality = v.i (P::Quality);
    in.unisons = v.i (P::StringUnisons);
    in.inharm = v[P::StringInharm];
    in.length = v[P::StringLength];
    in.detune = v[P::StringDetune];
    in.sustain = v[P::StringSustain];
    in.coupling = v[P::StringCoupling];
    in.brightness = v[P::StringBrightness];
    in.hardness = v[P::HammerHardness];
    in.mass = v[P::HammerMass];
    in.position = v[P::HammerPosition];
    in.hysteresis = v[P::HammerHysteresis];
    in.voiceBass = v[P::HammerVoiceBass];
    in.voiceTreble = v[P::HammerVoiceTreble];
    in.felt = v[P::HammerFelt];
    in.phantom = v[P::StringPhantom];
    in.duplex = v[P::StringDuplex];
    in.aliquot = v[P::GrandAliquot];
    in.damperStrength = v[P::DamperStrength];
    in.damperTop = v.i (P::DamperTop);
    in.registerStop = v.i (P::HistRegister);
    in.buff = v[P::HistBuff];
    in.stab = v[P::DanceStab];
    in.prep = v[P::FanPrep];
    in.bowPressure = v[P::FanBowPressure];
    in.bowSpeed = v[P::FanBowSpeed];
    in.outOfTune = v[P::CondDetune];
    in.wear = v[P::CondWear];
    in.damperLeak = v[P::CondDampers];
    in.tuning.a4 = v[P::TuneA4];
    in.tuning.temperament = v.i (P::TuneTemperament);
    in.tuning.root = v.i (P::TuneKey);
    in.tuning.stretch = v[P::TuneStretch];
    if (const int active = scaleActive.load(); active >= 0)
    {
        in.tuning.custom = scaleTables[(size_t) active][(size_t) std::clamp (in.tuning.root, 0, 11)].data();
        in.tuning.customVersion = scaleVersion.load();
    }

    auto& b = s.board;
    b.model = in.model;
    b.quality = in.quality;
    b.size = v[P::BodySize];
    b.resonance = v[P::BodyResonance];
    b.damping = v[P::BodyDamping];
    b.brightness = v[P::BodyBrightness];
    b.lid = v.i (P::GrandLid);
    b.panel = v.i (P::UprightPanel);
    b.rim = v[P::GrandRim];
    b.wall = v[P::UprightWall];
    b.box = v[P::UprightBox];
    auto mic = [&v] (P::Id x, P::Id y, P::Id level, P::Id width)
    {
        const float db = v[level];
        return hamar::dsp::MicSettings { v[x], v[y], db <= -39.9f ? 0.0f : juce::Decibels::decibelsToGain (db), v[width] };
    };
    b.micA = mic (P::MicAX, P::MicAY, P::MicALevel, P::MicAWidth);
    b.micB = mic (P::MicBX, P::MicBY, P::MicBLevel, P::MicBWidth);

    auto& bars = s.bars;
    bars.pickup = v[P::EpPickup];
    bars.voicing = v[P::EpVoicing];
    bars.bell = v[P::EpBell];
    bars.tonebar = v[P::EpTonebar];
    bars.decay = v[P::EpDecay];
    bars.hardness = v[P::HammerHardness];
    bars.ratio = v[P::FanRatio];
    bars.shimmer = v[P::FanShimmer];
    bars.ring = v[P::FanRing];
    auto& amp = s.amp;
    amp.drive = v[P::EpDrive];
    amp.bass = v[P::EpBass];
    amp.treble = v[P::EpTreble];
    amp.tremMode = v.i (P::EpTremMode);
    amp.tremRate = v[P::EpTremRate];
    amp.tremDepth = v[P::EpTremDepth];

    auto& dance = s.dance;
    dance.organ = v[P::DanceOrgan];
    dance.organTone = v[P::DanceOrganTone];
    dance.stab = v[P::DanceStab];
    dance.punch = v[P::DancePunch];
    dance.lofi = v[P::DanceLofi];
    dance.wow = v[P::DanceWow];

    auto& layer = s.layer;
    layer.level = v[P::LayerLevel];
    layer.type = v.i (P::LayerType);
    layer.attack = v[P::LayerAttack];
    layer.release = v[P::LayerRelease];
    layer.tone = v[P::LayerTone];
    layer.detune = v[P::LayerDetune];
    layer.octave = v.i (P::LayerOctave);

    auto& p = s.play;
    p.velocityCurve = v[P::PlayVelCurve];
    p.dynamics = v[P::PlayDynamics];
    p.releaseVelocity = v[P::PlayReleaseVel];
    p.transpose = v.i (P::PlayTranspose);
    p.hammerNoise = v[P::HammerNoise];
    p.keyNoise = v[P::HammerKeyNoise];
    p.damperNoise = v[P::DamperNoise];
    p.pedalNoise = v[P::PedalNoise];
    p.pedalResonance = v[P::PedalResonance];
    p.unaCordaDepth = v[P::PedalUnaCorda];
    p.sympathetic = v[P::SymAmount];
    p.actionNoise = v[P::CondNoise];
    return s;
}

// The pedals as the host and the screen hold them (after prepareToPlay the engine starts with its pedals up)
void HamarProcessor::applyPedals()
{
    engine.setSustain (std::max (midiSustain, screenSustainHeld.load() ? 1.0f : 0.0f));
    engine.setSostenuto (midiSostenuto);
    engine.setSoft (midiSoft);
}

hamar::dsp::EngineSettings HamarProcessor::currentSettings() const
{
    P::Values v;
    uiReader.read (v);
    auto s = makeSettings (v);
    s.instrument.sampleRate = currentSampleRate;
    return s;
}

int HamarProcessor::getNumPrograms()
{
    int n = 0;
    for (const auto& p : presetManager.getPresets())
        n += p.isFactory ? 1 : 0;
    return std::max (1, n);
}

int HamarProcessor::getCurrentProgram() { return std::max (0, presetManager.getCurrentIndex()); }

void HamarProcessor::setCurrentProgram (int index)
{
    if (index != presetManager.getCurrentIndex() && index < getNumPrograms())
        presetManager.load (index);
}

const juce::String HamarProcessor::getProgramName (int index)
{
    const auto& list = presetManager.getPresets();
    return juce::isPositiveAndBelow (index, (int) list.size()) ? list[(size_t) index].name : juce::String ("Init");
}

juce::String HamarProcessor::getDebugReport() const
{
    fjord::base::plugin::DebugReport report (*this, const_cast<hamar::PresetManager*> (&presetManager), maxBlockSize);
    report.add ("FX order", apvts.state.getProperty ("fxOrder").toString());
    return report.finish();
}

juce::AudioProcessorEditor* HamarProcessor::createEditor() { return new HamarEditor (*this); }

void HamarProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    apvts.state.setProperty ("stateVersion", kStateVersion, nullptr);
    apvts.state.setProperty ("darkTheme", fjord::base::ui::Theme::get().isDark(), nullptr);
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void HamarProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            presetManager.restoreFromState();
            applyScaleFromState();
            fxOrder.sync();
            undoManager.clearUndoHistory();

            if (apvts.state.hasProperty ("darkTheme"))
            {
                const bool dark = apvts.state.getProperty ("darkTheme");
                juce::MessageManager::callAsync ([dark] { fjord::base::ui::Theme::get().setDark (dark); });
            }
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HamarProcessor();
}
