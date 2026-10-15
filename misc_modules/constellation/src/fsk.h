#pragma once
#include "classifier.h"
#include "spectrum.h"
#include <complex>

namespace constellation {
    struct FskResult {
        double symbolRate = 0;
        double offset = 0;
        double deviation = 0;
        double spread = 0;
        double coherence = 0;
        std::vector<dsp::complex_t> points;
    };

    inline FskResult analyzeFsk(const std::vector<dsp::complex_t>& input, double sampleRate, double manualRate = 0) {
        FskResult best;
        if (input.size() < 2048 || (input.size() & (input.size() - 1)) || !std::isfinite(sampleRate) || sampleRate <= 0) { return best; }
        size_t count = input.size() - 1;
        std::vector<double> frequency(count), power(count);
        double averagePower = 0;
        for (size_t index = 0; index < count; ++index) {
            const auto& previous = input[index];
            const auto& current = input[index + 1];
            double real = current.re * previous.re + current.im * previous.im;
            double imag = current.im * previous.re - current.re * previous.im;
            frequency[index] = std::atan2(imag, real) * sampleRate / (2 * 3.14159265358979323846);
            power[index] = std::min(current.re * current.re + current.im * current.im, previous.re * previous.re + previous.im * previous.im);
            if (!std::isfinite(frequency[index]) || !std::isfinite(power[index])) { return best; }
            averagePower += power[index];
        }
        averagePower /= count;
        if (averagePower < 1e-16) { return best; }
        double mean = 0;
        double variance = 0;
        size_t valid = 0;
        for (size_t index = 0; index < count; ++index) {
            if (power[index] < averagePower * 0.25) { continue; }
            mean += frequency[index];
            variance += frequency[index] * frequency[index];
            ++valid;
        }
        if (valid < count / 2) { return best; }
        mean /= valid;
        double deviation = std::sqrt(std::max(0.0, variance / valid - mean * mean));
        double lower = mean - deviation;
        double upper = mean + deviation;
        for (int iteration = 0; iteration < 6; ++iteration) {
            double lowerSum = 0, upperSum = 0;
            size_t lowerCount = 0, upperCount = 0;
            double center = (lower + upper) / 2;
            for (size_t index = 0; index < count; ++index) {
                if (power[index] < averagePower * 0.25) { continue; }
                if (frequency[index] < center) { lowerSum += frequency[index]; ++lowerCount; }
                else { upperSum += frequency[index]; ++upperCount; }
            }
            if (lowerCount < valid / 5 || upperCount < valid / 5) { return best; }
            lower = lowerSum / lowerCount;
            upper = upperSum / upperCount;
        }
        if (upper - lower < sampleRate * 0.01 || std::max(std::abs(lower), std::abs(upper)) > sampleRate * 0.43) { return best; }
        double center = (lower + upper) / 2;
        std::vector<double> crossings;
        std::vector<std::complex<double>> spectrum(input.size());
        for (size_t index = 0; index + 1 < count; ++index) {
            double before = frequency[index] - center;
            double after = frequency[index + 1] - center;
            if (before * after >= 0 || power[index] < averagePower * 0.25 || power[index + 1] < averagePower * 0.25) { continue; }
            double fraction = -before / (after - before);
            crossings.push_back(index + fraction);
            double window = 0.5 - 0.5 * std::cos(2 * 3.14159265358979323846 * index / (input.size() - 1));
            spectrum[index] += (1 - fraction) * window;
            spectrum[index + 1] += fraction * window;
        }
        if (crossings.size() < 128) { return best; }
        std::vector<double> candidates;
        if (manualRate > 0) { candidates.push_back(manualRate); }
        else {
            fft(spectrum);
            size_t start = input.size() / 64;
            size_t end = input.size() / 2 - 2;
            std::vector<double> powers;
            std::vector<size_t> peaks;
            for (size_t index = start; index < end; ++index) {
                powers.push_back(std::norm(spectrum[index]));
                if (std::norm(spectrum[index]) > std::norm(spectrum[index - 1]) && std::norm(spectrum[index]) > std::norm(spectrum[index + 1])) { peaks.push_back(index); }
            }
            auto middle = powers.begin() + powers.size() / 2;
            std::nth_element(powers.begin(), middle, powers.end());
            double floor = std::max(*middle, 1e-30);
            std::sort(peaks.begin(), peaks.end(), [&](size_t first, size_t second) { return std::norm(spectrum[first]) > std::norm(spectrum[second]); });
            for (size_t index = 0; index < std::min<size_t>(6, peaks.size()); ++index) {
                size_t peak = peaks[index];
                if (std::norm(spectrum[peak]) / floor < 35) { continue; }
                double left = std::log(std::max(std::norm(spectrum[peak - 1]), 1e-30));
                double maximum = std::log(std::max(std::norm(spectrum[peak]), 1e-30));
                double right = std::log(std::max(std::norm(spectrum[peak + 1]), 1e-30));
                double denominator = left - 2 * maximum + right;
                double fraction = std::abs(denominator) > 1e-12 ? std::clamp(0.5 * (left - right) / denominator, -0.5, 0.5) : 0;
                candidates.push_back((peak + fraction) * sampleRate / input.size());
            }
        }
        for (double rate : candidates) {
            if (!std::isfinite(rate) || sampleRate / rate < 2 || sampleRate / rate > 64) { continue; }
            std::complex<double> moment = 0;
            for (double position : crossings) {
                double angle = 2 * 3.14159265358979323846 * position * rate / sampleRate;
                moment += std::complex<double>(std::cos(angle), std::sin(angle));
            }
            double coherence = std::abs(moment) / crossings.size();
            if (coherence < 0.82) { continue; }
            size_t alignedIntervals = 0;
            size_t longIntervals = 0;
            size_t singleIntervals = 0;
            for (size_t index = 1; index < crossings.size(); ++index) {
                double interval = (crossings[index] - crossings[index - 1]) * rate / sampleRate;
                if (std::abs(interval - std::round(interval)) < 0.2) { ++alignedIntervals; }
                if (interval > 1.5) { ++longIntervals; }
                if (interval > 0.5 && interval < 1.5) { ++singleIntervals; }
            }
            if (alignedIntervals < (crossings.size() - 1) * 0.85 || longIntervals < crossings.size() * 0.05 || singleIntervals < crossings.size() * 0.1) { continue; }
            double phase = std::arg(moment) / (2 * 3.14159265358979323846);
            std::vector<double> levels;
            for (double position = (phase + 1.5) * sampleRate / rate; position + 1 < count; position += sampleRate / rate) {
                size_t index = static_cast<size_t>(position);
                if (std::min(power[index], power[index + 1]) < averagePower * 0.25) { continue; }
                double fraction = position - index;
                levels.push_back(frequency[index] * (1 - fraction) + frequency[index + 1] * fraction);
            }
            if (levels.size() < 256) { continue; }
            double lowerSum = 0, upperSum = 0;
            size_t lowerCount = 0, upperCount = 0;
            for (double level : levels) {
                if (level < center) { lowerSum += level; ++lowerCount; }
                else { upperSum += level; ++upperCount; }
            }
            if (lowerCount < levels.size() / 5 || upperCount < levels.size() / 5) { continue; }
            double low = lowerSum / lowerCount;
            double high = upperSum / upperCount;
            double midpoint = (low + high) / 2;
            double half = (high - low) / 2;
            if (half < sampleRate * 0.005) { continue; }
            double error = 0;
            size_t inside = 0;
            for (double level : levels) {
                double normalized = (level - midpoint) / half;
                error += std::pow(std::abs(normalized) - 1, 2);
                if (std::abs(normalized) < 0.5) { ++inside; }
            }
            double spread = std::sqrt(error / levels.size());
            if (spread > 0.25 || inside > levels.size() * 0.08 || (best.symbolRate && coherence - spread < best.coherence - best.spread)) { continue; }
            best = {rate, midpoint, half, spread, coherence, {}};
            for (double level : levels) { best.points.push_back({static_cast<float>((level - midpoint) / half), 0}); }
        }
        return best;
    }

