// Photocell vibe engine: a model of the Shin-ei Uni-Vibe signal path.
//
// No JUCE dependency, so the offline test harness can compile it on its own.
//
// Circuit data comes from Darabundit, Wedelich & Bischoff, "Digital Grey Box
// Model of the Uni-Vibe Effects Pedal", DAFx-19 (Table 1 and eqs. 3-18), which
// measured an original late-60s unit:
//
//   stage   Cp      alpha  beta   LDR min   LDR max
//     1     15 nF   1.01   1.11   12.7 k    2.79 M
//     2     220 nF  0.98   1.09   6.86 k    2.59 M
//     3     470 pF  0.97   1.10   7.69 k    3.32 M
//     4     4.7 nF  0.95   1.09   6.22 k    4.16 M
//
// Each stage is a discrete phase splitter, NOT an ideal all-pass:
//
//   H(s) = alpha * ke*wo/(s+wo)  -  beta * (kc*wo + s)/(s+wo)
//   kc = Cp/(Cp+Cdc), ke = Cdc/(Cp+Cdc), wo = (Cp+Cdc) / (R' Cp Cdc), R' = LDR + R6
//
// which rearranges to  H(s) = -beta + (A + beta) * wo/(s+wo),  A = alpha*ke - beta*kc
// i.e. an inverted direct path plus a scaled one-pole low-pass. That form is
// what runs below, with a trapezoidal (TPT) low-pass so it stays stable while
// the cutoff is modulated every sample.
//
// The paper drives the stages from measured LDR wavetables, which are not
// published. Here the lamp and photocells are a parametric model instead
// (filament thermal lag -> light -> CdS power law -> asymmetric response),
// calibrated against the paper's resistance ranges and its Fig. 8.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace vibe
{

struct StageSpec
{
    double cp;      // phasing capacitor (F)
    double alpha;   // non-inverting (emitter) gain
    double beta;    // inverting (collector) gain
    double rMin;    // LDR resistance, lamp at full brightness (ohm)
    double rDark;   // LDR resistance, lamp dark (ohm)
};

inline constexpr std::array<StageSpec, 4> kStages {{
    { 15.0e-9,   1.01, 1.11, 12.7e3, 2.79e6 },
    { 220.0e-9,  0.98, 1.09, 6.86e3, 2.59e6 },
    { 470.0e-12, 0.97, 1.10, 7.69e3, 3.32e6 },
    { 4.7e-9,    0.95, 1.09, 6.22e3, 4.16e6 },
}};

inline constexpr double kCdc = 1.0e-6;   // emitter blocking cap in series with the LDR
inline constexpr double kR6  = 4.7e3;    // fixed resistor in series with the LDR

// Lamp / photocell model constants (estimates, tuned in tests/vibe_test.cpp).
inline constexpr double kLampHeatTau  = 0.014;  // filament warming (s)
inline constexpr double kLampCoolTau  = 0.020;  // filament cooling (s)
inline constexpr double kLightExp     = 1.7;    // light ~ power^1.7 (~ V^3.4)
inline constexpr double kLdrGamma     = 0.8;    // CdS: R ~ light^-gamma
inline constexpr double kLdrOnTau     = 0.008;  // resistance falling (lit) (s)
inline constexpr double kLdrOffTau    = 0.038;  // resistance rising (dark) (s)

struct Params
{
    float speedHz   = 1.8f;   // LFO rate
    float intensity = 0.7f;   // 0..1, LFO swing across the lamp
    float lampTrim  = 0.4f;   // 0..1, the internal lamp bias trimpot
    float drive     = 1.0f;   // linear input gain into the preamp
    float volume    = 1.0f;   // linear output gain
    bool  vibrato   = false;  // false = chorus (dry + shifted), true = shifted only
};

class Engine
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kControlInterval = 8;  // samples between lamp/LDR updates

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        controlDt = kControlInterval / fs;

        paramCoef     = 1.0 - std::exp (-controlDt / 0.03);
        gainCoef      = 1.0 - std::exp (-1.0 / (0.02 * fs));
        lampHeatCoef  = 1.0 - std::exp (-controlDt / kLampHeatTau);
        lampCoolCoef  = 1.0 - std::exp (-controlDt / kLampCoolTau);
        ldrOnCoef     = 1.0 - std::exp (-controlDt / kLdrOnTau);
        ldrOffCoef    = 1.0 - std::exp (-controlDt / kLdrOffTau);

        inputHpCoef   = onePoleCoef (25.0);
        preampLpCoef  = onePoleCoef (11000.0);
        dcBlockCoef   = onePoleCoef (8.0);

        for (size_t k = 0; k < 4; ++k)
        {
            const auto& s = kStages[k];
            const double kc = s.cp / (s.cp + kCdc);
            const double ke = kCdc / (s.cp + kCdc);
            dcGain[k]  = s.alpha * ke - s.beta * kc;
            seriesC[k] = s.cp * kCdc / (s.cp + kCdc);
        }

        reset();
    }

    void reset()
    {
        speedSm = target.speedHz;
        intensitySm = target.intensity;
        trimSm = target.lampTrim;
        driveSm = target.drive;
        volumeSm = target.volume;
        dryGainSm = target.vibrato ? 0.0 : 0.5;
        wetGainSm = target.vibrato ? 0.8 : 0.5;

        phase = 0.0;
        controlCounter = 0;

        // Settle the lamp and photocells at the resting bias point.
        const double v = lampBias (trimSm);
        filament = v * v;
        const double light = std::pow (filament, kLightExp);
        for (size_t k = 0; k < 4; ++k)
        {
            logR[k] = std::log (ldrTarget (k, light));
            g[k] = stageG (k);
            gInc[k] = 0.0;
        }

        for (auto& c : channels)
            c = {};
    }

    void setParams (const Params& p) { target = p; }

    // 0..1-ish lamp brightness for the UI.
    float getLampBrightness() const { return lampForUi.load (std::memory_order_relaxed); }

    // Current LDR resistance (ohm), for the test harness.
    double getLdrResistance (size_t stage) const { return std::exp (logR[stage]); }

    void process (float* const* io, int numChannels, int numSamples)
    {
        numChannels = std::min (numChannels, kMaxChannels);

        const double dryTarget = target.vibrato ? 0.0 : 0.5;
        const double wetTarget = target.vibrato ? 0.8 : 0.5;

        for (int n = 0; n < numSamples; ++n)
        {
            if (controlCounter == 0)
                controlTick();

            controlCounter = (controlCounter + 1) % kControlInterval;

            for (size_t k = 0; k < 4; ++k)
                g[k] += gInc[k];

            driveSm   += gainCoef * (target.drive  - driveSm);
            volumeSm  += gainCoef * (target.volume - volumeSm);
            dryGainSm += gainCoef * (dryTarget - dryGainSm);
            wetGainSm += gainCoef * (wetTarget - wetGainSm);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& c = channels[(size_t) ch];
                double x = (double) io[ch][n] * driveSm;

                // Preamp: input coupling, asymmetric soft clip, mild treble loss.
                c.inputHp += inputHpCoef * (x - c.inputHp);
                x = saturate (x - c.inputHp, 0.45, 0.20);
                c.preampLp += preampLpCoef * (x - c.preampLp);
                x = c.preampLp;

                const double dry = x;

                // Four staggered phase stages.
                for (size_t k = 0; k < 4; ++k)
                {
                    x = saturate (x, 0.22, 0.12);

                    const double G = g[k] / (1.0 + g[k]);
                    const double v = (x - c.lp[k]) * G;
                    const double lp = v + c.lp[k];
                    c.lp[k] = lp + v;

                    x = -kStages[k].beta * x + (dcGain[k] + kStages[k].beta) * lp;
                }

                double y = dryGainSm * dry + wetGainSm * x;

                // The clipping bias leaves a signal-dependent DC offset.
                c.dcBlock += dcBlockCoef * (y - c.dcBlock);
                y -= c.dcBlock;

                io[ch][n] = (float) (y * volumeSm);
            }
        }
    }

