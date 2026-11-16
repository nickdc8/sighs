// Drives SighsProcessor the way a host would, without a host.

#include "../Source/PluginProcessor.h"

static int failures = 0;

static void check (bool ok, const juce::String& what)
{
    std::cout << "  [" << (ok ? "ok" : "FAIL") << "] " << what << std::endl;
    failures += ok ? 0 : 1;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;

    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int channels : { 1, 2 })
            for (int blockSize : { 32, 512, 1024 })
            {
                SighsProcessor p;
                const auto set = channels == 1 ? juce::AudioChannelSet::mono()
                                               : juce::AudioChannelSet::stereo();
                juce::AudioProcessor::BusesLayout layout;
                layout.inputBuses.add (set);
                layout.outputBuses.add (set);
                const bool layoutOk = p.setBusesLayout (layout);

                p.setRateAndBufferSizeDetails (rate, blockSize);
                p.prepareToPlay (rate, blockSize);

                juce::AudioBuffer<float> buffer (channels, blockSize);
                juce::MidiBuffer midi;
                juce::Random random (3);
                bool finite = true;
                double energy = 0.0;
                float peak = 0.0f;

                for (int b = 0; b < (int) (rate * 3.0) / blockSize; ++b)
                {
                    for (int ch = 0; ch < channels; ++ch)
                        for (int i = 0; i < blockSize; ++i)
                            buffer.setSample (ch, i, 0.25f * (random.nextFloat() * 2.0f - 1.0f));

                    p.processBlock (buffer, midi);

                    for (int ch = 0; ch < channels; ++ch)
                        for (int i = 0; i < blockSize; ++i)
                        {
                            const float v = buffer.getSample (ch, i);
                            finite = finite && std::isfinite (v);
                            peak = juce::jmax (peak, std::abs (v));
                            energy += (double) v * v;
                        }
                }

                check (layoutOk && finite && energy > 1.0 && peak < 4.0f,
                       juce::String (rate) + " Hz, " + juce::String (channels) + " ch, block "
                           + juce::String (blockSize) + ", peak " + juce::String (peak, 2)
                           + ", latency " + juce::String (p.getLatencySamples()));
            }

    {
        SighsProcessor a, b;
        a.apvts.getParameter ("speed")->setValueNotifyingHost (0.9f);
        a.apvts.getParameter ("vibrato")->setValueNotifyingHost (1.0f);
        juce::MemoryBlock state;
        a.getStateInformation (state);
        b.setStateInformation (state.getData(), (int) state.getSize());
        check (juce::approximatelyEqual (b.apvts.getParameter ("speed")->getValue(),
                                         a.apvts.getParameter ("speed")->getValue())
                   && b.apvts.getParameter ("vibrato")->getValue() > 0.5f,
               "state round-trips");
    }

    {
        SighsProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        check (editor != nullptr && editor->getWidth() == 620, "editor constructs");

        const auto shot = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile ("build/editor.png"));
        out.deleteFile();
        if (auto stream = out.createOutputStream())
            juce::PNGImageFormat().writeImageToStream (shot, *stream);
    }

    std::cout << (failures == 0 ? "All checks passed" : "CHECKS FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
