// Offline test and tool binary of Hamar: renders the piano without a DAW, measures it and checks it.
//
//   HamarRenderTest [out dir] [--sr 96000]                         all tests (must print ALL PASSED)
//   HamarRenderTest --single <preset|init> <note> <out.wav> [velocity 1-127] [hold s] [length s] [param=value,...]
//   HamarRenderTest --demo <preset|init> <out.wav> [param=value,...]   a short piece with pedal, chords and a melody
//   HamarRenderTest --analyse <note> [velocity] [param=value,...]   partials against the stiff-string formula, decay
//   HamarRenderTest --compare <recording> <note> <velocity> [param=value,...]   measures a reference recording against Hamar
//   HamarRenderTest --midi <file.mid> <preset|init>[:p=v,...] <out.wav>   renders a MIDI file
//   HamarRenderTest --midi-scan <file.mid>                          every preset with a MIDI file: peaks, clipping, treble
//   HamarRenderTest --cpu                                          real-time factor of every quality level
//   HamarRenderTest --screenshot <page> <preset|init>[:param=value,...] <light|dark> <out.png> [fx index]
//                   pages: play instrument hammer strings pedals mics tuning fx browser about
//   HamarRenderTest --icon <png>                                   the app icon (resources/icon.png)
//   HamarRenderTest --param-info <tsv>                             id, kind, min, max, default per parameter
//   HamarRenderTest --loudness <json>                              RMS and peak of every factory preset
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace P = hamar::params;

static int failures = 0;
static double testSampleRate = 48000.0;

static void check (bool ok, const juce::String& what)
{
    if (! ok)
    {
        ++failures;
        printf ("FAIL  %s\n", what.toRawUTF8());
    }
}

static void setParam (HamarProcessor& proc, const juce::String& id, float value)
{
    if (auto* p = proc.apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    else
        printf ("Unknown parameter: %s\n", id.toRawUTF8());
}

// "hammer_hardness=0.5,model=3"
static void applyParams (HamarProcessor& proc, const juce::String& list)
{
    for (const auto& item : juce::StringArray::fromTokens (list, ",", ""))
        if (item.contains ("="))
            setParam (proc, item.upToFirstOccurrenceOf ("=", false, false).trim(), item.fromFirstOccurrenceOf ("=", false, false).getFloatValue());
}

static int findPreset (HamarProcessor& proc, const juce::String& name)
{
    const auto& list = proc.presetManager.getPresets();
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].name == name) return (int) i;
    return -1;
}

static bool loadPreset (HamarProcessor& proc, const juce::String& name)
{
    if (name == "init") return true;
    const int index = findPreset (proc, name);
    if (index < 0) { printf ("Preset not found: %s\n", name.toRawUTF8()); return false; }
    proc.presetManager.load (index);
    return true;
}

// A list of MIDI events at times in seconds
struct Event
{
    double time;
    juce::MidiMessage message;
};

static juce::AudioBuffer<float> render (HamarProcessor& proc, std::vector<Event> events, double seconds, int block = 512)
{
    proc.setPlayConfigDetails (0, 2, testSampleRate, block);
    proc.prepareToPlay (testSampleRate, block);
    std::sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.time < b.time; });
    const int total = (int) (seconds * testSampleRate);
    juce::AudioBuffer<float> out (2, total), buffer (2, block);
    size_t next = 0;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        juce::MidiBuffer midi;
        while (next < events.size() && (int) (events[next].time * testSampleRate) < pos + n)
        {
            midi.addEvent (events[next].message, std::max (0, (int) (events[next].time * testSampleRate) - pos));
            ++next;
        }
        buffer.setSize (2, n, false, false, true);
        buffer.clear();
        proc.processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, buffer, ch, 0, n);
    }
    return out;
}

static std::vector<Event> note (int number, int velocity, double start, double length)
{
    return { { start, juce::MidiMessage::noteOn (1, number, (juce::uint8) velocity) },
             { start + length, juce::MidiMessage::noteOff (1, number, (juce::uint8) 64) } };
}

static void append (std::vector<Event>& to, const std::vector<Event>& more) { to.insert (to.end(), more.begin(), more.end()); }

static Event pedal (double time, int value) { return { time, juce::MidiMessage::controllerEvent (1, 64, value) }; }

static bool writeWav (const juce::AudioBuffer<float>& audio, const juce::File& file)
{
    juce::WavAudioFormat wav;
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    if (auto stream = file.createOutputStream())
        if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (stream.get(), testSampleRate, 2, 24, {}, 0)))
        {
            stream.release();
            writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
            return true;
        }
    return false;
}

struct Stats
{
    float peak = 0.0f, rms = 0.0f;
    bool finite = true;
};

static Stats stats (const juce::AudioBuffer<float>& a, double from, double to)
{
    Stats s;
    double sum = 0.0;
    int count = 0;
    const int i0 = std::clamp ((int) (from * testSampleRate), 0, a.getNumSamples()), i1 = std::clamp ((int) (to * testSampleRate), 0, a.getNumSamples());
    for (int ch = 0; ch < 2; ++ch)
        for (int i = i0; i < i1; ++i)
        {
            const float x = a.getSample (ch, i);
            s.finite &= std::isfinite (x);
            s.peak = std::max (s.peak, std::abs (x));
            sum += (double) x * x;
            ++count;
        }
    s.rms = (float) std::sqrt (sum / std::max (1, count));
    return s;
}

static float db (float x) { return juce::Decibels::gainToDecibels (x, -140.0f); }

// Magnitude spectrum (mono sum) of 'seconds' from 'from', Hann window, zero padded to a power of two
struct Spectrum
{
    std::vector<float> mag;
    double binHz = 1.0;

    // Strongest peak within +-cents around f (parabolic interpolation); returns the frequency
    double peakNear (double f, double cents, float* level = nullptr) const
    {
        const int lo = std::max (1, (int) (f * std::exp2 (-cents / 1200.0) / binHz));
        const int hi = std::min ((int) mag.size() - 2, (int) (f * std::exp2 (cents / 1200.0) / binHz) + 1);
        int best = lo;
        for (int i = lo; i <= hi; ++i) if (mag[(size_t) i] > mag[(size_t) best]) best = i;
        const double a = std::log (mag[(size_t) best - 1] + 1e-12), b = std::log (mag[(size_t) best] + 1e-12), c = std::log (mag[(size_t) best + 1] + 1e-12);
        const double offset = 0.5 * (a - c) / (a - 2.0 * b + c);
        if (level != nullptr) *level = mag[(size_t) best];
        return (best + (std::isfinite (offset) ? offset : 0.0)) * binHz;
    }

