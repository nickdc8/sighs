#include "PluginProcessor.h"
#include "PluginEditor.h"

SighsProcessor::SighsProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "state", createLayout())
{
    speed     = apvts.getRawParameterValue ("speed");
    intensity = apvts.getRawParameterValue ("intensity");
    volume    = apvts.getRawParameterValue ("volume");
    drive     = apvts.getRawParameterValue ("drive");
    lampTrim  = apvts.getRawParameterValue ("lampTrim");
    vibrato   = apvts.getRawParameterValue ("vibrato");
}

juce::AudioProcessorValueTreeState::ParameterLayout SighsProcessor::createLayout()
{
    using Range = juce::NormalisableRange<float>;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // The original's foot pedal runs from a standstill to about 7.6 Hz.
    Range speedRange (0.1f, 10.0f, 0.01f);
    speedRange.setSkewForCentre (1.8f);

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "speed", 1 }, "Speed", speedRange, 1.2f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "intensity", 1 }, "Intensity", Range (0.0f, 1.0f, 0.001f), 0.8f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "volume", 1 }, "Volume", Range (-24.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "drive", 1 }, "Drive", Range (-12.0f, 24.0f, 0.1f), 3.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lampTrim", 1 }, "Lamp Trim", Range (0.0f, 1.0f, 0.001f), 0.4f));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "vibrato", 1 }, "Mode", juce::StringArray { "Chorus", "Vibrato" }, 0));

    return layout;
}

vibe::Params SighsProcessor::readParams() const
{
    vibe::Params p;
    p.speedHz   = speed->load();
    p.intensity = intensity->load();
    p.lampTrim  = lampTrim->load();
    p.drive     = juce::Decibels::decibelsToGain (drive->load());
    p.volume    = juce::Decibels::decibelsToGain (volume->load());
    p.vibrato   = vibrato->load() > 0.5f;
    return p;
}

bool SighsProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && layouts.getMainInputChannelSet() == out;
}

void SighsProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // 2x oversampling: stage 3 sweeps up to ~27 kHz, and the stages clip.
    const auto channels = (size_t) juce::jmax (1, getTotalNumOutputChannels());
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        channels, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    oversampling->initProcessing ((size_t) samplesPerBlock);
    setLatencySamples (juce::roundToInt (oversampling->getLatencyInSamples()));

    engine.setParams (readParams());
    engine.prepare (sampleRate * 2.0);
}

void SighsProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    engine.setParams (readParams());

    juce::dsp::AudioBlock<float> block (buffer);
    auto up = oversampling->processSamplesUp (block);

    float* channels[vibe::Engine::kMaxChannels] = {};
    const int numChannels = juce::jmin ((int) up.getNumChannels(), vibe::Engine::kMaxChannels);
    for (int ch = 0; ch < numChannels; ++ch)
        channels[ch] = up.getChannelPointer ((size_t) ch);

    engine.process (channels, numChannels, (int) up.getNumSamples());

    oversampling->processSamplesDown (block);
}

juce::AudioProcessorEditor* SighsProcessor::createEditor()
{
    return new SighsEditor (*this);
}

void SighsProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SighsProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SighsProcessor();
}
