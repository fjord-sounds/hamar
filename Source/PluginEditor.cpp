#include "PluginEditor.h"
#include "presets/Locks.h"

using namespace hamar;

ui::ViewSetup HamarView::makeSetup (HamarProcessor& p)
{
    ui::ViewSetup s { p.apvts, p.presetManager };
    s.undo = &p.undoManager;
    s.masterParamId = params::paramId (params::MasterGain);
    s.tabs = { "Play", "Instrument", "Hammer", "Strings", "Pedals", "Mics", "Tuning", "Layer", "FX" };
    s.tabWidth = 74;
    s.peaks = [&p] { return p.takeOutputPeaks(); };
    s.noteCounter = [&p] { return p.getNoteCount(); };
    s.lastVelocity = [&p] { return p.getLastVelocity(); };
    s.debugReport = [&p] { return p.getDebugReport(); };
    s.panic = [&p] { p.requestPanic(); };
    return s;
}

HamarView::HamarView (HamarProcessor& p)
    : PluginView (makeSetup (p), width, height),
      proc (p),
      keyboard (p.keyboardState),
      pedals (p),
      modelMenu (p.apvts),
      quality (p.apvts, params::paramId (params::Quality)),
      playPage (p, [this] (int note, const juce::Component& c) { return keyCentre (note, c); }),
      instrumentPage (p), hammerPage (p), stringsPage (p), pedalsPage (p),
      micsPage (p),
      tuningPage (p), layerPage (p),
      fxPage ({ p.apvts, &p.undoManager, p.fxMeters, [&p] { return p.fxOrder.get(); },
                [&p] (const fjord::base::fx::Order& o) { p.fxOrder.set (o); }, &p.chainPresets })
{
    // The padlock in the preset bar: parts of the sound that stay when switching presets
    fjord::base::ui::LockMenu::attach (getPresetBar(), p.apvts, locks::set(), locks::menuLayout());
    keyboard.setAvailableRange (21, 108);
    addAndMakeVisible (keyboard);
    addAndMakeVisible (pedals);
    addAndMakeVisible (modelMenu);
    addAndMakeVisible (quality);
    quality.setTooltip ("Quality: more strings per key, finer string stiffness, a denser soundboard and more keys at once - and more CPU");
    setPages ({ &playPage, &instrumentPage, &hammerPage, &stringsPage, &pedalsPage, &micsPage, &tuningPage, &layerPage, &fxPage });
}

float HamarView::keyCentre (int note, const juce::Component& target) const
{
    const bool black = juce::MidiMessage::isMidiNoteBlack (note);
    const float w = keyboard.getKeyWidth() * (black ? 0.62f : 1.0f);
    const float x = keyboard.getKeyStartPosition (note) + w * 0.5f;
    return target.getLocalPoint (&keyboard, juce::Point<float> (x, 0.0f)).x;
}

void HamarView::tickContent (bool offscreen)
{
    juce::ignoreUnused (offscreen);
    modelMenu.tick();
    pedals.tick();
    // The keyboard shows the keys of the model (the extended-bass grand starts at C0)
    const auto& m = dsp::model (juce::roundToInt (proc.apvts.getRawParameterValue (params::paramId (params::Model))->load()));
    if (m.lowKey != keyboardLow || m.highKey != keyboardHigh)
    {
        keyboardLow = m.lowKey;
        keyboardHigh = m.highKey;
        keyboard.setAvailableRange (keyboardLow, keyboardHigh);
        fitKeyboard();
    }
}

// The keys fill the keyboard's width, so the strings above line up with them
void HamarView::fitKeyboard()
{
    const int low = keyboardLow < 0 ? 21 : keyboardLow;
    int whites = 0;
    for (int n = low; n <= keyboardHigh; ++n) whites += juce::MidiMessage::isMidiNoteBlack (n) ? 0 : 1;
    keyboard.setKeyWidth ((float) keyboard.getWidth() / (float) std::max (1, whites));
}

void HamarView::browserVisibilityChanged (bool open)
{
    modelMenu.setVisible (! open);
    quality.setVisible (! open);
}

void HamarView::layoutTabRow (juce::Rectangle<int> tabs)
{
    quality.setBounds (tabs.removeFromRight (250).withSizeKeepingCentre (250, 26).translated (0, -2));
    tabs.removeFromRight (10);
    modelMenu.setBounds (tabs.removeFromRight (230).withSizeKeepingCentre (230, 28).translated (0, -2));
}

juce::Rectangle<int> HamarView::layoutContent (juce::Rectangle<int> r)
{
    auto footer = r.removeFromBottom (keyboardHeight + margin).withTrimmedBottom (margin);
    pedals.setBounds (footer.removeFromRight (190));
    footer.removeFromRight (10);
    keyboard.setBounds (footer);
    fitKeyboard();
    r.removeFromBottom (12);
    r.removeFromTop (4);
    for (auto* pg : std::initializer_list<juce::Component*> { &playPage, &instrumentPage, &hammerPage, &stringsPage, &pedalsPage,
                                                               &micsPage, &tuningPage, &layerPage, &fxPage })
        pg->setBounds (r);
    return r;
}