private:
    struct ChannelState
    {
        double inputHp = 0.0, preampLp = 0.0, dcBlock = 0.0;
        std::array<double, 4> lp {};
    };

    double onePoleCoef (double hz) const
    {
        return 1.0 - std::exp (-2.0 * 3.14159265358979323846 * hz / fs);
    }

    // tanh clipper with a bias for asymmetry, normalised to unity small-signal
    // gain and zero output at rest (after DAFx-19 sec. 3.2.1).
    static double saturate (double x, double gain, double bias)
    {
        const double tb = std::tanh (bias);
        return (std::tanh (gain * x + bias) - tb) / (gain * (1.0 - tb * tb));
    }

    static double lampBias (double trim) { return 0.12 + 0.20 * trim; }

    static double ldrTarget (size_t k, double light)
    {
        const auto& s = kStages[k];
        const double lit = s.rMin * std::pow (std::max (light, 1.0e-9), -kLdrGamma);
        return 1.0 / (1.0 / lit + 1.0 / s.rDark);
    }

    double stageG (size_t k) const
    {
        const double r = std::exp (logR[k]) + kR6;
        const double wo = 1.0 / (r * seriesC[k]);
        // Stage 3 reaches ~27 kHz at full brightness; keep the prewarp sane.
        return std::tan (std::min (wo / (2.0 * fs), 1.40));
    }

    void controlTick()
    {
        speedSm     += paramCoef * (target.speedHz   - speedSm);
        intensitySm += paramCoef * (target.intensity - intensitySm);
        trimSm      += paramCoef * (target.lampTrim  - trimSm);

        phase += 2.0 * 3.14159265358979323846 * speedSm * controlDt;
        if (phase > 2.0 * 3.14159265358979323846)
            phase -= 2.0 * 3.14159265358979323846;

        // Phase-shift oscillator, amplitude limited by back-to-back diodes.
        const double lfo = std::tanh (1.5 * std::sin (phase)) / std::tanh (1.5);

        // Lamp driver: bias from the trimpot, swing from the Intensity pot.
        // At high intensity the driver cuts off for part of the cycle.
        const double depth = 0.82 * std::pow (intensitySm, 1.5);
        const double volts = std::clamp (lampBias (trimSm) + depth * lfo, 0.0, 1.2);
        const double power = volts * volts;

        filament += (power > filament ? lampHeatCoef : lampCoolCoef) * (power - filament);
        const double light = std::pow (std::max (filament, 0.0), kLightExp);
        lampForUi.store ((float) std::min (1.0, std::sqrt (filament)), std::memory_order_relaxed);

        for (size_t k = 0; k < 4; ++k)
        {
            const double t = std::log (ldrTarget (k, light));
            logR[k] += (t < logR[k] ? ldrOnCoef : ldrOffCoef) * (t - logR[k]);
            gInc[k] = (stageG (k) - g[k]) / kControlInterval;
        }
    }

    double fs = 96000.0, controlDt = 0.0;
    double paramCoef = 0.0, gainCoef = 0.0;
    double lampHeatCoef = 0.0, lampCoolCoef = 0.0, ldrOnCoef = 0.0, ldrOffCoef = 0.0;
    double inputHpCoef = 0.0, preampLpCoef = 0.0, dcBlockCoef = 0.0;

    std::array<double, 4> dcGain {}, seriesC {};

    Params target;
    double speedSm = 1.8, intensitySm = 0.7, trimSm = 0.4;
    double driveSm = 1.0, volumeSm = 1.0, dryGainSm = 0.5, wetGainSm = 0.5;

    double phase = 0.0, filament = 0.0;
    int controlCounter = 0;
    std::array<double, 4> logR {}, g {}, gInc {};
    std::array<ChannelState, kMaxChannels> channels {};

    std::atomic<float> lampForUi { 0.0f };
};

} // namespace vibe
