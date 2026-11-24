#pragma once
// A small live spectrum for the multiband strip. Pure C++ (no JUCE): a radix-2 FFT and the dB conversion.
#include <cmath>
#include <complex>
#include <vector>

namespace spectrum
{
    constexpr int fftSize = 4096;

    // In-place radix-2 FFT (size must be a power of two).
    inline void fft(std::vector<std::complex<float>>& a)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(a[i], a[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const float ang = -6.28318530718f / (float) len;
            const std::complex<float> wl(std::cos(ang), std::sin(ang));
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<float> w(1.f, 0.f);
                for (size_t k = 0; k < len / 2; ++k)
                {
                    const auto u = a[i + k], v = a[i + k + len / 2] * w;
                    a[i + k] = u + v;
                    a[i + k + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
    }

    // Magnitude in dB (0 dB = a full-scale sine) for bins 0..n/2, from n windowed samples.
    inline void magnitudesDb(const float* samples, std::vector<float>& outDb)
    {
        std::vector<std::complex<float>> a((size_t) fftSize);
        for (int i = 0; i < fftSize; ++i)
        {
            const float w = 0.5f - 0.5f * std::cos(6.28318530718f * (float) i / (float) fftSize); // Hann
            a[(size_t) i] = { samples[i] * w, 0.f };
        }
        fft(a);
        outDb.resize((size_t) fftSize / 2);
        const float norm = 4.f / (float) fftSize; // a full-scale sine gives magnitude 1
        for (size_t k = 0; k < outDb.size(); ++k)
            outDb[k] = 20.f * std::log10(std::max(1e-7f, std::abs(a[k]) * norm));
    }
}
