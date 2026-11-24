#pragma once
// A small live spectrum for the multiband strip. Pure C++ (no JUCE): a radix-2 FFT and the dB conversion.
// The analyser owns its buffers, window and twiddle factors, so a call allocates nothing (the editor runs it 30 times a second).
#include <cmath>
#include <complex>
#include <vector>

namespace spectrum
{
    constexpr int fftSize = 4096;

    class Analyser
    {
    public:
        Analyser() : buf((size_t) fftSize), window((size_t) fftSize), twiddle((size_t) fftSize / 2), rev((size_t) fftSize)
        {
            for (int i = 0; i < fftSize; ++i)
                window[(size_t) i] = 0.5f - 0.5f * std::cos(6.28318530718f * (float) i / (float) fftSize);   // Hann
            for (int k = 0; k < fftSize / 2; ++k)
            {
                const double a = -6.283185307179586 * (double) k / (double) fftSize;
                twiddle[(size_t) k] = { (float) std::cos(a), (float) std::sin(a) };
            }
            for (int i = 1, j = 0; i < fftSize; ++i)   // bit-reversal table
            {
                int bit = fftSize >> 1;
                for (; j & bit; bit >>= 1) j ^= bit;
                j ^= bit;
                rev[(size_t) i] = j;
            }
        }

        // Magnitude in dB (0 dB = a full-scale sine) for bins 0..n/2, from fftSize samples.
        void magnitudesDb(const float* samples, std::vector<float>& outDb)
        {
            for (int i = 0; i < fftSize; ++i) buf[(size_t) rev[(size_t) i]] = { samples[i] * window[(size_t) i], 0.f };
            for (int len = 2; len <= fftSize; len <<= 1)
            {
                const int half = len / 2, stride = fftSize / len;
                for (int i = 0; i < fftSize; i += len)
                    for (int k = 0; k < half; ++k)
                    {
                        const auto u = buf[(size_t) (i + k)], v = buf[(size_t) (i + k + half)] * twiddle[(size_t) (k * stride)];
                        buf[(size_t) (i + k)] = u + v;
                        buf[(size_t) (i + k + half)] = u - v;
                    }
            }
            outDb.resize((size_t) fftSize / 2);
            const float norm = 4.f / (float) fftSize;   // a full-scale sine gives magnitude 1
            for (size_t k = 0; k < outDb.size(); ++k)
                outDb[k] = 20.f * std::log10(std::max(1e-7f, std::abs(buf[k]) * norm));
        }

    private:
        std::vector<std::complex<float>> buf, twiddle;
        std::vector<float> window;
        std::vector<int> rev;
    };
}
