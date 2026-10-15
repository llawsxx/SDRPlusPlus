#pragma once
#include <dsp/types.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>
#include "spectrum.h"

namespace constellation {
    enum class Broadcast { Unknown, CDR, DAB };

    struct BroadcastProfile {
        Broadcast type;
        int mode;
        double clock;
        int useful;
        int guard;
        int nullLength;
        int frameLength;
    };

    inline const std::array<BroadcastProfile, 7>& broadcastProfiles() {
        static const std::array<BroadcastProfile, 7> profiles{{
            {Broadcast::CDR, 1, 816000, 2048, 240, 0, 0},
            {Broadcast::CDR, 2, 816000, 1024, 140, 0, 0},
            {Broadcast::CDR, 3, 816000, 2048, 56, 0, 0},
            {Broadcast::DAB, 1, 2048000, 2048, 504, 2656, 196608},
            {Broadcast::DAB, 2, 2048000, 512, 126, 664, 49152},
            {Broadcast::DAB, 3, 2048000, 256, 63, 345, 49152},
            {Broadcast::DAB, 4, 2048000, 1024, 252, 1328, 98304}
        }};
        return profiles;
    }

    struct BroadcastResult {
        Broadcast type = Broadcast::Unknown;
        int mode = 0;
        double confidence = 0;
        double prefix = 0;
        double contrast = 0;
        double nullDepth = 0;
        double bandOffset = 0;
    };

    inline double broadcastPower(const dsp::complex_t& point) {
        return static_cast<double>(point.re) * point.re + static_cast<double>(point.im) * point.im;
    }

