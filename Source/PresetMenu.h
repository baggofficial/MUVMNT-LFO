#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PaperLookAndFeel.h"
#include "Presets.h"

// The header's preset button: a small curve icon and a chevron. Click for the preset menu
// (factory presets with a thumbnail of each curve, your own saved presets, copy / paste / save).
class PresetButton : public juce::Button
{
public:
    PresetButton() : juce::Button("presets") { setTooltip("Curve presets, copy and paste"); }
    void paintButton(juce::Graphics& g, bool over, bool down) override;
};

namespace PresetMenu
{
    // Opens the popup as a layer inside `host` (the editor), dropping down from `anchor`. The chosen preset / pasted
    // curve replaces the curve in `slot`. `changed` is called after a change so the editor can redraw;
    // `dismissRequested` is called when the popup wants to close (a click outside, Escape, or a choice): the caller
    // should then destroy the returned component (do it from the message loop, not from inside the callback).
    std::unique_ptr<juce::Component> open(juce::Component& host, juce::Component& anchor, FreeLfoProcessor& proc, int slot,
                                          std::function<void()> changed, std::function<void()> dismissRequested);

    // The popup's content (also used by the tests to render it without a screen).
    std::unique_ptr<juce::Component> createPanel(FreeLfoProcessor& proc, int slot, std::function<void()> changed, juce::LookAndFeel& laf);

    // Folder with the user's own presets (one small text file per preset).
    juce::File userFolder();
}
