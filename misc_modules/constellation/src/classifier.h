#pragma once
#include <dsp/types.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace constellation {
    enum class Modulation { Unknown, BPSK, QPSK, PSK8, QAM16, QAM64, QAM256, FSK2, CDR, DAB };

    inline const char* label(Modulation modulation) {
        static const char* names[] = {"Unknown", "BPSK", "QPSK", "8PSK", "16-QAM", "64-QAM", "256-QAM", "2-FSK / CPFSK-like", "CDR-compatible OFDM", "DAB / DAB+ compatible OFDM"};
        return names[static_cast<int>(modulation)];
    }

    inline int order(Modulation modulation) {
        static const int orders[] = {0, 2, 4, 8, 16, 64, 256, 2, 0, 0};
        return orders[static_cast<int>(modulation)];
    }

    inline std::vector<dsp::complex_t> reference(Modulation modulation) {
        if (modulation == Modulation::FSK2) { return {{-1, 0}, {1, 0}}; }
        std::vector<dsp::complex_t> points;
        int count = order(modulation);
        if (count <= 8) {
            for (int index = 0; index < count; ++index) {
                double angle = 2 * 3.14159265358979323846 * index / count + (count == 2 ? 0 : 3.14159265358979323846 / count);
                points.push_back({static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))});
            }
        } else {
            int side = static_cast<int>(std::sqrt(count));
            float scale = 1 / std::sqrt(2.0f * (count - 1) / 3);
            for (int row = 0; row < side; ++row) {
                for (int column = 0; column < side; ++column) { points.push_back({(2 * column - side + 1) * scale, (2 * row - side + 1) * scale}); }
            }
        }
        return points;
    }

    struct Detection {
        Modulation modulation = Modulation::Unknown;
        double confidence = 0;
        double evm = 0;
        double phase = 0;
        double score = std::numeric_limits<double>::infinity();
    };

    inline Detection classify(const std::vector<dsp::complex_t>& input, Modulation requested = Modulation::Unknown) {
        Detection best;
        if (input.size() < 1024) { return best; }
        std::vector<dsp::complex_t> points;
        double meanReal = 0;
        double meanImag = 0;
        for (const auto& point : input) {
            if (std::isfinite(point.re) && std::isfinite(point.im)) { points.push_back(point); meanReal += point.re; meanImag += point.im; }
        }
        if (points.size() < 1024) { return best; }
        meanReal /= points.size();
        meanImag /= points.size();
        double power = 0;
        for (const auto& point : points) { power += std::pow(point.re - meanReal, 2) + std::pow(point.im - meanImag, 2); }
        power /= points.size();
        if (power < 1e-16 || !std::isfinite(power)) { return best; }
        double fourthMoment = 0;
        double scale = 1 / std::sqrt(power);
        for (auto& point : points) {
            point.re = static_cast<float>((point.re - meanReal) * scale);
            point.im = static_cast<float>((point.im - meanImag) * scale);
            double magnitude = point.re * point.re + point.im * point.im;
            fourthMoment += magnitude * magnitude;
        }
        fourthMoment /= points.size();
        if (fourthMoment > 1.65) { return best; }
        double secondScore = std::numeric_limits<double>::infinity();
        for (int candidate = 1; candidate <= 6; ++candidate) {
            Modulation modulation = static_cast<Modulation>(candidate);
            if (requested != Modulation::Unknown && requested != modulation) { continue; }
            int count = order(modulation);
            bool qam = count > 8;
            int side = qam ? static_cast<int>(std::sqrt(count)) : 0;
            double normalizer = qam ? std::sqrt(2.0 * (count - 1) / 3) : 1;
            double distance = qam ? 2 / normalizer : 2 * std::sin(3.14159265358979323846 / count);
            double expectedFourth = qam ? 1.4 - 0.8 / (count - 1) : 1;
            if (std::abs(fourthMoment - expectedFourth) > 0.18) { continue; }
            double span = qam ? 3.14159265358979323846 / 2 : 2 * 3.14159265358979323846 / count;
            double phaseOffset = count <= 8 && count != 2 ? 3.14159265358979323846 / count : 0;
            double minimum = std::numeric_limits<double>::infinity();
            double bestPhase = 0;
            std::array<int, 256> bestOccupancy{};
            auto evaluate = [&](double angle) {
                double cosine = std::cos(angle);
                double sine = std::sin(angle);
                double error = 0;
                double correlation = 0;
                double targetRealSum = 0;
                double targetImagSum = 0;
                std::array<int, 256> occupancy{};
                for (const auto& point : points) {
                    double real = point.re * cosine + point.im * sine;
                    double imag = point.im * cosine - point.re * sine;
                    double targetReal;
                    double targetImag;
                    int slot;
                    if (qam) {
                        int column = std::clamp(static_cast<int>(std::round((real * normalizer + side - 1) / 2)), 0, side - 1);
                        int row = std::clamp(static_cast<int>(std::round((imag * normalizer + side - 1) / 2)), 0, side - 1);
                        targetReal = (2 * column - side + 1) / normalizer;
                        targetImag = (2 * row - side + 1) / normalizer;
                        slot = row * side + column;
                    } else {
                        int symbol = static_cast<int>(std::round((std::atan2(imag, real) - phaseOffset) * count / (2 * 3.14159265358979323846)));
                        slot = (symbol % count + count) % count;
                        double phase = 2 * 3.14159265358979323846 * slot / count + phaseOffset;
                        targetReal = std::cos(phase);
                        targetImag = std::sin(phase);
                    }
                    error += targetReal * targetReal + targetImag * targetImag;
                    correlation += real * targetReal + imag * targetImag;
                    targetRealSum += targetReal;
                    targetImagSum += targetImag;
                    ++occupancy[slot];
                }
                error /= points.size();
                correlation /= points.size();
                targetRealSum /= points.size();
                targetImagSum /= points.size();
                double fittedGain = std::clamp(correlation, 0.9, 1.1);
                error = std::max(0.0, error - targetRealSum * targetRealSum - targetImagSum * targetImagSum - 2 * fittedGain * correlation + fittedGain * fittedGain);
                if (error < minimum) { minimum = error; bestPhase = angle; bestOccupancy = occupancy; }
            };
            for (int step = 0; step < 48; ++step) { evaluate(span * step / 48); }
            double coarse = bestPhase;
            for (int step = -6; step <= 6; ++step) { evaluate(coarse + span * step / (48 * 12)); }
            int occupied = 0;
            double entropy = 0;
            for (int slot = 0; slot < count; ++slot) {
                if (!bestOccupancy[slot]) { continue; }
                ++occupied;
                double probability = static_cast<double>(bestOccupancy[slot]) / points.size();
                entropy -= probability * std::log(probability);
            }
            if (occupied < count * 0.6 || entropy / std::log(count) < 0.8 || minimum > std::pow(0.12 * distance, 2)) { continue; }
            double score = minimum / (distance * distance) + 0.08 * std::abs(fourthMoment - expectedFourth);
            if (score < best.score) {
                secondScore = best.score;
                best = {modulation, std::clamp(1 - std::sqrt(minimum) / (0.15 * distance), 0.0, 1.0), std::sqrt(minimum), bestPhase, score};
            } else { secondScore = std::min(secondScore, score); }
        }
        if (std::isfinite(secondScore) && secondScore < best.score * 1.25 + 0.0001) { return Detection{}; }
        return best;
    }
}
