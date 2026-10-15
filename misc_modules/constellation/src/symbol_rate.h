#pragma once
#include <dsp/types.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>
#include "spectrum.h"

class SymbolRateEstimator {
public:
    explicit SymbolRateEstimator(double sampleRate) : rate(sampleRate), envelope(windowSize), spectrum(windowSize) {}

    void push(const dsp::complex_t* input, int count) {
        for (int index = 0; index < count;) {
            if (skip) {
                size_t skipped = std::min(skip, static_cast<size_t>(count - index));
                skip -= skipped;
                index += static_cast<int>(skipped);
                continue;
            }
            double real = input[index].re;
            double imag = input[index].im;
            double power = real * real + imag * imag;
            envelope[position++] = std::isfinite(power) ? power : 0;
            ++index;
            if (position == windowSize) {
                estimate();
                position = 0;
                skip = rate / 2 > windowSize ? static_cast<size_t>(rate / 2) - windowSize : 0;
            }
        }
    }

    double symbolRate() const { return stable >= 2 ? estimateRate : 0; }
    double confidence() const { return stable >= 2 ? estimateConfidence : 0; }

private:
    void estimate() {
        double average = 0;
        for (double value : envelope) { average += value; }
        average /= windowSize;
        for (size_t index = 0; index < windowSize; ++index) {
            double window = 0.5 - 0.5 * std::cos(2 * 3.14159265358979323846 * index / (windowSize - 1));
            spectrum[index] = {(envelope[index] - average) * window, 0};
        }
        constellation::fft(spectrum);
        size_t lower = windowSize / 64;
        size_t upper = windowSize / 2 - 2;
        size_t peak = lower;
        std::vector<double> powers;
        powers.reserve(upper - lower);
        double total = 0;
        for (size_t index = lower; index < upper; ++index) {
            double power = std::norm(spectrum[index]);
            powers.push_back(power);
            total += power;
            if (power > std::norm(spectrum[peak])) { peak = index; }
        }
        auto middle = powers.begin() + powers.size() / 2;
        std::nth_element(powers.begin(), middle, powers.end());
        double peakPower = std::norm(spectrum[peak]);
        double ratio = peakPower / std::max(*middle, 1e-30);
        if (average < 1e-16 || ratio < 35 || total <= 0 || peakPower / total < 0.01) {
            stable = 0;
            estimateRate = 0;
            return;
        }
        double left = std::log(std::max(std::norm(spectrum[peak - 1]), 1e-30));
        double center = std::log(std::max(peakPower, 1e-30));
        double right = std::log(std::max(std::norm(spectrum[peak + 1]), 1e-30));
        double denominator = left - 2 * center + right;
        double fraction = std::abs(denominator) > 1e-12 ? std::clamp(0.5 * (left - right) / denominator, -0.5, 0.5) : 0;
        double candidate = (peak + fraction) * rate / windowSize;
        if (estimateRate > 0 && std::abs(candidate / estimateRate - 1) < 0.01) {
            ++stable;
            estimateRate = estimateRate * 0.8 + candidate * 0.2;
        } else { estimateRate = candidate; stable = 1; }
        estimateConfidence = std::clamp((std::log10(ratio) - 1.5) / 2, 0.0, 1.0);
    }

    static constexpr size_t windowSize = 32768;
    double rate;
    std::vector<double> envelope;
    std::vector<std::complex<double>> spectrum;
    size_t position = 0;
    size_t skip = 0;
    int stable = 0;
    double estimateRate = 0;
    double estimateConfidence = 0;
};