    double centroid (double maxHz = 16000.0) const
    {
        double num = 0.0, den = 0.0;
        for (size_t i = 1; i < mag.size() && i * binHz < maxHz; ++i) { num += i * binHz * mag[i]; den += mag[i]; }
        return den > 0.0 ? num / den : 0.0;
    }
};

static Spectrum spectrum (const juce::AudioBuffer<float>& a, double from, double seconds)
{
    const int length = (int) (seconds * testSampleRate);
    int order = 1;
    while ((1 << order) < length * 2) ++order;
    juce::dsp::FFT fft (order);
    std::vector<float> data ((size_t) (2 << order), 0.0f);
    const int start = (int) (from * testSampleRate);
    for (int i = 0; i < length && start + i < a.getNumSamples(); ++i)
    {
        const float w = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (length - 1));
        data[(size_t) i] = w * (a.getSample (0, start + i) + a.getSample (1, start + i));
    }
    fft.performFrequencyOnlyForwardTransform (data.data());
    Spectrum s;
    s.mag.assign (data.begin(), data.begin() + (1 << order) / 2);
    s.binHz = testSampleRate / (double) (1 << order);
    return s;
}

static const hamar::dsp::KeyPhysics& physics (HamarProcessor& proc, int key)
{
    proc.setPlayConfigDetails (0, 2, testSampleRate, 512);
    proc.prepareToPlay (testSampleRate, 512);
    return proc.getEngine().getTable()[key];
}

// Partials of one note against n f1 sqrt ((1 + B n^2) / (1 + B)); returns the largest deviation in cents
static double partialError (HamarProcessor& proc, int key, int velocity, int maxPartial, bool print)
{
    const auto k = physics (proc, key);
    const auto audio = render (proc, note (key, velocity, 0.0, 3.0), 2.0);
    const auto spec = spectrum (audio, 0.15, 1.6);
    double worst = 0.0;
    if (print) printf ("key %d  f1 %.3f Hz  B %.2e  strings %d  dispersion %d x %.3f\n", key, k.f1, k.b, k.strings, k.dispM, k.dispA);
    if (print) printf ("  after %.2f prompt %.2f damped %.2f hfAfter %.3f hf %.0f z %.2f K %.3g p %.2f m %.4f strike %.3f rattle %.2f bowed %d plucked %d\n",
                       k.after, k.prompt, k.damped, k.hfAfter, k.hfFreq, k.z, k.hammerK, k.hammerP, k.hammerMass, k.strike, k.rattle, (int) k.bowed, (int) k.plucked);
    for (int n = 1; n <= maxPartial; ++n)
    {
        const double expected = n * k.f1 * std::sqrt ((1.0 + k.b * n * n) / (1.0 + k.b));
        if (expected > 0.4 * testSampleRate || expected > 12000.0) break;
        float level = 0.0f;
        const double found = spec.peakNear (expected, 35.0, &level);
        const double cents = 1200.0 * std::log2 (found / expected);
        const double harmonic = 1200.0 * std::log2 (expected / (n * k.f1));
        if (print) printf ("  partial %2d  %9.2f Hz  expected %9.2f  %+6.2f cents  (stretch %+6.1f)  %6.1f dB\n",
                           n, found, expected, cents, harmonic, db (level));
        if (db (level) > -60.0f) worst = std::max (worst, std::abs (cents));
    }
    return worst;
}

static int analyse (int key, int velocity, const juce::String& extra)
{
    HamarProcessor proc;
    applyParams (proc, extra);
    partialError (proc, key, velocity, 16, true);
    const auto audio = render (proc, note (key, velocity, 0.0, 8.0), 8.0);
    printf ("  level:");
    for (double t : { 0.05, 0.25, 0.5, 1.0, 2.0, 4.0, 7.0 })
        printf ("  %.2fs %.1f dB", t, db (stats (audio, t, t + 0.1).rms));
    printf ("\n  centroid %.0f Hz (0.1-0.6 s), %.0f Hz (2-3 s)\n", spectrum (audio, 0.1, 0.5).centroid(), spectrum (audio, 2.0, 1.0).centroid());
    return 0;
}

static std::vector<Event> demoScore()
{
    // Slow pop progression with pedal, then a melody over broken chords, then a soft ending
    std::vector<Event> e;
    const int chords[4][4] = { { 48, 55, 60, 64 }, { 45, 52, 57, 60 }, { 41, 48, 53, 57 }, { 43, 50, 55, 59 } };
    double t = 0.0;
    for (int c = 0; c < 4; ++c)
    {
        e.push_back (pedal (t + 0.02, 127));
        for (int i = 0; i < 4; ++i) append (e, note (chords[c][i], 70 + 6 * i, t + 0.012 * i, 1.6));
        e.push_back (pedal (t + 1.9, 0));
        t += 2.0;
    }
    const int melody[] = { 72, 71, 69, 67, 69, 71, 72, 76 };
    for (int i = 0; i < 8; ++i)
    {
        if (i % 2 == 0) { e.push_back (pedal (t + 0.02, 127)); e.push_back (pedal (t + 0.98, 0)); }
        append (e, note (melody[i], 92 - (i % 3) * 8, t, 0.45));
        append (e, note (chords[(i / 2) % 4][0] - 12, 60, t, 0.9));
        append (e, note (chords[(i / 2) % 4][2], 48, t + 0.25, 0.2));
        t += 0.5;
    }
    e.push_back (pedal (t, 127));
    for (int i = 0; i < 4; ++i) append (e, note (chords[0][i] + 12, 40, t + 0.02 * i, 3.0));
    append (e, note (36, 55, t, 3.0));
    e.push_back (pedal (t + 4.5, 0));
    return e;
}