    class FskDetector {
    public:
        explicit FskDetector(double sampleRate, double manualRate = 0) : sampleRate(sampleRate), manualRate(manualRate) { window.reserve(windowSize); }

        void push(const dsp::complex_t* input, int count) {
            points.clear();
            for (int index = 0; index < count; ++index) {
                if (skip) { --skip; continue; }
                window.push_back(input[index]);
                if (window.size() != windowSize) { continue; }
                auto candidate = analyzeFsk(window, sampleRate, manualRate);
                if (candidate.symbolRate && previousRate && std::abs(candidate.symbolRate / previousRate - 1) < 0.01) { ++stable; }
                else { stable = candidate.symbolRate ? 1 : 0; }
                previousRate = candidate.symbolRate;
                result = std::move(candidate);
                if (stable >= 2) { points = std::move(result.points); }
                else { result.symbolRate = 0; }
                window.clear();
                skip = sampleRate / 4 > windowSize ? static_cast<size_t>(sampleRate / 4) - windowSize : 0;
            }
        }

        FskResult result;
        std::vector<dsp::complex_t> points;

    private:
        static constexpr size_t windowSize = 32768;
        double sampleRate;
        double manualRate;
        double previousRate = 0;
        int stable = 0;
        size_t skip = 0;
        std::vector<dsp::complex_t> window;
    };
}
