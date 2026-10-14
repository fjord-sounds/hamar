#pragma once
#include "Widgets.h"

namespace hamar::ui
{
    // Tuning: the temperament on a circle of fifths, the stretch a tuner gives this model (from its inharmonicity),
    // reference pitch and transposition.
    class TuningPage : public Page
    {
    public:
        explicit TuningPage (HamarProcessor& p)
            : proc (p), wheel (p.apvts),
              stretch ("Tuning  " + utf8 ("\xc2\xb7") + "  cents from equal temperament", false, -40.0f, 40.0f,
                       [] (float v) { return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v)); }),
              lock (p.apvts, locks::ids().tuning), playLock (p.apvts, locks::ids().play),
              loadScala ("Load Scala", false), clearScala ("Clear", false)
        {
            addAndMakeVisible (loadScala);
            addAndMakeVisible (clearScala);
            loadScala.onClick = [this]
            {
                chooser = std::make_unique<juce::FileChooser> ("Load a Scala tuning", juce::File::getSpecialLocation (juce::File::userHomeDirectory), "*.scl");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                      [this] (const juce::FileChooser& fc)
                                      {
                                          const auto f = fc.getResult();
                                          if (! f.existsAsFile()) return;
                                          const auto error = proc.loadScala (f.loadFileAsString(), f.getFileName());
                                          if (error.isNotEmpty())
                                              juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Hamar", error);
                                          repaint();
                                      });
            };
            clearScala.onClick = [this] { proc.clearScala(); repaint(); };
            auto& s = p.apvts;
            addAndMakeVisible (wheel);
            addAndMakeVisible (stretch);
            tuneRow.add (makeKnob (s, P::TuneA4, "A4"));
            tuneRow.add (makeChoice (s, P::TuneTemperament, "Temperament"), 2.2f);
            tuneRow.add (makeChoice (s, P::TuneKey, "Key"), 1.0f);
            tuneRow.add (makeKnob (s, P::TuneStretch, "Stretch"));
            tuneRow.addTo (*this);
            playRow.add (makeKnob (s, P::PlayTranspose, "Transpose"));
            playRow.addTo (*this);
            addAndMakeVisible (lock);
            addAndMakeVisible (playLock);
        }

        void tick() override
        {
            wheel.tick();
            if (const auto name = proc.getScalaName(); name != shownScala)
            {
                shownScala = name;
                clearScala.setVisible (name.isNotEmpty());
                repaint();
            }
            if (! table.update (proc)) return;
            stretch.setRange (table.lowKey(), dsp::kHighestKey);
            const float a4 = table.settings.tuning.a4;
            stretch.setLines ({ { [this, a4] (int key)
                                  {
                                      const double et = a4 * std::exp2 ((key - 69) / 12.0);
                                      return (float) (1200.0 * std::log2 (table[key].f1 / et));
                                  }, "Tuning", true } });
        }

        void paint (juce::Graphics& g) override
        {
            drawPanel (g, tuneArea.toFloat(), "Tuning");
            drawPanel (g, playArea.toFloat(), "Keyboard");
            const auto& p = Theme::get().palette();
            g.setColour (shownScala.isNotEmpty() ? p.accent : p.textDim);
            g.setFont (font (11.5f, shownScala.isNotEmpty()));
            g.drawText (shownScala.isNotEmpty() ? "Scala: " + shownScala : juce::String ("No Scala tuning - the temperament applies"),
                        scalaText, juce::Justification::centredLeft);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            auto graphs = r.removeFromTop (r.getHeight() - 200);
            r.removeFromTop (10);
            wheel.setBounds (graphs.removeFromLeft (graphs.getHeight() + 40));
            graphs.removeFromLeft (10);
            stretch.setBounds (graphs);
            playArea = r.removeFromRight (180);
            r.removeFromRight (10);
            tuneArea = r;
            lock.place (tuneArea);
            playLock.place (playArea);
            auto t = tuneArea.reduced (12, 0).withTrimmedTop (40);
            auto scalaRow = t.removeFromBottom (36).withTrimmedBottom (8);
            loadScala.setBounds (scalaRow.removeFromRight (130).reduced (2, 2));
            clearScala.setBounds (scalaRow.removeFromRight (90).reduced (2, 2));
            scalaText = scalaRow.withTrimmedLeft (6);
            tuneRow.layout (t.withHeight (100));
            playRow.layout (playArea.reduced (12, 0).withTrimmedTop (40).withHeight (110));
        }

    private:
        HamarProcessor& proc;
        TableWatcher table;
        TemperamentWheel wheel;
        KeyCurve stretch;
        ControlRow tuneRow, playRow;
        PanelLock lock, playLock;
        PillButton loadScala, clearScala;
        std::unique_ptr<juce::FileChooser> chooser;
        juce::String shownScala = "-";
        juce::Rectangle<int> scalaText;
        juce::Rectangle<int> tuneArea, playArea;
    };
}
