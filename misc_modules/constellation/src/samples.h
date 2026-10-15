#pragma once
#include <dsp/types.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <mutex>
#include <vector>

class ConstellationSamples {
public:
    static constexpr size_t capacity = 4096;

    void push(const dsp::complex_t* samples, int count) {
        std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
        if (!lock.owns_lock() || frozen || count <= 0) { return; }
        for (int index = std::max(0, count - static_cast<int>(capacity)); index < count; ++index) {
            if (!std::isfinite(samples[index].re) || !std::isfinite(samples[index].im)) { continue; }
            points[position] = samples[index];
            position = (position + 1) % capacity;
            size = std::min(size + 1, capacity);
        }
        lastData = std::chrono::steady_clock::now();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        position = 0;
        size = 0;
        lastData = {};
    }

    void setFrozen(bool value) {
        std::lock_guard<std::mutex> lock(mutex);
        frozen = value;
    }

    double age() const {
        std::lock_guard<std::mutex> lock(mutex);
        if (lastData == std::chrono::steady_clock::time_point{}) { return -1; }
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - lastData).count();
    }

    std::vector<dsp::complex_t> snapshot(size_t requested, bool normalize, bool removeMean, float gain, float degrees) const {
        std::vector<dsp::complex_t> result;
        {
            std::lock_guard<std::mutex> lock(mutex);
            size_t count = std::min(size, requested);
            result.reserve(count);
            size_t start = (position + capacity - count) % capacity;
            for (size_t index = 0; index < count; ++index) { result.push_back(points[(start + index) % capacity]); }
        }
        if (result.empty()) { return result; }
        double meanReal = 0;
        double meanImag = 0;
        if (removeMean) {
            for (const auto& point : result) { meanReal += point.re; meanImag += point.im; }
            meanReal /= result.size();
            meanImag /= result.size();
        }
        double power = 0;
        for (const auto& point : result) {
            double real = point.re - meanReal;
            double imag = point.im - meanImag;
            power += real * real + imag * imag;
        }
        double scale = gain;
        if (normalize && power > 1e-30) { scale /= std::sqrt(power / result.size()); }
        double angle = degrees * 3.14159265358979323846 / 180.0;
        double cosine = std::cos(angle) * scale;
        double sine = std::sin(angle) * scale;
        for (auto& point : result) {
            double real = point.re - meanReal;
            double imag = point.im - meanImag;
            point.re = static_cast<float>(real * cosine - imag * sine);
            point.im = static_cast<float>(real * sine + imag * cosine);
        }
        return result;
    }

private:
    mutable std::mutex mutex;
    std::array<dsp::complex_t, capacity> points{};
    size_t position = 0;
    size_t size = 0;
    bool frozen = false;
    std::chrono::steady_clock::time_point lastData{};
};