    inline BroadcastResult matchBroadcastPrefix(const std::vector<dsp::complex_t>& input, double sampleRate, const BroadcastProfile& profile) {
        BroadcastResult result;
        double delay = profile.useful * sampleRate / profile.clock;
        double guard = profile.guard * sampleRate / profile.clock;
        double period = delay + guard;
        size_t lag = static_cast<size_t>(delay);
        double fraction = delay - lag;
        size_t window = static_cast<size_t>(guard * 0.8);
        size_t step = std::max<size_t>(1, static_cast<size_t>(guard / 8));
        size_t margin = static_cast<size_t>(sampleRate * 0.002);
        if (window < 5 || input.size() < margin * 2 + lag + window + period * 12) { return result; }
        auto delayed = [&](size_t index) {
            return std::complex<double>(input[index + lag].re * (1 - fraction) + input[index + lag + 1].re * fraction,
                input[index + lag].im * (1 - fraction) + input[index + lag + 1].im * fraction);
        };
        std::complex<double> correlation{};
        double firstPower = 0;
        double secondPower = 0;
        auto accumulate = [&](size_t index, double sign) {
            std::complex<double> first(input[index].re, input[index].im);
            auto second = delayed(index);
            correlation += sign * first * std::conj(second);
            firstPower += sign * std::norm(first);
            secondPower += sign * std::norm(second);
        };
        for (size_t index = margin; index < margin + window; ++index) { accumulate(index, 1); }
        std::array<std::complex<double>, 256> folded{};
        std::array<int, 256> counts{};
        size_t bins = std::min<size_t>(folded.size(), static_cast<size_t>(std::ceil(period / step)));
        size_t end = input.size() - margin - lag - window - 1;
        for (size_t index = margin; index < end; ++index) {
            if ((index - margin) % step == 0) {
                auto value = correlation / std::sqrt(std::max(firstPower * secondPower, 1e-60));
                if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) { return {}; }
                size_t bin = static_cast<size_t>(std::fmod(index / period, 1.0) * bins);
                folded[bin] += value / std::max(1.0, std::abs(value));
                ++counts[bin];
            }
            accumulate(index, -1);
            accumulate(index + window, 1);
        }
        std::vector<double> populated;
        double peak = 0;
        for (size_t bin = 0; bin < bins; ++bin) {
            if (counts[bin] < 6) { continue; }
            double value = std::abs(folded[bin]) / counts[bin];
            populated.push_back(value);
            peak = std::max(peak, value);
        }
        if (populated.size() < 20) { return result; }
        auto middle = populated.begin() + populated.size() / 2;
        std::nth_element(populated.begin(), middle, populated.end());
        double contrast = peak - *middle;
        if (peak < 0.65 || contrast < 0.22) { return result; }
        result.type = profile.type;
        result.mode = profile.mode;
        result.prefix = peak;
        result.contrast = contrast;
        result.confidence = std::clamp(peak * contrast / 0.65, 0.0, 1.0);
        return result;
    }

    inline double broadcastNullDepth(const std::vector<dsp::complex_t>& input, double sampleRate, const BroadcastProfile& profile) {
        size_t binSize = std::max<size_t>(1, static_cast<size_t>(profile.nullLength * sampleRate / profile.clock / 8));
        size_t count = input.size() / binSize;
        if (!count) { return 0; }
        std::vector<double> powers(count);
        double average = 0;
        for (size_t bin = 0; bin < count; ++bin) {
            for (size_t index = 0; index < binSize; ++index) { powers[bin] += broadcastPower(input[bin * binSize + index]); }
            average += powers[bin];
        }
        average /= count;
        if (!std::isfinite(average) || average < 1e-20) { return 0; }
        std::vector<double> centers;
        std::vector<double> depths;
        double expectedLength = profile.nullLength * sampleRate / profile.clock / binSize;
        double expectedPeriod = profile.frameLength * sampleRate / profile.clock / binSize;
        for (size_t bin = 1; bin + 1 < count;) {
            if (powers[bin] >= average * 0.35) { ++bin; continue; }
            size_t start = bin;
            double total = 0;
            while (bin < count && powers[bin] < average * 0.35) { total += powers[bin++]; }
            size_t length = bin - start;
            if (bin == count || length < expectedLength * 0.55 || length > expectedLength * 1.5) { continue; }
            centers.push_back((start + bin) * 0.5);
            depths.push_back(1 - total / (length * average));
        }
        double best = 0;
        for (size_t index = 1; index < centers.size(); ++index) {
            if (std::abs(centers[index] - centers[index - 1] - expectedPeriod) < 2) {
                best = std::max(best, std::min(depths[index], depths[index - 1]));
            }
        }
        return best;
    }

    inline BroadcastResult analyzeDab(const std::vector<dsp::complex_t>& input, double sampleRate) {
        BroadcastResult best;
        if (sampleRate < 1600000) { return best; }
        for (const auto& profile : broadcastProfiles()) {
            if (profile.type != Broadcast::DAB) { continue; }
            BroadcastResult candidate;
            size_t length = static_cast<size_t>(profile.frameLength * sampleRate / profile.clock * 0.4);
            for (size_t segment = 0; segment < 3 && (segment + 1) * length <= input.size(); ++segment) {
                std::vector<dsp::complex_t> section(input.begin() + segment * length, input.begin() + (segment + 1) * length);
                auto match = matchBroadcastPrefix(section, sampleRate, profile);
                if (match.confidence > candidate.confidence) { candidate = match; }
            }
            if (candidate.type == Broadcast::Unknown) { continue; }
            candidate.nullDepth = broadcastNullDepth(input, sampleRate, profile);
            if (candidate.nullDepth < 0.65) { continue; }
            candidate.confidence *= candidate.nullDepth;
            if (candidate.confidence > best.confidence) { best = candidate; }
        }
        return best;
    }

    inline BroadcastResult analyzeCdr(const std::vector<dsp::complex_t>& input, double sampleRate, double bandwidth) {
        BroadcastResult best;
        size_t transformSize = 1;
        while (transformSize * 2 <= input.size()) { transformSize *= 2; }
        if (transformSize < 4096) { return best; }
        std::vector<std::complex<double>> spectrum(transformSize);
        for (size_t index = 0; index < transformSize; ++index) { spectrum[index] = {input[index].re, input[index].im}; }
        fft(spectrum);
        size_t outputSize = transformSize;
        while (outputSize > 1024 && sampleRate * outputSize / transformSize >= 256000) { outputSize /= 2; }
        double outputRate = sampleRate * outputSize / transformSize;
        for (double offset : {0.0, -175000.0, 175000.0, -225000.0, 225000.0, -125000.0, 125000.0}) {
            double width = offset == 0 ? std::min(80000.0, bandwidth * 0.9) : 45000;
            if (std::abs(offset) + width / 2 > std::min(bandwidth, sampleRate) / 2) { continue; }
            auto center = static_cast<int64_t>(std::llround(offset * transformSize / sampleRate));
            std::vector<std::complex<double>> filtered(outputSize);
            for (size_t bin = 0; bin < outputSize; ++bin) {
                auto signedBin = bin < outputSize / 2 ? static_cast<int64_t>(bin) : static_cast<int64_t>(bin) - static_cast<int64_t>(outputSize);
                double relative = std::abs(signedBin * sampleRate / transformSize) / (width / 2);
                if (relative >= 1) { continue; }
                double taper = relative < 0.85 ? 1 : 0.5 + 0.5 * std::cos((relative - 0.85) / 0.15 * 3.14159265358979323846);
                auto source = (center + signedBin + static_cast<int64_t>(transformSize)) % static_cast<int64_t>(transformSize);
                filtered[bin] = std::conj(spectrum[source]) * taper;
            }
            fft(filtered);
            std::vector<dsp::complex_t> channel(outputSize);
            for (size_t index = 0; index < outputSize; ++index) {
                channel[index] = {static_cast<float>(filtered[index].real() / transformSize), static_cast<float>(-filtered[index].imag() / transformSize)};
            }
            for (const auto& profile : broadcastProfiles()) {
                if (profile.type != Broadcast::CDR) { continue; }
                BroadcastResult candidate;
                size_t length = std::min(channel.size(), static_cast<size_t>(std::ceil(outputRate * 0.048)));
                for (size_t segment = 0; segment < 3; ++segment) {
                    size_t start = (channel.size() - length) * segment / 2;
                    std::vector<dsp::complex_t> section(channel.begin() + start, channel.begin() + start + length);
                    auto match = matchBroadcastPrefix(section, outputRate, profile);
                    if (match.confidence > candidate.confidence) { candidate = match; }
                }
                candidate.bandOffset = center * sampleRate / transformSize;
                if (candidate.confidence > best.confidence) { best = candidate; }
            }
        }
        return best;
    }

    class BroadcastDetector {
    public:
        BroadcastDetector(double inputRate, double bandwidth, int processingMode) : sampleRate(inputRate), channelBandwidth(bandwidth) {
            cdr = (processingMode == 1 || processingMode == 9) && bandwidth >= 40000 && bandwidth <= 650000;
            dab = (processingMode == 1 || processingMode == 10) && bandwidth >= 1536000 && bandwidth <= 2500000;
            if (!std::isfinite(inputRate) || inputRate < 40000 || inputRate > 6144000) { cdr = dab = false; }
            windowSize = (cdr || dab) ? static_cast<size_t>(std::ceil(sampleRate * (dab ? 0.215 : 0.1))) : 0;
            buffer.reserve(windowSize);
        }

        void push(const dsp::complex_t* input, int count) {
            if (!windowSize) { return; }
            for (int index = 0; index < count; ++index) {
                if (skip) { --skip; continue; }
                if (!std::isfinite(input[index].re) || !std::isfinite(input[index].im)) { reset(); continue; }
                buffer.push_back(input[index]);
                if (buffer.size() != windowSize) { continue; }
                auto candidate = dab ? analyzeDab(buffer, sampleRate) : analyzeCdr(buffer, sampleRate, channelBandwidth);
                if (candidate.type != Broadcast::Unknown && candidate.type == previous.type && candidate.mode == previous.mode) { ++stable; }
                else { stable = candidate.type == Broadcast::Unknown ? 0 : 1; }
                previous = candidate;
                result = stable >= 2 ? candidate : BroadcastResult{};
                buffer.clear();
                skip = static_cast<size_t>(std::max(0.0, sampleRate * 0.5 - windowSize));
            }
        }

        bool available() const { return windowSize > 0; }
        BroadcastResult result;

    private:
        void reset() { buffer.clear(); result = {}; previous = {}; stable = 0; }
        double sampleRate;
        double channelBandwidth;
        bool cdr = false;
        bool dab = false;
        size_t windowSize = 0;
        size_t skip = 0;
        int stable = 0;
        std::vector<dsp::complex_t> buffer;
        BroadcastResult previous;
    };
}