// Measures attack envelope, spectral balance over time, partials, noise between the partials and partial decay of a
// mono signal (onset aligned). Used to compare Hamar with reference recordings of real pianos.
static void describe (const char* label, const std::vector<float>& x, double sr, double f0)
{
    int onset = 0;
    float peak = 0.0f;
    for (float v : x) peak = std::max (peak, std::abs (v));
    while (onset < (int) x.size() && std::abs (x[(size_t) onset]) < 0.02f * peak) ++onset;
    onset = std::max (0, onset - (int) (0.002 * sr));
    auto rmsDb = [&] (double from, double to)
    {
        double sum = 0.0;
        int n = 0;
        for (int i = onset + (int) (from * sr); i < onset + (int) (to * sr) && i < (int) x.size(); ++i) { sum += (double) x[(size_t) i] * x[(size_t) i]; ++n; }
        return db ((float) std::sqrt (sum / std::max (1, n)) / peak);
    };
    printf ("%s\n  envelope (dB re peak):", label);
    for (double t : { 0.0, 0.002, 0.004, 0.008, 0.015, 0.03, 0.06, 0.12, 0.25, 0.5, 1.0, 2.0, 3.0 })
        printf (" %.3f:%.0f", t, rmsDb (t, t + std::max (0.002, t * 0.2)));
    printf ("\n");

    // Spectra of windows after the onset
    juce::AudioBuffer<float> buf (2, (int) x.size());
    for (int i = 0; i < (int) x.size(); ++i) { buf.setSample (0, i, 0.5f * x[(size_t) i]); buf.setSample (1, i, 0.5f * x[(size_t) i]); }
    const double keep = testSampleRate;
    testSampleRate = sr;
    for (auto w : { std::pair<double, double> { 0.0, 0.05 }, { 0.1, 0.3 }, { 1.0, 1.0 } })
    {
        const auto sp = spectrum (buf, onset / sr + w.first, w.second);
        float l1 = 0.0f;
        sp.peakNear (f0, 40.0, &l1);
        printf ("  %.2f-%.2fs centroid %5.0f Hz  partials (dB re 1st):", w.first, w.first + w.second, sp.centroid());
        double harmonicEnergy = 0.0, total = 0.0;
        for (int n = 1; n <= 20; ++n)
        {
            float l = 0.0f;
            sp.peakNear (f0 * n * std::sqrt (1.0 + 0.0004 * n * n), 60.0, &l);
            if (n <= 14) printf (" %.0f", db (l / l1));
        }
        // Energy near the partials vs between them (noise, phantom partials, board) up to 6 kHz
        for (size_t i = 1; i < sp.mag.size() && i * sp.binHz < 6000.0; ++i)
        {
            const double f = i * sp.binHz, e = (double) sp.mag[i] * sp.mag[i];
            const double nf = f / f0, frac = std::abs (nf - std::round (nf));
            total += e;
            if (frac < 0.12 || nf < 0.5) harmonicEnergy += e;
        }
        printf ("  between partials %.1f dB\n", 10.0 * std::log10 (std::max (1e-12, (total - harmonicEnergy) / std::max (1e-12, total))));
    }
    testSampleRate = keep;
}

static int compare (const juce::File& reference, int key, int velocity, const juce::String& extra)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (reference));
    if (reader == nullptr) { printf ("Cannot read %s\n", reference.getFullPathName().toRawUTF8()); return 1; }
    const int len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 4.0));
    juce::AudioBuffer<float> ref ((int) reader->numChannels, len);
    reader->read (&ref, 0, len, 0, true, true);
    std::vector<float> a ((size_t) len);
    for (int i = 0; i < len; ++i)
        for (int c = 0; c < ref.getNumChannels(); ++c) a[(size_t) i] += ref.getSample (c, i);
    const double f0 = 440.0 * std::exp2 ((key - 69) / 12.0);
    describe ("reference", a, reader->sampleRate, f0);

    HamarProcessor proc;
    applyParams (proc, extra);
    const double keep = testSampleRate;
    testSampleRate = reader->sampleRate;
    const auto audio = render (proc, note (key, velocity, 0.1, 4.0), 4.0);
    std::vector<float> b ((size_t) audio.getNumSamples());
    for (int i = 0; i < audio.getNumSamples(); ++i) b[(size_t) i] = audio.getSample (0, i) + audio.getSample (1, i);
    describe ("hamar", b, testSampleRate, f0);
    testSampleRate = keep;
    return 0;
}

// A MIDI file as a list of events (seconds), all tracks merged
static std::vector<Event> readMidi (const juce::File& f, double& length)
{
    std::vector<Event> events;
    length = 0.0;
    juce::FileInputStream in (f);
    juce::MidiFile midi;
    if (! in.openedOk() || ! midi.readFrom (in)) { printf ("Cannot read %s\n", f.getFullPathName().toRawUTF8()); return events; }
    midi.convertTimestampTicksToSeconds();
    for (int t = 0; t < midi.getNumTracks(); ++t)
        for (const auto* e : *midi.getTrack (t))
            if (e->message.isNoteOnOrOff() || e->message.isController())
            {
                events.push_back ({ e->message.getTimeStamp(), e->message });
                length = std::max (length, e->message.getTimeStamp());
            }
    return events;
}

static int renderMidi (const juce::File& file, const juce::String& preset, const juce::File& out)
{
    double length = 0.0;
    const auto events = readMidi (file, length);
    if (events.empty()) return 1;
    HamarProcessor proc;
    if (! loadPreset (proc, preset.upToFirstOccurrenceOf (":", false, false))) return 1;
    applyParams (proc, preset.fromFirstOccurrenceOf (":", false, false));
    const auto audio = render (proc, events, length + 3.0);
    const auto s = stats (audio, 0.0, length + 3.0);
    printf ("peak %.3f  rms %.4f  (%.1f s)\n", s.peak, s.rms, length);
    return writeWav (audio, out) ? 0 : 1;
}

