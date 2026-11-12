#include "PluginEditor.h"

const juce::Colour BurialLookAndFeel::bg     { 0xff0c0d10 };
const juce::Colour BurialLookAndFeel::panel  { 0xff15171b };
const juce::Colour BurialLookAndFeel::accent { 0xffd89a52 }; // sodium street-light amber
const juce::Colour BurialLookAndFeel::text   { 0xffc9c4ba };
const juce::Colour BurialLookAndFeel::dim    { 0xff6d6a64 };

namespace
{
    juce::Font makeFont (float size, bool bold = false)
    {
       #if JUCE_MAJOR_VERSION >= 8
        return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
       #else
        return juce::Font (size, bold ? juce::Font::bold : juce::Font::plain);
       #endif
    }

    constexpr int kW = 860, kH = 400;
    constexpr int colW = 92, rowH = 122, sectionGap = 16, margin = 30, headerH = 76, sectionLabelH = 26;
}

//==============================================================================
BurialLookAndFeel::BurialLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, accent.withAlpha (0.35f));
    setColour (juce::Label::textColourId, text);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::backgroundColourId, panel);
    setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.35f));
    setColour (juce::CaretComponent::caretColourId, accent);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, accent);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
}

void BurialLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                          float startAngle, float endAngle, juce::Slider& s)
{
    const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
    const auto c = area.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool bipolar = s.getProperties().getWithDefault ("bipolar", false);
    const float arcR = radius - 3.0f;

    // track
    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    if (std::abs (angle - from) > 0.01f)
    {
        juce::Path value;
        value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (accent.withAlpha (0.25f));
        g.strokePath (value, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (accent);
        g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // body
    const float bodyR = radius - 10.0f;
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (c.x - bodyR, c.y - bodyR + 2.5f, bodyR * 2.0f, bodyR * 2.0f);
    juce::ColourGradient grad (juce::Colour (0xff30333a), c.x - bodyR * 0.6f, c.y - bodyR * 0.8f,
                               juce::Colour (0xff121316), c.x + bodyR * 0.5f, c.y + bodyR, true);
    g.setGradientFill (grad);
    g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

    // pointer
    juce::Path p;
    p.addRoundedRectangle (-1.3f, -bodyR + 4.0f, 2.6f, bodyR * 0.45f, 1.3f);
    g.setColour (text);
    g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

void BurialLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
    g.setColour (panel);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) w - 16.0f, ay = (float) h * 0.5f;
    arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
    arrow.lineTo (ax, ay + 2.5f);
    arrow.lineTo (ax + 4.0f, ay - 2.0f);
    g.setColour (accent);
    g.strokePath (arrow, juce::PathStrokeType (1.5f));
}

juce::Font BurialLookAndFeel::getLabelFont (juce::Label& l)
{
    if (dynamic_cast<juce::Slider*> (l.getParentComponent()) != nullptr)
        return makeFont (13.0f);
    return LookAndFeel_V4::getLabelFont (l);
}

juce::Font BurialLookAndFeel::getComboBoxFont (juce::ComboBox&) { return makeFont (14.0f); }
juce::Font BurialLookAndFeel::getPopupMenuFont() { return makeFont (14.0f); }

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& title, bool bipolar)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 84, 18);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.22f, juce::MathConstants<float>::pi * 2.78f, true);
    slider.getProperties().set ("bipolar", bipolar);
    addAndMakeVisible (slider);

    label.setText (title.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (makeFont (11.5f, true).withExtraKerningFactor (0.18f));
    label.setColour (juce::Label::textColourId, BurialLookAndFeel::dim.brighter (0.35f));
    addAndMakeVisible (label);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);

    if (auto* p = state.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (16));
    slider.setBounds (r);
}

