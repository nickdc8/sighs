#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include "VibeDSP.h"

class SighsProcessor final : public juce::AudioProcessor
{
public:
    SighsProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sighs"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    float getLampBrightness() const { return engine.getLampBrightness(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    vibe::Params readParams() const;

    vibe::Engine engine;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;

    std::atomic<float>* speed = nullptr;
    std::atomic<float>* intensity = nullptr;
    std::atomic<float>* volume = nullptr;
    std::atomic<float>* drive = nullptr;
    std::atomic<float>* lampTrim = nullptr;
    std::atomic<float>* vibrato = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SighsProcessor)
};