// Plays a MIDI file with every factory preset: the peak before the clipper, how much the clipper works, and how much
// of the sound sits above 6 kHz where crackle and rattle show up (relative to the same preset playing softly)
static int midiScan (const juce::File& file)
{
    double length = 0.0;
    const auto events = readMidi (file, length);
    if (events.empty()) return 1;
    hamar::PresetManager::ignoreUserPresets = true;
    HamarProcessor proc;
    const auto& list = proc.presetManager.getPresets();
    printf ("%-26s %8s %8s %8s %9s\n", "preset", "raw peak", "clipped", "rms", "hf ratio");
    for (int i = 0; i < (int) list.size(); ++i)
    {
        HamarProcessor p;
        p.presetManager.load (i);
        setParam (p, "fx_out_on", 0.0f);
        const auto raw = render (p, events, length + 2.0);
        const auto s = stats (raw, 0.0, length + 2.0);
        int over = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int k = 0; k < raw.getNumSamples(); ++k) over += std::abs (raw.getSample (ch, k)) > 0.7f ? 1 : 0;
        const auto sp = spectrum (raw, 0.0, std::min (8.0, length + 1.0));
        double hf = 0.0, total = 0.0;
        for (size_t b = 1; b < sp.mag.size(); ++b)
        {
            const double e = (double) sp.mag[b] * sp.mag[b];
            total += e;
            if (b * sp.binHz > 6000.0) hf += e;
        }
        printf ("%-26s %8.3f %7.2f%% %8.4f %8.1f dB\n", list[(size_t) i].name.toRawUTF8(), s.peak,
                100.0 * over / (2.0 * raw.getNumSamples()), s.rms, 10.0 * std::log10 (std::max (1.0e-12, hf / std::max (1.0e-12, total))));
    }
    return 0;
}

// Sustain pedal check over presets: a short note with and without the pedal, level 2 s later
static int pedalCheck()
{
    hamar::PresetManager::ignoreUserPresets = true;
    HamarProcessor proc;
    const auto& list = proc.presetManager.getPresets();
    for (int i = 0; i < (int) list.size(); i += 7)
    {
        auto level = [&] (bool pedalDown)
        {
            HamarProcessor p;
            p.presetManager.load (i);
            std::vector<Event> e;
            if (pedalDown) e.push_back (pedal (0.0, 127));
            append (e, note (60, 90, 0.01, 0.3));
            return stats (render (p, e, 2.5), 2.0, 2.5).rms;
        };
        printf ("%-26s without %6.1f dB  with %6.1f dB\n", list[(size_t) i].name.toRawUTF8(), db (level (false)), db (level (true)));
    }
    return 0;
}

static int single (const juce::String& preset, int key, const juce::File& out, int velocity, double hold, double length, const juce::String& extra)
{
    HamarProcessor proc;
    if (! loadPreset (proc, preset)) return 1;
    applyParams (proc, extra);
    const auto audio = render (proc, note (key, velocity, 0.0, hold), length);
    const auto s = stats (audio, 0.0, length);
    printf ("peak %.3f  rms %.4f\n", s.peak, s.rms);
    return writeWav (audio, out) ? 0 : 1;
}

static int demo (const juce::String& preset, const juce::File& out, const juce::String& extra)
{
    HamarProcessor proc;
    if (! loadPreset (proc, preset)) return 1;
    applyParams (proc, extra);
    const auto audio = render (proc, demoScore(), 19.0);
    const auto s = stats (audio, 0.0, 19.0);
    printf ("peak %.3f  rms %.4f\n", s.peak, s.rms);
    return writeWav (audio, out) ? 0 : 1;
}

static int cpu()
{
    for (int quality = 0; quality < 4; ++quality)
    {
        HamarProcessor proc;
        setParam (proc, "quality", (float) quality);
        // Busy playing with the pedal down: broken chords over five octaves
        std::vector<Event> e;
        e.push_back (pedal (0.0, 127));
        for (int i = 0; i < 80; ++i)
            append (e, note (28 + (i * 7) % 64, 60 + (i * 13) % 50, i * 0.1, 0.3));
        const auto start = juce::Time::getMillisecondCounterHiRes();
        render (proc, e, 10.0, 256);
        const double ms = juce::Time::getMillisecondCounterHiRes() - start;
        printf ("%-9s %6.1f %% of one core (%d keys sounding at the end)\n",
                proc.apvts.getParameter ("quality")->getCurrentValueAsText().toRawUTF8(), ms / 100.0, proc.getEngine().activeVoices());
    }
    return 0;
}

static void soundTests()
{
    HamarProcessor proc;
    const auto audio = render (proc, note (60, 100, 0.0, 0.5), 3.0);
    const auto all = stats (audio, 0.0, 3.0), tail = stats (audio, 2.7, 3.0);
    printf ("init          peak %.3f  rms %.4f  tail %.1f dB\n", all.peak, all.rms, db (tail.peak));
    check (all.finite, "init: finite output");
    check (all.peak > 0.02f && all.peak <= 1.0f, "init: level");
    check (tail.peak < all.peak * 0.003f, "init: the damper stops the note");
}

static void tuningTests()
{
    // The first partial sits on the tuned frequency, the upper ones follow the stiff-string formula
    for (int key : { 33, 45, 60, 72, 84, 96 })
    {
        HamarProcessor proc;
        const auto k = physics (proc, key);
        const auto audio = render (proc, note (key, 90, 0.0, 3.0), 2.0);
        const double found = spectrum (audio, 0.2, 1.6).peakNear (k.f1, 30.0);
        const double cents = 1200.0 * std::log2 (found / k.f1);
        check (std::abs (cents) < 2.0, "tuning of key " + juce::String (key) + ": " + juce::String (cents, 2) + " cents");
    }
    for (int key : { 36, 48, 60, 72 })
    {
        HamarProcessor proc;
        const double worst = partialError (proc, key, 90, 8, false);   // the strong partials (the fit weights them most)
        check (worst < 6.0, "partials of key " + juce::String (key) + " off by " + juce::String (worst, 1) + " cents");
    }
}

