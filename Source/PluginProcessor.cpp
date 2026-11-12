#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Parameter order used by the preset table.
    const char* const presetOrder[] = {
        ParamID::pitch, ParamID::formant, ParamID::shadow, ParamID::wobble, ParamID::grit,
        ParamID::crackle, ParamID::lowCut, ParamID::highCut, ParamID::echo, ParamID::time,
        ParamID::space, ParamID::reverb, ParamID::mix, ParamID::output
    };

    struct Preset
    {
        const char* name;
        float values[14];
    };

    //                       pitch formant shadow wobble grit  crackle lowcut highcut echo  time   space reverb mix  out
    const Preset presets[] = {
        { "Untrue",        {  5.0f,  3.0f, 0.15f, 0.25f, 0.20f, 0.25f, 200.0f, 7000.0f, 0.20f, 375.0f, 0.85f, 0.40f, 1.0f,  0.0f } },
        { "Angel Up",      {  7.0f,  7.0f, 0.00f, 0.15f, 0.30f, 0.15f, 300.0f, 9000.0f, 0.15f, 300.0f, 0.90f, 0.50f, 1.0f, -2.0f } },
        { "Ghost Down",    { -5.0f, -2.0f, 0.00f, 0.30f, 0.25f, 0.20f, 150.0f, 4500.0f, 0.25f, 450.0f, 0.85f, 0.45f, 1.0f, -3.0f } },
        { "Night Bus",     {  3.0f,  1.0f, 0.30f, 0.40f, 0.20f, 0.50f, 350.0f, 3500.0f, 0.35f, 500.0f, 0.90f, 0.55f, 1.0f, -3.5f } },
        { "Pirate Radio",  {  4.0f,  4.0f, 0.00f, 0.20f, 0.60f, 0.60f, 500.0f, 3000.0f, 0.10f, 250.0f, 0.60f, 0.25f, 1.0f,  0.0f } },
        { "Choir of One",  {  5.0f,  2.0f, 0.60f, 0.20f, 0.10f, 0.15f, 180.0f, 6000.0f, 0.30f, 600.0f, 0.95f, 0.60f, 1.0f, -3.0f } },
        { "Clean",         {  0.0f,  0.0f, 0.00f, 0.00f, 0.00f, 0.00f,  20.0f, 20000.0f, 0.00f, 375.0f, 0.50f, 0.00f, 1.0f,  0.0f } },
    };

    juce::String pct (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
    juce::String semis (float v, int)
    {
        const auto s = juce::String (v, 1);
        return (v > 0.04f ? "+" + s : s) + " st";
    }
    juce::String hz (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
    juce::String ms (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; }
    juce::String db (float v, int) { return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB"; }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout BurialVoiceProcessor::createLayout()
{
    using P = juce::AudioParameterFloat;
    using A = juce::AudioParameterFloatAttributes;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto add = [&] (const char* id, const char* name, juce::NormalisableRange<float> range, float def,
                    juce::String (*fmt) (float, int))
    {
        layout.add (std::make_unique<P> (juce::ParameterID { id, 1 }, name, range, def,
                                         A().withStringFromValueFunction (fmt)));
    };

    auto skewed = [] (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    };

    add (ParamID::pitch,   "Pitch",    { -12.0f, 12.0f, 0.1f }, 5.0f,  semis);
    add (ParamID::formant, "Formant",  { -12.0f, 12.0f, 0.1f }, 3.0f,  semis);
    add (ParamID::shadow,  "Shadow",   { 0.0f, 1.0f }, 0.15f, pct);
    add (ParamID::wobble,  "Wobble",   { 0.0f, 1.0f }, 0.25f, pct);
    add (ParamID::grit,    "Grit",     { 0.0f, 1.0f }, 0.20f, pct);
    add (ParamID::crackle, "Crackle",  { 0.0f, 1.0f }, 0.25f, pct);
    add (ParamID::lowCut,  "Low Cut",  skewed (20.0f, 1500.0f, 200.0f), 200.0f, hz);
    add (ParamID::highCut, "High Cut", skewed (1000.0f, 20000.0f, 5000.0f), 7000.0f, hz);
    add (ParamID::echo,    "Echo",     { 0.0f, 1.0f }, 0.20f, pct);
    add (ParamID::time,    "Time",     skewed (60.0f, 1000.0f, 350.0f), 375.0f, ms);
    add (ParamID::space,   "Space",    { 0.0f, 1.0f }, 0.85f, pct);
    add (ParamID::reverb,  "Reverb",   { 0.0f, 1.0f }, 0.40f, pct);
    add (ParamID::mix,     "Mix",      { 0.0f, 1.0f }, 1.0f,  pct);
    add (ParamID::output,  "Output",   { -24.0f, 12.0f, 0.1f }, 0.0f, db);

    return layout;
}

BurialVoiceProcessor::BurialVoiceProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BurialVoice", createLayout())
{
}

bool BurialVoiceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
void BurialVoiceProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    const int numCh = juce::jmax (1, getTotalNumOutputChannels());

    shifters.resize ((size_t) numCh);
    for (auto& s : shifters)
        s.prepare (sampleRate);

    wobble.prepare (sampleRate, numCh);
    grit.prepare (numCh);
    echo.prepare (sampleRate, numCh);
    crackle.prepare (sampleRate, numCh);
    reverb.setSampleRate (sampleRate);

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, (juce::uint32) numCh };
    highPass.prepare (spec);
    lowPass.prepare (spec);
    highPass.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    lowPass.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    highPass.setResonance (0.7071f);
    lowPass.setResonance (0.85f);

    totalLatency = shifters.front().getLatency() + wobble.getLatency();
    setLatencySamples (totalLatency);

    dryDelay.resize ((size_t) numCh);
    for (auto& d : dryDelay)
        d.prepare (totalLatency + 8);

    dryBuffer.setSize (numCh, samplesPerBlock, false, false, true);

    for (auto* s : { &mixSm, &outSm, &wobbleSm, &gritSm, &crackleSm, &echoSm })
        s->reset (sampleRate, 0.05);
    lowCutSm.reset (sampleRate, 0.05);
    highCutSm.reset (sampleRate, 0.05);

    mixSm.setCurrentAndTargetValue (param (ParamID::mix));
    outSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param (ParamID::output)));
    wobbleSm.setCurrentAndTargetValue (param (ParamID::wobble));
    gritSm.setCurrentAndTargetValue (param (ParamID::grit));
    crackleSm.setCurrentAndTargetValue (param (ParamID::crackle));
    echoSm.setCurrentAndTargetValue (param (ParamID::echo));
    lowCutSm.setCurrentAndTargetValue (param (ParamID::lowCut));
    highCutSm.setCurrentAndTargetValue (param (ParamID::highCut));
    highPass.setCutoffFrequency (lowCutSm.getCurrentValue());
    lowPass.setCutoffFrequency (juce::jmin (highCutSm.getCurrentValue(), (float) sampleRate * 0.45f));

    reset();
}

