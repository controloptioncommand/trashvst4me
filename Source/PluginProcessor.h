#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/SpectralShifter.h"
#include "DSP/Effects.h"

namespace ParamID
{
    inline constexpr const char* pitch   = "pitch";
    inline constexpr const char* formant = "formant";
    inline constexpr const char* shadow  = "shadow";
    inline constexpr const char* wobble  = "wobble";
    inline constexpr const char* grit    = "grit";
    inline constexpr const char* crackle = "crackle";
    inline constexpr const char* lowCut  = "lowcut";
    inline constexpr const char* highCut = "highcut";
    inline constexpr const char* echo    = "echo";
    inline constexpr const char* time    = "time";
    inline constexpr const char* space   = "space";
    inline constexpr const char* reverb  = "reverb";
    inline constexpr const char* mix     = "mix";
    inline constexpr const char* output  = "output";
}

class BurialVoiceProcessor : public juce::AudioProcessor
{
public:
    BurialVoiceProcessor();
    ~BurialVoiceProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float param (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    std::vector<SpectralShifter> shifters;
    bv::Wobble wobble;
    bv::Grit grit;
    bv::Echo echo;
    bv::Crackle crackle;
    juce::Reverb reverb;
    juce::dsp::StateVariableTPTFilter<float> highPass, lowPass;
    std::vector<bv::DelayLine> dryDelay;

    juce::AudioBuffer<float> dryBuffer;
    juce::SmoothedValue<float> mixSm, outSm, wobbleSm, gritSm, crackleSm, echoSm, lowCutSm, highCutSm;

    int totalLatency = 0;
    int currentProgram = 0;
    double sr = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BurialVoiceProcessor)
};