static void touchTests()
{
    // A harder blow is louder and brighter (the felt gets stiffer as it is compressed)
    HamarProcessor soft, hard;
    const auto a = render (soft, note (60, 30, 0.0, 2.0), 1.0), b = render (hard, note (60, 120, 0.0, 2.0), 1.0);
    const double ca = spectrum (a, 0.05, 0.5).centroid(), cb = spectrum (b, 0.05, 0.5).centroid();
    const float la = stats (a, 0.0, 0.5).rms, lb = stats (b, 0.0, 0.5).rms;
    printf ("touch         pp %.1f dB %.0f Hz   ff %.1f dB %.0f Hz\n", db (la), ca, db (lb), cb);
    check (lb > la * 4.0f, "touch: ff is clearly louder than pp");
    check (cb > ca * 1.3, "touch: ff is brighter than pp");

    // Una corda makes it softer and darker
    HamarProcessor una;
    std::vector<Event> e { { 0.0, juce::MidiMessage::controllerEvent (1, 67, 127) } };
    append (e, note (60, 120, 0.01, 2.0));
    const auto c = render (una, e, 1.0);
    const float lc = stats (c, 0.0, 0.5).rms;
    const double cc = spectrum (c, 0.05, 0.5).centroid();
    printf ("una corda     ff %.1f dB %.0f Hz\n", db (lc), cc);
    check (lc < lb && cc < cb, "una corda: softer and darker");
}

static void pedalTests()
{
    // Released without pedal: the damper stops the string; with pedal it rings on
    HamarProcessor dry, wet;
    const auto a = render (dry, note (48, 90, 0.0, 0.3), 2.5);
    std::vector<Event> e { pedal (0.0, 127) };
    append (e, note (48, 90, 0.01, 0.3));
    const auto b = render (wet, e, 2.5);
    const float ra = stats (a, 2.0, 2.5).rms, rb = stats (b, 2.0, 2.5).rms;
    printf ("pedal         without %.1f dB   with %.1f dB (2 s after the blow)\n", db (ra), db (rb));
    check (rb > ra * 30.0f, "sustain pedal keeps the string ringing");

    // A held pedal survives the host preparing the plugin again (Logic does that at times)
    {
        HamarProcessor held;
        held.setPlayConfigDetails (0, 2, testSampleRate, 512);
        held.prepareToPlay (testSampleRate, 512);
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer down;
        down.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0);
        held.processBlock (buffer, down);
        held.prepareToPlay (testSampleRate, 512);   // the host prepares again while the pedal is down
        const auto c = render (held, note (48, 90, 0.01, 0.3), 2.5);
        check (stats (c, 2.0, 2.5).rms > ra * 30.0f, "the sustain pedal stays down across prepareToPlay");
    }

    // Sympathetic resonance: the pedal lets other strings answer a short note
    HamarProcessor symOff, symOn;
    symOff.apvts.getParameter ("sym_amount")->setValueNotifyingHost (0.0f);
    const auto c = render (symOff, e, 1.5), d = render (symOn, e, 1.5);
    check (stats (d, 0.5, 1.5).rms > stats (c, 0.5, 1.5).rms * 1.0005f, "sympathetic strings add to the sound");

    // Half pedal damps in between
    HamarProcessor half;
    std::vector<Event> h { pedal (0.0, 64) };
    append (h, note (48, 90, 0.01, 0.3));
    const auto f = render (half, h, 2.5);
    const float rh = stats (f, 2.0, 2.5).rms;
    check (rh > ra && rh < rb, "half pedal sits between");
}

static void toolTests()
{
    // Chord mode: one key plays a major triad (the third shows up in the spectrum)
    HamarProcessor plain, chord;
    setParam (chord, "tools_chord", 3.0f);
    const auto a = render (plain, note (60, 100, 0.0, 2.0), 1.5), b = render (chord, note (60, 100, 0.0, 2.0), 1.5);
    float la = 0.0f, lb = 0.0f;
    const double third = 440.0 * std::exp2 ((64 - 69) / 12.0);
    spectrum (a, 0.2, 1.0).peakNear (third, 30.0, &la);
    spectrum (b, 0.2, 1.0).peakNear (third, 30.0, &lb);
    printf ("chord         third without %.1f dB, with %.1f dB\n", db (la), db (lb));
    check (lb > la * 10.0f, "chord mode adds the third");

    // Layer: a pad under the piano adds a sustained tone
    HamarProcessor pad;
    setParam (pad, "layer_level", 0.6f);
    const auto c = render (pad, note (60, 100, 0.0, 3.0), 3.0);
    const float sa = stats (render (plain, note (60, 100, 0.0, 3.0), 3.0), 2.0, 3.0).rms, sc = stats (c, 2.0, 3.0).rms;
    printf ("layer         sustain without %.1f dB, with %.1f dB\n", db (sa), db (sc));
    check (sc > sa * 1.5f, "layer adds a sustained tone");

    // Condition: an old piano is out of tune
    HamarProcessor old;
    setParam (old, "cond_detune", 1.0f);
    const auto& k = physics (old, 62);
    const double et = 440.0 * std::exp2 ((62 - 69) / 12.0);
    const double cents = std::abs (1200.0 * std::log2 (k.f1 / et));
    check (cents > 1.0, "condition detunes the keys");
}

static void pressureTests()
{
    // Aftertouch: the clavichord's Bebung raises the pitch; a grand ignores it
    auto pitch = [] (int model, bool press)
    {
        HamarProcessor p;
        setParam (p, "model", (float) model);
        std::vector<Event> e = note (60, 90, 0.0, 2.0);
        if (press) e.push_back ({ 0.2, juce::MidiMessage::aftertouchChange (1, 60, 127) });
        const auto audio = render (p, e, 1.8);
        return spectrum (audio, 0.6, 1.0).peakNear (261.6, 60.0);
    };
    const double bebung = 1200.0 * std::log2 (pitch (24, true) / pitch (24, false));
    const double grand = 1200.0 * std::log2 (pitch (0, true) / pitch (0, false));
    printf ("aftertouch    clavichord %+.1f cents, grand %+.2f cents\n", bebung, grand);
    check (bebung > 6.0 && bebung < 20.0, "clavichord Bebung raises the pitch with key pressure");
    check (std::abs (grand) < 0.3, "a grand ignores key pressure");
}