void BurialVoiceProcessor::reset()
{
    for (auto& s : shifters) s.reset();
    for (auto& d : dryDelay) d.reset();
    wobble.reset();
    grit.reset();
    echo.reset();
    crackle.reset();
    reverb.reset();
    highPass.reset();
    lowPass.reset();
}

//==============================================================================
void BurialVoiceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels();
    const int numCh = juce::jmin (buffer.getNumChannels(), (int) shifters.size());
    const int n = buffer.getNumSamples();

    for (int ch = numIn; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);

    if (dryBuffer.getNumSamples() < n)
        dryBuffer.setSize (dryBuffer.getNumChannels(), n, false, false, true);

    // ---- latency-compensated dry copy ----------------------------------------------
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* x = buffer.getReadPointer (ch);
        auto* d = dryBuffer.getWritePointer (ch);
        auto& line = dryDelay[(size_t) ch];
        for (int i = 0; i < n; ++i)
        {
            line.push (x[i]);
            d[i] = line.readInt (totalLatency);
        }
    }

    // ---- pitch / formant / shadow ------------------------------------------------------
    const float pitch = param (ParamID::pitch);
    const float formant = param (ParamID::formant);
    const float shadow = param (ParamID::shadow);
    for (int ch = 0; ch < numCh; ++ch)
    {
        shifters[(size_t) ch].setParameters (pitch, formant, shadow);
        shifters[(size_t) ch].process (buffer.getWritePointer (ch), n);
    }

    // ---- wobble -> grit -> filters -> echo ------------------------------------------------
    wobbleSm.setTargetValue (param (ParamID::wobble));
    gritSm.setTargetValue (param (ParamID::grit));
    echoSm.setTargetValue (param (ParamID::echo));
    lowCutSm.setTargetValue (param (ParamID::lowCut));
    highCutSm.setTargetValue (param (ParamID::highCut));
    echo.setTimeMs (param (ParamID::time));

    const float nyquistGuard = (float) sr * 0.45f;

    for (int i = 0; i < n; ++i)
    {
        const float wobDelay = wobble.tick (wobbleSm.getNextValue());
        const float g = gritSm.getNextValue();
        const float e = echoSm.getNextValue();
        const float echoTime = echo.nextTimeSamples();

        if (lowCutSm.isSmoothing())
            highPass.setCutoffFrequency (lowCutSm.getNextValue());
        if (highCutSm.isSmoothing())
            lowPass.setCutoffFrequency (juce::jmin (highCutSm.getNextValue(), nyquistGuard));

        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* w = buffer.getWritePointer (ch);
            float x = wobble.process (ch, w[i], wobDelay);
            x = grit.process (ch, x, g);
            x = highPass.processSample (ch, x);
            x = lowPass.processSample (ch, x);
            x = echo.process (ch, x, e, echoTime);
            w[i] = x;
        }
    }

    // ---- reverb ----------------------------------------------------------------------------
    {
        const float space = param (ParamID::space);
        const float amount = param (ParamID::reverb);
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.3f + 0.68f * space;
        rp.damping = 0.6f - 0.2f * space;
        rp.width = 1.0f;
        rp.wetLevel = amount * 0.45f;
        rp.dryLevel = 0.5f * (1.0f - 0.35f * amount);
        rp.freezeMode = 0.0f;
        reverb.setParameters (rp);

        if (numCh >= 2)
            reverb.processStereo (buffer.getWritePointer (0), buffer.getWritePointer (1), n);
        else if (numCh == 1)
            reverb.processMono (buffer.getWritePointer (0), n);
    }

    // ---- crackle, dry/wet, output ----------------------------------------------------------
    mixSm.setTargetValue (param (ParamID::mix));
    outSm.setTargetValue (juce::Decibels::decibelsToGain (param (ParamID::output)));
    crackleSm.setTargetValue (param (ParamID::crackle));

    for (int i = 0; i < n; ++i)
    {
        const float m = mixSm.getNextValue();
        const float gain = outSm.getNextValue();
        const float c = crackleSm.getNextValue();

        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* w = buffer.getWritePointer (ch);
            const float wet = w[i] + crackle.next (ch, c);
            const float dry = dryBuffer.getSample (ch, i);
            w[i] = (dry * (1.0f - m) + wet * m) * gain;
        }
    }
}

//==============================================================================
int BurialVoiceProcessor::getNumPrograms() { return (int) std::size (presets); }

const juce::String BurialVoiceProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, getNumPrograms()) ? presets[index].name : "";
}

void BurialVoiceProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return;

    currentProgram = index;
    for (size_t i = 0; i < std::size (presetOrder); ++i)
        if (auto* p = apvts.getParameter (presetOrder[i]))
            p->setValueNotifyingHost (p->convertTo0to1 (presets[index].values[i]));
}

void BurialVoiceProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void BurialVoiceProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = state.getProperty ("program", 0);
            apvts.replaceState (state);
        }
}

juce::AudioProcessorEditor* BurialVoiceProcessor::createEditor() { return new BurialVoiceEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BurialVoiceProcessor(); }
