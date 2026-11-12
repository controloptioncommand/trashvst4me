#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class BurialLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BurialLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh,
                       juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getPopupMenuFont() override;

    static const juce::Colour bg, panel, accent, text, dim;
};

class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const char* paramId, const juce::String& title, bool bipolar = false);
    void resized() override;

    juce::Slider slider;
    juce::Label label;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class BurialVoiceEditor : public juce::AudioProcessorEditor
{
public:
    explicit BurialVoiceEditor (BurialVoiceProcessor&);
    ~BurialVoiceEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void renderBackground();

    BurialVoiceProcessor& processor;
    BurialLookAndFeel lnf;
    juce::Image background;

    Knob pitch, formant, shadow, wobble, grit, crackle, lowCut, highCut, echo, time, space, reverb, mix, output;
    juce::ComboBox presetBox;

    struct Section { juce::String name; juce::Rectangle<int> area; };
    std::vector<Section> sections;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BurialVoiceEditor)
};