static void scalaTests()
{
    // A 19-step equal scale: 19 keys make an octave; a just 12-note scale: the major third is 5/4 (built on C)
    auto f1 = [] (HamarProcessor& p, int key) { p.setPlayConfigDetails (0, 2, testSampleRate, 512); p.prepareToPlay (testSampleRate, 512);
                                                return (double) p.getEngine().getTable()[key].f1; };
    juce::String edo19 = "! 19-EDO\n19 equal\n19\n";
    for (int i = 1; i <= 19; ++i) edo19 << juce::String (1200.0 * i / 19.0, 5) << "\n";
    HamarProcessor a;
    setParam (a, "tune_stretch", 0.0f);
    check (a.loadScala (edo19, "edo19.scl").isEmpty(), "scala: 19-EDO loads");
    const double octave = 1200.0 * std::log2 (f1 (a, 79) / f1 (a, 60));
    check (std::abs (octave - 1200.0) < 0.5, "scala: 19 keys make an octave (" + juce::String (octave, 2) + " cents)");

    HamarProcessor b;
    setParam (b, "tune_stretch", 0.0f);
    const juce::String just = "Just\n12\n16/15\n9/8\n6/5\n5/4\n4/3\n45/32\n3/2\n8/5\n5/3\n9/5\n15/8\n2/1\n";
    check (b.loadScala (just, "just.scl").isEmpty(), "scala: just intonation loads");
    const double third = 1200.0 * std::log2 (f1 (b, 64) / f1 (b, 60));
    check (std::abs (third - 386.31) < 0.5, "scala: the just major third (" + juce::String (third, 2) + " cents)");

    // It survives a project round trip
    juce::MemoryBlock state;
    b.getStateInformation (state);
    HamarProcessor c;
    c.setStateInformation (state.getData(), (int) state.getSize());
    check (std::abs (1200.0 * std::log2 (f1 (c, 64) / f1 (c, 60)) - third) < 0.01, "scala: kept in the project");
    check (b.loadScala ("nonsense", "x.scl").isNotEmpty(), "scala: a broken file is refused");
}

static void modelTests()
{
    // Every model sounds, stays finite and below clipping over the whole keyboard
    const auto& names = hamar::dsp::models();
    for (int m = 0; m < (int) names.size(); ++m)
    {
        HamarProcessor proc;
        setParam (proc, "model", (float) m);
        std::vector<Event> e;
        for (int key : { 21, 33, 45, 57, 69, 81, 93, 105 }) append (e, note (key, 110, 0.05 * (key - 21) / 12.0, 1.0));
        const auto audio = render (proc, e, 1.6);
        const auto s = stats (audio, 0.0, 1.6);
        printf ("  %-16s peak %.3f  rms %.4f\n", names[(size_t) m].name, s.peak, s.rms);
        check (s.finite && s.peak > 0.02f && s.peak < 1.0f, juce::String ("model ") + names[(size_t) m].name);
    }
}

static void convolverTest()
{
    // The zero-latency partitioned convolution equals a plain convolution (head, tail, block boundaries)
    for (int block : { 128, 256 })
    {
        const int irLength = 1000, length = 3000;
        fjord::base::dsp::Random rnd;
        std::vector<float> ir ((size_t) irLength), x ((size_t) length);
        for (auto& v : ir) v = rnd.nextBipolar() * 0.1f;
        for (auto& v : x) v = rnd.nextBipolar();
        hamar::dsp::Convolver conv;
        conv.prepare (block, irLength / block + 2);
        hamar::dsp::Convolver::Kernel kernel;
        kernel.build (ir.data(), irLength, block, conv.getFft());
        conv.setKernel (&kernel);
        double worst = 0.0;
        for (int t = 0; t < length; ++t)
        {
            const float y = conv.process (x[(size_t) t]);
            double ref = 0.0;
            for (int k = 0; k < irLength && k <= t; ++k) ref += (double) ir[(size_t) k] * x[(size_t) (t - k)];
            worst = std::max (worst, std::abs (ref - y));
        }
        check (worst < 1.0e-3, "convolver (block " + juce::String (block) + ") error " + juce::String (worst, 6));
    }
}

static void blockSizeTest()
{
    for (int block : { 1, 37, 512, 4096 })
    {
        HamarProcessor proc;
        const auto audio = render (proc, note (60, 100, 0.0, 0.3), 0.6, block);
        const auto s = stats (audio, 0.0, 0.6);
        check (s.finite && s.peak > 0.01f, "block size " + juce::String (block));
    }
}

static void presetRoundTrip()
{
    HamarProcessor a, b;
    if (! a.presetManager.getPresets().empty()) a.presetManager.load (0);
    juce::MemoryBlock state;
    a.getStateInformation (state);
    b.setStateInformation (state.getData(), (int) state.getSize());
    for (const auto& s : hamar::params::specs())
        check (std::abs (a.apvts.getRawParameterValue (s.paramId)->load() - b.apvts.getRawParameterValue (s.paramId)->load()) < 1.0e-5f,
               juce::String ("state round trip: ") + s.paramId);
}

// Every factory preset plays a chord progression (C, G/B, F, Dm in four voices, velocity 100): finite, audible, and below
// full scale before the clipper - the clipper must stay out of normal playing (it would crackle on held chords)
static void factoryPresetTests()
{
    hamar::PresetManager::ignoreUserPresets = true;
    HamarProcessor proc;
    const auto& list = proc.presetManager.getPresets();
    std::vector<Event> phrase;
    const int chords[4][4] = { { 60, 64, 67, 72 }, { 59, 64, 67, 71 }, { 53, 65, 69, 72 }, { 57, 65, 69, 74 } };
    for (int c = 0; c < 4; ++c)
        for (int n : chords[c]) append (phrase, note (n, 100, c * 1.0, 0.98));
    float loudest = 0.0f;
    for (int i = 0; i < (int) list.size(); ++i)
    {
        if (! list[(size_t) i].isFactory) continue;
        HamarProcessor p;
        p.presetManager.load (i);
        setParam (p, "fx_out_on", 0.0f);
        const auto audio = render (p, phrase, 4.5);
        const auto r = stats (audio, 0.0, 4.5);
        loudest = std::max (loudest, r.peak);
        const bool ok = r.finite && r.peak < 0.95f && r.peak > 1.0e-3f;
        if (! ok) printf ("  %-12s %-26s peak %.3f  rms %.4f\n", list[(size_t) i].category.toRawUTF8(), list[(size_t) i].name.toRawUTF8(), r.peak, r.rms);
        check (ok, "preset " + list[(size_t) i].name + " (chords below full scale before the clipper)");
    }
    printf ("  %d factory presets, highest peak before the clipper %.2f\n", (int) list.size(), loudest);
}

