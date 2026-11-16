# Sighs

A photocell chorus/vibrato plugin for macOS, modelled on the Shin-ei Uni-Vibe
(the "Bridge of Sighs" sound). Built with JUCE 9 as a VST3, an AU and a
standalone app.

It is named "Sighs" because Uni-Vibe is a Dunlop trademark.

## Install locations

Building installs the plugins automatically:

- VST3: `~/Library/Audio/Plug-Ins/VST3/Sighs.vst3`
- AU: `~/Library/Audio/Plug-Ins/Components/Sighs.component`
- Standalone: `build/Sighs_artefacts/Release/Standalone/Sighs.app`

## Controls

| Control | Range | What it does |
| --- | --- | --- |
| Volume | -24 to +12 dB | Output level |
| Intensity | 0 to 1 | LFO swing across the lamp, i.e. sweep depth |
| Speed | 0.1 to 10 Hz | LFO rate (the original foot pedal tops out around 7.6 Hz) |
| Chorus / Vibrato | switch | Chorus is an even mix of dry and shifted signal; Vibrato is shifted only |
| Drive | -12 to +24 dB | Input gain into the preamp and stage clipping |
| Lamp Trim | 0 to 1 | The internal lamp bias trimpot; shifts where the sweep sits |

The lamp in the corner of the UI pulses with the modelled filament.

**Bridge of Sighs starting point:** Chorus mode, Speed around 1-1.5 Hz,
Intensity 0.8 or higher, placed before your amp sim or fuzz.

## How it is modelled

Circuit data comes from Darabundit, Wedelich and Bischoff, "Digital Grey Box
Model of the Uni-Vibe Effects Pedal" (DAFx 2019), which measured an original
late-60s unit.

**Phase stages.** Four staggered stages using the measured values:

| Stage | Capacitor | Non-inverting gain | Inverting gain | LDR min | LDR max |
| --- | --- | --- | --- | --- | --- |
| 1 | 15 nF | 1.01 | 1.11 | 12.7 k | 2.79 M |
| 2 | 220 nF | 0.98 | 1.09 | 6.86 k | 2.59 M |
| 3 | 470 pF | 0.97 | 1.10 | 7.69 k | 3.32 M |
| 4 | 4.7 nF | 0.95 | 1.09 | 6.22 k | 4.16 M |

Each stage is modelled as the imperfect discrete phase splitter it really is,
not an ideal all-pass. The unbalanced gains and the blocking capacitor give
each stage a small high shelf, which is part of the throb.

**Lamp and photocells.** The paper drove its model from measured wavetables
that are not published, so this uses a parametric model instead: LFO, lamp
driver with bias, filament thermal lag, light output, then cadmium-sulfide
photocells that respond quickly when lit and recover slowly in the dark. It is
tuned so the resistance minimums land on the measured values, and at 4 Hz full
intensity the first cell peaks around 1.5 M against roughly 1.2 M in the paper.

**Signal path.** Preamp (input coupling, asymmetric soft clip, mild treble
loss), four phase stages each with a mild asymmetric clip, then the
chorus/vibrato mix. Everything runs at 2x oversampling, because stage 3 sweeps
up to about 27 kHz and the stages clip. Reported latency is 49 samples.

### Estimates, not from a source

- Preamp tone: 25 Hz high-pass, 11 kHz low-pass.
- All clipping amounts.
- The 1 uF blocking capacitor and 4.7 k series resistor (from memory of the
  schematic, not confirmed against one).
- Lamp and photocell time constants.

### Not modelled

- The Speed/Intensity interaction the paper describes on the real unit.
- Unit-to-unit variation; the values are those of the one pedal the paper measured.

## Status

- The stage cascade matches the published transfer function to within 2% at
  1 kHz, and stays finite under full-scale noise at extreme settings.
- A headless test runs the processor at 44.1/48/96 kHz, mono and stereo, block
  sizes 32 to 1024. State save/restore works and the editor renders.
- Not yet loaded in a real host.
- `auval` did not find the AU ("didn't find the component"). The bundle is
  signed and its plist looks right, so macOS may just need a logout or reboot
  to rescan; that is unconfirmed.
- The VST3 has not been run through a validator such as pluginval.
- How close it sounds to a real unit has not been judged by ear.

## Building

Requires Xcode and CMake. JUCE is not committed; clone it into the project first:

```sh
git clone --depth 1 --branch 9.0.5 https://github.com/juce-framework/JUCE.git JUCE
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 10
```

The build is native (arm64 on Apple silicon), minimum macOS 12.

## Tests

```sh
# DSP only, no JUCE: photocell ranges, notch positions, stress test.
# Pass a path to also render an audition clip.
clang++ -std=c++20 -O2 tests/vibe_test.cpp -o build/vibe_test
build/vibe_test build/sighs_demo.wav

# The JUCE processor, driven the way a host would.
cmake --build build --target SighsProcessorTest -j 10
build/SighsProcessorTest_artefacts/Release/SighsProcessorTest
```

The second test also writes a snapshot of the UI to `build/editor.png`.

## Layout

- `Source/VibeDSP.h`: the whole model, with no JUCE dependency. The lamp and
  photocell constants are at the top if the throb shape needs adjusting.
- `Source/PluginProcessor.*`: parameters, oversampling, state.
- `Source/PluginEditor.*`: the UI.
- `tests/`: the two test programs above.

## Sources

- [Digital Grey Box Model of the Uni-Vibe Effects Pedal (DAFx 2019)](https://ccrma.stanford.edu/~champ/files/DAFx2019_paper_31.pdf)
- [Pat Jones's Univibe clone notes](https://www.openvmshobbyist.com/~patj/univibe.txt)
