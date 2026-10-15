#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace constellation {
    inline void fft(std::vector<std::complex<double>>& spectrum) {
        size_t count = spectrum.size();
        for (size_t index = 1, reversed = 0; index < count; ++index) {
            size_t bit = count >> 1;
            while (reversed & bit) { reversed ^= bit; bit >>= 1; }
            reversed ^= bit;
            if (index < reversed) { std::swap(spectrum[index], spectrum[reversed]); }
        }
        for (size_t length = 2; length <= count; length <<= 1) {
            double angle = -2 * 3.14159265358979323846 / length;
            std::complex<double> increment(std::cos(angle), std::sin(angle));
            for (size_t start = 0; start < count; start += length) {
                std::complex<double> phase(1, 0);
                for (size_t index = 0; index < length / 2; ++index) {
                    auto even = spectrum[start + index];
                    auto odd = spectrum[start + index + length / 2] * phase;
                    spectrum[start + index] = even + odd;
                    spectrum[start + index + length / 2] = even - odd;
                    phase *= increment;
                }
            }
        }
    }
}
