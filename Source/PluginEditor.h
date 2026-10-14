#pragma once
#include "PluginProcessor.h"
#include "ui/PlayPage.h"
#include "ui/InstrumentPage.h"
#include "ui/HammerPage.h"
#include "ui/StringsPage.h"
#include "ui/PedalsPage.h"
#include "ui/MicsPage.h"
#include "ui/TuningPage.h"
#include "ui/LayerPage.h"
#include <fjord_base/fx/FxPage.h>

// The actual UI at a fixed size (1200 x 800) inside the shared frame of fjord-base (header, presets, About).
// The editor scales it as a whole.
class HamarView : public hamar::ui::PluginView
{
public:
    static constexpr int width = 1200, height = 800;

    explicit HamarView (HamarProcessor&);

    // For the screenshot tool
    static inline const juce::StringArray pageNames { "play", "instrument", "hammer", "strings", "pedals", "mics", "tuning", "layer", "fx" };
    void selectPage (int index) { showPage (index); }
    fjord::base::fx::FxPage& getFxPage() { return fxPage; }

protected:
    juce::Rectangle<int> layoutContent (juce::Rectangle<int>) override;
    void layoutTabRow (juce::Rectangle<int>) override;
    void browserVisibilityChanged (bool open) override;
    void tickContent (bool offscreen) override;

private:
    static hamar::ui::ViewSetup makeSetup (HamarProcessor&);
    float keyCentre (int note, const juce::Component& target) const;
    void fitKeyboard();

    HamarProcessor& proc;
    hamar::ui::Keyboard keyboard;
    hamar::ui::PedalStrip pedals;
    hamar::ui::ModelMenu modelMenu;
    hamar::ui::Segmented quality;
    hamar::ui::PlayPage playPage;
    hamar::ui::InstrumentPage instrumentPage;
    hamar::ui::HammerPage hammerPage;
    hamar::ui::StringsPage stringsPage;
    hamar::ui::PedalsPage pedalsPage;
    hamar::ui::MicsPage micsPage;
    hamar::ui::TuningPage tuningPage;
    hamar::ui::LayerPage layerPage;
    fjord::base::fx::FxPage fxPage;
    int keyboardLow = -1, keyboardHigh = 108;

    static constexpr int keyboardHeight = 84;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HamarView)
};

// Plugin window: freely scalable (75 - 150 %) with a fixed aspect ratio, the size is remembered.
class HamarEditor : public hamar::ui::ScaledEditor<HamarView, HamarProcessor>
{
public:
    using ScaledEditor::ScaledEditor;
};
