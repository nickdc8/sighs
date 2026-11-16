// Offline check of the vibe engine: photocell ranges, notch positions,
// numerical sanity, and a rendered audition file.
//
//   clang++ -std=c++20 -O2 tests/vibe_test.cpp -o build/vibe_test && build/vibe_test [out.wav]

#include "../Source/VibeDSP.h"

#include <complex>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

static constexpr double kFs = 96000.0;
static constexpr double kPi = 3.14159265358979323846;

static int failures = 0;

static void check (bool ok, const char* what)
{
    std::printf ("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (! ok)
        ++failures;
}

// Small-signal response of the four stages for given LDR resistances.
static std::complex<double> wetResponse (double hz, const std::array<double, 4>& r)
{
    const std::complex<double> s (0.0, 2.0 * kPi * hz);
    std::complex<double> h (1.0, 0.0);

    for (size_t k = 0; k < 4; ++k)
    {
        const auto& st = vibe::kStages[k];
        const double kc = st.cp / (st.cp + vibe::kCdc);
        const double ke = vibe::kCdc / (st.cp + vibe::kCdc);
        const double wo = (st.cp + vibe::kCdc) / ((r[k] + vibe::kR6) * st.cp * vibe::kCdc);
        h *= st.alpha * ke * wo / (s + wo) - st.beta * (kc * wo + s) / (s + wo);
    }

    return h;
}

static void printNotches (const char* label, const std::array<double, 4>& r)
{
    std::printf ("  %-22s notches (chorus):", label);
    double prev = 0.0, prevPrev = 0.0, prevHz = 0.0;

    for (double hz = 20.0; hz < 20000.0; hz *= 1.01)
    {
        const double mag = std::abs (0.5 * (1.0 + wetResponse (hz, r)));
        if (prevPrev > prev && mag > prev && prev < 0.35)
            std::printf ("  %.0f Hz (%.1f dB)", prevHz, 20.0 * std::log10 (prev));
        prevPrev = prev;
        prev = mag;
        prevHz = hz;
    }

    std::printf ("\n");
}

static void ldrRanges (float speed, float intensity)
{
    vibe::Engine e;
    vibe::Params p;
    p.speedHz = speed;
    p.intensity = intensity;
    e.setParams (p);
    e.prepare (kFs);

    std::vector<float> buf (64, 0.0f);
    float* io[1] = { buf.data() };
    std::array<double, 4> lo, hi;
    lo.fill (1e12);
    hi.fill (0.0);

    const int blocks = (int) (kFs * 6.0 / 64.0);
    for (int b = 0; b < blocks; ++b)
    {
        e.process (io, 1, 64);
        if (b > blocks / 2)
            for (size_t k = 0; k < 4; ++k)
            {
                lo[k] = std::min (lo[k], e.getLdrResistance (k));
                hi[k] = std::max (hi[k], e.getLdrResistance (k));
            }
    }

    std::printf ("  %4.1f Hz int %.2f |", speed, intensity);
    for (size_t k = 0; k < 4; ++k)
        std::printf (" %7.1fk-%7.1fk", lo[k] / 1e3, hi[k] / 1e3);
    std::printf ("\n");
}

static void writeWav (const char* path, const std::vector<float>& mono)
{
    FILE* f = std::fopen (path, "wb");
    if (f == nullptr)
        return;

    const uint32_t rate = 48000, dataBytes = (uint32_t) mono.size() * 2;
    const uint32_t riff = 36 + dataBytes, fmtLen = 16, byteRate = rate * 2;
    const uint16_t pcm = 1, chans = 1, align = 2, bits = 16;

    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riff, 4, 1, f);
    std::fwrite ("WAVEfmt ", 1, 8, f); std::fwrite (&fmtLen, 4, 1, f);
    std::fwrite (&pcm, 2, 1, f); std::fwrite (&chans, 2, 1, f);
    std::fwrite (&rate, 4, 1, f); std::fwrite (&byteRate, 4, 1, f);
    std::fwrite (&align, 2, 1, f); std::fwrite (&bits, 2, 1, f);
    std::fwrite ("data", 1, 4, f); std::fwrite (&dataBytes, 4, 1, f);

    for (float s : mono)
    {
        const int16_t v = (int16_t) std::lrint (std::clamp (s, -1.0f, 1.0f) * 32767.0f);
        std::fwrite (&v, 2, 1, f);
    }

    std::fclose (f);
}

// Plucked-string chord (Karplus-Strong) at 96 kHz, as a stand-in for a guitar.
static std::vector<float> pluckedChords (double seconds)
{
    std::vector<float> out ((size_t) (seconds * kFs), 0.0f);
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> noise (-1.0f, 1.0f);

    const double chords[2][4] = { { 110.0, 164.81, 220.0, 261.63 },    // Am
                                  { 98.0, 146.83, 196.0, 246.94 } };   // G

    const double strum = 2.0;
    for (size_t c = 0; (double) c * strum < seconds; ++c)
        for (size_t note = 0; note < 4; ++note)
        {
            const size_t len = (size_t) (kFs / chords[c % 2][note]);
            std::vector<float> line (len);
            for (auto& v : line)
                v = noise (rng);

            // Soften the excitation so it reads as a string rather than a click.
            for (int pass = 0; pass < 6; ++pass)
                for (size_t i = 1; i < len; ++i)
                    line[i] = 0.5f * (line[i] + line[i - 1]);

            size_t idx = 0;
            const size_t start = (size_t) (((double) c * strum + (double) note * 0.03) * kFs);
            for (size_t n = start; n < out.size() && n < start + (size_t) (strum * 1.5 * kFs); ++n)
            {
                const size_t next = (idx + 1) % len;
                out[n] += 0.5f * line[idx];
                line[idx] = 0.4985f * (line[idx] + line[next]);
                idx = next;
            }
        }

    return out;
}