static int paramInfo (const juce::File& out)
{
    juce::StringArray lines;
    for (const auto& s : hamar::params::specs())
    {
        const char* kinds[] = { "float", "int", "choice", "bool" };
        lines.add (juce::String (s.paramId) + "\t" + kinds[(int) s.kind] + "\t" + juce::String (s.min, 6) + "\t"
                   + juce::String (s.max, 6) + "\t" + juce::String (s.def, 6));
    }
    return out.replaceWithText (lines.joinIntoString ("\n") + "\n") ? 0 : 1;
}

// Loudness of every factory preset, measured on a chord progression (C, G/B, F, Dm in four voices, velocity 100, one
// second each, without pedal and without the clipper): chords add up very differently from one instrument to the next
// (long-ringing bars, plucked choirs, prepared strings on some keys), so the preset level is set from what is played.
// Momentary loudness (the loudest 400 ms) and the true peak of the phrase.
static int loudness (const juce::File& out)
{
    hamar::PresetManager::ignoreUserPresets = true;
    HamarProcessor proc;
    auto* json = new juce::DynamicObject();
    const auto& list = proc.presetManager.getPresets();
    std::vector<Event> phrase;
    const int chords[4][4] = { { 60, 64, 67, 72 }, { 59, 64, 67, 71 }, { 53, 65, 69, 72 }, { 57, 65, 69, 74 } };
    for (int c = 0; c < 4; ++c)
        for (int n : chords[c]) append (phrase, note (n, 100, c * 1.0, 0.98));
    for (int i = 0; i < (int) list.size(); ++i)
    {
        HamarProcessor p;
        p.presetManager.load (i);
        setParam (p, "preset_level", 0.0f);
        setParam (p, "fx_out_on", 0.0f);
        const auto audio = render (p, phrase, 5.0);
        // Momentary loudness: the loudest 400 ms window (short stabs and long chords are heard at their loudest moment)
        float loudest = 0.0f;
        for (double t = 0.0; t < 4.0; t += 0.1) loudest = std::max (loudest, stats (audio, t, t + 0.4).rms);
        juce::Array<juce::var> v { db (loudest), db (stats (audio, 0.0, 5.0).peak) };
        json->setProperty (list[(size_t) i].name, v);
    }
    return out.replaceWithText (juce::JSON::toString (juce::var (json))) ? 0 : 1;
}