//==============================================================================
BurialVoiceEditor::BurialVoiceEditor (BurialVoiceProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      pitch   (p.apvts, ParamID::pitch,   "Pitch", true),
      formant (p.apvts, ParamID::formant, "Formant", true),
      shadow  (p.apvts, ParamID::shadow,  "Shadow"),
      wobble  (p.apvts, ParamID::wobble,  "Wobble"),
      grit    (p.apvts, ParamID::grit,    "Grit"),
      crackle (p.apvts, ParamID::crackle, "Crackle"),
      lowCut  (p.apvts, ParamID::lowCut,  "Low Cut"),
      highCut (p.apvts, ParamID::highCut, "High Cut"),
      echo    (p.apvts, ParamID::echo,    "Echo"),
      time    (p.apvts, ParamID::time,    "Time"),
      space   (p.apvts, ParamID::space,   "Space"),
      reverb  (p.apvts, ParamID::reverb,  "Reverb"),
      mix     (p.apvts, ParamID::mix,     "Mix"),
      output  (p.apvts, ParamID::output,  "Output")
{
    for (auto* k : { &pitch, &formant, &shadow, &wobble, &grit, &crackle, &lowCut, &highCut,
                     &echo, &time, &space, &reverb, &mix, &output })
        addAndMakeVisible (*k);

    for (int i = 0; i < processor.getNumPrograms(); ++i)
        presetBox.addItem (processor.getProgramName (i), i + 1);
    presetBox.setSelectedId (processor.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this]
    {
        if (const int id = presetBox.getSelectedId(); id > 0)
            processor.setCurrentProgram (id - 1);
    };
    addAndMakeVisible (presetBox);

    setLookAndFeel (&lnf); // after the children exist, so they all pick it up
    setSize (kW, kH);
}

BurialVoiceEditor::~BurialVoiceEditor() { setLookAndFeel (nullptr); }

void BurialVoiceEditor::renderBackground()
{
    background = juce::Image (juce::Image::ARGB, getWidth(), getHeight(), true);
    juce::Graphics g (background);
    const auto bounds = getLocalBounds().toFloat();

    // night sky gradient with a faint warm glow from the top-right (street light)
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff16181d), 0.0f, 0.0f,
                                             juce::Colour (0xff08090b), 0.0f, bounds.getHeight(), false));
    g.fillAll();
    g.setGradientFill (juce::ColourGradient (BurialLookAndFeel::accent.withAlpha (0.10f), bounds.getRight() - 120.0f, -40.0f,
                                             juce::Colours::transparentBlack, bounds.getRight() - 520.0f, 260.0f, true));
    g.fillRect (bounds);

    juce::Random rng (2007);

    // rain
    for (int i = 0; i < 420; ++i)
    {
        const float x = rng.nextFloat() * bounds.getWidth();
        const float y = rng.nextFloat() * bounds.getHeight();
        const float len = 8.0f + rng.nextFloat() * 26.0f;
        g.setColour (juce::Colours::white.withAlpha (0.015f + rng.nextFloat() * 0.035f));
        g.drawLine (x, y, x - len * 0.18f, y + len, 0.7f);
    }

    // film grain
    for (int i = 0; i < 9000; ++i)
    {
        const int x = rng.nextInt (getWidth()), y = rng.nextInt (getHeight());
        g.setColour (juce::Colours::white.withAlpha (rng.nextFloat() * 0.05f));
        g.fillRect (x, y, 1, 1);
    }

    // vignette
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, bounds.getCentreX(), bounds.getCentreY(),
                                             juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true));
    g.fillRect (bounds);

    // title
    g.setColour (BurialLookAndFeel::text);
    g.setFont (makeFont (30.0f).withExtraKerningFactor (0.42f));
    g.drawText ("BURIAL VOICE", margin, 18, 460, 34, juce::Justification::centredLeft);
    g.setColour (BurialLookAndFeel::dim);
    g.setFont (makeFont (11.5f).withExtraKerningFactor (0.25f));
    g.drawText ("GHOST VOCAL PROCESSOR", margin + 2, 50, 460, 16, juce::Justification::centredLeft);

    g.setFont (makeFont (11.0f, true).withExtraKerningFactor (0.18f));
    g.drawText ("PRESET", getWidth() - margin - 230 - 62, 26, 56, 22, juce::Justification::centredRight);

    // section panels
    for (auto& s : sections)
    {
        const auto r = s.area.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.30f));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

        g.setColour (BurialLookAndFeel::accent.withAlpha (0.85f));
        g.setFont (makeFont (11.0f, true).withExtraKerningFactor (0.3f));
        g.drawText (s.name, s.area.getX() + 12, s.area.getY() + 6, s.area.getWidth() - 24, 16, juce::Justification::centredLeft);
    }

    g.setColour (BurialLookAndFeel::dim.withAlpha (0.7f));
    g.setFont (makeFont (10.5f).withExtraKerningFactor (0.15f));
    g.drawText ("double-click a knob to reset", margin, getHeight() - 24, 400, 16, juce::Justification::centredLeft);
    g.drawText ("v1.0", getWidth() - margin - 100, getHeight() - 24, 100, 16, juce::Justification::centredRight);
}

void BurialVoiceEditor::paint (juce::Graphics& g)
{
    if (background.isValid())
        g.drawImageAt (background, 0, 0);
    else
        g.fillAll (BurialLookAndFeel::bg);
}

void BurialVoiceEditor::resized()
{
    presetBox.setBounds (getWidth() - margin - 230, 26, 230, 24);

    sections.clear();
    int x = margin;
    const int top = headerH;

    auto place = [&] (const juce::String& name, int cols, std::initializer_list<std::initializer_list<Knob*>> rows)
    {
        const juce::Rectangle<int> area (x, top, cols * colW, sectionLabelH + rowH * 2 + 8);
        sections.push_back ({ name, area });

        int rowY = top + sectionLabelH;
        for (auto& row : rows)
        {
            const int count = (int) row.size();
            int kx = x + (cols - count) * colW / 2;
            for (auto* k : row)
            {
                k->setBounds (kx, rowY, colW, rowH - 6);
                kx += colW;
            }
            rowY += rowH;
        }
        x += cols * colW + sectionGap;
    };

    place ("VOICE",   2, { { &pitch, &formant }, { &shadow } });
    place ("TEXTURE", 2, { { &wobble, &grit }, { &crackle } });
    place ("TONE",    1, { { &lowCut }, { &highCut } });
    place ("SPACE",   2, { { &echo, &time }, { &space, &reverb } });
    place ("OUT",     1, { { &mix }, { &output } });

    renderBackground();
}
