#include "PluginEditor.h"

namespace
{
const juce::Colour panelTop    { 0xff3a3d40 };
const juce::Colour panelBottom { 0xff1f2123 };
const juce::Colour ink         { 0xffe9e4d6 };
const juce::Colour inkDim      { 0xff9a968b };
const juce::Colour lampColour  { 0xffffb347 };

juce::Font labelFont (float size, bool bold = true)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}
}

void VibeLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float startAngle, float endAngle,
                                        juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (10.0f);
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    // Scale ticks around the knob.
    g.setColour (inkDim);
    for (int i = 0; i <= 10; ++i)
    {
        const float a = startAngle + (float) i / 10.0f * (endAngle - startAngle);
        const auto dir = juce::Point<float> (std::sin (a), -std::cos (a));
        g.drawLine ({ centre + dir * (radius + 3.0f), centre + dir * (radius + 8.0f) },
                    i % 5 == 0 ? 1.8f : 1.0f);
    }

    // Skirt, then the fluted black cap.
    const auto skirt = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (skirt.translated (0.0f, 3.0f));
    g.setGradientFill ({ juce::Colour (0xff4a4a4a), centre.x, skirt.getY(),
                         juce::Colour (0xff101010), centre.x, skirt.getBottom(), false });
    g.fillEllipse (skirt);

    const auto cap = skirt.reduced (radius * 0.22f);
    g.setGradientFill ({ juce::Colour (0xff2c2c2c), centre.x, cap.getY(),
                         juce::Colour (0xff050505), centre.x, cap.getBottom(), false });
    g.fillEllipse (cap);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawEllipse (cap, 1.0f);

    // Pointer.
    juce::Path pointer;
    pointer.addRoundedRectangle (-1.6f, -radius + 2.0f, 3.2f, radius * 0.55f, 1.6f);
    g.setColour (ink);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

SighsEditor::SighsEditor (SighsProcessor& p)
    : AudioProcessorEditor (p),
      vibeProcessor (p),
      modeAttachment (*p.apvts.getParameter ("vibrato"),
                      [this] (float v) { isVibrato = v > 0.5f; updateModeButtons(); })
{
    setLookAndFeel (&lookAndFeel);

    addKnob (volume, "volume", "VOLUME");
    addKnob (intensity, "intensity", "INTENSITY");
    addKnob (speed, "speed", "SPEED");
    addKnob (drive, "drive", "DRIVE");
    addKnob (lampTrim, "lampTrim", "LAMP TRIM");

    for (auto* b : { &chorusButton, &vibratoButton })
    {
        b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff17181a));
        b->setColour (juce::TextButton::buttonOnColourId, ink);
        b->setColour (juce::TextButton::textColourOffId, inkDim);
        b->setColour (juce::TextButton::textColourOnId, juce::Colour (0xff17181a));
        addAndMakeVisible (*b);
    }

    chorusButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    vibratoButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
    chorusButton.onClick  = [this] { modeAttachment.setValueAsCompleteGesture (0.0f); };
    vibratoButton.onClick = [this] { modeAttachment.setValueAsCompleteGesture (1.0f); };
    modeAttachment.sendInitialUpdate();

    setSize (620, 380);
    startTimerHz (30);
}

SighsEditor::~SighsEditor()
{
    setLookAndFeel (nullptr);
}

void SighsEditor::addKnob (Knob& knob, const juce::String& paramId, const juce::String& caption)
{
    knob.caption = caption;
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    knob.slider.setPopupDisplayEnabled (true, true, this);
    addAndMakeVisible (knob.slider);
    knob.attachment = std::make_unique<SliderAttachment> (vibeProcessor.apvts, paramId, knob.slider);
}

void SighsEditor::updateModeButtons()
{
    chorusButton.setToggleState (! isVibrato, juce::dontSendNotification);
    vibratoButton.setToggleState (isVibrato, juce::dontSendNotification);
}

void SighsEditor::timerCallback()
{
    const float next = vibeProcessor.getLampBrightness();
    if (std::abs (next - lamp) > 0.004f)
    {
        lamp = next;
        repaint (lampArea.expanded (40));
    }
}

void SighsEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setGradientFill ({ panelTop, 0.0f, 0.0f, panelBottom, 0.0f, bounds.getBottom(), false });
    g.fillAll();

    // Hammertone-ish speckle, seeded so it doesn't shimmer on repaint.
    juce::Random random (20);
    for (int i = 0; i < 1400; ++i)
    {
        g.setColour (juce::Colours::white.withAlpha (random.nextFloat() * 0.035f));
        g.fillEllipse (random.nextFloat() * bounds.getWidth(), random.nextFloat() * bounds.getHeight(),
                       2.5f, 1.5f);
    }

    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (bounds.reduced (6.0f), 8.0f, 1.5f);

    g.setColour (ink);
    g.setFont (labelFont (30.0f));
    g.drawText ("SIGHS", 28, 20, 200, 34, juce::Justification::centredLeft);
    g.setColour (inkDim);
    g.setFont (labelFont (11.5f, false));
    g.drawText ("PHOTOCELL CHORUS / VIBRATO", 30, 52, 260, 16, juce::Justification::centredLeft);

    // Lamp: glow scales with the modelled filament.
    const auto lampCentre = lampArea.toFloat().getCentre();
    const float glow = 26.0f + 30.0f * lamp;
    g.setGradientFill ({ lampColour.withAlpha (0.10f + 0.55f * lamp), lampCentre.x, lampCentre.y,
                         lampColour.withAlpha (0.0f), lampCentre.x + glow, lampCentre.y, true });
    g.fillEllipse (juce::Rectangle<float> (glow * 2.0f, glow * 2.0f).withCentre (lampCentre));
    g.setColour (juce::Colour (0xff141414));
    g.fillEllipse (lampArea.toFloat().expanded (3.0f));
    g.setColour (juce::Colour (0xff3b2a14).interpolatedWith (juce::Colour (0xfffff0c8), lamp));
    g.fillEllipse (lampArea.toFloat());

    g.setFont (labelFont (12.0f));
    for (auto* knob : { &volume, &intensity, &speed, &drive, &lampTrim })
    {
        const auto area = knob->slider.getBounds();
        g.setColour (ink);
        g.drawText (knob->caption, area.getX() - 20, area.getBottom() - 2, area.getWidth() + 40, 16,
                    juce::Justification::centred);
    }
}

void SighsEditor::resized()
{
    lampArea = juce::Rectangle<int> (22, 22).withCentre ({ getWidth() - 48, 44 });

    const int big = 132, top = 92;
    const int gap = (getWidth() - 3 * big) / 4;
    volume.slider.setBounds (gap, top, big, big);
    intensity.slider.setBounds (gap * 2 + big, top, big, big);
    speed.slider.setBounds (gap * 3 + big * 2, top, big, big);

    const int small = 78, lower = 262;
    drive.slider.setBounds (58, lower, small, small);
    lampTrim.slider.setBounds (getWidth() - 58 - small, lower, small, small);

    const int buttonW = 96, buttonH = 34;
    chorusButton.setBounds (getWidth() / 2 - buttonW, lower + 22, buttonW, buttonH);
    vibratoButton.setBounds (getWidth() / 2, lower + 22, buttonW, buttonH);
}