// The app icon: a grand from above on a polar-night card, its strings fanning out in ivory and brass, one of them
// glowing where the hammer just struck - the dot of the logo
static int makeIcon (const juce::File& out)
{
    constexpr int size = 1024;
    juce::Image img (juce::Image::ARGB, size, size, true);
    juce::Graphics g (img);
    const auto card = juce::Rectangle<float> (100.0f, 100.0f, 824.0f, 824.0f);
    juce::Path shape;
    shape.addRoundedRectangle (card, 210.0f);
    for (int i = 12; i > 0; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.012f));
        g.fillPath (shape, juce::AffineTransform::translation (0.0f, 10.0f).scaled (1.0f + (float) i * 0.004f, 1.0f + (float) i * 0.004f, 512.0f, 540.0f));
    }
    g.saveState();
    g.reduceClipRegion (shape);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff141b27), 0.0f, 100.0f, juce::Colour (0xff0a0f18), 0.0f, 924.0f, false));
    g.fillRect (card);
    const juce::Colour ivory (0xfff4e7c8), brass (0xffe6c88a), walnut (0xffbf8a4e);
    g.setGradientFill (juce::ColourGradient (brass.withAlpha (0.28f), 600.0f, 470.0f, brass.withAlpha (0.0f), 960.0f, 470.0f, true));
    g.fillRect (card);

    // The grand from above: straight bass side, round tail, the bentside, the keyboard at the bottom
    const float l = 250.0f, t = 210.0f, w = 520.0f, h = 600.0f, b = t + h;
    juce::Path grand;
    grand.startNewSubPath (l, b);
    grand.lineTo (l, t + 0.12f * h);
    grand.quadraticTo (l, t, l + 0.14f * w, t);
    grand.cubicTo (l + 0.42f * w, t, l + 0.5f * w, t + 0.32f * h, l + 0.62f * w, t + 0.55f * h);
    grand.cubicTo (l + 0.72f * w, t + 0.74f * h, l + w, t + 0.72f * h, l + w, t + 0.86f * h);
    grand.lineTo (l + w, b);
    grand.closeSubPath();
    g.setColour (juce::Colour (0xff1b2333));
    g.fillPath (grand);
    g.setColour (brass.withAlpha (0.85f));
    g.strokePath (grand, juce::PathStrokeType (10.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Strings, ending under the bentside; one glows
    juce::Path inner (grand);
    inner.applyTransform (juce::AffineTransform::scale (0.9f, 0.92f, l + w * 0.5f, b));
    g.saveState();
    g.reduceClipRegion (inner);
    for (int i = 0; i < 15; ++i)
    {
        const float x = l + 45.0f + (float) i * (w - 90.0f) / 14.0f;
        const bool lit = i == 9;
        const juce::Colour c = i < 4 ? walnut : ivory;
        if (lit)
            for (auto [width, alpha] : { std::pair { 40.0f, 0.12f }, std::pair { 18.0f, 0.3f } })
            {
                g.setColour (brass.withAlpha (alpha));
                g.drawLine (x, b - 40.0f, x, t - 40.0f, width);
            }
        g.setColour (c.withAlpha (lit ? 1.0f : 0.55f));
        g.drawLine (x, b - 40.0f, x, t - 40.0f, lit ? 7.0f : 4.0f);
    }
    g.restoreState();
    // The keyboard edge and the dot of the logo
    g.setColour (ivory);
    g.fillRoundedRectangle (juce::Rectangle<float> (l + 24.0f, b - 34.0f, w - 48.0f, 20.0f), 8.0f);
    g.setColour (brass);
    g.fillEllipse (juce::Rectangle<float> (64.0f, 64.0f).withCentre ({ l + 45.0f + 9.0f * (w - 90.0f) / 14.0f, b - 120.0f }));
    g.restoreState();

    out.getParentDirectory().createDirectory();
    out.deleteFile();
    if (auto stream = out.createOutputStream())
        juce::PNGImageFormat().writeImageToStream (img, *stream);
    return out.existsAsFile() ? 0 : 1;
}

static int takeScreenshot (const juce::String& pageName, const juce::String& presetName, const juce::String& theme,
                           const juce::File& outFile, int fxIndex)
{
    auto pages = HamarView::pageNames;
    pages.addArray ({ "browser", "about" });
    const int pageIndex = pages.indexOf (pageName);
    if (pageIndex < 0) { printf ("Unknown page: %s\n", pageName.toRawUTF8()); return 1; }
    const int browserIndex = pages.indexOf ("browser"), aboutIndex = pages.indexOf ("about"), fxIndexPage = pages.indexOf ("fx");

    hamar::PresetManager::ignoreUserPresets = true;
    fjord::base::fx::ChainPresets::ignoreUserPresets = true;
    HamarProcessor proc;
    if (! loadPreset (proc, presetName.upToFirstOccurrenceOf (":", false, false))) return 1;
    applyParams (proc, presetName.fromFirstOccurrenceOf (":", false, false));   // "init:model=14,ep_drive=0.5"
    proc.setPlayConfigDetails (0, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    fjord::base::ui::Theme::get().setDark (theme == "dark");
    std::unique_ptr<juce::AudioProcessorEditor> editorWindow (proc.createEditor());
    editorWindow->setSize (HamarView::width, HamarView::height);
    auto* editor = dynamic_cast<HamarEditor*> (editorWindow.get());
    if (editor == nullptr) return 1;
    auto& view = editor->getView();

    if (pageIndex == browserIndex)    { view.selectPage (0); view.showBrowser (true); }
    else if (pageIndex == aboutIndex) { view.selectPage (0); view.showAbout(); }
    else                              view.selectPage (pageIndex);
    if (pageIndex == fxIndexPage && fxIndex >= 0)
        view.getFxPage().selectEffect (fxIndex);

    // Play a chord while the visuals animate, so they show the sound
    juce::AudioBuffer<float> buffer (2, 512);
    for (int k = 0; k < 60; ++k)
    {
        juce::MidiBuffer midi;
        if (k == 0) for (int n : { 48, 55, 60, 64 }) midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
        buffer.clear();
        proc.processBlock (buffer, midi);
        view.tickAnimations();
    }

    const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 2.0f);
    outFile.getParentDirectory().createDirectory();
    outFile.deleteFile();
    if (auto stream = outFile.createOutputStream())
        juce::PNGImageFormat().writeImageToStream (image, *stream);
    editorWindow.reset();
    fjord::base::ui::Theme::get().setDark (false);
    return outFile.existsAsFile() ? 0 : 1;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    fjord::base::ui::Theme::get().setPersistent (false);   // tests never write user settings
    hamar::PresetManager::ignoreUserPresets = true;

    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (argv[i]);
    if (const int i = args.indexOf ("--sr"); i >= 0 && i + 1 < args.size())
    {
        testSampleRate = args[i + 1].getDoubleValue();
        args.removeRange (i, 2);
    }
    auto file = [] (const juce::String& path) { return juce::File::getCurrentWorkingDirectory().getChildFile (path); };
    auto arg = [&args] (int i, const juce::String& fallback) { return i < args.size() ? args[i] : fallback; };

    if (args.size() > 1 && args[0] == "--param-info") return paramInfo (file (args[1]));
    if (args.size() > 1 && args[0] == "--loudness")   return loudness (file (args[1]));
    if (args.size() > 3 && args[0] == "--single")
        return single (args[1], args[2].getIntValue(), file (args[3]), arg (4, "100").getIntValue(), arg (5, "2").getDoubleValue(),
                       arg (6, "4").getDoubleValue(), arg (7, ""));
    if (args.size() > 2 && args[0] == "--demo")       return demo (args[1], file (args[2]), arg (3, ""));
    if (args.size() > 1 && args[0] == "--analyse")    return analyse (args[1].getIntValue(), arg (2, "90").getIntValue(), arg (3, ""));
    if (args.size() > 0 && args[0] == "--cpu")        return cpu();
    if (args.size() > 0 && args[0] == "--pedal-check") return pedalCheck();
    if (args.size() > 0 && args[0] == "--screen-pedal")
    {
        HamarProcessor p;
        p.setPlayConfigDetails (0, 2, testSampleRate, 512);
        p.prepareToPlay (testSampleRate, 512);
        p.setScreenSustain (true);
        const auto audio = render (p, note (60, 90, 0.01, 0.3), 2.5);
        printf ("screen pedal held: %s, level after 2 s %.1f dB\n", p.getScreenSustain() ? "yes" : "no", db (stats (audio, 2.0, 2.5).rms));
        return 0;
    }
    if (args.size() > 3 && args[0] == "--midi")       return renderMidi (file (args[1]), args[2], file (args[3]));
    if (args.size() > 1 && args[0] == "--midi-scan")  return midiScan (file (args[1]));
    if (args.size() > 1 && args[0] == "--icon")       return makeIcon (file (args[1]));
    if (args.size() > 3 && args[0] == "--compare")    return compare (file (args[1]), args[2].getIntValue(), args[3].getIntValue(), arg (4, ""));
    if (args.size() > 4 && args[0] == "--screenshot")
        return takeScreenshot (args[1], args[2], args[3], file (args[4]), args.size() > 5 ? args[5].getIntValue() : -1);

    printf ("Hamar render test at %.0f Hz\n", testSampleRate);
    convolverTest();
    soundTests();
    tuningTests();
    touchTests();
    pedalTests();
    toolTests();
    pressureTests();
    scalaTests();
    modelTests();
    blockSizeTest();
    presetRoundTrip();
    factoryPresetTests();

    printf (failures == 0 ? "ALL PASSED\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