int main (int argc, char** argv)
{
    std::printf ("LDR resistance swing per stage (paper: min 12.7k/6.9k/7.7k/6.2k, "
                 "LDR1 peaks ~1.2M at 4 Hz full intensity)\n");
    for (float speed : { 0.5f, 2.0f, 4.0f, 7.6f })
        for (float intensity : { 0.2f, 0.5f, 0.7f, 1.0f })
            ldrRanges (speed, intensity);

    std::printf ("\nStatic notch positions across the sweep\n");
    printNotches ("dark (all at max)", { 2.79e6, 2.59e6, 3.32e6, 4.16e6 });
    printNotches ("300k", { 300e3, 300e3, 300e3, 300e3 });
    printNotches ("100k", { 100e3, 100e3, 100e3, 100e3 });
    printNotches ("30k", { 30e3, 30e3, 30e3, 30e3 });
    printNotches ("bright (all at min)", { 12.7e3, 6.86e3, 7.69e3, 6.22e3 });

    std::printf ("\nEngine vs analytic response (static, 1 kHz, small signal)\n");
    {
        vibe::Engine e;
        vibe::Params p;
        p.intensity = 0.0f;
        p.vibrato = true;
        e.setParams (p);
        e.prepare (kFs);

        const int n = (int) kFs;
        std::vector<float> buf ((size_t) n);
        for (int i = 0; i < n; ++i)
            buf[(size_t) i] = 0.001f * (float) std::sin (2.0 * kPi * 1000.0 * i / kFs);
        float* io[1] = { buf.data() };
        e.process (io, 1, n);

        double peak = 0.0;
        for (int i = n / 2; i < n; ++i)
            peak = std::max (peak, (double) std::abs (buf[(size_t) i]));

        std::array<double, 4> r;
        for (size_t k = 0; k < 4; ++k)
            r[k] = e.getLdrResistance (k);

        // 0.8 is the vibrato-mode wet gain; the preamp low-pass is ~0.996 at 1 kHz.
        const double expected = 0.001 * 0.8 * std::abs (wetResponse (1000.0, r))
                                / std::sqrt (1.0 + std::pow (1000.0 / 11000.0, 2.0));
        std::printf ("  measured %.6f, analytic %.6f\n", peak, expected);
        check (std::abs (peak / expected - 1.0) < 0.02, "stage cascade matches eq. 13 within 2%");
    }

    std::printf ("\nStress: full-scale noise, extreme settings\n");
    {
        vibe::Engine e;
        vibe::Params p;
        p.speedHz = 10.0f;
        p.intensity = 1.0f;
        p.lampTrim = 1.0f;
        p.drive = 16.0f;
        e.setParams (p);
        e.prepare (kFs);

        std::mt19937 rng (1);
        std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
        std::vector<float> l (512), r (512);
        float* io[2] = { l.data(), r.data() };
        bool finite = true;
        float peak = 0.0f;

        for (int b = 0; b < 2000; ++b)
        {
            for (size_t i = 0; i < 512; ++i)
                l[i] = r[i] = noise (rng);

            p.vibrato = (b / 100) % 2 == 1;
            p.speedHz = (b % 7 == 0) ? 0.1f : 10.0f;
            e.setParams (p);
            e.process (io, 2, 512);

            for (size_t i = 0; i < 512; ++i)
            {
                finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
                peak = std::max (peak, std::abs (l[i]));
            }
        }

        std::printf ("  peak %.2f\n", peak);
        check (finite, "output stays finite");
        check (peak < 20.0f, "output stays bounded");
    }

    if (argc > 1)
    {
        // Slow, deep chorus-mode throb in the Bridge of Sighs neighbourhood.
        auto dry = pluckedChords (12.0);
        vibe::Engine e;
        vibe::Params p;
        p.speedHz = 1.1f;
        p.intensity = 0.85f;
        p.drive = 1.5f;
        e.setParams (p);
        e.prepare (kFs);

        auto wet = dry;
        float* io[1] = { wet.data() };
        e.process (io, 1, (int) wet.size());

        // Dry first half for comparison is not needed; write wet, decimated 2:1
        // (the content is well below 24 kHz after the preamp low-pass).
        std::vector<float> out;
        for (size_t i = 0; i + 1 < wet.size(); i += 2)
            out.push_back (0.5f * (wet[i] + wet[i + 1]));

        writeWav (argv[1], out);
        std::printf ("\nWrote %s\n", argv[1]);
    }

    std::printf ("\n%s\n", failures == 0 ? "All checks passed" : "CHECKS FAILED");
    return failures == 0 ? 0 : 1;
}
