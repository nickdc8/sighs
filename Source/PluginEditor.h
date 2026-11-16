#pragma once

#include "PluginProcessor.h"

class VibeLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;
};

class SighsEditor final : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit SighsEditor (SighsProcessor&);
    ~SighsEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::String caption;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void timerCallback() override;
    void addKnob (Knob&, const juce::String& paramId, const juce::String& caption);
    void updateModeButtons();

    SighsProcessor& vibeProcessor;
    VibeLookAndFeel lookAndFeel;

    Knob volume, intensity, speed, drive, lampTrim;
    juce::TextButton chorusButton { "CHORUS" }, vibratoButton { "VIBRATO" };
    juce::ParameterAttachment modeAttachment;

    juce::Rectangle<int> lampArea;
    float lamp = 0.0f;
    bool isVibrato = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SighsEditor)
};
